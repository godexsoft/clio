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

#include "etl/ETLHelpers.hpp"
#include "util/Coroutine.hpp"
#include "util/CoroutineGroup.hpp"
#include "util/Spawn.hpp"

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

using namespace testing;

template <typename T>
class AsyncQueue {
    std::queue<T> queue_;
    std::vector<std::function<void(std::optional<T>)>> waitingReceivers_;
    std::atomic<bool> closed_{false};
    mutable std::mutex mutex_;

public:
    bool
    empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void
    push(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!waitingReceivers_.empty()) {
            auto receiver = std::move(waitingReceivers_.back());
            waitingReceivers_.pop_back();
            receiver(std::make_optional(std::move(value)));
        } else {
            queue_.push(std::move(value));
        }
    }

    void
    notifyClosed()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty()) {
            return;
        }
        std::ranges::for_each(waitingReceivers_, [](auto& f) { f(std::nullopt); });
        waitingReceivers_.clear();
    }

    template <typename Handler>
    void
    asyncPop(Handler&& handler)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty()) {
            auto value = std::move(queue_.front());
            queue_.pop();
            boost::asio::post(
                boost::asio::get_associated_executor(handler),
                [handler = std::forward<Handler>(handler), value = std::move(value)]() mutable {
                    handler(std::make_optional(std::move(value)));
                }
            );
        } else if (closed_) {
            boost::asio::post(
                boost::asio::get_associated_executor(handler),
                [handler = std::forward<Handler>(handler)]() mutable { handler(std::nullopt); }
            );
        } else {
            waitingReceivers_.push_back(std::forward<Handler>(handler));
        }
    }
};

template <typename T>
class Channel {
private:
    class Shared {
        AsyncQueue<T> queue_;
        std::atomic<bool> closed_{false};

    public:
        Shared(std::size_t) : queue_()
        {
        }

        AsyncQueue<T>&
        queue()
        {
            return queue_;
        }

        void
        close()
        {
            // std::cout << "closing..\n";
            closed_ = true;
            queue_.notifyClosed();
            // std::cout << "closed..\n";
        }

        bool
        isClosed() const
        {
            return closed_.load();
        }
    };

    class Sender {
        std::shared_ptr<Shared> shared_;
        struct Inner {
            std::shared_ptr<Shared> shared;

            ~Inner()
            {
                shared->close();
            }
        };
        std::shared_ptr<Inner> inner_;

    public:
        Sender(std::shared_ptr<Shared> shared)
            : shared_(std::move(shared)), inner_(std::make_shared<Inner>(shared_)) {};
        Sender(Sender&&) = default;
        Sender(Sender const&) = default;
        Sender&
        operator=(Sender&&) = default;
        Sender&
        operator=(Sender const&) = default;

        template <typename D, typename CompletionToken>
        auto
        asyncSend(D&& data, CompletionToken&& token)
            requires(std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<D>>)
        {
            return boost::asio::async_initiate<CompletionToken, void(bool)>(
                [this](auto&& handler, auto&& data) {
                    // Post the actual work to avoid blocking the caller
                    boost::asio::post(
                        boost::asio::get_associated_executor(handler),
                        [handler = std::forward<decltype(handler)>(handler),
                         data = std::forward<decltype(data)>(data),
                         shared = shared_]() mutable {
                            try {
                                if (shared->isClosed()) {
                                    handler(false);
                                    return;
                                }
                                shared->queue().push(std::forward<decltype(data)>(data));
                                handler(true);
                            } catch (...) {
                                handler(false);
                            }
                        }
                    );
                },
                token,
                std::forward<D>(data)
            );
        }

        bool
        send(T const& data)
        {
            if (shared_->isClosed()) {
                return false;
            }
            try {
                shared_->queue().push(data);
                return true;
            } catch (...) {
                return false;
            }
        }

        template <typename D>
        bool
        trySend(D&& data)
            requires(std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<D>>)
        {
            if (shared_->isClosed()) {
                return false;
            }
            // ThreadSafeQueue doesn't have tryPush, so we'll use a simple approach
            // In a real implementation, you'd want a non-blocking version
            try {
                shared_->queue().push(std::forward<D>(data));
                return true;
            } catch (...) {
                return false;
            }
        }
    };

    class Receiver {
        std::shared_ptr<Shared> shared_;

