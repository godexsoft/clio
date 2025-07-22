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

#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>

#include <exception>
#include <type_traits>

namespace util {

template <typename T>
struct IsStrandType : std::false_type {};

template <typename Executor>
struct IsStrandType<boost::asio::strand<Executor>> : std::true_type {};

template <typename T>
concept IsStrand = IsStrandType<std::decay_t<T>>::value;

/**
 * @brief A generic completion handler that restores `boost::asio::spawn`'s behaviour from Boost 1.83
 *
 * This is intended to be passed as the third argument to `boost::asio::spawn` so that exceptions are not ignored but
 * propagated to `io_context.run()` call site.
 */
inline constexpr struct PropagatingCompletionHandler {
    /**
     * @brief The completion handler
     * @tparam R Return type (omitted for functions returning `void`)
     * @param ePtr The exception that was caught on the coroutine
     */
    template <typename... R>
    void
    operator()(std::exception_ptr ePtr, R...)
    {
        if (ePtr)
            std::rethrow_exception(ePtr);
    }
} kPROPAGATE_EXCEPTIONS;

/**
 * @brief Spawns a coroutine using `boost::asio::spawn`
 *
 * @note This uses kPROPAGATE_EXCEPTIONS to force asio to propagate exceptions through `io_context`
 * @note Since implicit strand was removed from boost::asio::spawn this helper function adds the strand back
 *
 * @tparam Ctx The type of the context/strand
 * @tparam F The type of the function to execute
 * @param ctx The execution context
 * @param func The function to execute
 * @return Propagated from underlying `boost::asio::spawn` call
 */
template <typename Ctx, typename F>
auto
spawn(Ctx&& ctx, F&& func)
{
    if constexpr (IsStrand<Ctx>) {
        return boost::asio::spawn(std::forward<Ctx>(ctx), std::forward<F>(func), kPROPAGATE_EXCEPTIONS);
    } else {
        return boost::asio::spawn(
            boost::asio::make_strand(std::forward<Ctx>(ctx).get_executor()),
            std::forward<F>(func),
            kPROPAGATE_EXCEPTIONS
        );
    }
}

}  // namespace util
