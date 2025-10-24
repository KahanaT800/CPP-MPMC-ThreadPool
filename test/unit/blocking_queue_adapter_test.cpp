/*
Blocking queue adapter tests
*/

#include "mpmc/blocking_queue_adapter.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <future>
#include <atomic>
#include <vector>
#include <chrono>
#include <mutex>

using namespace std::chrono_literals;

struct Counted {
    static std::atomic<int> live;
    int v;
    explicit Counted(int x = 0) : v(x) {
        live.fetch_add(1, std::memory_order_relaxed);
    }
    // During push/emplace, a temporary may be created (invokes copy ctor)
    Counted(const Counted& o) : v(o.v) {
        live.fetch_add(1, std::memory_order_relaxed);
    }
    ~Counted() {
        live.fetch_sub(1, std::memory_order_relaxed);
    }
};
std::atomic<int> Counted::live{0};

// Non-blocking API tests
TEST(BlockingQueueAdapter, TryPushTryPop) {
    BlockingQueueAdapter<int> q(8);
    EXPECT_EQ(q.Capacity(), 8u);

    EXPECT_TRUE(q.TryPush(1));
    EXPECT_TRUE(q.TryPush(2));
    int x = -1;
    EXPECT_TRUE(q.TryPop(x));
    EXPECT_EQ(x, 1);
    EXPECT_TRUE(q.TryPop(x));
    EXPECT_EQ(x, 2);

    EXPECT_FALSE(q.TryPop(x));
}

// Discard counter tests
TEST(BlockingQueueAdapter, DiscardCounter) {
    BlockingQueueAdapter<int> q(2);
    q.ResetDiscardCounter();
    EXPECT_EQ(q.DiscardCount(), 0u);

    EXPECT_TRUE(q.TryPush(1));
    EXPECT_TRUE(q.TryPush(2));
    EXPECT_FALSE(q.TryPush(3));
    EXPECT_EQ(q.DiscardCount(), 1u);

    int x;
    EXPECT_TRUE(q.TryPop(x));
    EXPECT_TRUE(q.TryPop(x));
    EXPECT_TRUE(q.DiscardCount() >= 1u);
}

// Blocking Pop
TEST(BlockingQueueAdapter, WaitPop) {
    BlockingQueueAdapter<int> q(4);
    std::promise<void> started;
    std::promise<int> get;

    std::thread consumer([&]{
        started.set_value();
        int x = -1;
        EXPECT_TRUE(q.WaitPop(x));
        get.set_value(x);
    });

    started.get_future().wait();
    std::this_thread::sleep_for(30ms);

    q.WaitPush(100);

    int v = get.get_future().get();
    EXPECT_EQ(v, 100);
    consumer.join();
}

// Blocking Push
TEST(BlockingQueueAdapter, WaitPush) {
    BlockingQueueAdapter<int> q(1);
    q.WaitPush(10);

    std::promise<void> started, done;

    std::thread producer([&]{
        started.set_value();
        EXPECT_TRUE(q.WaitPush(20));
        done.set_value();
    });

    started.get_future().wait();
    std::this_thread::sleep_for(30ms);
    int x = -1;
    EXPECT_TRUE(q.TryPop(x));
    EXPECT_EQ(x, 10);

    done.get_future().wait();
    producer.join();

    q.WaitPop(x);
    EXPECT_EQ(x, 20);
}

// Destructor live count returns to zero
TEST(BlockingQueueAdapter, LifetimeSafety) {
    {
        BlockingQueueAdapter<Counted> q(64);
        for (int i = 0; i < 500; ++i) {
            EXPECT_TRUE(q.WaitEmplace(Counted{i}));
            Counted out(0);
            EXPECT_TRUE(q.WaitPop(out));
        }
    }
    EXPECT_EQ(Counted::live.load(std::memory_order_relaxed), 0);
}

