#include "etlng/ETLService.hpp"

#include "data/BackendInterface.hpp"
#include "data/LedgerCacheInterface.hpp"
#include "data/Types.hpp"
#include "etl/CacheLoader.hpp"
#include "etl/ETLState.hpp"
#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "etl/SystemState.hpp"
#include "etl/impl/AmendmentBlockHandler.hpp"
#include "etl/impl/LedgerFetcher.hpp"
#include "etlng/LoadBalancerInterface.hpp"
#include "etlng/MonitorInterface.hpp"
#include "etlng/impl/AmendmentBlockHandler.hpp"
#include "etlng/impl/CacheUpdater.hpp"
#include "etlng/impl/Extraction.hpp"
#include "etlng/impl/LedgerPublisher.hpp"
#include "etlng/impl/Loading.hpp"
#include "etlng/impl/Monitor.hpp"
#include "etlng/impl/Registry.hpp"
#include "etlng/impl/Scheduling.hpp"
#include "etlng/impl/TaskManager.hpp"
#include "etlng/impl/ext/Cache.hpp"
#include "etlng/impl/ext/Core.hpp"
#include "etlng/impl/ext/NFT.hpp"
#include "etlng/impl/ext/Successor.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "util/Assert.hpp"
#include "util/Profiler.hpp"
#include "util/async/context/BasicExecutionContext.hpp"
#include "util/log/Logger.hpp"
#include "util/newconfig/ConfigDefinition.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/json/object.hpp>
#include <boost/signals2/connection.hpp>
#include <fmt/core.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>
#include <xrpl/proto/org/xrpl/rpc/v1/ledger.pb.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace etlng {

ETLService::ETLService(
    boost::asio::io_context& ioc,
    util::config::ClioConfigDefinition const& config,
    std::shared_ptr<BackendInterface> backend,
    std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions,
    std::shared_ptr<etlng::LoadBalancerInterface> balancer,
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers
)
    : config_(config)
    , backend_(std::move(backend))
    , subscriptions_(std::move(subscriptions))
    , balancer_(std::move(balancer))
    , ledgers_(std::move(ledgers))
    , cacheLoader_(std::make_shared<etl::CacheLoader<>>(config, backend_, backend_->cache()))
    , cacheUpdater_(std::make_shared<impl::CacheUpdater>(backend_->cache()))
    , fetcher_(std::make_shared<etl::impl::LedgerFetcher>(backend_, balancer_))
    , extractor_(std::make_shared<impl::Extractor>(fetcher_))
    , publisher_(ioc, backend_, subscriptions_, state_)
    , amendmentBlockHandler_(std::make_unique<etlng::impl::AmendmentBlockHandler>(ctx_, state_))
    , loader_(std::make_shared<impl::Loader>(
          backend_,
          fetcher_,
          impl::makeRegistry(
              state_,
              impl::CacheExt{cacheUpdater_},
              impl::CoreExt{backend_},
              impl::SuccessorExt{backend_, backend_->cache()},
              impl::NFTExt{backend_}
          ),
          amendmentBlockHandler_
      ))
{
    LOG(log_.info()) << "Creating ETLng...";
}

ETLService::~ETLService()
{
    stop();
    LOG(log_.debug()) << "Destroying ETLng";
}

void
ETLService::run()
{
    LOG(log_.info()) << "Running ETLng...";

    // TODO: write-enabled node should start in readonly and do the 10 second dance to become a writer
    mainLoop_.emplace(ctx_.execute([this] {
        state_.isWriting =
            not state_.isReadOnly;  // TODO: this is now needed because we don't have a mechanism for readonly or
                                    // ETL writer node. remove later in favor of real mechanism

        auto const rng = loadInitialLedgerIfNeeded();

        LOG(log_.info()) << "Waiting for next ledger to be validated by network...";
        std::optional<uint32_t> const mostRecentValidated = ledgers_->getMostRecent();

        if (not mostRecentValidated) {
            LOG(log_.info()) << "The wait for the next validated ledger has been aborted. "
                                "Exiting monitor loop";
            return;
        }

        ASSERT(rng.has_value(), "Ledger range can't be null");
        auto const nextSequence = rng->maxSequence + 1;

        LOG(log_.debug()) << "Database is populated. Starting monitor loop. sequence = " << nextSequence;
        startMonitor(nextSequence);

        // TODO: we only want to run the full ETL task man if we are POSSIBLY a write node
        // but definitely not in strict readonly
        if (not state_.isReadOnly)
            startLoading(nextSequence);
    }));
}

