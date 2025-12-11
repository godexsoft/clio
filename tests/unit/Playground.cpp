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
#include <utility>
#include <vector>

using namespace testing;

TEST(ChannelTests, MultipleSendersOneReceiverIOContext)
{
    boost::asio::io_context ioc{};
    auto [sender, receiver] = util::Channel<int>::createChannel(ioc, 10);

    std::vector<int> receivedValues;
    auto const numSenders = 3uz;
    auto const valuesPerSender = 500uz;

    util::spawn(
        ioc,
        [&receiver,
         &receivedValues,
         totalExpected = numSenders * valuesPerSender](boost::asio::yield_context yield) mutable {
            while (receivedValues.size() < totalExpected) {
                auto value = receiver.asyncReceive(yield);
                if (value.has_value()) {
                    receivedValues.push_back(*value);
                } else {
                    break;
                }
            }
        }
    );

    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        util::spawn(ioc, [senderCopy = sender, senderId](boost::asio::yield_context yield) mutable {
            for (auto i = 0uz; i < valuesPerSender; ++i) {
                int value = (senderId * 100) + i;
                bool success = senderCopy.asyncSend(value, yield);
                if (not success)
                    break;
            }
        });
    }

    ioc.run();

    EXPECT_EQ(receivedValues.size(), numSenders * valuesPerSender);

    std::ranges::sort(receivedValues);
    std::vector<int> expectedValues;
    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        for (auto i = 0uz; i < valuesPerSender; ++i)
            expectedValues.push_back((senderId * 100) + i);
    }
    std::ranges::sort(expectedValues);

    EXPECT_EQ(receivedValues, expectedValues);
}

TEST(ChannelTests, MultipleSendersOneReceiverThreadPool)
{
    boost::asio::thread_pool ioc{4};
    auto [sender, receiver] = util::Channel<int>::createChannel(ioc, 10);

    util::Mutex<std::vector<int>> receivedValues;
    auto const numSenders = 3uz;
    auto const valuesPerSender = 500uz;

    util::spawn(ioc, [&receiver, &receivedValues](boost::asio::yield_context yield) mutable {
        auto value = receiver.asyncReceive(yield);
        while (value.has_value()) {
            receivedValues.lock()->push_back(*value);
            value = receiver.asyncReceive(yield);
        }
    });

    {
        auto localSender = std::move(sender);
        for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
            util::spawn(ioc, [senderCopy = localSender, senderId](boost::asio::yield_context yield) mutable {
                for (auto i = 0uz; i < valuesPerSender; ++i) {
                    int value = (senderId * 100) + i;
                    bool success = senderCopy.asyncSend(value, yield);
                    if (not success)
                        break;
                }
            });
        }
    }

    ioc.join();

    EXPECT_EQ(receivedValues.lock()->size(), numSenders * valuesPerSender);

    std::ranges::sort(receivedValues.lock().get());
    std::vector<int> expectedValues;
    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        for (auto i = 0uz; i < valuesPerSender; ++i)
            expectedValues.push_back((senderId * 100) + i);
    }
    std::ranges::sort(expectedValues);

    EXPECT_EQ(receivedValues.lock().get(), expectedValues);
}

TEST(ChannelTests, MultipleSendersOneReceiverThreadPoolWithPost)
{
    boost::asio::thread_pool pool{4};
    auto [sender, receiver] = util::Channel<int>::createChannel(pool, 10);  // buffered channel

    util::Mutex<std::vector<int>> receivedValues;
    auto const numSenders = 3uz;
    auto const valuesPerSender = 500uz;
    auto const totalExpected = numSenders * valuesPerSender;

    auto receiveNext = [&receiver, &receivedValues, totalExpected](this auto&& self) -> void {
        if (receivedValues.lock()->size() >= totalExpected)
            return;

        receiver.asyncReceive([&receivedValues, self = std::forward<decltype(self)>(self)](auto value) {
            if (value.has_value()) {
                receivedValues.lock()->push_back(*value);
                self();
            }
        });
    };

    boost::asio::post(pool, receiveNext);

    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        auto senderCopy = sender;

        boost::asio::post(pool, [senderCopy = std::move(senderCopy), senderId, &pool]() mutable {
            auto sendNext =
                [senderCopy = std::move(senderCopy), senderId, &pool](this auto&& self, std::size_t i) -> void {
                if (i >= valuesPerSender)
                    return;

                int value = (senderId * 100) + i;
                senderCopy.asyncSend(
                    value, [self = std::forward<decltype(self)>(self), &pool, i](bool success) mutable {
                        if (success)
                            boost::asio::post(pool, [self = std::move(self), i]() mutable { self(i + 1); });
                    }
                );
            };

            sendNext(0);
        });
    }

    pool.join();

    EXPECT_EQ(receivedValues.lock()->size(), numSenders * valuesPerSender);

    std::ranges::sort(receivedValues.lock().get());
    std::vector<int> expectedValues;
    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        for (auto i = 0uz; i < valuesPerSender; ++i)
            expectedValues.push_back((senderId * 100) + i);
    }
    std::ranges::sort(expectedValues);

    EXPECT_EQ(receivedValues.lock().get(), expectedValues);
}
