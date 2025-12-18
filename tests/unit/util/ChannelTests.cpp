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

#include "util/Assert.hpp"
#include "util/Channel.hpp"
#include "util/Mutex.hpp"
#include "util/OverloadSet.hpp"
#include "util/Spawn.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/detail/error_code.hpp>
#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace testing;

namespace {

constexpr auto kDEFAULT_THREAD_POOL_SIZE = 4;

enum class ContextType { IOContext, ThreadPool };
enum class ApproachType { Spawn, Callback };

struct ChannelTestParams {
    ContextType contextType;
    ApproachType approachType;

    [[nodiscard]] std::string
    toString() const
    {
        std::string context = (contextType == ContextType::IOContext) ? "IOContext" : "ThreadPool";
        std::string approach = (approachType == ApproachType::Spawn) ? "Spawn" : "Callback";
        return fmt::format("{}Using{}", context, approach);
    }
};

class ContextWrapper {
public:
    using ContextVariant = std::variant<boost::asio::io_context, boost::asio::thread_pool>;

    explicit ContextWrapper(ContextType type)
        : context_([type] {
            if (type == ContextType::IOContext)
                return ContextVariant(std::in_place_type_t<boost::asio::io_context>());

            if (type == ContextType::ThreadPool)
                return ContextVariant(std::in_place_type_t<boost::asio::thread_pool>(), kDEFAULT_THREAD_POOL_SIZE);

            ASSERT(false, "Unknown new type of context");
            std::unreachable();
        }())
    {
    }

    template <typename T>
    [[nodiscard]] T&
    get()
    {
        return std::get<T>(context_);
    }

    void
    run()
    {
        std::visit(
            util::OverloadSet{
                [](boost::asio::io_context& context) { context.run(); },
                [](boost::asio::thread_pool& context) { context.join(); },
            },
            context_
        );
    }

private:
    ContextVariant context_;
};

}  // namespace

class ChannelParameterizedTest : public TestWithParam<ChannelTestParams> {
protected:
    ChannelParameterizedTest() : params_(GetParam()), context_(params_.contextType)
    {
    }

    ChannelTestParams params_{};
    ContextWrapper context_;

