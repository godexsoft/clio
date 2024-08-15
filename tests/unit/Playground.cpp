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
#include "util/async/Error.hpp"
#include "util/async/context/BasicExecutionContext.hpp"

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
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

struct Task {
    uint8_t priority;
    uint32_t seq;

    bool
    operator<(Task const& o) const
    {
        if (priority < o.priority)
            return true;
        return seq < o.seq;
    }
};

struct SchedulerInterface {
    virtual ~SchedulerInterface() = default;
    [[nodiscard]] virtual std::optional<Task>
    next() = 0;
};

struct ForwardScheduler : SchedulerInterface {
    uint32_t startSeq_;
    std::optional<uint32_t> maxSeq_;
    std::atomic_uint32_t seq_;

    std::chrono::steady_clock::time_point last;
    std::chrono::steady_clock::duration delta = std::chrono::milliseconds{100};
    std::mutex m;

    ForwardScheduler(ForwardScheduler const& other)
        : startSeq_(other.startSeq_), maxSeq_(other.maxSeq_), seq_(other.seq_.load())
    {
    }

    ForwardScheduler(uint32_t ss, std::optional<uint32_t> ms = std::nullopt) : startSeq_(ss), maxSeq_(ms), seq_(ss)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        std::lock_guard lg(m);
        auto now = std::chrono::steady_clock::now();
        if (maxSeq_.has_value() && maxSeq_.value() <= seq_)
            return std::nullopt;

        // emulate ledger progress delay
        if (now - last < delta)
            return std::nullopt;  // no task at this moment

        last = now;
        std::cout << "-------------\n";

        return {{.priority = 0, .seq = seq_++}};
    }
};

struct BackfillScheduler : SchedulerInterface {
    uint32_t startSeq_;
    uint32_t minSeq_ = 0u;

    std::atomic_uint32_t seq_;

    BackfillScheduler(BackfillScheduler const& other)
        : startSeq_(other.startSeq_), minSeq_(other.minSeq_), seq_(other.seq_.load())
    {
    }

    BackfillScheduler(uint32_t ss) : startSeq_(ss), seq_(ss)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        if (seq_ == minSeq_)
            return std::nullopt;
        return {{.priority = 0, .seq = seq_--}};
    }
};

template <typename... Schedulers>
class SchedulerChain : public SchedulerInterface {
    std::tuple<Schedulers...> schedulers_;

public:
    template <typename... Ts>
    SchedulerChain(Ts&&... schedulers) : schedulers_(std::forward<Ts>(schedulers)...)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        std::optional<Task> task;
        auto const expand = [&](auto& s) {
            if (task.has_value())
                return false;

            task = s.next();
            return task.has_value();
        };

        std::apply([&expand](auto&&... xs) { (... || expand(xs)); }, schedulers_);
        return task;
    }
};

class PriorityQueue {
    util::async::AnyStrand& strand_;
    std::priority_queue<Batch> queue_;

public:
    PriorityQueue(util::async::AnyStrand& strand) : strand_(strand)
    {
    }

    void
    add(Batch batch)
    {
        strand_.execute([&batch, this] { queue_.push(batch); }).wait();
    }

    std::optional<Batch>
    next()
    {
        return strand_
            .execute([this] -> std::optional<Batch> {
                if (queue_.empty())
                    return std::nullopt;

                auto top = queue_.top();
                queue_.pop();

                return top;
            })
            .get()
            .value_or(std::nullopt);
    }
};

class TaskManager {
    util::async::AnyExecutionContext ctx_;
    std::shared_ptr<RegistryInterface> registry_;
    std::unique_ptr<SchedulerInterface> schedulers_;

public:
    template <typename CtxType>
    TaskManager(
        CtxType& ctx,
        std::shared_ptr<RegistryInterface> const& reg,
        std::unique_ptr<SchedulerInterface> scheduler
    )
        : ctx_(ctx), registry_(reg), schedulers_(std::move(scheduler))
    {
    }

