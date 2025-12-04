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

#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace testing;
using namespace util;

namespace {

struct TestStruct {
    int value = 0;
    std::string name;

    bool
    operator==(TestStruct const& other) const
    {
        return value == other.value && name == other.name;
    }

    bool
    operator!=(TestStruct const& other) const
    {
        return !(*this == other);
    }
};

template <typename T>
struct NotificationCounter {
    int count = 0;
    std::vector<T> values;

    void
    operator()(T const& value)
    {
        ++count;
        values.push_back(value);
    }
};

}  // namespace

class ObservableValueTest : public ::testing::Test {};

TEST_F(ObservableValueTest, ConceptCompliance)
{
    static_assert(Observable<int>);
    static_assert(Observable<std::string>);
    static_assert(Observable<double>);
    static_assert(Observable<TestStruct>);
    static_assert(Observable<bool>);
    static_assert(Observable<char>);
    static_assert(Observable<float>);

    struct NonCopyable {
        int value = 0;
        NonCopyable() = default;
        NonCopyable(NonCopyable const&) = delete;
        NonCopyable(NonCopyable&&) = default;
        NonCopyable&
        operator=(NonCopyable const&) = delete;
        NonCopyable&
        operator=(NonCopyable&&) = default;
        bool
        operator==(NonCopyable const& other) const
        {
            return value == other.value;
        }
    };
    static_assert(!Observable<NonCopyable>);

    struct NonMovable {
        int value = 0;
        NonMovable() = default;
        NonMovable(NonMovable const&) = default;
        NonMovable(NonMovable&&) = delete;
        NonMovable&
        operator=(NonMovable const&) = default;
        NonMovable&
        operator=(NonMovable&&) = delete;
        bool
        operator==(NonMovable const& other) const
        {
            return value == other.value;
        }
    };
    static_assert(!Observable<NonMovable>);

    struct NonComparable {
        int value = 0;
        NonComparable() = default;
        NonComparable(NonComparable const&) = default;
        NonComparable(NonComparable&&) = default;
        NonComparable&
        operator=(NonComparable const&) = default;
        NonComparable&
        operator=(NonComparable&&) = default;
    };
    static_assert(!Observable<NonComparable>);

    struct NonDefaultInitializable {
        int value;
        NonDefaultInitializable() = delete;
        explicit NonDefaultInitializable(int v) : value(v)
        {
        }
        NonDefaultInitializable(NonDefaultInitializable const&) = default;
        NonDefaultInitializable(NonDefaultInitializable&&) = default;
        NonDefaultInitializable&
        operator=(NonDefaultInitializable const&) = default;
        NonDefaultInitializable&
        operator=(NonDefaultInitializable&&) = default;
        bool
        operator==(NonDefaultInitializable const& other) const
        {
            return value == other.value;
        }
    };

    static_assert(Observable<NonDefaultInitializable>);
    static_assert(!std::default_initializable<NonDefaultInitializable>);

    static_assert(Observable<std::vector<int>>);
    static_assert(Observable<std::map<int, int>>);
    static_assert(Observable<std::set<int>>);
    static_assert(Observable<std::pair<int, std::string>>);

    static_assert(std::default_initializable<int>);
    static_assert(std::default_initializable<std::string>);
    static_assert(std::default_initializable<std::vector<int>>);
    static_assert(std::default_initializable<TestStruct>);
}

TEST_F(ObservableValueTest, Construction)
{
    ObservableValue<int> obs{42};

    EXPECT_EQ(static_cast<int>(obs), 42);
    EXPECT_EQ(obs.get(), 42);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, ConstructionWithDifferentTypes)
{
    ObservableValue<std::string> obsStr{"hello"};
    EXPECT_EQ(obsStr.get(), "hello");

    ObservableValue<double> obsDouble{3.14};
    EXPECT_DOUBLE_EQ(obsDouble.get(), 3.14);

    ObservableValue<bool> obsBool{true};
    EXPECT_TRUE(obsBool.get());
}

TEST_F(ObservableValueTest, DefaultConstruction)
{
    ObservableValue<int> obsInt;
    EXPECT_EQ(obsInt.get(), 0);

    ObservableValue<double> obsDouble;
    EXPECT_DOUBLE_EQ(obsDouble.get(), 0.0);

    ObservableValue<bool> obsBool;
    EXPECT_FALSE(obsBool.get());

    ObservableValue<char> obsChar;
    EXPECT_EQ(obsChar.get(), '\0');

    EXPECT_FALSE(obsInt.hasObservers());
    EXPECT_FALSE(obsDouble.hasObservers());
    EXPECT_FALSE(obsBool.hasObservers());
    EXPECT_FALSE(obsChar.hasObservers());
}

