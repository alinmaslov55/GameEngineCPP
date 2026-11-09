#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"
#include "Engine/Scene/Scene.h"

#include <atomic>
#include <chrono>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

namespace {

TEST(JobSystemTest, LifecycleInitAndShutdown) {
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());
    EXPECT_EQ(Engine::JobSystem::GetWorkerCount(), 0u);

    Engine::JobSystem::Init(2);
    EXPECT_TRUE(Engine::JobSystem::IsInitialized());
    EXPECT_EQ(Engine::JobSystem::GetWorkerCount(), 2u);

    // Re-initialization should be ignored gracefully
    Engine::JobSystem::Init(4);
    EXPECT_EQ(Engine::JobSystem::GetWorkerCount(), 2u);

    Engine::JobSystem::Shutdown();
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());
    EXPECT_EQ(Engine::JobSystem::GetWorkerCount(), 0u);

    // Redundant shutdown should be idempotent
    Engine::JobSystem::Shutdown();
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());
}

TEST(JobSystemTest, SingleJobDispatchAndWait) {
    Engine::JobSystem::Init(2);

    std::atomic<int> counter{ 0 };
    auto handle = Engine::JobSystem::Dispatch([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        counter.store(42, std::memory_order_release);
    });

    EXPECT_TRUE(handle.IsValid());
    handle.Wait();

    EXPECT_TRUE(handle.IsComplete());
    EXPECT_EQ(counter.load(std::memory_order_acquire), 42);

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, ParallelExecutionAcrossWorkerThreads) {
    Engine::JobSystem::Init(4);

    std::mutex threadIdMutex;
    std::set<std::thread::id> workerThreadIds;
    std::vector<Engine::JobHandle> handles;
    handles.reserve(16);

    for (int i = 0; i < 16; ++i) {
        handles.push_back(Engine::JobSystem::Dispatch([&threadIdMutex, &workerThreadIds]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard lock(threadIdMutex);
            workerThreadIds.insert(std::this_thread::get_id());
        }));
    }

    Engine::JobSystem::WaitAll(handles);

    // Verify multiple worker threads were concurrently utilized
    EXPECT_GE(workerThreadIds.size(), 2u);

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, TaskChainingWithThen) {
    Engine::JobSystem::Init(2);

    std::atomic<int> state{ 0 };

    auto h1 = Engine::JobSystem::Dispatch([&state]() {
        state.store(10, std::memory_order_release);
    });

    auto h2 = h1.Then([&state]() {
        int val = state.load(std::memory_order_acquire);
        state.store(val * 2, std::memory_order_release); // 10 * 2 = 20
    });

    auto h3 = h2.Then([&state]() {
        int val = state.load(std::memory_order_acquire);
        state.store(val + 5, std::memory_order_release); // 20 + 5 = 25
    });

    h3.Wait();
    EXPECT_TRUE(h1.IsComplete());
    EXPECT_TRUE(h2.IsComplete());
    EXPECT_TRUE(h3.IsComplete());
    EXPECT_EQ(state.load(std::memory_order_acquire), 25);

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, MultiDependencyDispatchAfter) {
    Engine::JobSystem::Init(3);

    std::atomic<int> a{ 0 };
    std::atomic<int> b{ 0 };
    std::atomic<int> c{ 0 };
    std::atomic<int> result{ 0 };

    auto ha = Engine::JobSystem::Dispatch([&a]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        a.store(10, std::memory_order_release);
    });

    auto hb = Engine::JobSystem::Dispatch([&b]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        b.store(20, std::memory_order_release);
    });

    auto hc = Engine::JobSystem::Dispatch([&c]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        c.store(30, std::memory_order_release);
    });

    auto hSum = Engine::JobSystem::DispatchAfter({ ha, hb, hc }, [&a, &b, &c, &result]() {
        result.store(a.load(std::memory_order_acquire) +
                     b.load(std::memory_order_acquire) +
                     c.load(std::memory_order_acquire),
                     std::memory_order_release);
    });

    hSum.Wait();
    EXPECT_EQ(result.load(std::memory_order_acquire), 60);

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, SynchronousParallelFor) {
    Engine::JobSystem::Init(3);

    constexpr size_t kSize = 10000;
    std::vector<int> data(kSize, 0);

    Engine::JobSystem::ParallelFor(kSize, [&data](size_t i) {
        data[i] = static_cast<int>(i * 3);
    });

    // Since ParallelFor is synchronized with std::latch, all elements must be transformed on return
    for (size_t i = 0; i < kSize; ++i) {
        ASSERT_EQ(data[i], static_cast<int>(i * 3));
    }

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, ParallelForBatched) {
    Engine::JobSystem::Init(3);

    constexpr size_t kSize = 5000;
    std::vector<int> data(kSize, 0);

    Engine::JobSystem::ParallelForBatched(static_cast<size_t>(0), kSize, [&data](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            data[i] = 42;
        }
    });

    for (size_t i = 0; i < kSize; ++i) {
        ASSERT_EQ(data[i], 42);
    }

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, WorkStealingHighLoad) {
    Engine::JobSystem::Init(4);

    constexpr int kJobCount = 1000;
    std::atomic<int> completedJobs{ 0 };
    std::vector<Engine::JobHandle> handles;
    handles.reserve(kJobCount);

    for (int i = 0; i < kJobCount; ++i) {
        handles.push_back(Engine::JobSystem::Dispatch([&completedJobs]() {
            completedJobs.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    Engine::JobSystem::WaitAll(handles);
    EXPECT_EQ(completedJobs.load(std::memory_order_relaxed), kJobCount);

    Engine::JobSystem::Shutdown();
}

TEST(JobSystemTest, UninitializedFallbackExecutesSynchronously) {
    // Ensure JobSystem is shut down
    Engine::JobSystem::Shutdown();
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());

    int value = 0;
    auto handle = Engine::JobSystem::Dispatch([&value]() {
        value = 99;
    });

    EXPECT_TRUE(handle.IsComplete());
    EXPECT_EQ(value, 99);

    std::vector<int> data(10, 0);
    Engine::JobSystem::ParallelFor(10, [&data](size_t i) {
        data[i] = 7;
    });

    for (int x : data) {
        EXPECT_EQ(x, 7);
    }
}

TEST(JobSystemTest, CacheLineAlignmentAndFalseSharingPrevention) {
    // 1. Verify Scene::Box2DTask alignment and size
    static_assert(alignof(Engine::Scene::Box2DTask) == 64, "Box2DTask must be aligned to 64 bytes");
    static_assert(sizeof(Engine::Scene::Box2DTask) == 64, "Box2DTask size must equal 64 bytes for cache isolation");
    EXPECT_EQ(alignof(Engine::Scene::Box2DTask), 64u);
    EXPECT_EQ(sizeof(Engine::Scene::Box2DTask), 64u);

    // 2. Verify adjacent task slots in Scene::m_Box2DTasks array are exactly 64 bytes apart
    Engine::Scene scene;
    const auto* slots = scene.GetBox2DTaskSlots();
    const uintptr_t slot0 = reinterpret_cast<uintptr_t>(&slots[0]);
    const uintptr_t slot1 = reinterpret_cast<uintptr_t>(&slots[1]);
    EXPECT_EQ(slot1 - slot0, 64u);
    EXPECT_EQ(slot0 % 64, 0u);
    EXPECT_EQ(slot1 % 64, 0u);

    // 3. Verify JobSystem queue alignments
    static_assert(alignof(Engine::JobSystem::WorkerQueue) >= 64, "WorkerQueue must be aligned to 64 bytes");
    static_assert(alignof(Engine::JobSystem::GlobalQueue) >= 64, "GlobalQueue must be aligned to 64 bytes");
    EXPECT_GE(alignof(Engine::JobSystem::WorkerQueue), 64u);
    EXPECT_GE(alignof(Engine::JobSystem::GlobalQueue), 64u);
}

TEST(JobSystemTest, HighFrequencySingleJobProducerConsumerStress) {
    // Test with 4 worker threads
    Engine::JobSystem::Init(4);
    EXPECT_TRUE(Engine::JobSystem::IsInitialized());

    // 1. High-frequency sequential producer-consumer stress (single job dispatch + wait in tight loop)
    // This is the maximum hazard pattern for condition variable lost wakeups
    constexpr int kSequentialIterations = 10000;
    std::atomic<int> seqCounter{ 0 };

    for (int i = 0; i < kSequentialIterations; ++i) {
        auto handle = Engine::JobSystem::Dispatch([&seqCounter]() {
            seqCounter.fetch_add(1, std::memory_order_relaxed);
        });
        handle.Wait();
    }
    EXPECT_EQ(seqCounter.load(std::memory_order_relaxed), kSequentialIterations);
    EXPECT_EQ(Engine::JobSystem::GetPendingJobCount(), 0u);

    // 2. High-frequency parallel producer-consumer stress (100k micro-jobs dispatched concurrently)
    constexpr int kConcurrentJobs = 100000;
    std::atomic<int> concurrentCounter{ 0 };
    std::vector<Engine::JobHandle> handles;
    handles.reserve(kConcurrentJobs);

    for (int i = 0; i < kConcurrentJobs; ++i) {
        handles.push_back(Engine::JobSystem::Dispatch([&concurrentCounter]() {
            concurrentCounter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    Engine::JobSystem::WaitAll(handles);
    EXPECT_EQ(concurrentCounter.load(std::memory_order_relaxed), kConcurrentJobs);
    EXPECT_EQ(Engine::JobSystem::GetPendingJobCount(), 0u);

    Engine::JobSystem::Shutdown();
    EXPECT_FALSE(Engine::JobSystem::IsInitialized());
}

} // namespace

