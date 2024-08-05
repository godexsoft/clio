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

#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyOperation.hpp"
#include "util/async/AnyStrand.hpp"
#include "util/async/context/BasicExecutionContext.hpp"

#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace testing;

enum class TransactionType { type1, type2, type3, type4 };

struct Transaction {
    TransactionType type;
};

template <TransactionType... Types>
struct Spec {
    constexpr static bool
    wants(TransactionType t)
    {
        return ((Types == t) || ...);
    }
};

struct RegistryInterface {
    virtual ~RegistryInterface() = default;
    virtual void
    dispatch(std::vector<Transaction> const& data) = 0;
};

namespace impl {

template <typename... Ps>
class Registry : public RegistryInterface {
    std::tuple<Ps...> store_;

public:
    void
    dispatch(std::vector<Transaction> const& data) override
    {
        // send all path
        {
            auto const expand = [&](auto& p) {
                if constexpr (requires { p.onTxs(data); }) {
                    p.onTxs(data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send filtered tx path
        {
            auto const expand = [&]<typename P>(P& p, Transaction const& t) {
                if constexpr (requires { p.onTx(t); }) {
                    if (P::spec::wants(t.type))
                        p.onTx(t);
                }
            };

            for (auto const& t : data) {
                std::apply([&expand, &t](auto&&... xs) { (expand(xs, t), ...); }, store_);
            }
        }
    }
};
}  // namespace impl

struct P1 {
    using spec = Spec<TransactionType::type1, TransactionType::type3>;

    void
    onTx(Transaction const& tx)
    {
        std::cout << fmt::format("got tx sent to plug1 {}\n", static_cast<int>(tx.type));
    }
};

struct P2 {
    void
    onTxs(std::vector<Transaction> const& txs)
    {
        std::cout << fmt::format("got txs sent to plug2 cnt={}\n", txs.size());
    }
};

struct P3 {
    using spec = Spec<TransactionType::type4>;

    void
    onTx(Transaction const& tx)
    {
        std::cout << fmt::format("got tx sent to plug3 {}\n", static_cast<int>(tx.type));
    }
};

struct Batch {
    uint32_t seq;
    std::vector<Transaction> txs;

    bool
    operator<(Batch const& o) const
    {
        return seq < o.seq;
    }
};

template <typename T>
class PriorityQueue {
    std::priority_queue<T> queue_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    uint32_t maxSize_;

    bool finished_ = false;

public:
    PriorityQueue(uint32_t maxSize) : maxSize_(maxSize)
    {
    }

    void
    push(T const& elt)
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [this]() { return queue_.size() <= maxSize_ or finished_; });
        if (not finished_)
            queue_.push(elt);
        cv_.notify_all();
    }

    void
    push(T&& elt)
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [this]() { return queue_.size() <= maxSize_ or finished_; });
        if (not finished_)
            queue_.push(std::move(elt));
        cv_.notify_all();
    }

    std::optional<T>
    pop()
    {
        std::unique_lock lck(m_);
        cv_.wait(lck, [this]() { return !queue_.empty() or finished_; });
        if (finished_)
            return std::nullopt;

        T ret = std::move(queue_.top());
        queue_.pop();

        cv_.notify_all();
        return ret;
    }

    void
    finish()
    {
        std::unique_lock lck(m_);
        finished_ = true;
        cv_.notify_all();
    }
};

class Worker {
    util::async::AnyStrand strand;
    util::async::AnyOperation<void> operation;

public:
    Worker(util::async::AnyExecutionContext& ctx, auto&& block)
        : strand(ctx.makeStrand())
        , operation(strand.execute(std::forward<decltype(block)>(block), std::chrono::seconds{5}))
    {
    }

    void
    wait()
    {
        operation.wait();
    }
};

class Scheduler {
    util::async::AnyExecutionContext ctx_;
    PriorityQueue<Batch>& queue_;
    std::shared_ptr<RegistryInterface> registry_;

    std::size_t forwardSeq_ = 12345;
    std::size_t backwardSeq_ = 12344;

public:
    Scheduler(Scheduler const&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler&
    operator=(Scheduler const&) = delete;
    Scheduler&
    operator=(Scheduler&&) = delete;

    template <typename CtxType>
    Scheduler(CtxType& ctx, PriorityQueue<Batch>& q, std::shared_ptr<RegistryInterface> const& reg)
        : ctx_(ctx), queue_(q), registry_(reg)
    {
    }

    void
    run()
    {
        constexpr auto BACKWARD_WORKERS = 4;
        std::vector<std::unique_ptr<Worker>> workers;

        std::cout << "starting scheduler...\n";

        workers.reserve(BACKWARD_WORKERS + 1);
        for (int i = 0; i < BACKWARD_WORKERS; ++i)
            workers.push_back(spawn(backwardSeq_ - i, -BACKWARD_WORKERS));
        workers.push_back(spawn(forwardSeq_, 1));

        auto loader = ctx_.execute([this](auto stopRequested) {
            while (not stopRequested) {
                if (auto b = queue_.pop(); b.has_value()) {
                    std::cout << fmt::format("dispatching transactions for seq {}\n", b->seq);
                    registry_->dispatch(b->txs);  // thru coro async context for executing DB stuff seamlessly
                } else {
                    break;
                }
            }
            std::cout << "Exiting loader..\n";
        });

        for (auto& w : workers)
            w->wait();

        queue_.finish();
        std::cout << "all finished in scheduler..\n";
    }

private:
    std::unique_ptr<Worker>
    spawn(std::size_t initialSeq, int stride)
    {
        return std::make_unique<Worker>(ctx_, [this, initialSeq, stride](auto stopRequested) {
            auto seq = initialSeq;

            while (not stopRequested) {
                extract(seq);
                seq += stride;
            }

            std::cout << fmt::format("finishing extraction (started from {} with {})\n", initialSeq, stride);
        });
    }

    void
    extract(std::size_t seq)
    {
        std::cout << fmt::format("extracting {}\n", seq);
        queue_.push({.seq = seq, .txs = {{TransactionType::type1}, {TransactionType::type2}}});
        queue_.push({.seq = seq, .txs = {{TransactionType::type2}, {TransactionType::type3}}});
        queue_.push({.seq = seq, .txs = {{TransactionType::type3}, {TransactionType::type1}}});
        queue_.push({.seq = seq, .txs = {{TransactionType::type3}, {TransactionType::type4}}});
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
};

TEST(PlaygroundTest, Test)
{
    constexpr static auto MAX_BATCHES = 1024z;

    // somewhere in main
    auto context = util::async::CoroExecutionContext(8);
    auto reg = std::make_shared<impl::Registry<P1, P2, P3>>();
    auto q = PriorityQueue<Batch>(MAX_BATCHES);
    auto scheduler = Scheduler(context, q, reg);

    scheduler.run();
}
