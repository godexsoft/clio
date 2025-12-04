//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "util/ObservableValue.hpp"

#include <boost/signals2/connection.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace testing;
using namespace util;

namespace {

template <typename T>
struct NotificationCounter {
    std::atomic<int> count{0};
    std::vector<T> values;
    std::mutex valuesMutex;

    void
    operator()(T const& value)
    {
        count.fetch_add(1);
        std::lock_guard<std::mutex> lock(valuesMutex);
        values.push_back(value);
    }

    std::vector<T>
    getValues() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(valuesMutex));
        return values;
    }
};

}  // namespace

class ObservableValueAtomicTest : public ::testing::Test {};

TEST_F(ObservableValueAtomicTest, BasicConstruction)
{
    ObservableValue<std::atomic<int>> obs{42};

    EXPECT_EQ(obs.get(), 42);
    EXPECT_EQ(static_cast<int>(obs), 42);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueAtomicTest, DefaultConstruction)
{
    ObservableValue<std::atomic<int>> obsInt;
    EXPECT_EQ(obsInt.get(), 0);

    ObservableValue<std::atomic<bool>> obsBool;
    EXPECT_FALSE(obsBool.get());

    EXPECT_FALSE(obsInt.hasObservers());
    EXPECT_FALSE(obsBool.hasObservers());
}

TEST_F(ObservableValueAtomicTest, BasicObservation)
{
    ObservableValue<std::atomic<int>> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    obs = 20;
    EXPECT_EQ(obs.get(), 20);
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_EQ(values.size(), 1);
    EXPECT_EQ(values[0], 20);
}

TEST_F(ObservableValueAtomicTest, SetMethod)
{
    ObservableValue<std::atomic<int>> obs{5};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    obs.set(15);
    EXPECT_EQ(obs.get(), 15);
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 15);

    obs.set(15);  // Same value should not notify
    EXPECT_EQ(obs.get(), 15);
    EXPECT_EQ(counter.count.load(), 1);
}

TEST_F(ObservableValueAtomicTest, AtomicGuardBasicUsage)
{
    ObservableValue<std::atomic<int>> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        guard.store(25);
    }

    EXPECT_EQ(obs.get(), 25);
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 25);
}

TEST_F(ObservableValueAtomicTest, AtomicGuardIntermediateChanges)
{
    ObservableValue<std::atomic<int>> obs{50};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        guard.store(100);  // Should notify: 50→100
        guard.store(50);   // Should notify: 100→50
    }

    EXPECT_EQ(obs.get(), 50);
    EXPECT_EQ(counter.count.load(), 2);  // Two notifications for intermediate changes

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 100);  // First change: 50→100
    EXPECT_EQ(values[1], 50);   // Second change: 100→50
}

TEST_F(ObservableValueAtomicTest, AtomicGuardNoChangeNoNotification)
{
    ObservableValue<std::atomic<int>> obs{42};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        guard.store(42);  // Same value, should not notify
        guard.store(42);  // Same value again, should not notify
    }

    EXPECT_EQ(obs.get(), 42);
    EXPECT_EQ(counter.count.load(), 0);  // No notifications for same values
}

TEST_F(ObservableValueAtomicTest, AtomicGuardMultipleChanges)
{
    ObservableValue<std::atomic<int>> obs{1};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        guard.store(2);
    }

    {
        auto guard = obs.operator->();
        guard.store(3);
    }

    EXPECT_EQ(obs.get(), 3);
    EXPECT_EQ(counter.count.load(), 2);

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 2);
    EXPECT_EQ(values[1], 3);
}

TEST_F(ObservableValueAtomicTest, DirectAtomicAccess)
{
    ObservableValue<std::atomic<int>> obs{100};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    // Direct atomic access bypasses observation
    obs.atomic().store(200);
    EXPECT_EQ(obs.get(), 200);
    EXPECT_EQ(counter.count.load(), 0);  // No notification

    // But observable operations still work
    obs.set(300);
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 300);
}

TEST_F(ObservableValueAtomicTest, MultipleObservers)
{
    ObservableValue<std::atomic<int>> obs{0};

    NotificationCounter<int> counter1, counter2;

    auto conn1 = obs.observe(std::ref(counter1));
    auto conn2 = obs.observe(std::ref(counter2));

    obs = 42;

    EXPECT_EQ(counter1.count.load(), 1);
    EXPECT_EQ(counter2.count.load(), 1);

    auto values1 = counter1.getValues();
    auto values2 = counter2.getValues();
    EXPECT_EQ(values1[0], 42);
    EXPECT_EQ(values2[0], 42);

    conn1.disconnect();
    obs = 100;

    EXPECT_EQ(counter1.count.load(), 1);  // No more notifications
    EXPECT_EQ(counter2.count.load(), 2);  // Still getting notifications
}

