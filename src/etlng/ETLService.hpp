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

#include "data/BackendInterface.hpp"
#include "data/Types.hpp"
#include "etl/CacheLoader.hpp"
#include "etl/ETLState.hpp"
#include "etl/LedgerFetcherInterface.hpp"
#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "etl/SystemState.hpp"
#include "etl/impl/AmendmentBlockHandler.hpp"
#include "etl/impl/LedgerFetcher.hpp"
#include "etlng/AmendmentBlockHandlerInterface.hpp"
#include "etlng/ETLServiceInterface.hpp"
#include "etlng/ExtractorInterface.hpp"
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace etlng {

/**
 * @brief This class is responsible for continuously extracting data from a p2p node, and writing that data to the
 * databases.
 *
 * Usually, multiple different processes share access to the same network accessible databases, in which case only one
 * such process is performing ETL and writing to the database. The other processes simply monitor the database for new
 * ledgers, and publish those ledgers to the various subscription streams. If a monitoring process determines that the
 * ETL writer has failed (no new ledgers written for some time), the process will attempt to become the ETL writer.
 *
 * If there are multiple monitoring processes that try to become the ETL writer at the same time, one will win out, and
 * the others will fall back to monitoring/publishing. In this sense, this class dynamically transitions from monitoring
 * to writing and from writing to monitoring, based on the activity of other processes running on different machines.
 */
class ETLService : public ETLServiceInterface {
    util::Logger log_{"ETL"};

    util::config::ClioConfigDefinition config_;
    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions_;
    std::shared_ptr<etlng::LoadBalancerInterface> balancer_;
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers_;
    std::shared_ptr<etl::CacheLoader<>> cacheLoader_;
    std::shared_ptr<impl::CacheUpdater> cacheUpdater_;

    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;
    std::shared_ptr<ExtractorInterface> extractor_;

    etl::SystemState state_;
    util::async::CoroExecutionContext ctx_{8};

    impl::LedgerPublisher publisher_;

    std::shared_ptr<AmendmentBlockHandlerInterface> amendmentBlockHandler_;
    std::shared_ptr<impl::Loader> loader_;
    std::unique_ptr<MonitorInterface> monitor_;
    std::unique_ptr<impl::TaskManager> taskMan_;

    boost::signals2::scoped_connection monitorSubscription_;

    std::optional<util::async::CoroExecutionContext::Operation<void>> mainLoop_;

public:
    /**
     * @brief Create an instance of ETLService.
     *
     * @param ioc TODO remove
     * @param config The configuration to use
     * @param backend BackendInterface implementation
     * @param subscriptions Subscription manager
     * @param balancer Load balancer to use
     * @param ledgers The network validated ledgers datastructure
     */
    ETLService(
        boost::asio::io_context& ioc,  // TODO: remove. currently for publisher
        util::config::ClioConfigDefinition const& config,
        std::shared_ptr<BackendInterface> backend,
        std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions,
        std::shared_ptr<etlng::LoadBalancerInterface> balancer,
        std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers
    );

    ~ETLService() override;

    void
    run() override;

    void
    stop() override;

    boost::json::object
    getInfo() const override;

    bool
    isAmendmentBlocked() const override;

    bool
    isCorruptionDetected() const override;

    std::optional<etl::ETLState>
    getETLState() const override;

    std::uint32_t
    lastCloseAgeSeconds() const override;

private:
    // TODO: this better be std::expected
    std::optional<data::LedgerRange>
    loadInitialLedgerIfNeeded();

    void
    startMonitor(uint32_t seq);

    void
    startLoading(uint32_t seq);
};

}  // namespace etlng
