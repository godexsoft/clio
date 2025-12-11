//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

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

/*
 * Use this file for temporary tests and implementations.
 * Note: Please don't push your temporary work to the repo.
 */

#include "util/Channel.hpp"
#include "util/Mutex.hpp"
#include "util/Spawn.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/detail/error_code.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
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

public:
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

// Test parameter combinations
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