TEST_F(ObservableValueTest, DefaultConstructionWithContainers)
{
    ObservableValue<std::string> obsString;
    EXPECT_EQ(obsString.get(), "");
    EXPECT_TRUE(obsString.get().empty());

    ObservableValue<std::vector<int>> obsVector;
    EXPECT_TRUE(obsVector.get().empty());
    EXPECT_EQ(obsVector.get().size(), 0);

    ObservableValue<std::set<int>> obsSet;
    EXPECT_TRUE(obsSet.get().empty());
    EXPECT_EQ(obsSet.get().size(), 0);

    ObservableValue<std::map<int, std::string>> obsMap;
    EXPECT_TRUE(obsMap.get().empty());
    EXPECT_EQ(obsMap.get().size(), 0);
}

TEST_F(ObservableValueTest, DefaultConstructionWithCustomType)
{
    ObservableValue<TestStruct> obsStruct;
    EXPECT_EQ(obsStruct.get().value, 0);
    EXPECT_EQ(obsStruct.get().name, "");
}

TEST_F(ObservableValueTest, DefaultConstructionThenAssignment)
{
    ObservableValue<int> obs;
    EXPECT_EQ(obs.get(), 0);

    NotificationCounter<int> counter;
    auto connection = obs.observe(std::ref(counter));

    obs = 42;
    EXPECT_EQ(obs.get(), 42);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 42);

    obs = 42;
    EXPECT_EQ(counter.count, 1);

    obs.set(100);
    EXPECT_EQ(obs.get(), 100);
    EXPECT_EQ(counter.count, 2);
    EXPECT_EQ(counter.values[1], 100);
}

TEST_F(ObservableValueTest, DefaultConstructionWithGuard)
{
    ObservableValue<std::string> obs;
    EXPECT_EQ(obs.get(), "");

    NotificationCounter<std::string> counter;
    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        std::string& ref = guard;
        ref = "modified through guard";
    }

    EXPECT_EQ(obs.get(), "modified through guard");
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], "modified through guard");
}

TEST_F(ObservableValueTest, DefaultConstructionNotificationBehavior)
{
    ObservableValue<int> obs;
    NotificationCounter<int> counter;
    auto connection = obs.observe(std::ref(counter));

    obs = 1;
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 1);

    obs = 0;
    EXPECT_EQ(counter.count, 2);
    EXPECT_EQ(counter.values[1], 0);

    obs = 0;
    EXPECT_EQ(counter.count, 2);
}

TEST_F(ObservableValueTest, NonDefaultInitializableTypeWithParameterizedConstructor)
{
    struct NonDefaultInitializable {
        int value;
        NonDefaultInitializable() = delete;
        explicit NonDefaultInitializable(int v) : value(v)
        {
        }
        NonDefaultInitializable(NonDefaultInitializable const&) = default;
        NonDefaultInitializable(NonDefaultInitializable&&) = default;
        NonDefaultInitializable&
        operator=(NonDefaultInitializable const&) = default;
        NonDefaultInitializable&
        operator=(NonDefaultInitializable&&) = default;
        bool
        operator==(NonDefaultInitializable const& other) const
        {
            return value == other.value;
        }
    };

    ObservableValue<NonDefaultInitializable> obs{NonDefaultInitializable{42}};
    EXPECT_EQ(obs.get().value, 42);

    NotificationCounter<NonDefaultInitializable> counter;
    auto connection = obs.observe(std::ref(counter));

    obs = NonDefaultInitializable{100};
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0].value, 100);
    EXPECT_EQ(obs.get().value, 100);
}

TEST_F(ObservableValueTest, MoveSemantics)
{
    ObservableValue<int> obs1{100};

    ObservableValue<int> obs2 = std::move(obs1);
    EXPECT_EQ(obs2.get(), 100);

    ObservableValue<int> obs3{200};
    obs3 = std::move(obs2);
    EXPECT_EQ(obs3.get(), 100);
}

TEST_F(ObservableValueTest, CopyOperationsDeleted)
{
    static_assert(!std::is_copy_constructible_v<ObservableValue<int>>);
    static_assert(!std::is_copy_assignable_v<ObservableValue<int>>);
}

