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

#include "util/Channel.hpp"
#include "util/Mutex.hpp"
#include "util/Spawn.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/detail/error_code.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace testing;

enum class ContextType { IOContext, ThreadPool };
enum class ApproachType { Spawn, Callback };

struct ChannelTestParams {
    ContextType contextType;
    ApproachType approachType;

    std::string
    toString() const
    {
        std::string context = (contextType == ContextType::IOContext) ? "IOContext" : "ThreadPool";
        std::string approach = (approachType == ApproachType::Spawn) ? "Spawn" : "Callback";
        return context + "_" + approach;
    }
};

class ContextWrapper {
public:
    using ContextVariant = std::variant<boost::asio::io_context*, boost::asio::thread_pool*>;

    explicit ContextWrapper(ContextType type) : type_(type)
    {
        if (type == ContextType::IOContext) {
            ioContext_ = std::make_unique<boost::asio::io_context>();
            context_ = ioContext_.get();
        } else {
            threadPool_ = std::make_unique<boost::asio::thread_pool>(4);
            context_ = threadPool_.get();
        }
    }

    template <typename T>
    T&
    get()
    {
        return *std::get<T*>(context_);
    }

    void
    run()
    {
        if (type_ == ContextType::IOContext) {
            get<boost::asio::io_context>().run();
        } else {
            get<boost::asio::thread_pool>().join();
        }
    }

    ContextVariant&
    getExecutor()
    {
        return context_;
    }

private:
    ContextType type_;
    ContextVariant context_;
    std::unique_ptr<boost::asio::io_context> ioContext_;
    std::unique_ptr<boost::asio::thread_pool> threadPool_;
};

class ChannelParameterizedTest : public TestWithParam<ChannelTestParams> {
protected:
    void
    SetUp() override
    {
        params_ = GetParam();
        context_ = std::make_unique<ContextWrapper>(params_.contextType);
    }

    ChannelTestParams params_{};
    std::unique_ptr<ContextWrapper> context_;

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
                    if (value.has_value()) {
                        receivedValues.push_back(*value);
                    } else {
                        break;
                    }
                }
            });

            for (auto senderId = 0uz; senderId < kNUM_SENDERS; ++senderId) {
                util::spawn(executor, [sender, senderId](boost::asio::yield_context yield) mutable {
                    for (auto i = 0uz; i < kVALUES_PER_SENDER; ++i) {
                        int value = (senderId * 100) + i;
                        bool success = sender.asyncSend(value, yield);
                        if (!success)
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

        context_->run();

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
                        bool success = senderCopy.asyncSend(value, yield);
                        if (!success)
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
                                     &executor](this auto&& self, std::size_t i) -> void {
                        if (i >= kVALUES_PER_SENDER)
                            return;

                        int value = (senderId * 100) + i;
                        senderCopy.asyncSend(
                            value, [self = std::forward<decltype(self)>(self), &executor, i](bool success) mutable {
                                if (success) {
                                    boost::asio::post(executor, [self = std::move(self), i]() mutable { self(i + 1); });
                                }
                            }
                        );
                    };
                    sendNext(0);
                });
            }
        }

        context_->run();

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
    auto& executorVariant = context_->getExecutor();

    if (params_.contextType == ContextType::IOContext) {
        auto& executor = std::get<boost::asio::io_context*>(executorVariant);
        auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 10);
        runIOContextTest(std::move(sender), std::move(receiver), *executor);
    } else {
        auto& executor = std::get<boost::asio::thread_pool*>(executorVariant);
        auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 10);
        runThreadPoolTest(std::move(sender), std::move(receiver), *executor);
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllCombinations,
    ChannelParameterizedTest,
    Values(
        ChannelTestParams{ContextType::IOContext, ApproachType::Spawn},
        ChannelTestParams{ContextType::IOContext, ApproachType::Callback},
        ChannelTestParams{ContextType::ThreadPool, ApproachType::Spawn},
        ChannelTestParams{ContextType::ThreadPool, ApproachType::Callback}
    ),
    [](TestParamInfo<ChannelTestParams> const& info) { return info.param.toString(); }
);

