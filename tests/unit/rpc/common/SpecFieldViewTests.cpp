#include "rpc/common/spec/FieldView.hpp"

#include <boost/json/parse.hpp>
#include <gtest/gtest.h>

using namespace rpc::spec;

// FieldView / ObjectView navigation — child() and element() on the boost::json backend.

TEST(RpcSpecDSL_FieldView, ChildReturnsAbsentFaWhenParentAbsent)
{
    auto request = boost::json::parse(R"JSON({})JSON");
    ObjectView root{request};
    auto fa = root.child("foo");
    EXPECT_FALSE(fa.present());
    auto child = fa.child("bar");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldView, ChildReturnsAbsentFaWhenParentNotObject)
{
    auto request = boost::json::parse(R"JSON({ "foo": 42 })JSON");
    ObjectView root{request};
    auto fa = root.child("foo");
    EXPECT_TRUE(fa.present());
    EXPECT_FALSE(fa.isObject());
    auto child = fa.child("bar");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldView, ChildNavigatesIntoSubObject)
{
    auto request = boost::json::parse(R"JSON({ "foo": { "bar": "hello" } })JSON");
    ObjectView root{request};
    auto fa = root.child("foo");
    ASSERT_TRUE(fa.present());
    ASSERT_TRUE(fa.isObject());

    auto child = fa.child("bar");
    ASSERT_TRUE(child.present());
    EXPECT_TRUE(child.isString());
    EXPECT_EQ(child.asString(), "hello");
}

TEST(RpcSpecDSL_FieldView, ChildMissingKeyReturnsAbsent)
{
    auto request = boost::json::parse(R"JSON({ "foo": { "a": 1 } })JSON");
    ObjectView root{request};
    auto fa = root.child("foo");
    auto child = fa.child("missing");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldView, ElementNavigatesIntoArray)
{
    auto request = boost::json::parse(R"JSON({ "ids": [10, 20, 30] })JSON");
    ObjectView root{request};
    auto fa = root.child("ids");
    ASSERT_TRUE(fa.isArray());

    auto elem0 = fa.element(0);
    ASSERT_TRUE(elem0.present());
    EXPECT_TRUE(elem0.isInt64());
    EXPECT_EQ(elem0.asInt64(), 10);

    auto elem2 = fa.element(2);
    ASSERT_TRUE(elem2.present());
    EXPECT_EQ(elem2.asInt64(), 30);
}

TEST(RpcSpecDSL_FieldView, ElementOutOfBoundsReturnsAbsent)
{
    auto request = boost::json::parse(R"JSON({ "ids": [1, 2] })JSON");
    ObjectView root{request};
    auto fa = root.child("ids");
    EXPECT_FALSE(fa.element(5).present());
}

// Root-level navigation on a non-object input — root.isObject() reports false and
// child() returns absent FAs without crashing.
TEST(RpcSpecDSL_FieldView, RootOverNonObjectReportsIsObjectFalse)
{
    auto arr = boost::json::parse(R"JSON([1, 2, 3])JSON");
    ObjectView root{arr};
    EXPECT_FALSE(root.isObject());
    EXPECT_TRUE(root.isArray());
    auto fa = root.child("anything");
    EXPECT_FALSE(fa.present());
}

// Const-constructed ObjectView preserves read-only navigation; the FA it yields
// cannot be written through.
TEST(RpcSpecDSL_FieldView, ConstRootYieldsReadOnlyChild)
{
    auto const request = boost::json::parse(R"JSON({ "foo": 1 })JSON");
    ObjectView const root{request};
    auto fa = root.child("foo");
    ASSERT_TRUE(fa.present());
    EXPECT_TRUE(fa.isInt64());
    EXPECT_EQ(fa.asInt64(), 1);
}