TEST_F(ObservableValueTest, AssignmentOperator)
{
    ObservableValue<int> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    obs = 20;
    EXPECT_EQ(obs.get(), 20);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 20);

    obs = 20;
    EXPECT_EQ(obs.get(), 20);
    EXPECT_EQ(counter.count, 1);
}

TEST_F(ObservableValueTest, SetMethod)
{
    ObservableValue<int> obs{5};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    obs.set(15);
    EXPECT_EQ(obs.get(), 15);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 15);

    obs.set(15);
    EXPECT_EQ(obs.get(), 15);
    EXPECT_EQ(counter.count, 1);
}

TEST_F(ObservableValueTest, ObserverManagement)
{
    ObservableValue<int> obs{0};

    EXPECT_FALSE(obs.hasObservers());

    NotificationCounter<int> counter1, counter2;

    auto conn1 = obs.observe(std::ref(counter1));
    EXPECT_TRUE(obs.hasObservers());

    auto conn2 = obs.observe(std::ref(counter2));
    EXPECT_TRUE(obs.hasObservers());

    obs = 42;
    EXPECT_EQ(counter1.count, 1);
    EXPECT_EQ(counter2.count, 1);
    EXPECT_EQ(counter1.values[0], 42);
    EXPECT_EQ(counter2.values[0], 42);

    conn1.disconnect();
    obs = 100;
    EXPECT_EQ(counter1.count, 1);
    EXPECT_EQ(counter2.count, 2);
    EXPECT_EQ(counter2.values[1], 100);

    conn2.disconnect();
    EXPECT_FALSE(obs.hasObservers());

    obs = 200;
    EXPECT_EQ(counter1.count, 1);
    EXPECT_EQ(counter2.count, 2);
}

TEST_F(ObservableValueTest, ObservableGuardBasicUsage)
{
    ObservableValue<int> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 25;
    }

    EXPECT_EQ(obs.get(), 25);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 25);
}

TEST_F(ObservableValueTest, ObservableGuardNoChangeNoNotification)
{
    ObservableValue<int> obs{50};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 100;
        ref = 50;
    }

    EXPECT_EQ(obs.get(), 50);
    EXPECT_EQ(counter.count, 0);
}

TEST_F(ObservableValueTest, ObservableGuardMultipleChanges)
{
    ObservableValue<int> obs{1};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 2;
    }

    {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 3;
    }

    EXPECT_EQ(obs.get(), 3);
    EXPECT_EQ(counter.count, 2);
    EXPECT_EQ(counter.values[0], 2);
    EXPECT_EQ(counter.values[1], 3);
}

TEST_F(ObservableValueTest, ComplexTypeObservation)
{
    TestStruct initial{.value = 42, .name = "test"};
    ObservableValue<TestStruct> obs{initial};

    NotificationCounter<TestStruct> counter;
    auto connection = obs.observe(std::ref(counter));

    TestStruct newValue{.value = 100, .name = "changed"};
    obs = newValue;

    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0].value, 100);
    EXPECT_EQ(counter.values[0].name, "changed");
}

TEST_F(ObservableValueTest, ComplexTypeGuardModification)
{
    TestStruct initial{.value = 10, .name = "initial"};
    ObservableValue<TestStruct> obs{initial};

    NotificationCounter<TestStruct> counter;
    auto connection = obs.observe(std::ref(counter));

    {
        auto guard = obs.operator->();
        TestStruct& ref = guard;
        ref.value = 20;
        ref.name = "modified";
    }

    EXPECT_EQ(obs.get().value, 20);
    EXPECT_EQ(obs.get().name, "modified");
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0].value, 20);
    EXPECT_EQ(counter.values[0].name, "modified");
}

TEST_F(ObservableValueTest, StringObservation)
{
    ObservableValue<std::string> obs{"initial"};
    NotificationCounter<std::string> counter;

    auto connection = obs.observe(std::ref(counter));

    obs = "changed";
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], "changed");

    obs.set("set_method");
    EXPECT_EQ(counter.count, 2);
    EXPECT_EQ(counter.values[1], "set_method");

    obs = "set_method";
    EXPECT_EQ(counter.count, 2);
}

