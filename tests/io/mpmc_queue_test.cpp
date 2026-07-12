#include <gtest/gtest.h>
#include <vortex/io/mpmc_queue.h>

#include <thread>
#include <vector>

using namespace vortex::io::detail;

TEST(MPMCQueue, SingleThreadEnqueueDequeue) {
    MPMCQueue<int, 8> q;
    EXPECT_TRUE(q.try_enqueue(42));
    EXPECT_TRUE(q.try_enqueue(99));

    auto r1 = q.try_dequeue();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 42);

    auto r2 = q.try_dequeue();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 99);
}

TEST(MPMCQueue, EmptyDequeue) {
    MPMCQueue<int, 8> q;
    auto r = q.try_dequeue();
    EXPECT_FALSE(r.has_value());
}

TEST(MPMCQueue, FullQueue) {
    MPMCQueue<int, 4> q;
    // Fill the queue
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.try_enqueue(i));
    }
    // Queue should be full
    EXPECT_FALSE(q.try_enqueue(999));
}

TEST(MPMCQueue, MultiProducerMultiConsumer) {
    static constexpr int kNumProducers = 4;
    static constexpr int kNumConsumers = 4;
    static constexpr int kItemsPerProducer = 1000;
    static constexpr int kQueueSize = 1024;

    MPMCQueue<int, kQueueSize> q;
    std::atomic<int> produced_total{0};
    std::atomic<int> consumed_total{0};
    std::atomic<int> consumed_sum{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    for (int p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                int value = p * kItemsPerProducer + i;
                while (!q.try_enqueue(value)) {
                    std::this_thread::yield();
                }
                produced_total.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < kNumConsumers; ++c) {
        consumers.emplace_back([&]() {
            while (!done.load(std::memory_order_acquire)) {
                auto r = q.try_dequeue();
                if (r.has_value()) {
                    consumed_total.fetch_add(1, std::memory_order_relaxed);
                    consumed_sum.fetch_add(*r, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
            // Drain remaining
            for (;;) {
                auto r = q.try_dequeue();
                if (!r.has_value()) break;
                consumed_total.fetch_add(1, std::memory_order_relaxed);
                consumed_sum.fetch_add(*r, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    done.store(true, std::memory_order_release);
    for (auto& t : consumers) t.join();

    EXPECT_EQ(consumed_total.load(), kNumProducers * kItemsPerProducer);

    // Verify sum: sum of 0..(N-1) for each producer
    int expected_sum = 0;
    for (int p = 0; p < kNumProducers; ++p) {
        int base = p * kItemsPerProducer;
        expected_sum += (kItemsPerProducer * (2 * base + kItemsPerProducer - 1)) / 2;
    }
    EXPECT_EQ(consumed_sum.load(), expected_sum);
}

TEST(WorkStealingQueue, OwnerPushPop) {
    WorkStealingQueue<int, 16> q;
    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.push(30));

    auto r1 = q.pop();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 30);  // LIFO for owner

    auto r2 = q.pop();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 20);
}

TEST(WorkStealingQueue, StealFromOtherThread) {
    WorkStealingQueue<int, 16> q;
    q.push(100);
    q.push(200);

    std::atomic<int> stolen_val{-1};

    std::thread thief([&]() {
        auto r = q.steal();
        if (r.has_value()) {
            stolen_val.store(*r, std::memory_order_relaxed);
        }
    });

    thief.join();
    EXPECT_EQ(stolen_val.load(), 100);  // Steals from bottom (FIFO for thieves)
}

TEST(WorkStealingQueue, StealEmpty) {
    WorkStealingQueue<int, 16> q;
    auto r = q.steal();
    EXPECT_FALSE(r.has_value());
}

TEST(SPSCQueue, SingleThread) {
    SPSCQueue<int, 8> q;
    EXPECT_TRUE(q.try_enqueue(1));
    EXPECT_TRUE(q.try_enqueue(2));

    auto r1 = q.try_dequeue();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 1);

    auto r2 = q.try_dequeue();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 2);

    EXPECT_FALSE(q.try_dequeue().has_value());
}