void
ETLService::stop()
{
    LOG(log_.info()) << "Stop called";

    if (taskMan_)
        taskMan_->stop();
    if (monitor_)
        monitor_->stop();
}

boost::json::object
ETLService::getInfo() const
{
    boost::json::object result;

    result["etl_sources"] = balancer_->toJson();
    result["is_writer"] = static_cast<int>(state_.isWriting);
    result["read_only"] = static_cast<int>(state_.isReadOnly);
    auto last = publisher_.getLastPublish();
    if (last.time_since_epoch().count() != 0)
        result["last_publish_age_seconds"] = std::to_string(publisher_.lastPublishAgeSeconds());
    return result;
}

bool
ETLService::isAmendmentBlocked() const
{
    return state_.isAmendmentBlocked;
}

bool
ETLService::isCorruptionDetected() const
{
    return state_.isCorruptionDetected;
}

std::optional<etl::ETLState>
ETLService::getETLState() const
{
    return balancer_->getETLState();
}

std::uint32_t
ETLService::lastCloseAgeSeconds() const
{
    return publisher_.lastCloseAgeSeconds();
}

std::optional<data::LedgerRange>
ETLService::loadInitialLedgerIfNeeded()
{
    auto rng = backend_->hardFetchLedgerRangeNoThrow();
    if (not rng.has_value()) {
        LOG(log_.info()) << "Database is empty. Will download a ledger from the network.";

        LOG(log_.info()) << "Waiting for next ledger to be validated by network...";
        if (auto const mostRecentValidated = ledgers_->getMostRecent(); mostRecentValidated.has_value()) {
            auto const seq = *mostRecentValidated;
            LOG(log_.info()) << "Ledger " << seq << " has been validated. Downloading... ";

            auto [ledger, timeDiff] = ::util::timed<std::chrono::duration<double>>([this, seq]() {
                return extractor_->extractLedgerOnly(seq).and_then([this, seq](auto&& data) {
                    // TODO: loadInitialLedger in balancer should be called fetchEdgeKeys or similar
                    data.edgeKeys = balancer_->loadInitialLedger(seq, *loader_);

                    // TODO: this should be interruptible for graceful shutdown
                    return loader_->loadInitialLedger(data);
                });
            });

            if (not ledger.has_value()) {
                LOG(log_.error()) << "Failed to load initial ledger. Exiting monitor loop";
                return std::nullopt;
            }

            LOG(log_.debug()) << "Time to download and store ledger = " << timeDiff;
            LOG(log_.info()) << "Finished loadInitialLedger. cache size = " << backend_->cache().size();

            return backend_->hardFetchLedgerRangeNoThrow();
        }

        LOG(log_.info()) << "The wait for the next validated ledger has been aborted. "
                            "Exiting monitor loop";
        return std::nullopt;
    }

    LOG(log_.info()) << "Database already populated. Picking up from the tip of history";
    cacheLoader_->load(rng->maxSequence);

    return rng;
}

void
ETLService::startMonitor(uint32_t seq)
{
    monitor_ = std::make_unique<impl::Monitor>(ctx_, backend_, ledgers_, seq);
    monitorSubscription_ = monitor_->subscribe([this](uint32_t seq) {
        log_.info() << "MONITOR got new seq from db: " << seq;

        // FIXME: is this the best way?
        if (not state_.isWriting) {
            auto const diff = data::synchronousAndRetryOnTimeout([this, seq](auto yield) {
                return backend_->fetchLedgerDiff(seq, yield);
            });
            cacheUpdater_->update(seq, diff);
        }

        publisher_.publish(seq, {});
    });
    monitor_->run();
}

void
ETLService::startLoading(uint32_t seq)
{
    auto scheduler = impl::makeScheduler(impl::ForwardScheduler{*ledgers_, seq});
    // TODO: add impl::BackfillScheduler{seq - 1, seq - 1000},
    // TODO2: lift limit and start with rng.minSeq

    taskMan_ = std::make_unique<impl::TaskManager>(ctx_, std::move(scheduler), *extractor_, *loader_, *monitor_, seq);
    taskMan_->run({.numExtractors = config_.get<std::size_t>("extractor_threads")});
}

}  // namespace etlng