TEST_F(ObservableValueTest, MultipleObserversWithDifferentLifetimes)
{
    ObservableValue<int> obs{0};

    NotificationCounter<int> counter1, counter2, counter3;

    auto conn1 = obs.observe(std::ref(counter1));

    obs = 1;
    EXPECT_EQ(counter1.count, 1);

    auto conn2 = obs.observe(std::ref(counter2));
    obs = 2;
    EXPECT_EQ(counter1.count, 2);
    EXPECT_EQ(counter2.count, 1);

    conn1.disconnect();
    auto conn3 = obs.observe(std::ref(counter3));
    obs = 3;
    EXPECT_EQ(counter1.count, 2);
    EXPECT_EQ(counter2.count, 2);
    EXPECT_EQ(counter3.count, 1);
}

TEST_F(ObservableValueTest, NoNotificationWhenNoObservers)
{
    ObservableValue<int> obs{0};

    obs = 1;
    obs.set(2);

    {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 3;
    }

    EXPECT_EQ(obs.get(), 3);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, ManyObservers)
{
    ObservableValue<int> obs{0};

    std::vector<std::unique_ptr<NotificationCounter<int>>> counters;
    std::vector<boost::signals2::connection> connections;

    constexpr int kNUM_OBSERVERS = 100;
    for (int i = 0; i < kNUM_OBSERVERS; ++i) {
        counters.push_back(std::make_unique<NotificationCounter<int>>());
        connections.push_back(obs.observe(std::ref(*counters.back())));
    }

    EXPECT_TRUE(obs.hasObservers());

    obs = 42;

    for (auto const& counter : counters) {
        EXPECT_EQ(counter->count, 1);
        EXPECT_EQ(counter->values[0], 42);
    }

    for (auto& conn : connections) {
        conn.disconnect();
    }

    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, TypeConversions)
{
    ObservableValue<double> obs{1.0};

    NotificationCounter<double> doubleCounter;
    auto connection = obs.observe(std::ref(doubleCounter));

    obs = 2;
    obs = 3.14;
    obs = static_cast<double>(4.0f);

    EXPECT_EQ(doubleCounter.count, 3);
    EXPECT_DOUBLE_EQ(doubleCounter.values[0], 2.0);
    EXPECT_DOUBLE_EQ(doubleCounter.values[1], 3.14);
    EXPECT_DOUBLE_EQ(doubleCounter.values[2], 4.0);
}

TEST_F(ObservableValueTest, EnhancedConceptRequirements)
{
    struct ComplexObservable {
        std::string name;
        int value{};
        std::vector<int> data;

        ComplexObservable() = default;
        ComplexObservable(std::string n, int v, std::vector<int> d) : name(std::move(n)), value(v), data(std::move(d))
        {
        }
        ComplexObservable(ComplexObservable const& other) = default;
        ComplexObservable(ComplexObservable&& other) noexcept = default;

        ComplexObservable&
        operator=(ComplexObservable&& other) noexcept
        {
            if (this != &other) {
                name = std::move(other.name);
                value = other.value;
                data = std::move(other.data);
            }
            return *this;
        }

        bool
        operator==(ComplexObservable const& other) const
        {
            return name == other.name && value == other.value && data == other.data;
        }

        ComplexObservable&
        operator=(ComplexObservable const& other)
        {
            if (this != &other) {
                name = other.name;
                value = other.value;
                data = other.data;
            }
            return *this;
        }
    };

    static_assert(Observable<ComplexObservable>);

    ComplexObservable initial{"test", 42, {1, 2, 3}};
    ObservableValue<ComplexObservable> obs{std::move(initial)};

    NotificationCounter<ComplexObservable> counter;
    auto connection = obs.observe(std::ref(counter));

    ComplexObservable newValue{"changed", 100, {4, 5, 6}};
    obs = std::move(newValue);

    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0].name, "changed");
    EXPECT_EQ(counter.values[0].value, 100);
    EXPECT_EQ(counter.values[0].data, std::vector<int>({4, 5, 6}));

    ComplexObservable sameValue{"changed", 100, {4, 5, 6}};
    obs = std::move(sameValue);
    EXPECT_EQ(counter.count, 1);
}

TEST_F(ObservableValueTest, ExceptionInObserver)
{
    ObservableValue<int> obs{0};

    NotificationCounter<int> goodCounter;
    auto goodConnection = obs.observe(std::ref(goodCounter));

    auto throwingConnection = obs.observe([](int const&) { throw std::runtime_error("Observer exception"); });

    EXPECT_THROW(obs = 42, std::runtime_error);

    // Value is still updated even when observers throw
    EXPECT_EQ(obs.get(), 42);
}

