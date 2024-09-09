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

#include "data/BackendInterface.hpp"
#include "data/LedgerCache.hpp"
#include "data/Types.hpp"
#include "etl/CacheLoader.hpp"
#include "etl/ETLState.hpp"
#include "etl/LedgerFetcherInterface.hpp"
#include "etl/LoadBalancer.hpp"
#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "etl/impl/LedgerFetcher.hpp"
#include "etlng/ETLServiceInterface.hpp"
#include "etlng/ExtractorInterface.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/RegistryInterface.hpp"
#include "etlng/SchedulerInterface.hpp"
#include "etlng/impl/Extraction.hpp"
#include "etlng/impl/Loading.hpp"
#include "etlng/impl/Scheduling.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "util/Assert.hpp"
#include "util/StrandedPriorityQueue.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyOperation.hpp"
#include "util/async/AnyStrand.hpp"
#include "util/async/context/BasicExecutionContext.hpp"
#include "util/config/Config.hpp"
#include "util/log/Logger.hpp"

#include <boost/json/object.hpp>
#include <fmt/core.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>
#include <xrpl/proto/org/xrpl/rpc/v1/ledger.pb.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace etlng {

template <ripple::TxType... Types>
struct Spec {
    constexpr static bool
    wants(ripple::TxType t)
    {
        return ((Types == t) || ...);
    }
};