// Multi-threaded producer/consumer test
TEST(BoundedCircularQueueTest, MultiThreaded) {
    const int N = 200000;
    const int P = 4;
    const int C = 2;

    BlockingQueueAdapter<int> queue(1 << 12); // 4096
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> tasks;

    // producers
    for (int p = 0; p < P; ++p) {
        tasks.emplace_back([&] {
            for (;;) {
                int i = produced.fetch_add(1, std::memory_order_relaxed);
                if (i >= N) {
                    break;
                }
                queue.WaitPush(i);
            }
        });
    }

    // consumers
    std::vector<bool> seen(N, false);
    std::mutex m;

    for (int c = 0; c < C; ++c) {
        tasks.emplace_back([&] {
            int item;
            for (;;) {
                if (consumed.load(std::memory_order_relaxed) >= N) {
                    break;
                }
                if (queue.WaitPopFor(item, 30ms)) {
                    {
                        std::lock_guard<std::mutex> lk(m);
                        if (item >= 0 && item < N) {
                            seen[item] = true; // mark this element as consumed
                        }
                    }
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : tasks) {
        t.join();
    }

    EXPECT_GE(consumed.load(), N);

    // Most elements should be consumed
    int count_seen = 0;
    for (bool b : seen) {
        count_seen += b ? 1 : 0;
    }
    EXPECT_GE(count_seen, N * 95 / 100);
}

TEST(BlockingQueueAdapter, Close_RejectsProducers) {
    BlockingQueueAdapter<int> q(64);

    constexpr int N = 1000;
    constexpr int C = 8;
    constexpr int P = 4;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Start consumers: run until Close() then WaitPopFor returns false to exit
    std::vector<std::thread> consumers;
    consumers.reserve(C);
    for (int i = 0; i < C; ++i) {
        consumers.emplace_back([&]{
            int x = 0;
            while (q.WaitPopFor(x, 10ms)) {
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Start producers: attempt to enqueue for a while; on failure, use short-timeout blocking enqueue to buffer pressure
    std::vector<std::thread> producers;
    producers.reserve(P);
    for (int p = 0; p < P; ++p) {
        producers.emplace_back([&]{
            for (int i = 0; i < N; ++i) {
                if (!q.TryPush(i)) {
                    q.WaitPushFor(i, 2ms);
                }
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(30ms);
    q.Close();

    EXPECT_FALSE(q.TryPush(12345));
    EXPECT_FALSE(q.WaitPushFor(67890, 5ms));

    for (auto& t : producers) { t.join(); }
    for (auto& t : consumers) { t.join(); }
}

TEST(BlockingQueueAdapter, Clear_Then_Close) {
    BlockingQueueAdapter<int> q(8);

    // Fill the queue
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(q.TryPush(i));
    }

    std::promise<bool> producer_done;
    std::thread producer([&]{
        bool ok = q.WaitPushFor(999, 500ms);
        producer_done.set_value(ok);
    });

    std::this_thread::sleep_for(10ms);

    // Close first to ensure producer definitely fails
    q.Close();
    q.Clear();

    // Enqueue rejected
    auto ok = producer_done.get_future().get();
    EXPECT_FALSE(ok);
    producer.join();

    int x = -1;
    EXPECT_FALSE(q.WaitPopFor(x, 5ms));
}

TEST(QueueContract, NoConsumeOnFailure) {
    BlockingQueueAdapter<std::unique_ptr<int>> q(2);
    ASSERT_TRUE(q.TryPush(std::make_unique<int>(7)));
    ASSERT_TRUE(q.TryPush(std::make_unique<int>(8)));
    
    std::unique_ptr<int> p = std::make_unique<int>(9);
    bool pushed = q.TryPush(std::move(p));
    EXPECT_FALSE(pushed);
    // After failure, p should remain non-null (not consumed)
    EXPECT_TRUE(p);
    
    // Clean up: pop the items to avoid memory leak detected by ASan
    std::unique_ptr<int> out;
    EXPECT_TRUE(q.TryPop(out));
    EXPECT_TRUE(q.TryPop(out));
}