TEST_F(ObservableValueTest, GuardExceptionSafety)
{
    ObservableValue<int> obs{10};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    try {
        auto guard = obs.operator->();
        int& ref = guard;
        ref = 20;
        throw std::runtime_error("Test exception");
    } catch (...) {
        [[maybe_unused]] auto nothing = true;
    }

    EXPECT_EQ(obs.get(), 20);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(counter.values[0], 20);
}

TEST_F(ObservableValueTest, ComprehensiveIntegrationTest)
{
    ObservableValue<std::string> obs{"start"};

    NotificationCounter<std::string> counter1, counter2;
    auto conn1 = obs.observe(std::ref(counter1));
    auto conn2 = obs.observe(std::ref(counter2));

    obs = "first";
    obs.set("second");
    obs = "second";

    {
        auto guard = obs.operator->();
        std::string& ref = guard;
        ref = "third";
    }

    conn1.disconnect();
    obs = "fourth";

    EXPECT_EQ(obs.get(), "fourth");

    EXPECT_EQ(counter1.count, 3);
    EXPECT_EQ(counter1.values[0], "first");
    EXPECT_EQ(counter1.values[1], "second");
    EXPECT_EQ(counter1.values[2], "third");

    EXPECT_EQ(counter2.count, 4);
    EXPECT_EQ(counter2.values[0], "first");
    EXPECT_EQ(counter2.values[1], "second");
    EXPECT_EQ(counter2.values[2], "third");
    EXPECT_EQ(counter2.values[3], "fourth");

    EXPECT_TRUE(obs.hasObservers());

    conn2.disconnect();
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, RegularConnectionPersistsAfterDestruction)
{
    ObservableValue<int> obs{0};
    NotificationCounter<int> counter;

    {
        auto connection = obs.observe(std::ref(counter));
        obs = 1;
        EXPECT_EQ(counter.count, 1);
        EXPECT_EQ(counter.values[0], 1);
    }

    obs = 2;
    EXPECT_EQ(counter.count, 2);
    EXPECT_EQ(counter.values[1], 2);

    EXPECT_TRUE(obs.hasObservers());
}

TEST_F(ObservableValueTest, ScopedConnectionDisconnectsOnDestruction)
{
    ObservableValue<int> obs{0};
    NotificationCounter<int> counter;

    {
        boost::signals2::scoped_connection scoped = obs.observe(std::ref(counter));
        obs = 1;
        EXPECT_EQ(counter.count, 1);
        EXPECT_EQ(counter.values[0], 1);
        EXPECT_TRUE(obs.hasObservers());
    }

    obs = 2;
    EXPECT_EQ(counter.count, 1);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, ManualDisconnectWithRegularConnection)
{
    ObservableValue<int> obs{0};
    NotificationCounter<int> counter;

    auto connection = obs.observe(std::ref(counter));

    obs = 1;
    EXPECT_EQ(counter.count, 1);
    EXPECT_TRUE(obs.hasObservers());

    connection.disconnect();

    obs = 2;
    EXPECT_EQ(counter.count, 1);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, ScopedConnectionCanBeDisconnectedManually)
{
    ObservableValue<int> obs{0};
    NotificationCounter<int> counter;

    boost::signals2::scoped_connection scoped = obs.observe(std::ref(counter));

    obs = 1;
    EXPECT_EQ(counter.count, 1);
    EXPECT_TRUE(obs.hasObservers());

    scoped.disconnect();

    obs = 2;
    EXPECT_EQ(counter.count, 1);
    EXPECT_FALSE(obs.hasObservers());
}

TEST_F(ObservableValueTest, MixedConnectionTypes)
{
    ObservableValue<int> obs{0};
    NotificationCounter<int> counter1, counter2, counter3;

    auto regularConn = obs.observe(std::ref(counter1));

    {
        boost::signals2::scoped_connection scoped1 = obs.observe(std::ref(counter2));
        boost::signals2::scoped_connection scoped2 = obs.observe(std::ref(counter3));

        obs = 1;
        EXPECT_EQ(counter1.count, 1);
        EXPECT_EQ(counter2.count, 1);
        EXPECT_EQ(counter3.count, 1);
        EXPECT_TRUE(obs.hasObservers());
    }

    obs = 2;
    EXPECT_EQ(counter1.count, 2);
    EXPECT_EQ(counter2.count, 1);
    EXPECT_EQ(counter3.count, 1);
    EXPECT_TRUE(obs.hasObservers());

    regularConn.disconnect();
    EXPECT_FALSE(obs.hasObservers());
}
