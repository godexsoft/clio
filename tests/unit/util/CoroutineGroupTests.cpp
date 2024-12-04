//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

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

#include "util/AsioContextTestFixture.hpp"
#include "util/CoroutineGroup.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

using namespace util;

struct CoroutineGroupTests : SyncAsioContextTest {
    testing::StrictMock<testing::MockFunction<void()>> callback1;
    testing::StrictMock<testing::MockFunction<void()>> callback2;
    testing::StrictMock<testing::MockFunction<void()>> callback3;
};

TEST_F(CoroutineGroupTests, SpawnWait)
{
    testing::Sequence const sequence;
    EXPECT_CALL(callback1, Call).InSequence(sequence);
    EXPECT_CALL(callback2, Call).InSequence(sequence);
    EXPECT_CALL(callback3, Call).InSequence(sequence);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield, 2};

        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{1}};
            timer.async_wait(yield);
            callback1.Call();
        });
        EXPECT_EQ(group.size(), 1);

        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{2}};
            timer.async_wait(yield);
            callback2.Call();
        });
        EXPECT_EQ(group.size(), 2);

        group.asyncWait(yield);
        EXPECT_EQ(group.size(), 0);

        callback3.Call();
    });
}

TEST_F(CoroutineGroupTests, SpawnWaitSpawnWait)
{
    testing::Sequence const sequence;
    EXPECT_CALL(callback1, Call).InSequence(sequence);
    EXPECT_CALL(callback2, Call).InSequence(sequence);
    EXPECT_CALL(callback3, Call).InSequence(sequence);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield, 2};

        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{1}};
            timer.async_wait(yield);
            callback1.Call();
        });
        EXPECT_EQ(group.size(), 1);

        group.asyncWait(yield);
        EXPECT_EQ(group.size(), 0);

        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{1}};
            timer.async_wait(yield);
            callback2.Call();
        });
        EXPECT_EQ(group.size(), 1);

        group.asyncWait(yield);
        EXPECT_EQ(group.size(), 0);

        callback3.Call();
    });
}

TEST_F(CoroutineGroupTests, ChildCoroutinesFinishBeforeWait)
{
    testing::Sequence const sequence;
    EXPECT_CALL(callback2, Call).InSequence(sequence);
    EXPECT_CALL(callback1, Call).InSequence(sequence);
    EXPECT_CALL(callback3, Call).InSequence(sequence);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield, 2};
        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{2}};
            timer.async_wait(yield);
            callback1.Call();
        });
        group.spawn(yield, [&](boost::asio::yield_context yield) {
            boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{1}};
            timer.async_wait(yield);
            callback2.Call();
        });

        boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{3}};
        timer.async_wait(yield);

        group.asyncWait(yield);
        callback3.Call();
    });
}

TEST_F(CoroutineGroupTests, EmptyGroup)
{
    EXPECT_CALL(callback1, Call);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield};
        group.asyncWait(yield);
        callback1.Call();
    });
}

TEST_F(CoroutineGroupTests, TooManyCoroutines)
{
    EXPECT_CALL(callback1, Call);
    EXPECT_CALL(callback2, Call);
    EXPECT_CALL(callback3, Call);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield, 1};

        EXPECT_TRUE(group.spawn(yield, [this](boost::asio::yield_context innerYield) {
            boost::asio::steady_timer timer{innerYield.get_executor(), std::chrono::milliseconds{1}};
            timer.async_wait(innerYield);
            callback1.Call();
        }));

        EXPECT_FALSE(group.spawn(yield, [this](boost::asio::yield_context) { callback2.Call(); }));
        EXPECT_TRUE(group.isFull());

        boost::asio::steady_timer timer{yield.get_executor(), std::chrono::milliseconds{2}};
        timer.async_wait(yield);

        EXPECT_FALSE(group.isFull());
        EXPECT_TRUE(group.spawn(yield, [this](boost::asio::yield_context) { callback2.Call(); }));

        group.asyncWait(yield);
        callback3.Call();
    });
}

TEST_F(CoroutineGroupTests, SpawnForeign)
{
    testing::Sequence const sequence;
    EXPECT_CALL(callback1, Call).InSequence(sequence);
    EXPECT_CALL(callback2, Call).InSequence(sequence);

    runSpawn([this](boost::asio::yield_context yield) {
        CoroutineGroup group{yield, 1};

        auto const onForeignComplete = group.registerForeign();
        [&]() { ASSERT_TRUE(onForeignComplete.has_value()); }();

        [&]() { ASSERT_FALSE(group.registerForeign().has_value()); }();

        boost::asio::spawn(ctx_, [this, &onForeignComplete](boost::asio::yield_context innerYield) {
            boost::asio::steady_timer timer{innerYield.get_executor(), std::chrono::milliseconds{2}};
            timer.async_wait(innerYield);
            callback1.Call();
            onForeignComplete->operator()();
        });

        group.asyncWait(yield);
        callback2.Call();
    });
}
