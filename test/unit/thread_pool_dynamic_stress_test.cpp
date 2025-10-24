#include "thread_pool/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <future>
#include <vector>
#include <functional>

using namespace std::chrono_literals;

bool WaitUntil(const std::function<bool()>& predicate,
               const std::function<void()>& on_tick,
               std::chrono::milliseconds timeout,
               std::chrono::milliseconds tick = 1ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        on_tick();
        std::this_thread::sleep_for(tick);
    }
    return predicate();
}

TEST(ThreadPoolDynamicStress, FallsBack) {
    // First backlog tasks, then release and shrink back to core threads
    thread_pool::ThreadPoolConfig cfg;
    cfg.queue_cap = 64;
    cfg.core_threads = 1;
    cfg.max_threads = 4;
    cfg.load_check_interval = 1ms;
    cfg.keep_alive = 30ms;
    cfg.scale_up_threshold = 0.5;
    cfg.scale_down_threshold = 0.2;
    cfg.pending_hi = 2;
    cfg.pending_low = 1;
    cfg.debounce_hits = 1;
    cfg.cooldown = 3ms;
    cfg.queue_policy = thread_pool::QueueFullPolicy::Block;

    thread_pool::ThreadPool pool(cfg);
    pool.Start();

    std::atomic<bool> unblock{false};
    std::atomic<int> done{0};
    const int total = static_cast<int>(cfg.max_threads) * 3;

    for (int i = 0; i < total; ++i) {
        pool.Post([&] {
            while (!unblock.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(50us);
            }
            done.fetch_add(1, std::memory_order_relaxed);
        });
    }

    EXPECT_TRUE(WaitUntil(
        [&] { return pool.CurrentThreads() > cfg.core_threads; },
        [&] { pool.TriggerLoadCheck(); },
        200ms,
        1ms));

    unblock.store(true, std::memory_order_relaxed);
    EXPECT_TRUE(WaitUntil(
        [&] { return done.load(std::memory_order_acquire) == total; },
        [] {},
        1s,
        2ms));

    EXPECT_TRUE(WaitUntil(
        [&] { return pool.CurrentThreads() == cfg.core_threads; },
        [&] { pool.TriggerLoadCheck(); },
        400ms,
        2ms));

    pool.Stop();
}

TEST(ThreadPoolDynamicStress, KeepsWorkers) {
    // Pin tasks to ensure no shrink before cooldown elapses
    thread_pool::ThreadPoolConfig cfg;
    cfg.queue_cap = 64;
    cfg.core_threads = 2;
    cfg.max_threads = 6;
    cfg.load_check_interval = 1ms;
    cfg.keep_alive = 60ms;
    cfg.scale_up_threshold = 0.5;
    cfg.scale_down_threshold = 0.2;
    cfg.pending_hi = 2;
    cfg.pending_low = 1;
    cfg.debounce_hits = 1;
    cfg.cooldown = 5ms;
    cfg.queue_policy = thread_pool::QueueFullPolicy::Block;

    thread_pool::ThreadPool pool(cfg);
    pool.Start();

    std::atomic<bool> release{false};
    std::vector<std::future<void>> keepers;
    keepers.reserve(cfg.max_threads);
    for (std::size_t i = 0; i < cfg.max_threads; ++i) {
        keepers.push_back(pool.Submit([&] {
            while (!release.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(50us);
            }
        }));
    }

    EXPECT_TRUE(WaitUntil(
        [&] { return pool.CurrentThreads() >= cfg.max_threads; },
        [&] { pool.TriggerLoadCheck(); },
        250ms,
        1ms));

    const auto watch_window = cfg.cooldown * 6;
    const auto deadline = std::chrono::steady_clock::now() + watch_window;
    while (std::chrono::steady_clock::now() < deadline) {
        pool.TriggerLoadCheck();
        std::this_thread::sleep_for(2ms);
    }
    EXPECT_GE(pool.CurrentThreads(), cfg.max_threads);

    release.store(true, std::memory_order_relaxed);
    for (auto& fut : keepers) {
        fut.get();
    }

    EXPECT_TRUE(WaitUntil(
        [&] { return pool.CurrentThreads() == cfg.core_threads; },
        [&] { pool.TriggerLoadCheck(); },
        400ms,
        2ms));

    pool.Stop();
}

TEST(ThreadPoolDynamicStress, HighConcurrency) {
    // Simulate a high-concurrency burst
    thread_pool::ThreadPoolConfig cfg;
    cfg.queue_cap = 1024;
    cfg.core_threads = 4;
    cfg.max_threads = 12;
    cfg.load_check_interval = 1ms;
    cfg.keep_alive = 50ms;
    cfg.scale_up_threshold = 0.6;
    cfg.scale_down_threshold = 0.2;
    cfg.pending_hi = 32;
    cfg.pending_low = 8;
    cfg.debounce_hits = 1;
    cfg.cooldown = 5ms;
    cfg.queue_policy = thread_pool::QueueFullPolicy::Block;

    thread_pool::ThreadPool pool(cfg);
    pool.Start();

    constexpr int producers = 4;
    constexpr int per_producer = 800;
    std::atomic<int> sum{0};
    std::atomic<int> left{producers * per_producer};

    const auto begin = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&] {
            for (int i = 0; i < per_producer; ++i) {
                pool.Post([&] {
                    sum.fetch_add(1, std::memory_order_relaxed);
                    left.fetch_sub(1, std::memory_order_relaxed);
                });
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    EXPECT_TRUE(WaitUntil(
        [&] { return left.load(std::memory_order_acquire) == 0; },
        [&] { pool.TriggerLoadCheck(); },
        2s,
        2ms));

    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_LT(elapsed, 1s);
    EXPECT_EQ(sum.load(std::memory_order_relaxed), producers * per_producer);

    pool.Stop();
}
