/*
Basic thread pool tests
*/

#include "thread_pool/thread_pool.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <stdexcept>

// Smoke/flow test
TEST(ThreadPoolBasic, Smoke_Sum100k) {
    thread_pool::ThreadPool pool(4, 2048);
    pool.Start();

    constexpr int N = 100000;
    std::atomic<long long> sum{0};

    for (int i = 1; i <= N; ++i) {
        pool.Submit([&sum, i]{ sum.fetch_add(i, std::memory_order_relaxed); });
    }

    pool.Stop(thread_pool::StopMode::Graceful);
    const long long expect = 1LL * N * (N + 1) / 2;
    EXPECT_EQ(sum.load(), expect);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

// Exception propagation
TEST(ThreadPoolBasic, ExceptionPropagation) {
    thread_pool::ThreadPool pool(2, 64);
    pool.Start();

    auto ok = pool.Submit([](int a, int b){ return a + b; }, 7, 5);
    EXPECT_EQ(ok.get(), 12);

    auto bad = pool.Submit([]() -> int {
        throw std::runtime_error("error");
    });
    EXPECT_THROW((void)bad.get(), std::runtime_error);

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

// Force stop
TEST(ThreadPoolBasic, ForceStop) {
    thread_pool::ThreadPool pool(4, 256);
    pool.Start();

    std::atomic<int> executed{0};
    constexpr int total = 5000;

    // Submit a large number of time-consuming tasks
    for (int i = 0; i < total; ++i) {
        pool.Submit([&executed] {
            executed.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }

    // Force stop immediately
    pool.Stop(thread_pool::StopMode::Force);

    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);

    int done = executed.load(std::memory_order_relaxed);
    EXPECT_LT(done, total);

    EXPECT_EQ(pool.ActiveTasks(), 0u);
    EXPECT_EQ(pool.Pending(), 0u);
}

// submit() throws after Stop
TEST(ThreadPoolBasic, Stop_RejectSubmit) {
    thread_pool::ThreadPool pool(4, 256);
    pool.Start();

    std::atomic<bool> gate{false};
    // Submit tasks that will block
    for (int i = 0; i < 120; ++i) {
        pool.Submit([&gate]{
            while (!gate.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
        });
    }

    while (pool.ActiveTasks() == 0) {
        std::this_thread::yield();
    }

    gate.store(true, std::memory_order_relaxed);
    pool.Stop(thread_pool::StopMode::Graceful);

    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
    EXPECT_EQ(pool.ActiveTasks(), 0u);
    EXPECT_EQ(pool.Pending(), 0u);

    EXPECT_THROW({
        pool.Submit([]{});
    }, std::runtime_error);
}

TEST(ThreadPoolBasic, Post) {
    thread_pool::ThreadPool pool(4, 1024);
    pool.Start();

    constexpr int N = 5000;
    std::atomic<int> cnt{0};

    for (int i = 0; i < N; ++i) {
        pool.Post([&cnt]{
            cnt.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(cnt.load(std::memory_order_relaxed), N);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
    EXPECT_EQ(pool.ActiveTasks(), 0u);
    EXPECT_EQ(pool.Pending(), 0u);
}

TEST(ThreadPoolBasic, Submit_Return) {
    thread_pool::ThreadPool pool(4, 256);
    pool.Start();

    auto fut1 = pool.Submit([](int a, int b) {
        return a + b;
    }, 10, 20);
    EXPECT_EQ(fut1.get(), 30);

    constexpr int N = 1000;
    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 1; i <= N; ++i) {
        futures.push_back(pool.Submit([i]{ return i * i; }));
    }

    long long sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    const long long expected = 1LL * N * (N + 1) * (2 * N + 1) / 6;
    EXPECT_EQ(sum, expected);

    auto fut_err = pool.Submit([]() -> int {
        throw std::runtime_error("Error");
    });
    EXPECT_THROW(fut_err.get(), std::runtime_error);

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

// Queue policy tests

TEST(ThreadPoolBasic, Policy_Block) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 4);
    pool.Start();
    pool.SetQueueFullPolicy(thread_pool::QueueFullPolicy::Block);

    std::atomic<bool> gate{false};
    std::promise<void> started;

    // Occupy the worker
    auto hold = pool.Submit([&]{
        started.set_value();
        while (!gate.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
    });

    started.get_future().wait();
    for (int i = 0; i < 4; ++i) {
        pool.Submit([]{});
    }
    EXPECT_EQ(pool.Pending(), 4u);

    // Submit one more, it should be blocked
    std::promise<bool> done;
    std::thread t([&]{
        auto fut = pool.Submit([]{});
        done.set_value(true);
        (void)fut;
    });

    std::this_thread::sleep_for(50ms);
    auto ready = done.get_future();
    EXPECT_EQ(ready.wait_for(0ms), std::future_status::timeout);

    // Release the worker
    gate.store(true, std::memory_order_relaxed);
    hold.get();

    EXPECT_EQ(ready.wait_for(500ms), std::future_status::ready);
    t.join();

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, Policy_Discard) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 4);
    pool.Start();
    pool.SetQueueFullPolicy(thread_pool::QueueFullPolicy::Discard);

    std::atomic<bool> gate{false};
    std::promise<void> started;

    // Occupy the worker
    auto hold = pool.Submit([&]{
        started.set_value();
        while (!gate.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
    });

    started.get_future().wait();
    for (int i = 0; i < 4; ++i) {
        pool.Submit([]{});
    }
    EXPECT_EQ(pool.Pending(), 4u);

    // Submit one more, it should be discarded
    auto f1 = pool.Submit([]{ return 1; });
    EXPECT_THROW((void)f1.get(), std::runtime_error);
    EXPECT_EQ(pool.DiscardedTasks(), 1u);

    // Submit another one, it should be discarded
    auto f2 = pool.Submit([]{ return 2; });
    EXPECT_THROW((void)f2.get(), std::runtime_error);
    EXPECT_EQ(pool.DiscardedTasks(), 2u);


    // Release the worker
    gate.store(true, std::memory_order_relaxed);
    hold.get();

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, Policy_Overwrite) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 4);
    pool.Start();
    pool.SetQueueFullPolicy(thread_pool::QueueFullPolicy::Overwrite);

    std::atomic<bool> gate{false};
    std::promise<void> started;

    // Occupy the worker
    auto hold = pool.Submit([&]{
        started.set_value();
        while (!gate.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
    });
    started.get_future().wait();
    std::future<int> old_futs[4];
    for (int i = 0; i < 4; ++i) {
        old_futs[i] = pool.Submit([i]{ return 100 + i; });
    }
    EXPECT_EQ(pool.Pending(), 4u);

    // Submit 3 new tasks
    std::future<int> new_futs[3];
    for (int j = 0; j < 3; ++j) {
        new_futs[j] = pool.Submit([j]{ return 200 + j; });
    }

    EXPECT_EQ(pool.OverwrittedTasks(), 3u);
    EXPECT_EQ(pool.Pending(), 4u);

    // Release the worker
    gate.store(true, std::memory_order_relaxed);
    hold.get();

    // Among the old tasks, the 3 overwritten ones should throw
    for (int i = 0; i < 3; ++i) {
        EXPECT_THROW((void)old_futs[i].get(), std::runtime_error);
    }
    EXPECT_NO_THROW({
        int v = old_futs[3].get();
        EXPECT_EQ(v, 103);
    });

    for (int j = 0; j < 3; ++j) {
        EXPECT_NO_THROW({
            int v = new_futs[j].get();
            EXPECT_EQ(v, 200 + j);
        });
    }

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, Pause) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 4);
    pool.Start();
    pool.Pause();
    EXPECT_TRUE(pool.Paused());

    auto submit_future = std::async(std::launch::async, [&]{
        return pool.Submit([]{ return 555; });
    });

    EXPECT_EQ(submit_future.wait_for(100ms), std::future_status::timeout);
    EXPECT_GE(pool.PausedWait(), 1u);

    pool.Resume();

    EXPECT_EQ(submit_future.wait_for(500ms), std::future_status::ready);
    auto fut = submit_future.get();
    EXPECT_NO_THROW({
        int v = fut.get();
        EXPECT_EQ(v, 555);
    });

    pool.Stop(thread_pool::StopMode::Graceful);
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, Pause_ConsumerDoesNotRun) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 4);
    pool.Start(); 
    pool.Pause();

    std::atomic<bool> started{false};
    auto fut = std::async(std::launch::async, [&]{
        return pool.Submit([&]{ 
            started.store(true, std::memory_order_relaxed); 
            return 1; 
        });
    });

    std::this_thread::sleep_for(200ms);
    EXPECT_FALSE(started.load(std::memory_order_relaxed));
    pool.Resume();

    auto f = fut.get();
    EXPECT_EQ(f.get(), 1);
    pool.Stop(thread_pool::StopMode::Graceful);
}