    static constexpr auto kNUM_SENDERS = 3uz;
    static constexpr auto kVALUES_PER_SENDER = 500uz;
    static constexpr auto kTOTAL_EXPECTED = kNUM_SENDERS * kVALUES_PER_SENDER;

protected:
    template <typename Sender, typename Receiver>
    void
    runIOContextTest(Sender sender, Receiver receiver, boost::asio::io_context& executor)
    {
        std::vector<int> receivedValues;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&receiver, &receivedValues](boost::asio::yield_context yield) mutable {
                while (receivedValues.size() < kTOTAL_EXPECTED) {
                    auto value = receiver.asyncReceive(yield);
                    if (not value.has_value())
                        break;
                    receivedValues.push_back(*value);
                }
            });

            for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
                util::spawn(executor, [sender, senderId](boost::asio::yield_context yield) mutable {
                    for (auto i = 0uz; i < kVALUES_PER_SENDER; ++i) {
                        int value = (senderId * 100) + i;
                        if (not sender.asyncSend(value, yield))
                            break;
                    }
                });
            }
        } else {
            auto receiveNext = [&receiver, &receivedValues](this auto&& self) -> void {
                if (receivedValues.size() >= kTOTAL_EXPECTED)
                    return;

                receiver.asyncReceive([&receivedValues, self = std::forward<decltype(self)>(self)](auto value) {
                    if (value.has_value()) {
                        receivedValues.push_back(*value);
                        self();
                    }
                });
            };

            boost::asio::post(executor, receiveNext);

            for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
                auto senderCopy = sender;
                boost::asio::post(executor, [senderCopy = std::move(senderCopy), senderId, &executor]() mutable {
                    auto sendNext = [senderCopy = std::move(senderCopy),
                                     senderId,
                                     &executor](this auto&& self, std::size_t i) -> void {
                        if (i >= kVALUES_PER_SENDER)
                            return;

                        int value = (senderId * 100) + i;
                        senderCopy.asyncSend(
                            value, [self = std::forward<decltype(self)>(self), &executor, i](bool success) mutable {
                                if (success)
                                    boost::asio::post(executor, [self = std::move(self), i]() mutable { self(i + 1); });
                            }
                        );
                    };
                    sendNext(0);
                });
            }
        }

        context_.run();

        EXPECT_EQ(receivedValues.size(), kTOTAL_EXPECTED);
        std::ranges::sort(receivedValues);

        std::vector<int> expectedValues;
        for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
            for (auto i = 0uz; i < kVALUES_PER_SENDER; ++i) {
                expectedValues.push_back((senderId * 100) + i);
            }
        }
        std::ranges::sort(expectedValues);
        EXPECT_EQ(receivedValues, expectedValues);
    }

    template <typename Sender, typename Receiver>
    void
    runThreadPoolTest(Sender sender, Receiver receiver, boost::asio::thread_pool& executor)
    {
        util::Mutex<std::vector<int>> receivedValues;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&receiver, &receivedValues](boost::asio::yield_context yield) mutable {
                auto value = receiver.asyncReceive(yield);
                while (value.has_value()) {
                    receivedValues.lock()->push_back(*value);
                    value = receiver.asyncReceive(yield);
                }
            });

            auto localSender = std::move(sender);
            for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
                util::spawn(executor, [senderCopy = localSender, senderId](boost::asio::yield_context yield) mutable {
                    for (auto i = 0uz; i < kVALUES_PER_SENDER; ++i) {
                        int value = (senderId * 100) + i;
                        if (not senderCopy.asyncSend(value, yield))
                            break;
                    }
                });
            }
        } else {
            auto receiveNext = [&receiver, &receivedValues](this auto&& self) -> void {
                if (receivedValues.lock()->size() >= kTOTAL_EXPECTED)
                    return;

                receiver.asyncReceive([&receivedValues, self = std::forward<decltype(self)>(self)](auto value) {
                    if (value.has_value()) {
                        receivedValues.lock()->push_back(*value);
                        self();
                    }
                });
            };

            boost::asio::post(executor, receiveNext);

            for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
                auto senderCopy = sender;
                boost::asio::post(executor, [senderCopy = std::move(senderCopy), senderId, &executor]() mutable {
                    auto sendNext = [senderCopy = std::move(senderCopy),
                                     senderId,
                                     &executor](this auto&& self, std::size_t i) {
                        if (i >= kVALUES_PER_SENDER)
                            return;

                        int value = (senderId * 100) + i;
                        senderCopy.asyncSend(
                            value, [self = std::forward<decltype(self)>(self), &executor, i](bool success) mutable {
                                if (success)
                                    boost::asio::post(executor, [self = std::move(self), i]() mutable { self(i + 1); });
                            }
                        );
                    };
                    sendNext(0);
                });
            }
        }

        context_.run();

        EXPECT_EQ(receivedValues.lock()->size(), kTOTAL_EXPECTED);
        std::ranges::sort(receivedValues.lock().get());

        std::vector<int> expectedValues;
        for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
            for (auto i = 0uz; i < kVALUES_PER_SENDER; ++i) {
                expectedValues.push_back((senderId * 100) + i);
            }
        }
        std::ranges::sort(expectedValues);
        EXPECT_EQ(receivedValues.lock().get(), expectedValues);
    }
};

