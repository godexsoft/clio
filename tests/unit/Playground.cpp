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

#include "util/Assert.hpp"
#include "util/Mutex.hpp"

#include <boost/asio/async_result.hpp>
#include <gmock/gmock.h>

#include <atomic>
#include <functional>
#include <queue>
#include <sstream>
#include <utility>
// #include <gtest/gtest.h>

using namespace testing;

#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// Aliases for brevity
namespace asio = boost::asio;
using asio::make_work_guard;
using boost::system::error_code;

template <typename CompletionToken>
auto
asyncComputeInCoro(asio::io_context& ioc, CompletionToken&& token)
{
    return asio::async_initiate<CompletionToken, void(error_code, int)>(
        [&ioc](auto&& handler) {
            asio::spawn(ioc, [handler = std::forward<decltype(handler)>(handler)](asio::yield_context yield) mutable {
                std::cout << "[Child Coro]  Started on thread: " << std::this_thread::get_id() << std::endl;

                asio::steady_timer timer(yield.get_executor());
                timer.expires_after(std::chrono::milliseconds(2));
                std::cout << "[Child Coro]  Performing 'computation' (waiting on timer)..." << std::endl;
                timer.async_wait(yield);

                int result = 1337;
                std::cout << "[Child Coro]  Computation finished. Result: " << result << std::endl;

                asio::dispatch(yield.get_executor(), [handler = std::move(handler), result]() mutable {
                    handler(error_code{}, result);
                });
            });
        },
        token
    );
}

TEST(PlaygroundTest, Test)
{
    asio::io_context ioc;
    auto work = make_work_guard(ioc);

    unsigned int numThreads = 4;
    std::vector<std::thread> threadPool;
    std::cout << "Starting an io_context with a pool of " << numThreads << " threads." << std::endl;
    for (unsigned int i = 0; i < numThreads; ++i) {
        threadPool.emplace_back([&ioc]() { ioc.run(); });
    }

    asio::spawn(ioc, [&](asio::yield_context yield) {
        std::cout << "[Parent Coro] Started on thread: " << std::this_thread::get_id() << std::endl;

        error_code ec;

        std::cout << "[Parent Coro] Requesting value from child coroutine..." << std::endl;
        int computedValue = asyncComputeInCoro(ioc, yield[ec]);

        std::cout << "[Parent Coro] Resumed on thread: " << std::this_thread::get_id() << std::endl;
        if (ec) {
            std::cerr << "[Parent Coro] Computation failed: " << ec.message() << std::endl;
        } else {
            std::cout << "[Parent Coro] The computed value is: " << computedValue << " ✅" << std::endl;
        }

        work.reset();
    });

    for (auto& t : threadPool) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "Program finished." << std::endl;
}

template <typename ContextType>
class AsyncMutex {
    struct Data {
        std::queue<boost::asio::async_result<boost::asio::yield_context, void()>::handler_type> waiters;
        bool inUse = false;
    };

    struct Lock {
        friend class AsyncMutex;

        Lock() = delete;
        Lock(Lock&& other) : mtx_{other.mtx_}
        {
            other.wasMoved_ = true;
        }
        Lock(Lock const&) = delete;
        Lock&
        operator=(Lock&& other)
        {
            mtx_ = other.mtx_;
            other.wasMoved_ = true;
        };
        Lock&
        operator=(Lock const&) = delete;

        ~Lock()
        {
            if (wasMoved_)
                return;

            auto l = mtx_.get().shared_.lock();
            l->inUse = false;  // unlock

            if (not l->waiters.empty()) {
                auto coro = std::move(l->waiters.front());
                l->waiters.pop();

                asio::post(mtx_.get().ctx_.get(), [coro = std::move(coro)] mutable { coro(); });
            } else {
                std::cout << "no more waiters left... do nothing" << std::endl;
            }
        }

    private:
        Lock(AsyncMutex& mtx) : mtx_{mtx}
        {
            // auto l = mtx_.get().shared_.lock();
            // ASSERT(not l->inUse, "Inconsistent state: already in use");
            // l->inUse = true;
        }

        std::reference_wrapper<AsyncMutex> mtx_;
        bool wasMoved_ = false;
    };

    util::Mutex<Data> shared_;
    std::reference_wrapper<ContextType> ctx_;

public:
    AsyncMutex(ContextType& ctx) : ctx_{ctx}
    {
    }

    AsyncMutex() = delete;
    AsyncMutex(AsyncMutex&&) = delete;
    AsyncMutex(AsyncMutex const&) = delete;
    AsyncMutex&
    operator=(AsyncMutex&&) = delete;
    AsyncMutex&
    operator=(AsyncMutex const&) = delete;

    // suspends the coroutine "yield" until lock is acquired
    Lock
    asyncAcquire(boost::asio::yield_context yield)
    {
        return asio::async_initiate<boost::asio::yield_context, void(Lock)>(
            [this, exec = yield.get_executor()](auto&& handler) {
                asio::spawn(
                    exec,
                    [this, handler = std::forward<decltype(handler)>(handler)](asio::yield_context innerYield) mutable {
                        while (true) {
                            while (shared_.lock()->inUse) {
                                // suspend the coro while the lock is in use
                                asio::async_initiate<boost::asio::yield_context, void()>(
                                    [&](auto&& localHandler) mutable {
                                        auto l = shared_.lock();
                                        l->waiters.push(std::forward<decltype(localHandler)>(localHandler));
                                    },
                                    innerYield
                                );
                            }

                            // some other user released the lock so let's grab it back if we can
                            if (auto l = shared_.lock(); not l->inUse) {
                                l->inUse = true;  // we are now holding the lock

                                asio::dispatch(
                                    innerYield.get_executor(),
                                    [handler = std::move(handler), res = Lock(*this)]() mutable {
                                        handler(std::move(res));
                                    }
                                );

                                return;  // finally we are done
                            }  // if we can't take the lock or it's in use already - go back to waiting
                        }
                    }
                );
            },
            yield
        );
    }
};

TEST(PlaygroundTest, TestMutex)
{
    asio::io_context ioc;
    auto work = make_work_guard(ioc);

    unsigned int numThreads = 4;
    std::vector<std::thread> threadPool;
    std::cout << "Starting an io_context with a pool of " << numThreads << " threads." << std::endl;
    for (unsigned int i = 0; i < numThreads; ++i) {
        threadPool.emplace_back([&ioc]() { ioc.run(); });
    }

    AsyncMutex mtx(ioc);

    std::atomic_uint cnt = 0u;
    for (auto i = 0uz; i < 10uz; ++i) {
        asio::spawn(ioc, [&, c = ++cnt](auto&& yield) {
            {
                std::stringstream ss;
                ss << "before " << c << " coro on " << std::this_thread::get_id() << std::endl;
                std::cout << ss.str();
            }

            auto l = mtx.asyncAcquire(yield);

            {
                std::stringstream ss;
                ss << "after " << c << " coro on " << std::this_thread::get_id() << std::endl;
                std::cout << ss.str();
            }

            if (c == 10u) {
                std::cout << "RESET WORK!\n";
                work.reset();
            }
        });  // at the end we release the lock by destroying `l`
    }

    for (auto& t : threadPool) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "Program finished." << std::endl;
}
