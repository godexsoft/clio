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

#include "util/async/Concepts.hpp"
#include "util/async/Outcome.hpp"
#include "util/async/context/impl/Cancellation.hpp"
#include "util/async/context/impl/Timer.hpp"

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace util::async {
namespace impl {

template <typename OutcomeType>
class BasicOperation {
protected:
    std::future<typename OutcomeType::DataType> future_;

public:
    using DataType = typename OutcomeType::DataType;

    explicit BasicOperation(OutcomeType* outcome) : future_{outcome->getStdFuture()}
    {
    }

    BasicOperation(BasicOperation&&) = default;

    BasicOperation(BasicOperation const&) = delete;

    [[nodiscard]] auto
    get()
    {
        return future_.get();
    }

    void
    wait()
    {
        future_.wait();
    }
};

template <typename CtxType, typename OpType>
struct BasicScheduledOperation {
    struct State {
        std::mutex m;
        std::condition_variable ready;
        std::optional<OpType> op{std::nullopt};

        void
        emplace(auto&& op)
        {
            std::lock_guard const lock{m};
            op.emplace(std::forward<decltype(op)>(op));
            ready.notify_all();
        }

        [[nodiscard]] OpType&
        get()
        {
            std::unique_lock lock{m};
            ready.wait(lock, [this] { return op.has_value(); });
            return op.value();
        }
    };

    std::shared_ptr<State> state = std::make_shared<State>();
    typename CtxType::Timer timer;

    BasicScheduledOperation(auto& executor, auto delay, auto&& fn)
        : timer(executor, delay, [state = state, fn = std::forward<decltype(fn)>(fn)](auto ec) mutable {
            state->emplace(fn(ec));
        })
    {
    }

    [[nodiscard]] auto
    get()
    {
        return state->get().get();
    }

    void
    wait() noexcept
    {
        state->get().wait();
    }

    void
    cancel() noexcept
    {
        timer.cancel();
    }

    void
    requestStop() noexcept
        requires(SomeStoppableOperation<OpType>)
    {
        state->get().requestStop();
    }

    void
    abort() noexcept
    {
        cancel();

        if constexpr (SomeStoppableOperation<OpType>)
            requestStop();
    }
};

}  // namespace impl

/**
 * @brief The `future` side of async operations that can be stopped
 *
 * @tparam RetType The return type of the operation
 * @tparam StopSourceType The type of the stop source
 */
template <typename RetType, typename StopSourceType>
class StoppableOperation : public impl::BasicOperation<StoppableOutcome<RetType, StopSourceType>> {
    using OutcomeType = StoppableOutcome<RetType, StopSourceType>;

    StopSourceType stopSource_;

public:
    /**
     * @brief Construct a new Stoppable Operation object
     *
     * @param outcome The outcome to wrap
     */
    explicit StoppableOperation(OutcomeType* outcome)
        : impl::BasicOperation<OutcomeType>(outcome), stopSource_(outcome->getStopSource())
    {
    }

    /** @brief Requests the operation to stop */
    void
    requestStop() noexcept
    {
        stopSource_.requestStop();
    }
};

/**
 * @brief The `future` side of async operations that cannot be stopped
 *
 * @tparam RetType The return type of the operation
 */
template <typename RetType>
using Operation = impl::BasicOperation<Outcome<RetType>>;

/**
 * @brief The `future` side of async operations that can be scheduled
 *
 * @tparam CtxType The type of the execution context
 * @tparam OpType The type of the wrapped operation
 */
template <typename CtxType, typename OpType>
using ScheduledOperation = impl::BasicScheduledOperation<CtxType, OpType>;

}  // namespace util::async