TEST_P(ChannelParameterizedTest, MultipleSendersOneReceiver)
{
    if (params_.contextType == ContextType::IOContext) {
        auto& executor = context_.get<boost::asio::io_context>();
        auto [sender, receiver] = util::Channel<int>::create(executor, 10);
        runIOContextTest(std::move(sender), std::move(receiver), executor);
    } else {
        auto& executor = context_.get<boost::asio::thread_pool>();
        auto [sender, receiver] = util::Channel<int>::create(executor, 10);
        runThreadPoolTest(std::move(sender), std::move(receiver), executor);
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllContextAndDispatchTypes,
    ChannelParameterizedTest,
    Values(
        ChannelTestParams{ContextType::IOContext, ApproachType::Spawn},
        ChannelTestParams{ContextType::IOContext, ApproachType::Callback},
        ChannelTestParams{ContextType::ThreadPool, ApproachType::Spawn},
        ChannelTestParams{ContextType::ThreadPool, ApproachType::Callback}
    ),
    [](TestParamInfo<ChannelTestParams> const& info) { return info.param.toString(); }
);

TEST_P(ChannelParameterizedTest, ChannelClosureScenarios)
{
    if (params_.contextType == ContextType::IOContext) {
        auto& executor = context_.get<boost::asio::io_context>();

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 5);

                EXPECT_FALSE(receiver.isClosed());

                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                {
                    auto tempSender = std::move(sender);
                    // tempSender will be destroyed here, closing the channel
                }

                EXPECT_TRUE(receiver.isClosed());

                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                testCompleted = true;
            });
        } else {
            auto [sender, receiver] = util::Channel<int>::create(executor, 5);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

            EXPECT_FALSE(receiverPtr->isClosed());

            auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

            senderPtr->value().asyncSend(42, [&executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                EXPECT_TRUE(success);

                receiverPtr->asyncReceive([&executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                    EXPECT_TRUE(value.has_value());
                    EXPECT_EQ(*value, 42);

                    boost::asio::post(executor, [&executor, receiverPtr, senderPtr, &testCompleted]() {
                        // destroy sender to close channel
                        senderPtr->reset();
                        EXPECT_TRUE(receiverPtr->isClosed());

                        boost::asio::post(executor, [receiverPtr, &testCompleted]() {
                            // attempting to receive from closed channel should return nullopt
                            receiverPtr->asyncReceive([&testCompleted](auto closedValue) {
                                EXPECT_FALSE(closedValue.has_value());
                                testCompleted = true;
                            });
                        });
                    });
                });
            });
        }

        context_.run();
        EXPECT_TRUE(testCompleted);
    } else {
        auto& executor = context_.get<boost::asio::thread_pool>();

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 5);

                EXPECT_FALSE(receiver.isClosed());

                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                {
                    auto tempSender = std::move(sender);
                    // tempSender will be destroyed here, closing the channel
                }

                EXPECT_TRUE(receiver.isClosed());

                // attempting to receive from closed channel should return nullopt
                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(executor, [&executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 5);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

                EXPECT_FALSE(receiverPtr->isClosed());

                auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

                senderPtr->value().asyncSend(42, [&executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                    EXPECT_TRUE(success);

                    receiverPtr->asyncReceive([&executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                        EXPECT_TRUE(value.has_value());
                        EXPECT_EQ(*value, 42);

                        boost::asio::post(executor, [&executor, receiverPtr, senderPtr, &testCompleted]() {
                            // destroy sender to close channel
                            senderPtr->reset();
                            EXPECT_TRUE(receiverPtr->isClosed());

                            boost::asio::post(executor, [receiverPtr, &testCompleted]() {
                                // attempting to receive from closed channel should return nullopt
                                receiverPtr->asyncReceive([&testCompleted](auto closedValue) {
                                    EXPECT_FALSE(closedValue.has_value());
                                    *testCompleted.lock() = true;
                                });
                            });
                        });
                    });
                });
            });
        }

        context_.run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}

TEST_P(ChannelParameterizedTest, TrySendTryReceiveMethods)
{
    if (params_.contextType == ContextType::IOContext) {
        auto& executor = context_.get<boost::asio::io_context>();

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context /*yield*/) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 3);

                auto emptyValue = receiver.tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                bool sendSuccess1 = sender.trySend(42);
                EXPECT_TRUE(sendSuccess1);

                bool sendSuccess2 = sender.trySend(43);
                EXPECT_TRUE(sendSuccess2);

                bool sendSuccess3 = sender.trySend(44);
                EXPECT_TRUE(sendSuccess3);

                // trySend should fail when channel is full
                bool sendSuccess4 = sender.trySend(45);
                EXPECT_FALSE(sendSuccess4);

                auto value1 = receiver.tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 42);

                auto value2 = receiver.tryReceive();
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 43);

                bool sendSuccess5 = sender.trySend(46);
                EXPECT_TRUE(sendSuccess5);

                auto value3 = receiver.tryReceive();
                EXPECT_TRUE(value3.has_value());
                EXPECT_EQ(*value3, 44);

                auto value4 = receiver.tryReceive();
                EXPECT_TRUE(value4.has_value());
                EXPECT_EQ(*value4, 46);

                auto emptyValue2 = receiver.tryReceive();
                EXPECT_FALSE(emptyValue2.has_value());

                testCompleted = true;
            });
        } else {
            auto [sender, receiver] = util::Channel<int>::create(executor, 2);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));
            auto senderPtr = std::make_shared<decltype(sender)>(std::move(sender));

            boost::asio::post(executor, [receiverPtr, senderPtr, &testCompleted]() {
                auto emptyValue = receiverPtr->tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                bool sendSuccess1 = senderPtr->trySend(100);
                EXPECT_TRUE(sendSuccess1);

                bool sendSuccess2 = senderPtr->trySend(101);
                EXPECT_TRUE(sendSuccess2);

                // trySend should fail when channel is full
                bool sendSuccess3 = senderPtr->trySend(102);
                EXPECT_FALSE(sendSuccess3);

                auto value1 = receiverPtr->tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 100);

                bool sendSuccess4 = senderPtr->trySend(103);
                EXPECT_TRUE(sendSuccess4);

                auto value2 = receiverPtr->tryReceive();
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 101);

                auto value3 = receiverPtr->tryReceive();
                EXPECT_TRUE(value3.has_value());
                EXPECT_EQ(*value3, 103);

                testCompleted = true;
            });
        }

        context_.run();
        EXPECT_TRUE(testCompleted);
    } else {
        auto& executor = context_.get<boost::asio::thread_pool>();

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context /*yield*/) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 2);

                auto emptyValue = receiver.tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                bool sendSuccess1 = sender.trySend(200);
                EXPECT_TRUE(sendSuccess1);

                bool sendSuccess2 = sender.trySend(201);
                EXPECT_TRUE(sendSuccess2);

                bool sendSuccess3 = sender.trySend(202);
                EXPECT_FALSE(sendSuccess3);

                auto value1 = receiver.tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 200);

                auto value2 = receiver.tryReceive();
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 201);

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(executor, [&executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 2);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));
                auto senderPtr = std::make_shared<decltype(sender)>(std::move(sender));

                auto emptyValue = receiverPtr->tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                bool sendSuccess1 = senderPtr->trySend(300);
                EXPECT_TRUE(sendSuccess1);

                auto value1 = receiverPtr->tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 300);

                *testCompleted.lock() = true;
            });
        }

        context_.run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}

