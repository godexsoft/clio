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

#pragma once

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace util::async::impl {

class StopState {
    std::atomic_bool isStopRequested_{false};

public:
    void
    requestStop() noexcept
    {
        isStopRequested_ = true;
    }

    [[nodiscard]] bool
    isStopRequested() const noexcept
    {
        return isStopRequested_;
    }
};

using SharedStopState = std::shared_ptr<StopState>;

class YieldContextStopSource {
    SharedStopState shared_ = std::make_shared<StopState>();

public:
    class Token {
        friend class YieldContextStopSource;
        SharedStopState shared_;
        boost::asio::yield_context yield_;

        Token(YieldContextStopSource* source, boost::asio::yield_context yield)
            : shared_{source->shared_}, yield_{std::move(yield)}
        {
        }

    public:
        Token(Token const&) = default;
        Token(Token&&) = default;

        [[nodiscard]] bool
        isStopRequested() const noexcept
        {
            // yield explicitly
            boost::asio::post(yield_);
            return shared_->isStopRequested();
        }

        void
        sleep(std::chrono::steady_clock::duration delay) const noexcept
        {
            boost::asio::steady_timer timer(boost::asio::get_associated_executor(yield_));
            timer.expires_after(delay);
            timer.async_wait(yield_);
        }

        [[nodiscard]] operator bool() const noexcept
        {
            return isStopRequested();
        }

        [[nodiscard]] operator boost::asio::yield_context() const noexcept
        {
            return yield_;
        }
    };

    [[nodiscard]] Token
    operator[](boost::asio::yield_context yield) noexcept
    {
        return {this, yield};
    }

    void
    requestStop() noexcept
    {
        shared_->requestStop();
    }
};

class BasicStopSource {
    SharedStopState shared_ = std::make_shared<StopState>();

public:
    class Token {
        friend class BasicStopSource;
        SharedStopState shared_;

        explicit Token(BasicStopSource* source) : shared_{source->shared_}
        {
        }

    public:
        Token(Token const&) = default;
        Token(Token&&) = default;
        [[nodiscard]] bool

        isStopRequested() const noexcept
        {
            return shared_->isStopRequested();
        }

        void
        // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
        sleep(std::chrono::steady_clock::duration delay) const noexcept
        {
            std::this_thread::sleep_for(delay);
        }

        [[nodiscard]] operator bool() const noexcept
        {
            return isStopRequested();
        }
    };

    [[nodiscard]] Token
    getToken()
    {
        return Token{this};
    }

    void
    requestStop()
    {
        shared_->requestStop();
    }
};

}  // namespace util::async::impl