namespace impl {

template <typename... Ps>
class Registry : public RegistryInterface {
    std::tuple<Ps...> store_;

public:
    void
    dispatch(model::Batch const& data) override
    {
        // send entire batch path (for objects etc.)
        {
            auto const expand = [&](auto& p) {
                if constexpr (requires { p.onData(data); }) {
                    p.onData(data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send filtered tx path
        {
            auto const expand = [&]<typename P>(P& p, model::Transaction const& t) {
                if constexpr (requires { p.onTx(t); }) {
                    if (P::spec::wants(t.type))
                        p.onTx(t);
                }
            };

            for (auto const& t : data.transactions) {
                std::apply([&expand, &t](auto&&... xs) { (expand(xs, t), ...); }, store_);
            }
        }
    }
};
}  // namespace impl

class TaskManager {
    util::async::AnyExecutionContext ctx_;
    std::unique_ptr<SchedulerInterface> schedulers_;
    std::shared_ptr<ExtractorInterface> extractor_;
    std::shared_ptr<LoaderInterface> loader_;

    util::Logger log_{"ETL"};

public:
    using PriorityQueue = util::StrandedPriorityQueue<model::Batch>;

    template <typename CtxType>
    TaskManager(
        CtxType& ctx,
        std::unique_ptr<SchedulerInterface> scheduler,
        std::shared_ptr<ExtractorInterface> extractor,
        std::shared_ptr<LoaderInterface> loader
    )
        : ctx_(ctx), schedulers_(std::move(scheduler)), extractor_(std::move(extractor)), loader_(std::move(loader))
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

        LOG(log_.debug()) << "starting task manager...\n";

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

        LOG(log_.debug()) << "all finished in task manager..\n";
    }

private:
    util::async::AnyOperation<void>
    spawnExtractor(util::async::AnyStrand& strand, PriorityQueue& queue) const
    {
        return strand.execute([this, &queue](auto stopRequested) {
            while (not stopRequested) {
                if (auto task = schedulers_->next(); task.has_value()) {
                    if (auto maybeBatch = extractor_->extractDiff(task->seq); maybeBatch.has_value()) {
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
                if (auto batch = queue.next(); batch.has_value())
                    loader_->load(*batch);
            }
        });
    }
};

struct Test {
    util::Logger log_{"ETL"};

    void
    onData(model::Batch const& txs) const
    {
        LOG(log_.info()) << "!!!!!!!!! got txs sent to plug2 cnt=" << txs.transactions.size();
    }
};

struct Test2 {
    util::Logger log_{"ETL"};
    using spec = Spec<ripple::TxType::ttNFTOKEN_MINT, ripple::TxType::ttNFTOKEN_BURN>;

    void
    onTx(model::Transaction const& tx) const
    {
        LOG(log_.info()) << "!!!!!!!!! got tx sent to plug: " << tx.type;
    }
};

class ETLService : public ETLServiceInterface {
    util::Logger log_{"ETL"};

    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions_;
    std::shared_ptr<etl::LoadBalancer> balancer_;
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers_;
    std::shared_ptr<etl::CacheLoader<data::LedgerCache>> cacheLoader_;

    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;
    std::shared_ptr<ExtractorInterface> extractor_;
    std::shared_ptr<LoaderInterface> loader_;
    util::async::CoroExecutionContext ctx_;

    std::optional<util::async::CoroExecutionContext::Operation<void>> mainLoop_;

public:
    ETLService(
        util::Config const& config,
        std::shared_ptr<BackendInterface> backend,
        std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions,
        std::shared_ptr<etl::LoadBalancer> balancer,
        std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers
    )
        : backend_(std::move(backend))
        , subscriptions_(std::move(subscriptions))
        , balancer_(std::move(balancer))
        , ledgers_(std::move(ledgers))
        , cacheLoader_(std::make_shared<etl::CacheLoader<data::LedgerCache>>(config, backend_, backend_->cache()))
        , fetcher_(std::make_shared<etl::impl::LedgerFetcher<etl::LoadBalancer>>(backend_, balancer_))
        , extractor_(std::make_shared<impl::Extractor>(fetcher_))
        , loader_(std::make_shared<impl::Loader>(
              backend_,
              balancer_,
              fetcher_,
              std::make_shared<impl::Registry<Test, Test2>>()
          ))
        , ctx_(8)
    {
        // start monitor mode
        // extractors, loaders, plugins all that jazz
        // if we are a writer node, attempt to become a writer
        LOG(log_.info()) << "Starting ETLng...";
    }

    ~ETLService() override
    {
        LOG(log_.debug()) << "Stopping ETL";
    }

    void
    run() override
    {
        LOG(log_.info()) << "run() in ETLng...";

        mainLoop_.emplace(ctx_.execute([this] {
            [[maybe_unused]] auto rng = loadInitialLedgerIfNeeded();

            // LOG(log_.info()) << "Waiting for next ledger to be validated by network...";
            // std::optional<uint32_t> mostRecentValidated = ledgers_->getMostRecent();

            // if (not mostRecentValidated) {
            //     LOG(log_.info()) << "The wait for the next validated ledger has been aborted. "
            //                         "Exiting monitor loop";
            //     return;
            // }

            // uint32_t nextSequence = *mostRecentValidated;

            // // todo: this should be inside of task manager
            // // while (not isStopping()) {
            // //     nextSequence = publishNextSequence(nextSequence);
            // // }

            // auto scheduler = std::make_unique<impl::SchedulerChain<impl::ForwardScheduler, impl::BackfillScheduler>>(
            //     impl::ForwardScheduler{ledgers_, nextSequence},
            //     impl::BackfillScheduler{nextSequence - 1, nextSequence - 1000}
            //     // todo lift limit and start with rng.minSeq
            // );
            // auto man = TaskManager(ctx_, std::move(scheduler), extractor_, loader_);

            // man.run();
        }));
    }

    // uint32_t
    // publishNextSequence(uint32_t nextSequence)
    // {
    //     if (auto rng = backend_->hardFetchLedgerRangeNoThrow(); rng && rng->maxSequence >= nextSequence) {
    //         publisher_.publish(nextSequence, {});
    //         ++nextSequence;
    //     } else if (networkValidatedLedgers_->waitUntilValidatedByNetwork(nextSequence,
    //     util::MILLISECONDS_PER_SECOND)) {
    //         LOG(log_.info()) << "Ledger with sequence = " << nextSequence << " has been validated by the network. "
    //                          << "Attempting to find in database and publish";

    //         // Attempt to take over responsibility of ETL writer after 10 failed
    //         // attempts to publish the ledger. publishLedger() fails if the
    //         // ledger that has been validated by the network is not found in the
    //         // database after the specified number of attempts. publishLedger()
    //         // waits one second between each attempt to read the ledger from the
    //         // database
    //         constexpr size_t timeoutSeconds = 10;
    //         bool const success = ledgerPublisher_.publish(nextSequence, timeoutSeconds);

    //         if (!success) {
    //             LOG(log_.warn()) << "Failed to publish ledger with sequence = " << nextSequence << " . Beginning
    //             ETL";

    //             // returns the most recent sequence published empty optional if no sequence was published
    //             std::optional<uint32_t> lastPublished = runETLPipeline(nextSequence, extractorThreads_);
    //             LOG(log_.info()) << "Aborting ETL. Falling back to publishing";

    //             // if no ledger was published, don't increment nextSequence
    //             if (lastPublished)
    //                 nextSequence = *lastPublished + 1;
    //         } else {
    //             ++nextSequence;
    //         }
    //     }
    //     return nextSequence;
    // }

    std::optional<data::LedgerRange>
    loadInitialLedgerIfNeeded()
    {
        auto rng = backend_->hardFetchLedgerRangeNoThrow();
        if (!rng) {
            LOG(log_.info()) << "Database is empty. Will download a ledger from the network.";
            std::optional<ripple::LedgerHeader> ledger;

            try {
                LOG(log_.info()) << "Waiting for next ledger to be validated by network...";
                std::optional<uint32_t> mostRecentValidated = ledgers_->getMostRecent();

                if (mostRecentValidated) {
                    LOG(log_.info()) << "Ledger " << *mostRecentValidated << " has been validated. Downloading... ";
                    auto seq = *mostRecentValidated;
                    ledger = extractor_->extractFull(seq).and_then([this, seq](auto&& data) {
                        // TODO: loadInitialLedger in balancer should be called fetchEdgeKeys or similar
                        return loader_->loadInitialLedger(data, balancer_->loadInitialLedger(seq));
                    });
                } else {
                    LOG(log_.info()) << "The wait for the next validated ledger has been aborted. "
                                        "Exiting monitor loop";
                    return std::nullopt;
                }
            } catch (std::runtime_error const& e) {
                LOG(log_.fatal()) << "Failed to load initial ledger: " << e.what();
                // amendmentBlockHandler_.onAmendmentBlock();
                return std::nullopt;
            }

            if (ledger) {
                rng = backend_->hardFetchLedgerRangeNoThrow();
            } else {
                LOG(log_.error()) << "Failed to load initial ledger. Exiting monitor loop";
                return std::nullopt;
            }
        } else {
            LOG(log_.info()) << "Database already populated. Picking up from the tip of history";
            cacheLoader_->load(rng->maxSequence);
        }

        return rng;
    }

    boost::json::object
    getInfo() const override
    {
        return {{"ok", true}};
    }

    bool
    isAmendmentBlocked() const override
    {
        return false;
    }

    bool
    isCorruptionDetected() const override
    {
        return false;
    }

    std::optional<etl::ETLState>
    getETLState() const override
    {
        return std::nullopt;
    }

    /**
     * @brief Get time passed since last ledger close, in seconds.
     *
     * @return Time passed since last ledger close
     */
    std::uint32_t
    lastCloseAgeSeconds() const override
    {
        return 0;
    }

private:
};
}  // namespace etlng
