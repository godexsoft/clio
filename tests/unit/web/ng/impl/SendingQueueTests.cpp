#include "util/AsioContextTestFixture.hpp"
#include "util/CoroutineGroup.hpp"
#include "web/ng/impl/SendingQueue.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using namespace web::ng::impl;

struct SendingQueueTests : SyncAsioContextTest {};

TEST_F(SendingQueueTests, SendsInOrder)
{
    std::vector<std::string> sent;
    SendingQueue<std::string> queue{[&sent](std::string const& message, auto&&) {
        sent.push_back(message);
    }};

    runSpawn([&queue](boost::asio::yield_context yield) {
        EXPECT_TRUE(queue.send("one", yield).has_value());
        EXPECT_TRUE(queue.send("two", yield).has_value());
    });

    EXPECT_EQ(sent, (std::vector<std::string>{"one", "two"}));
}

// While one coroutine is draining the queue, other senders only push and return. Without a limit on
// queue_ itself nothing bounds how many of them may pile up.
TEST_F(SendingQueueTests, UnboundedByDefault)
{
    constexpr size_t kSendersWhileBlocked = 100;
    size_t sentCount = 0;

    SendingQueue<std::string> queue{[&sentCount](std::string const&, auto&& yield) {
        boost::asio::post(yield);  // the peer is slow: suspend inside the drain loop
        ++sentCount;
    }};

    runSpawn([&queue](boost::asio::yield_context yield) {
        util::CoroutineGroup group{yield};
        for (size_t i = 0; i <= kSendersWhileBlocked; ++i) {
            group.spawn(yield, [&queue](boost::asio::yield_context innerYield) {
                EXPECT_TRUE(queue.send("message", innerYield).has_value());
            });
        }
        group.asyncWait(yield);
    });

    EXPECT_EQ(sentCount, kSendersWhileBlocked + 1);
}

// The regression test: with a limit set, the sender that would push past it is rejected with
// timed_out instead of growing the queue. Before the fix SendingQueue had no limit at all and every
// one of these senders succeeded.
TEST_F(SendingQueueTests, RejectsWhenFull)
{
    constexpr size_t kMaxSize = 4;
    size_t sentCount = 0;
    size_t rejectedCount = 0;

    SendingQueue<std::string> queue{
        [&sentCount](std::string const&, auto&& yield) {
            boost::asio::post(yield);  // the peer is slow: suspend inside the drain loop
            ++sentCount;
        },
        kMaxSize
    };

    runSpawn([&queue, &rejectedCount](boost::asio::yield_context yield) {
        util::CoroutineGroup group{yield};
        for (size_t i = 0; i < kMaxSize * 4; ++i) {
            group.spawn(yield, [&queue, &rejectedCount](boost::asio::yield_context innerYield) {
                auto const result = queue.send("message", innerYield);
                if (not result.has_value()) {
                    EXPECT_EQ(result.error(), boost::asio::error::timed_out);
                    ++rejectedCount;
                }
            });
        }
        group.asyncWait(yield);
    });

    EXPECT_GT(rejectedCount, 0u) << "the limit must reject senders once the queue is full";
    EXPECT_LE(sentCount, kMaxSize + 1) << "no more than the limit may ever be pending";
}

// Once the limit has been hit the connection is doomed, so every later send fails too rather than
// silently resuming.
TEST_F(SendingQueueTests, StaysFailedAfterRejection)
{
    SendingQueue<std::string> queue{
        [](std::string const&, auto&& yield) { boost::asio::post(yield); }, 1
    };

    runSpawn([&queue](boost::asio::yield_context yield) {
        util::CoroutineGroup group{yield};
        for (size_t i = 0; i < 4; ++i) {
            group.spawn(yield, [&queue](boost::asio::yield_context innerYield) {
                queue.send("message", innerYield);
            });
        }
        group.asyncWait(yield);

        auto const result = queue.send("after", yield);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), boost::asio::error::timed_out);
    });
}

TEST_F(SendingQueueTests, SetMaxSizeAppliesLater)
{
    SendingQueue<std::string> queue{[](std::string const&, auto&& yield) {
        boost::asio::post(yield);
    }};
    queue.setMaxSize(1);

    runSpawn([&queue](boost::asio::yield_context yield) {
        util::CoroutineGroup group{yield};
        bool rejected = false;
        for (size_t i = 0; i < 4; ++i) {
            group.spawn(yield, [&queue, &rejected](boost::asio::yield_context innerYield) {
                if (not queue.send("message", innerYield).has_value())
                    rejected = true;
            });
        }
        group.asyncWait(yield);
        EXPECT_TRUE(rejected);
    });
}
