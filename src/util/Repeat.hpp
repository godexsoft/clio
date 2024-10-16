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

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <semaphore>

namespace util {

/**
 * @brief A class to repeat some action at a regular interval
 * @note io_context must be stopped before the Repeat object is destroyed. Otherwise it is undefined behavior
 */
class Repeat {
    boost::asio::steady_timer timer_;
    std::atomic_bool stopping_{false};
    std::binary_semaphore semaphore_{0};

public:
    /**
     * @brief Construct a new Repeat object
     *
     * @param ioc The io_context to use
     */
    Repeat(boost::asio::io_context& ioc);

    /**
     * @brief Stop repeating
     * @note This method will block to ensure the repeating is actually stopped. But blocking time should be very short.
     */
    void
    stop();

    /**
     * @brief Start asynchronously repeating
     *
     * @tparam Action The action type
     * @param interval The interval to repeat
     * @param action The action to call regularly
     */
    template <std::invocable Action>
    void
    start(std::chrono::steady_clock::duration interval, Action&& action)
    {
        stopping_ = false;
        startImpl(interval, std::forward<Action>(action));
    }

private:
    template <std::invocable Action>
    void
    startImpl(std::chrono::steady_clock::duration interval, Action&& action)
    {
        timer_.expires_after(interval);
        timer_.async_wait([this, interval, action = std::forward<Action>(action)](auto const&) mutable {
            if (stopping_) {
                semaphore_.release();
                return;
            }
            action();

            startImpl(interval, std::forward<Action>(action));
        });
    }
};

}  // namespace util