    void
    run()
    {
        constexpr static auto EXTRACTION_WORKERS = 5;
        constexpr static auto LOADING_WORKERS = 4;

        std::vector<util::async::AnyOperation<void>> extractors;
        std::vector<util::async::AnyOperation<void>> loaders;

        auto schedulingStrand = ctx_.makeStrand();
        auto loadingStrand = ctx_.makeStrand();
        PriorityQueue queue(loadingStrand);

        std::cout << "starting scheduler...\n";

        extractors.reserve(EXTRACTION_WORKERS);
        for ([[maybe_unused]] auto _ : std::views::iota(0, EXTRACTION_WORKERS))
            extractors.push_back(spawnExtractor(schedulingStrand, queue));

        loaders.reserve(LOADING_WORKERS);
        for ([[maybe_unused]] auto _ : std::views::iota(0, LOADING_WORKERS))
            loaders.push_back(spawnLoader(queue));

        for (auto& w : extractors)
            w.wait();
        for (auto& w : loaders)
            w.wait();

        std::cout << "all finished in scheduler..\n";
    }

private:
    util::async::AnyOperation<void>
    spawnExtractor(util::async::AnyStrand& strand, PriorityQueue& queue)
    {
        return strand.execute(
            [this, &queue](auto stopRequested) {
                while (not stopRequested) {
                    if (auto task = schedulers_->next(); task.has_value())
                        extract(task->seq, queue);
                }
            },
            std::chrono::seconds{5}
        );
    }

    util::async::AnyOperation<void>
    spawnLoader(PriorityQueue& queue)
    {
        return ctx_.execute(
            [&queue, this](auto stopRequested) {
                while (not stopRequested) {
                    if (auto batch = queue.next(); batch.has_value())
                        load(batch.value());
                }
            },
            std::chrono::seconds{5}
        );
    }

    void
    extract(std::size_t seq, PriorityQueue& queue)
    {
        std::cout << fmt::format("{}:: extracting {}\n", std::this_thread::get_id(), seq);
        queue.add({.seq = seq, .txs = {{TransactionType::type1}, {TransactionType::type2}}});
        queue.add({.seq = seq, .txs = {{TransactionType::type2}, {TransactionType::type3}}});
        queue.add({.seq = seq, .txs = {{TransactionType::type3}, {TransactionType::type1}}});
        queue.add({.seq = seq, .txs = {{TransactionType::type3}, {TransactionType::type4}}});
        std::this_thread::sleep_for(std::chrono::milliseconds{3});
    }

    void
    load(Batch const& batch)
    {
        std::cout << fmt::format("{}:: loading {}\n", std::this_thread::get_id(), batch.seq);
        registry_->dispatch(batch.txs);
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
};

TEST(PlaygroundTest, Test)
{
    auto context = util::async::CoroExecutionContext(8);
    auto reg = std::make_shared<impl::Registry<P1, P2, P3>>();
    auto scheduler = std::make_unique<SchedulerChain<ForwardScheduler, BackfillScheduler>>(
        ForwardScheduler{1234, 1500}, BackfillScheduler{1233}
    );
    auto man = TaskManager(context, reg, std::move(scheduler));

    man.run();
}

TEST(PlaygroundTest, Test2)
{
    auto ctx = util::async::CoroExecutionContext(1);
    auto test = std::move(ctx);
    EXPECT_EQ(42, test.execute([]() { return 42; }).get());

    auto realCtx = util::async::CoroExecutionContext(1);
    auto wrappedOwned = util::async::AnyExecutionContext(std::move(realCtx));
    EXPECT_EQ(42, wrappedOwned.execute([]() { return 42; }).get());

    auto cando = std::move(wrappedOwned);
    EXPECT_EQ(42, cando.execute([]() { return 42; }).get());

    auto evencando = cando;
    EXPECT_EQ(42, evencando.execute([]() { return 42; }).get());

    auto realCtx2 = util::async::PoolExecutionContext(1);
    auto wrappedUnowned = util::async::AnyExecutionContext(realCtx2);
    EXPECT_EQ(42, wrappedUnowned.execute([]() { return 42; }).get());
}