TEST_P(ChannelParameterizedTest, TryMethodsWithClosedChannel)
{
    if (params_.contextType == ContextType::IOContext) {
        auto& executor = context_.get<boost::asio::io_context>();

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context /*yield*/) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 3);

                bool sendSuccess1 = sender.trySend(42);
                EXPECT_TRUE(sendSuccess1);

                bool sendSuccess2 = sender.trySend(43);
                EXPECT_TRUE(sendSuccess2);

                {
                    auto tempSender = std::move(sender);
                    // tempSender destroyed here, closing the channel
                }

                EXPECT_TRUE(receiver.isClosed());

                auto value1 = receiver.tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 42);

                auto value2 = receiver.tryReceive();
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 43);

                auto emptyValue = receiver.tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                testCompleted = true;
            });
        } else {
            auto [sender, receiver] = util::Channel<int>::create(executor, 3);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));
            auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

            boost::asio::post(executor, [receiverPtr, senderPtr, &testCompleted]() {
                bool sendSuccess1 = senderPtr->value().trySend(100);
                EXPECT_TRUE(sendSuccess1);

                bool sendSuccess2 = senderPtr->value().trySend(101);
                EXPECT_TRUE(sendSuccess2);

                senderPtr->reset();

                EXPECT_TRUE(receiverPtr->isClosed());

                auto value1 = receiverPtr->tryReceive();
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 100);

                auto value2 = receiverPtr->tryReceive();
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 101);

                auto emptyValue = receiverPtr->tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                testCompleted = true;
            });
        }

        context_.run();
        EXPECT_TRUE(testCompleted);
    } else {
        auto& executor = context_.get<boost::asio::thread_pool>();

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context /*yield*/) mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 2);

                bool sendSuccess = sender.trySend(200);
                EXPECT_TRUE(sendSuccess);

                {
                    auto tempSender = std::move(sender);
                }

                EXPECT_TRUE(receiver.isClosed());

                auto value = receiver.tryReceive();
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 200);

                auto emptyValue = receiver.tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(executor, [&executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::create(executor, 2);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));
                auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

                bool sendSuccess = senderPtr->value().trySend(300);
                EXPECT_TRUE(sendSuccess);

                senderPtr->reset();

                EXPECT_TRUE(receiverPtr->isClosed());

                auto value = receiverPtr->tryReceive();
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 300);

                auto emptyValue = receiverPtr->tryReceive();
                EXPECT_FALSE(emptyValue.has_value());

                *testCompleted.lock() = true;
            });
        }

        context_.run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}

TEST(ChannelTest, MultipleSenderCopiesErrorHandling)
{
    boost::asio::io_context executor;
    bool testCompleted = false;

    util::spawn(executor, [&executor, &testCompleted](boost::asio::yield_context yield) mutable {
        auto [sender, receiver] = util::Channel<int>::create(executor, 5);

        bool success = sender.asyncSend(42, yield);
        EXPECT_TRUE(success);

        auto value = receiver.asyncReceive(yield);
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(*value, 42);

        auto senderCopy = sender;
        {
            auto tempSender = std::move(sender);
            // tempSender destroyed here, but senderCopy still exists
        }

        EXPECT_FALSE(receiver.isClosed());

        {
            auto tempSender = std::move(senderCopy);
            // now all senders are destroyed, channel should close
        }

        EXPECT_TRUE(receiver.isClosed());

        auto closedValue = receiver.asyncReceive(yield);
        EXPECT_FALSE(closedValue.has_value());

        testCompleted = true;
    });

    executor.run();
    EXPECT_TRUE(testCompleted);
}
