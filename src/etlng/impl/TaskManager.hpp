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

#include "etlng/ExtractorInterface.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/SchedulerInterface.hpp"
// #include "etlng/impl/Monitor.hpp"
#include "util/StrandedPriorityQueue.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyOperation.hpp"
#include "util/async/AnyStrand.hpp"
#include "util/log/Logger.hpp"

#include <xrpl/protocol/TxFormats.h>

#include <chrono>
#include <functional>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace etlng::impl {

class TaskManager {
    util::async::AnyExecutionContext ctx_;
    std::reference_wrapper<SchedulerInterface> schedulers_;
    std::reference_wrapper<ExtractorInterface> extractor_;
    std::reference_wrapper<LoaderInterface> loader_;
    // std::reference_wrapper<Monitor> monitor_;

    util::Logger log_{"ETL"};

    struct ReverseOrderComparator {
        bool
        operator()(model::LedgerData const& lhs, model::LedgerData const& rhs) const
        {
            return lhs.seq > rhs.seq;
        }
    };

public:
    // reverse order loading is needed (i.e. start with oldest seq in forward fill buffer)
    using PriorityQueue = util::StrandedPriorityQueue<model::LedgerData, ReverseOrderComparator>;

    TaskManager(
        util::async::AnyExecutionContext&& ctx,
        std::reference_wrapper<SchedulerInterface> scheduler,
        std::reference_wrapper<ExtractorInterface> extractor,
        std::reference_wrapper<LoaderInterface> loader
        // std::shared_ptr<Monitor> monitor
    )
        : ctx_(std::move(ctx)), schedulers_(scheduler), extractor_(extractor), loader_(loader)
    // , monitor_(std::move(monitor))
    {
    }

    void
    run()
    {
        constexpr static auto kEXTRACTION_WORKERS = 5;
        constexpr static auto kLOADING_WORKERS = 1;  // loading should always be serial due to successors etc.

        std::vector<util::async::AnyOperation<void>> extractors;
        std::vector<util::async::AnyOperation<void>> loaders;

        auto schedulingStrand = ctx_.makeStrand();
        PriorityQueue queue(ctx_.makeStrand());

        LOG(log_.debug()) << "Starting task manager...\n";

        // auto monitor = spawnMonitor();

        extractors.reserve(kEXTRACTION_WORKERS);
        for ([[maybe_unused]] auto _ : std::views::iota(0, kEXTRACTION_WORKERS))
            extractors.push_back(spawnExtractor(schedulingStrand, queue));

        loaders.reserve(kLOADING_WORKERS);
        for ([[maybe_unused]] auto _ : std::views::iota(0, kLOADING_WORKERS))
            loaders.push_back(spawnLoader(queue));

        // monitor.wait();

        for (auto& w : extractors)
            w.wait();
        for (auto& w : loaders)
            w.wait();

        LOG(log_.debug()) << "All finished in task manager..\n";
    }

private:
    util::async::AnyOperation<void>
    spawnExtractor(util::async::AnyStrand& strand, PriorityQueue& queue)
    {
        return strand.execute([this, &queue](auto stopRequested) {
            while (not stopRequested) {
                if (auto task = schedulers_.get().next(); task.has_value()) {
                    if (auto maybeBatch = extractor_.get().extractLedgerWithDiff(task->seq); maybeBatch.has_value()) {
                        LOG(log_.debug()) << "Adding data after extracting diff";
                        queue.enqueue(std::move(*maybeBatch));
                    } else {
                        break;  // TODO: handle server shutdown or other node took over ETL
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds{100});  // TODO: use timer instead?
                }
            }
        });
    }

    util::async::AnyOperation<void>
    spawnLoader(PriorityQueue& queue)
    {
        return ctx_.execute([this, &queue](auto stopRequested) {
            while (not stopRequested) {
                // TODO: currently the data does not tell the loader whether it's out of order or not
                if (auto data = queue.dequeue(); data.has_value())
                    loader_.get().load(*data);
            }
        });
    }

    // util::async::AnyOperation<void>
    // spawnMonitor() const
    // {
    //     return ctx_.execute([this](auto stopRequested) {
    //         while (not stopRequested) {
    //             monitor_->publishNextWhenAvailable();
    //         }
    //     });
    // }
};

}  // namespace etlng::impl