TEST(ThreadPoolBasic, Pause_ThenStop_Graceful) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 8); 
    pool.Start(); 
    pool.Pause();

    auto af = std::async(std::launch::async, [&]{ 
        return pool.Submit([]{ 
            return 7; 
        }); 
    });
    std::this_thread::sleep_for(150ms);
    EXPECT_EQ(af.wait_for(0ms), std::future_status::timeout);

    pool.Stop(thread_pool::StopMode::Graceful);
    auto f = af.get();
    EXPECT_EQ(f.get(), 7); // executed successfully
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, Pause_ThenStop_Force) {
    using namespace std::chrono_literals;
    thread_pool::ThreadPool pool(1, 8); 
    pool.Start(); 
    pool.Pause();

    auto af = std::async(std::launch::async, [&]{ 
        return pool.Submit([]{ 
            return 7; 
        }); 
    });
    std::this_thread::sleep_for(150ms);
    pool.Stop(thread_pool::StopMode::Force);

    auto f = af.get();
    EXPECT_THROW({ (void)f.get(); }, std::exception); // canceled
    EXPECT_EQ(pool.State(), thread_pool::PoolState::STOPPED);
}

TEST(ThreadPoolBasic, PauseResume_Safety) {
    thread_pool::ThreadPool pool(2, 16); 
    pool.Start();
    pool.Pause(); 
    pool.Pause(); 
    EXPECT_TRUE(pool.Paused());
    pool.Resume(); 
    pool.Resume(); 
    EXPECT_FALSE(pool.Paused());

    // Repeated submissions
    std::vector<std::future<int>> futs;
    for (int i=0;i<100;i++) {
    futs.push_back(pool.Submit(
        [i]{ 
            return i; 
        })
    );
    }
    for (int i=0;i<100;i++) {
    EXPECT_EQ(futs[i].get(), i);
    }

    pool.Stop(thread_pool::StopMode::Graceful);
}