    public:
        Receiver(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {};
        Receiver(Receiver&&) = default;
        Receiver(Receiver const&) = delete;
        Receiver&
        operator=(Receiver&&) = default;
        Receiver&
        operator=(Receiver const&) = delete;

        std::optional<T>
        tryReceive()
        {
            if (auto ptr = shared_.lock())
                return ptr->queue().tryPop();
            return std::nullopt;
        }

        template <typename CompletionToken>
        auto
        asyncReceive(CompletionToken&& token)
        {
            return boost::asio::async_initiate<CompletionToken, void(std::optional<T>)>(
                [this](auto&& handler) {
                    boost::asio::post(
                        boost::asio::get_associated_executor(handler),
                        [shared = shared_,
                         sharedHandler = std::make_shared<std::decay_t<decltype(handler)>>(
                             std::forward<decltype(handler)>(handler)
                         )]() mutable {
                            if (shared->queue().empty() and shared->isClosed()) {
                                (*sharedHandler)(std::nullopt);  // Channel already destroyed
                                return;
                            }

                            shared->queue().asyncPop([sharedHandler](std::optional<T> opt) mutable {
                                (*sharedHandler)(std::move(opt));
                            });
                        }
                    );
                },
                token
            );
        }

        bool
        isClosed() const
        {
            if (auto ptr = shared_.lock()) {
                return ptr->isClosed();
            }
            return true;  // If shared is destroyed, consider it closed
        }
    };

public:
    static std::pair<Sender, Receiver>
    createChannel(std::size_t capacity)
    {
        auto shared = std::make_shared<Shared>(capacity);
        auto sender = Sender{shared};
        auto receiver = Receiver{shared};

        return {std::move(sender), std::move(receiver)};
    }
};

TEST(ChannelTests, MultipleSendersOneReceiver)
{
    boost::asio::io_context ioc{};
    auto [sender, receiver] = Channel<int>::createChannel(10);  // buffered channel

    std::vector<int> receivedValues;
    auto const numSenders = 3uz;
    std::size_t const valuesPerSender = 500uz;

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
                    break;  // channel closed
                }
            }
        }
    );

    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        // Need to copy sender for each coroutine
        auto senderCopy = sender;  // This needs to work

        util::spawn(ioc, [senderCopy = std::move(senderCopy), senderId](boost::asio::yield_context yield) mutable {
            for (auto i = 0uz; i < valuesPerSender; ++i) {
                int value = (senderId * 100) + i;  // unique values per sender
                bool success = senderCopy.asyncSend(value, yield);
                if (!success) {
                    break;  // Channel closed
                }
            }
        });
    }

    ioc.run();

    EXPECT_EQ(receivedValues.size(), numSenders * valuesPerSender);

    std::ranges::sort(receivedValues);
    std::vector<int> expectedValues;
    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        for (auto i = 0uz; i < valuesPerSender; ++i) {
            expectedValues.push_back((senderId * 100) + i);
        }
    }
    std::ranges::sort(expectedValues);

    EXPECT_EQ(receivedValues, expectedValues);
}

TEST(ChannelTests, MultipleSendersOneReceiverThreadPool)
{
    boost::asio::thread_pool ioc{2};
    auto [sender, receiver] = Channel<int>::createChannel(10);  // buffered channel

    std::vector<int> receivedValues;
    auto const numSenders = 3uz;
    std::size_t const valuesPerSender = 500uz;

    util::spawn(ioc, [&receiver, &receivedValues](boost::asio::yield_context yield) mutable {
        auto value = receiver.asyncReceive(yield);
        while (value.has_value()) {
            // std::cout << "add one value to received" << std::endl;
            receivedValues.push_back(*value);
            value = receiver.asyncReceive(yield);
            // std::cout << "got new value" << std::endl;
        }
    });

    {
        auto senderHuy = std::move(sender);
        for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
            util::spawn(ioc, [senderCopy = senderHuy, senderId](boost::asio::yield_context yield) mutable {
                // std::cout << "coroutine started" << std::endl;
                for (auto i = 0uz; i < valuesPerSender; ++i) {
                    int value = (senderId * 100) + i;  // unique values per sender
                    // std::cout << "sending value: " << value << std::endl;
                    bool success = senderCopy.asyncSend(value, yield);
                    // std::cout << "sent value: " << success << " " << value << std::endl;
                    if (!success) {
                        break;  // Channel closed
                    }
                }
                // std::cout << "coroutine done" << std::endl;
            });
        }
    }

    ioc.join();

    EXPECT_EQ(receivedValues.size(), numSenders * valuesPerSender);

    std::ranges::sort(receivedValues);
    std::vector<int> expectedValues;
    for (auto senderId = 0uz; senderId < numSenders; ++senderId) {
        for (auto i = 0uz; i < valuesPerSender; ++i) {
            expectedValues.push_back((senderId * 100) + i);
        }
    }
    std::ranges::sort(expectedValues);

    EXPECT_EQ(receivedValues, expectedValues);
}
