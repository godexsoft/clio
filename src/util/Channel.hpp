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

#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/system/detail/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace util {

template <typename T>
class Channel {
private:
    class Shared {
        boost::asio::any_io_executor executor_;
        boost::asio::experimental::concurrent_channel<void(boost::system::error_code, T)> ch_;

    public:
        Shared(auto&& context, std::size_t capacity) : executor_(context.get_executor()), ch_(context, capacity)
        {
        }

        [[nodiscard]] auto&
        channel()
        {
            return ch_;
        }

        void
        close()
        {
            ch_.close();
        }

        [[nodiscard]] bool
        isClosed() const
        {
            return not ch_.is_open();
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

        template <typename D>
        bool
        asyncSend(D&& data, boost::asio::yield_context yield)
            requires(std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<D>>)
        {
            boost::system::error_code ec;
            shared_->channel().async_send(ec, std::forward<decltype(data)>(data), yield[ec]);
            return not ec;
        }

        template <typename D>
        void
        asyncSend(D&& data, std::invocable<bool> auto&& fn)
            requires(std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<D>>)
        {
            boost::system::error_code ec;
            shared_->channel().async_send(
                ec,
                std::forward<decltype(data)>(data),
                [fn = std::forward<decltype(fn)>(fn)](boost::system::error_code ec) mutable { fn(not ec); }
            );
        }

        template <typename D>
        bool
        trySend(D&& data)
            requires(std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<D>>)
        {
            boost::system::error_code ec;
            return shared_->channel().try_send(ec, std::forward<decltype(data)>(data));
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
            std::optional<T> result;
            auto const received = shared_->channel().try_receive([&result](boost::system::error_code ec, auto&& value) {
                if (not ec)
                    result = std::forward<decltype(value)>(value);
            });

            if (received)
                return result;

            return std::nullopt;
        }

        [[nodiscard]] std::optional<T>
        asyncReceive(boost::asio::yield_context yield)
        {
            boost::system::error_code ec;
            auto value = shared_->channel().async_receive(yield[ec]);

            if (ec)
                return std::nullopt;
            return value;
        }

        void
        asyncReceive(std::invocable<std::optional<std::remove_cvref_t<T>>> auto&& fn)
        {
            shared_->channel().async_receive(
                [fn = std::forward<decltype(fn)>(fn)](boost::system::error_code ec, std::optional<T>&& value) {
                    if (ec) {
                        fn(std::optional<T>(std::nullopt));
                        return;
                    }

                    fn(std::move(value));
                }
            );
        }

        [[nodiscard]] bool
        isClosed() const
        {
            return shared_->isClosed();
        }
    };

public:
    static std::pair<Sender, Receiver>
    create(auto&& context, std::size_t capacity)
    {
        auto shared = std::make_shared<Shared>(context, capacity);
        auto sender = Sender{shared};
        auto receiver = Receiver{shared};

        return {std::move(sender), std::move(receiver)};
    }
};

}  // namespace util
