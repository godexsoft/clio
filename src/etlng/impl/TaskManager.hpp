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

#include "etlng/ExtractorInterface.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/SchedulerInterface.hpp"
#include "etlng/impl/Monitor.hpp"
#include "util/StrandedPriorityQueue.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyOperation.hpp"
#include "util/async/AnyStrand.hpp"
#include "util/log/Logger.hpp"

#include <xrpl/protocol/TxFormats.h>

#include <memory>
#include <ranges>
#include <utility>
#include <vector>

namespace etlng::impl {

class TaskManager {
    util::async::AnyExecutionContext ctx_;
    std::unique_ptr<SchedulerInterface> schedulers_;
    std::shared_ptr<ExtractorInterface> extractor_;
    std::shared_ptr<LoaderInterface> loader_;
    std::shared_ptr<Monitor> monitor_;

    util::Logger log_{"ETL"};

public:
    // reverse order loading is needed (i.e. start with oldest seq in forward fill buffer)
    using PriorityQueue = util::StrandedPriorityQueue<model::LedgerData, decltype([](auto&& lhs, auto&& rhs) {
                                                          return lhs.seq > rhs.seq;
                                                      })>;

    template <typename CtxType>
    TaskManager(
        CtxType& ctx,
        std::unique_ptr<SchedulerInterface> scheduler,
        std::shared_ptr<ExtractorInterface> extractor,
        std::shared_ptr<LoaderInterface> loader,
        std::shared_ptr<Monitor> monitor
    )
        : ctx_(ctx)
        , schedulers_(std::move(scheduler))
        , extractor_(std::move(extractor))
        , loader_(std::move(loader))
        , monitor_(std::move(monitor))
    {
    }

    void
    run()
    {
        constexpr static auto ExtractionWorkers = 5;
        constexpr static auto LoadingWorkers = 1;  // loading should always be serial due to successors etc.

        std::vector<util::async::AnyOperation<void>> extractors;
        std::vector<util::async::AnyOperation<void>> loaders;

        auto schedulingStrand = ctx_.makeStrand();
        auto loadingStrand = ctx_.makeStrand();
        PriorityQueue queue(loadingStrand);

        LOG(log_.debug()) << "Starting task manager...\n";

        auto monitor = spawnMonitor();

        extractors.reserve(ExtractionWorkers);
        for ([[maybe_unused]] auto _ : std::views::iota(0, ExtractionWorkers))
            extractors.push_back(spawnExtractor(schedulingStrand, queue));

        loaders.reserve(LoadingWorkers);
        for ([[maybe_unused]] auto _ : std::views::iota(0, LoadingWorkers))
            loaders.push_back(spawnLoader(queue));

        monitor.wait();

        for (auto& w : extractors)
            w.wait();
        for (auto& w : loaders)
            w.wait();

        LOG(log_.debug()) << "All finished in task manager..\n";
    }

private:
    util::async::AnyOperation<void>
    spawnExtractor(util::async::AnyStrand& strand, PriorityQueue& queue) const
    {
        return strand.execute([this, &queue](auto stopRequested) {
            while (not stopRequested) {
                if (auto task = schedulers_->next(); task.has_value()) {
                    if (auto maybeBatch = extractor_->extractLedgerWithDiff(task->seq); maybeBatch.has_value()) {
                        LOG(log_.debug()) << "Adding data after extracting diff";
                        queue.add(std::move(*maybeBatch));
                    } else {
                        break;  // TODO: handle server shutdown or other node took over ETL
                    }
                }
            }
        });
    }

    util::async::AnyOperation<void>
    spawnLoader(PriorityQueue& queue) const
    {
        return ctx_.execute([this, &queue](auto stopRequested) {
            while (not stopRequested) {
                // TODO: currently the data does not tell the loader whether it's out of order or not
                if (auto data = queue.next(); data.has_value())
                    loader_->load(*data);
            }
        });
    }

    util::async::AnyOperation<void>
    spawnMonitor() const
    {
        return ctx_.execute([this](auto stopRequested) {
            while (not stopRequested) {
                monitor_->publishNextWhenAvailable();
            }
        });
    }
};

}  // namespace etlng::impl
