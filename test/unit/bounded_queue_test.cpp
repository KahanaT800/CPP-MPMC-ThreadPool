/*
Ring buffer (bounded queue) tests
*/

#include "mpmc/bounded_circular_queue.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <stdexcept>

// Constructor tests
TEST(BoundedCircularQueueTest, Constructors) {
    // Normal construction
    EXPECT_NO_THROW({
        BoundedCircularQueue<int> queue(8);
        EXPECT_EQ(queue.Capacity(), 8u);
    });
    // Capacity less than 2 is adjusted to 2
    EXPECT_NO_THROW({
        BoundedCircularQueue<int> queue1(1);
        EXPECT_EQ(queue1.Capacity(), 2u);
        BoundedCircularQueue<int> queue2(0);
        EXPECT_EQ(queue2.Capacity(), 2u);
    });
}

// Basic push/pop operations
TEST(BoundedCircularQueueTest, PushPop) {
    BoundedCircularQueue<int> queue(4);
    int item;

    // Pop on empty queue fails
    EXPECT_FALSE(queue.TryPop(item));

    // Push succeeds
    EXPECT_TRUE(queue.TryPush(1));
    EXPECT_TRUE(queue.TryPush(2));
    EXPECT_TRUE(queue.TryPush(3));
    EXPECT_TRUE(queue.TryPush(4));
    EXPECT_EQ(queue.ApproxSize(), 4u);
    // Push on full queue fails
    EXPECT_TRUE(queue.Full());
    EXPECT_FALSE(queue.TryPush(5));

    // Pop succeeds
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 1);
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 2);

    // Push succeeds
    EXPECT_TRUE(queue.TryPush(5));
    EXPECT_TRUE(queue.TryPush(6));
    // Push on full queue fails
    EXPECT_FALSE(queue.TryPush(7));

    // Pop remaining elements
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 3);
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 4);
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 5);
    EXPECT_TRUE(queue.TryPop(item));
    EXPECT_EQ(item, 6);
    // Pop on empty queue fails
    EXPECT_TRUE(queue.Empty());
    EXPECT_FALSE(queue.TryPop(item));
}

// Wrap-around behavior
TEST(BoundedCircularQueueTest, WrapAround) {
    BoundedCircularQueue<int> queue(8);
    int item;

    for (int i = 0; i < 100000; ++i) {
        EXPECT_TRUE(queue.TryPush(i));
        EXPECT_TRUE(queue.TryPop(item));
        EXPECT_EQ(item, i);
    }

    EXPECT_TRUE(queue.Empty());
    // Still usable after many cycles
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(queue.TryPush(i));
    }
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(queue.TryPop(item));
    }
}

// Multi-threaded producer/consumer test
TEST(BoundedCircularQueueTest, MultiThreaded) {
    const int N = 200000;
    const int P = 4;
    const int C = 2;

    BoundedCircularQueue<int> queue(1 << 12); // 4096
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
                while (!queue.TryPush(i)) {
                    // yield while spinning
                    std::this_thread::yield();
                }
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
                if (queue.TryPop(item)) {
                    {
                        std::lock_guard<std::mutex> lk(m);
                        if (item >= 0 && item < N) {
                            seen[item] = true; // mark consumed
                        }
                    }
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
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

// Destructor live count returns to zero
TEST(BoundedQueue, LifetimeSafety) {
    {
        BoundedCircularQueue<Counted> queue(64);
        for (int i = 0; i < 1000; ++i) {
            EXPECT_TRUE(queue.TryPush(Counted{i}));
            Counted out;
            EXPECT_TRUE(queue.TryPop(out));
        }
        // No live objects should remain after scope ends
    }
    EXPECT_EQ(Counted::live.load(), 0);
}

