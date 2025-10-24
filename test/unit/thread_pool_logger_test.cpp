#include "thread_pool/fwd.hpp"
#include "thread_pool/thread_pool.hpp"
#include "thread_pool/config.hpp"
#include "logger.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>
#include <fmt/format.h>

#include <future>
#include <vector>
#include <mutex>
#include <string>
#include <atomic>

namespace {
class CapturingSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    const std::vector<std::string>& Messages() const noexcept { 
        return messages_; 
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        messages_.emplace_back(fmt::to_string(buf));
    }

    void flush_() override {}

private:
    std::vector<std::string> messages_;
};

class LoggerScope {
public:
    explicit LoggerScope(thread_pool::LoggerPtr replacement)
        : previous_(thread_pool::log::LoadLogger()) {
        thread_pool::log::SetLogger(std::move(replacement));
    }
    ~LoggerScope() noexcept {
        if (previous_) {
            thread_pool::log::SetLogger(std::move(previous_));
        }
    }
    LoggerScope(const LoggerScope&) = delete;
    LoggerScope& operator=(const LoggerScope&) = delete;
private:
    thread_pool::LoggerPtr previous_;
};
}

TEST(LoggerIntegration, MySink) {
    auto sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("logger-test", sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);

    LoggerScope guard(logger);

    TP_LOG_INFO("logger integration {}", 42);

    const auto& messages = sink->Messages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages.front().find("logger integration 42"), std::string::npos);
}

TEST(LoggerIntegration, Hook) {
    auto sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("logger-scope", sink);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);

    LoggerScope guard(logger);

    std::atomic<bool> hook_called{false};
    {
        TP_PERF_SCOPE_HOOK("sample-scope", [&](std::chrono::nanoseconds) {
            hook_called.store(true, std::memory_order_relaxed);
        });
    }

    EXPECT_TRUE(hook_called.load(std::memory_order_relaxed));
    const auto& messages = sink->Messages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages.front().find("[perf] sample-scope took"), std::string::npos);
}

TEST(ThreadPoolLogger, Statistics_Normal) {
    thread_pool::ThreadPool pool(2, 16);
    pool.Start();

    constexpr int task_nums = 100;
    std::vector<std::future<void>> futures;
    futures.reserve(task_nums);
    for (int i = 0; i < task_nums; ++i) {
        futures.emplace_back(pool.Submit([]{}));
    }
    for (auto& fut : futures) {
        fut.get();
    }

    pool.Stop(thread_pool::StopMode::Graceful);

    auto stats = pool.GetStatistics();
    EXPECT_EQ(stats.statistic_total_submitted, task_nums);
    EXPECT_EQ(stats.statistic_total_completed, task_nums);
    EXPECT_EQ(stats.statistic_total_failed, 0U);
    EXPECT_GT(stats.statistic_total_exec_time.count(), 0);
}

TEST(ThreadPoolMetrics, Statistics_Cancel) {
    thread_pool::ThreadPool pool(1, 1);
    pool.Start();

    auto slow = pool.Submit([]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    auto rejected = pool.Submit([]{});

    pool.Stop(thread_pool::StopMode::Force);
    auto stats = pool.GetStatistics();
    EXPECT_GE(stats.statistic_total_cancelled, 1U);
    EXPECT_GE(stats.statistic_total_rejected, 0U);
}      