TEST_F(ObservableValueAtomicTest, ThreadSafetyBasic)
{
    ObservableValue<std::atomic<int>> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 4;
    static constexpr auto kINCREMENTS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs]() {
            for (int j = 0; j < kINCREMENTS_PER_THREAD; ++j) {
                int expected = obs.get();
                int newValue = expected + 1;
                obs.set(newValue);
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    // Final value may be less than kNumThreads * kIncrementsPerThread due to race conditions
    EXPECT_GT(obs.get(), 0);
    EXPECT_GT(counter.count.load(), 0);

    auto values = counter.getValues();
    for (auto const& value : values) {
        EXPECT_GT(value, 0);
    }
}

TEST_F(ObservableValueAtomicTest, ThreadSafetyWithGuards)
{
    ObservableValue<std::atomic<int>> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 4;
    static constexpr auto kOPERATIONS_PER_THREAD = 50;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs]() {
            for (int j = 0; j < kOPERATIONS_PER_THREAD; ++j) {
                {
                    auto guard = obs.operator->();
                    int current = guard.load();
                    guard.store(current + 1);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_GT(obs.get(), 0);
    EXPECT_GT(counter.count.load(), 0);
}

TEST_F(ObservableValueAtomicTest, AtomicBoolSpecialization)
{
    ObservableValue<std::atomic<bool>> obs{false};
    NotificationCounter<bool> counter;

    auto connection = obs.observe(std::ref(counter));

    obs = true;
    EXPECT_TRUE(obs.get());
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_TRUE(values[0]);

    obs = true;  // Same value should not notify
    EXPECT_EQ(counter.count.load(), 1);

    obs.set(false);
    EXPECT_FALSE(obs.get());
    EXPECT_EQ(counter.count.load(), 2);
}

TEST_F(ObservableValueAtomicTest, CompareAndSwapBehavior)
{
    ObservableValue<std::atomic<int>> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    // Test that compare-and-swap works correctly in set()
    obs.set(10);  // Same value, should not notify
    EXPECT_EQ(counter.count.load(), 0);

    obs.set(20);  // Different value, should notify
    EXPECT_EQ(counter.count.load(), 1);

    auto values = counter.getValues();
    EXPECT_EQ(values[0], 20);
}

TEST_F(ObservableValueAtomicTest, RaceConditionNotificationIntegrity)
{
    ObservableValue<std::atomic<int>> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 10;
    static constexpr auto kOPERATIONS_PER_THREAD = 20;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs]() {
            for (int j = 0; j < kOPERATIONS_PER_THREAD; ++j) {
                obs.set(j % 3);
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_GT(counter.count.load(), 0);

    auto values = counter.getValues();
    for (auto const& value : values) {
        EXPECT_GE(value, 0);
        EXPECT_LE(value, 2);
    }

    int finalValue = obs.get();
    EXPECT_GE(finalValue, 0);
    EXPECT_LE(finalValue, 2);
}

TEST_F(ObservableValueAtomicTest, DeterministicNotificationTest)
{
    ObservableValue<std::atomic<int>> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 5;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs, i]() { obs.set(i + 1); });
    }

    for (auto& thread : threads)
        thread.join();

    // Each thread sets a unique value, so expect exactly kNumThreads notifications
    EXPECT_EQ(counter.count.load(), kNUM_THREADS);

    auto values = counter.getValues();
    EXPECT_EQ(values.size(), kNUM_THREADS);

    for (auto const& value : values) {
        EXPECT_GE(value, 1);
        EXPECT_LE(value, kNUM_THREADS);
    }

    int finalValue = obs.get();
    EXPECT_GE(finalValue, 1);
    EXPECT_LE(finalValue, kNUM_THREADS);
}

TEST_F(ObservableValueAtomicTest, NoNotificationForSameValue)
{
    ObservableValue<std::atomic<int>> obs{42};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 10;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs]() { obs.set(42); });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(counter.count.load(), 0);  // No notifications since value never changed
    EXPECT_EQ(obs.get(), 42);
}

TEST_F(ObservableValueAtomicTest, AtomicGuardRaceConditionCorrectness)
{
    ObservableValue<std::atomic<int>> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    static constexpr auto kNUM_THREADS = 3;

    std::vector<std::thread> threads;
    threads.reserve(kNUM_THREADS);

    // Test that guards properly notify for all value changes
    // Each thread will make unique changes to avoid race condition conflicts
    for (int i = 0; i < kNUM_THREADS; ++i) {
        threads.emplace_back([&obs, i]() {
            auto guard = obs.operator->();
            int baseValue = (i + 1) * 10;  // 10, 20, 30
            guard.store(baseValue);        // Store unique values
            guard.store(baseValue + 1);    // Then increment
        });
    }

    for (auto& thread : threads)
        thread.join();

    // We should get some notifications (exact count depends on race conditions)
    // but at least one per thread since they use unique base values
    EXPECT_GE(counter.count.load(), kNUM_THREADS);

    auto values = counter.getValues();
    EXPECT_GE(values.size(), kNUM_THREADS);

    for (auto const& value : values)
        EXPECT_GT(value, 0);
}