// Test channel closure scenarios: receiver detects when all senders are destroyed
TEST_P(ChannelParameterizedTest, ChannelClosureScenarios)
{
    auto& executorVariant = context_->getExecutor();

    if (params_.contextType == ContextType::IOContext) {
        auto& executor = std::get<boost::asio::io_context*>(executorVariant);

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);

                // Test 1: Channel should be open initially
                EXPECT_FALSE(receiver.isClosed());

                // Test 2: Send and receive a value successfully
                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                // Test 3: Destroy sender by moving it out of scope
                {
                    auto tempSender = std::move(sender);
                    // tempSender will be destroyed here, closing the channel
                }

                // Test 4: Channel should now be closed
                EXPECT_TRUE(receiver.isClosed());

                // Test 5: Attempting to receive from closed channel should return nullopt
                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                testCompleted = true;
            });
        } else {
            // Callback version - test closure scenarios with proper sequencing
            auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

            // Test 1: Channel should be open initially
            EXPECT_FALSE(receiverPtr->isClosed());

            // Use a shared_ptr to control sender lifetime
            auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

            // Test 2: Send and receive a value first
            senderPtr->value().asyncSend(42, [executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                EXPECT_TRUE(success);

                // Test 3: Receive the value
                receiverPtr->asyncReceive([executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                    EXPECT_TRUE(value.has_value());
                    EXPECT_EQ(*value, 42);

                    // Test 4: Post operation to destroy sender and then test closure
                    boost::asio::post(*executor, [executor, receiverPtr, senderPtr, &testCompleted]() {
                        // Destroy sender to close channel
                        senderPtr->reset();

                        // Test 5: Channel should now be closed
                        EXPECT_TRUE(receiverPtr->isClosed());

                        // Test 6: Post another operation to test asyncReceive after closure
                        boost::asio::post(*executor, [receiverPtr, &testCompleted]() {
                            // Attempting to receive from closed channel should return nullopt
                            receiverPtr->asyncReceive([&testCompleted](auto closedValue) {
                                EXPECT_FALSE(closedValue.has_value());
                                testCompleted = true;
                            });
                        });
                    });
                });
            });
        }

        context_->run();
        EXPECT_TRUE(testCompleted);
    } else {
        // ThreadPool version
        auto& executor = std::get<boost::asio::thread_pool*>(executorVariant);

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);

                // Test 1: Channel should be open initially
                EXPECT_FALSE(receiver.isClosed());

                // Test 2: Send and receive a value successfully
                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                // Test 3: Destroy sender by moving it out of scope
                {
                    auto tempSender = std::move(sender);
                    // tempSender will be destroyed here, closing the channel
                }

                // Test 4: Channel should now be closed
                EXPECT_TRUE(receiver.isClosed());

                // Test 5: Attempting to receive from closed channel should return nullopt
                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(*executor, [executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

                // Test 1: Channel should be open initially
                EXPECT_FALSE(receiverPtr->isClosed());

                // Use a shared_ptr to control sender lifetime
                auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

                // Test 2: Send and receive a value first
                senderPtr->value().asyncSend(42, [executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                    EXPECT_TRUE(success);

                    // Test 3: Receive the value
                    receiverPtr->asyncReceive([executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                        EXPECT_TRUE(value.has_value());
                        EXPECT_EQ(*value, 42);

                        // Test 4: Post operation to destroy sender and then test closure
                        boost::asio::post(*executor, [executor, receiverPtr, senderPtr, &testCompleted]() {
                            // Destroy sender to close channel
                            senderPtr->reset();

                            // Test 5: Channel should now be closed
                            EXPECT_TRUE(receiverPtr->isClosed());

                            // Test 6: Post another operation to test asyncReceive after closure
                            boost::asio::post(*executor, [receiverPtr, &testCompleted]() {
                                // Attempting to receive from closed channel should return nullopt
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

        context_->run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}

// Test error handling scenarios: sending to closed channel, receiving from closed channel
TEST_P(ChannelParameterizedTest, BasicErrorHandling)
{
    auto& executorVariant = context_->getExecutor();

    if (params_.contextType == ContextType::IOContext) {
        auto& executor = std::get<boost::asio::io_context*>(executorVariant);

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                // Test 1: Create channel and close it by destroying sender
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);

                // Send a value first to verify normal operation
                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                // Receive the value
                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                // Create a copy of sender to test with
                auto senderCopy = sender;

                // Destroy original sender by moving it out of scope
                {
                    auto tempSender = std::move(sender);
                    // tempSender destroyed here, but senderCopy still exists
                }

                // Channel should still be open because senderCopy exists
                EXPECT_FALSE(receiver.isClosed());

                // Test 2: Destroy all senders to close channel
                {
                    auto tempSender = std::move(senderCopy);
                    // Now all senders are destroyed, channel should close
                }

                // Test 3: Channel should now be closed
                EXPECT_TRUE(receiver.isClosed());

                // Test 4: Receiving from closed channel should return nullopt
                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                testCompleted = true;
            });
        } else {
            // Callback version - test error handling including closure scenarios
            auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

            // Use a shared_ptr to control sender lifetime
            auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

            // Test 1: Send a value successfully first
            senderPtr->value().asyncSend(42, [executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                EXPECT_TRUE(success);

                // Test 2: Receive the value
                receiverPtr->asyncReceive([executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                    EXPECT_TRUE(value.has_value());
                    EXPECT_EQ(*value, 42);

                    // Test 3: Post operation to destroy sender and then test error conditions
                    boost::asio::post(*executor, [executor, receiverPtr, senderPtr, &testCompleted]() {
                        // Destroy sender to close channel
                        senderPtr->reset();

                        // Test 4: Channel should now be closed
                        EXPECT_TRUE(receiverPtr->isClosed());

                        // Test 5: Post another operation to test asyncReceive after closure
                        boost::asio::post(*executor, [receiverPtr, &testCompleted]() {
                            // Test 6: Receiving from closed channel should return nullopt
                            receiverPtr->asyncReceive([&testCompleted](auto closedValue) {
                                EXPECT_FALSE(closedValue.has_value());
                                testCompleted = true;
                            });
                        });
                    });
                });
            });
        }

        context_->run();
        EXPECT_TRUE(testCompleted);
    } else {
        // ThreadPool version
        auto& executor = std::get<boost::asio::thread_pool*>(executorVariant);

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);

                // Test 1: Send and receive successfully
                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                // Test 2: Destroy sender to close channel
                {
                    auto tempSender = std::move(sender);
                    // tempSender destroyed here, closing channel
                }

                // Test 3: Channel should be closed
                EXPECT_TRUE(receiver.isClosed());

                // Test 4: Receiving from closed channel should return nullopt
                auto closedValue = receiver.asyncReceive(yield);
                EXPECT_FALSE(closedValue.has_value());

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(*executor, [executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 5);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

                // Use a shared_ptr to control sender lifetime
                auto senderPtr = std::make_shared<std::optional<decltype(sender)>>(std::move(sender));

                // Test 1: Send a value successfully first
                senderPtr->value().asyncSend(42, [executor, receiverPtr, senderPtr, &testCompleted](bool success) {
                    EXPECT_TRUE(success);

                    // Test 2: Receive the value
                    receiverPtr->asyncReceive([executor, receiverPtr, senderPtr, &testCompleted](auto value) {
                        EXPECT_TRUE(value.has_value());
                        EXPECT_EQ(*value, 42);

                        // Test 3: Post operation to destroy sender and then test error conditions
                        boost::asio::post(*executor, [executor, receiverPtr, senderPtr, &testCompleted]() {
                            // Destroy sender to close channel
                            senderPtr->reset();

                            // Test 4: Channel should now be closed
                            EXPECT_TRUE(receiverPtr->isClosed());

                            // Test 5: Post another operation to test asyncReceive after closure
                            boost::asio::post(*executor, [receiverPtr, &testCompleted]() {
                                // Test 6: Receiving from closed channel should return nullopt
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

        context_->run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}

// Test capacity limits and backpressure behavior
TEST_P(ChannelParameterizedTest, CapacityAndBackpressure)
{
    auto& executorVariant = context_->getExecutor();

    if (params_.contextType == ContextType::IOContext) {
        auto& executor = std::get<boost::asio::io_context*>(executorVariant);

        bool testCompleted = false;

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                // Test with capacity 1 to test backpressure
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 1);

                // Test 1: Send first value should succeed
                bool success1 = sender.asyncSend(42, yield);
                EXPECT_TRUE(success1);

                // Test 2: Receive the value
                auto value1 = receiver.asyncReceive(yield);
                EXPECT_TRUE(value1.has_value());
                EXPECT_EQ(*value1, 42);

                // Test 3: Send multiple values and receive them
                bool success2 = sender.asyncSend(43, yield);
                EXPECT_TRUE(success2);

                auto value2 = receiver.asyncReceive(yield);
                EXPECT_TRUE(value2.has_value());
                EXPECT_EQ(*value2, 43);

                testCompleted = true;
            });
        } else {
            // Callback version - test capacity behavior with send/receive sequence
            auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 1);
            auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

            // Test 1: Send first value should succeed
            sender.asyncSend(42, [receiverPtr, &testCompleted](bool success) {
                EXPECT_TRUE(success);

                // Test 2: Receive the value
                receiverPtr->asyncReceive([receiverPtr, &testCompleted](auto value) {
                    EXPECT_TRUE(value.has_value());
                    EXPECT_EQ(*value, 42);
                    testCompleted = true;
                });
            });
            // sender destroyed when this scope ends
        }

        context_->run();
        EXPECT_TRUE(testCompleted);
    } else {
        // ThreadPool version
        auto& executor = std::get<boost::asio::thread_pool*>(executorVariant);

        util::Mutex<bool> testCompleted{false};

        if (params_.approachType == ApproachType::Spawn) {
            util::spawn(*executor, [executor, &testCompleted](boost::asio::yield_context yield) mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 1);

                // Test basic send/receive with small capacity
                bool success = sender.asyncSend(42, yield);
                EXPECT_TRUE(success);

                auto value = receiver.asyncReceive(yield);
                EXPECT_TRUE(value.has_value());
                EXPECT_EQ(*value, 42);

                *testCompleted.lock() = true;
            });
        } else {
            boost::asio::post(*executor, [executor, &testCompleted]() mutable {
                auto [sender, receiver] = util::Channel<int>::createChannel(*executor, 1);
                auto receiverPtr = std::make_shared<decltype(receiver)>(std::move(receiver));

                // Test 1: Send first value should succeed
                sender.asyncSend(42, [receiverPtr, &testCompleted](bool success) {
                    EXPECT_TRUE(success);

                    // Test 2: Receive the value
                    receiverPtr->asyncReceive([receiverPtr, &testCompleted](auto value) {
                        EXPECT_TRUE(value.has_value());
                        EXPECT_EQ(*value, 42);
                        *testCompleted.lock() = true;
                    });
                });
                // sender destroyed when this scope ends
            });
        }

        context_->run();
        EXPECT_TRUE(*testCompleted.lock());
    }
}
