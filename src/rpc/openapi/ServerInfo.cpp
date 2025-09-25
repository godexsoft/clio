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

#include "rpc/openapi/ServerInfo.hpp"

#include "data/BackendInterface.hpp"
#include "data/DBHelpers.hpp"
#include "etlng/ETLServiceInterface.hpp"
#include "etlng/LoadBalancerInterface.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "rpc/Counters.hpp"
#include "rpc/JS.hpp"
#include "rpc/common/Types.hpp"
#include "util/Assert.hpp"
#include "util/build/Build.hpp"
#include "util/log/Logger.hpp"

#include <api/DefaultApi.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <fmt/format.h>
#include <model/CacheInfo.hpp>
#include <model/Counters.hpp>
#include <model/ETLInfo.hpp>
#include <model/ETLInfo_etl_sources_inner.hpp>
#include <model/Info.hpp>
#include <model/RPC.hpp>
#include <model/RPCMetrics.hpp>
#include <model/ServerInfoErrorResponse.hpp>
#include <model/ServerInfoRequest.hpp>
#include <model/ServerInfoResponse.hpp>
#include <model/ServerInfoSuccessResponse.hpp>
#include <model/Subscriptions.hpp>
#include <model/UniversalErrorResponseCodes.hpp>
#include <model/ValidatedLedger.hpp>
#include <model/WorkQueue.hpp>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace openapi_clio::model;

namespace {

// Temporary utility to convert json report into openapi model
// In reality this should be either provided by etl_->getInfo() instead of json
// or etl_ should expose all the info through some getters
ETLInfo
createETLInfo(boost::json::object const& etlJson)
{
    auto etlInfo = ETLInfo();

    if (etlJson.contains("etl_sources")) {
        std::vector<ETLInfo_etl_sources_inner> sources;
        for (auto const& source : etlJson.at("etl_sources").as_array()) {
            auto s = ETLInfo_etl_sources_inner();
            auto const& sourceObj = source.as_object();

            if (sourceObj.contains("validated_range"))
                s.setValidatedRange(boost::json::value_to<std::string>(sourceObj.at("validated_range")));

            if (sourceObj.contains("is_connected"))
                s.setIsConnected(boost::json::value_to<std::string>(sourceObj.at("is_connected")));

            if (sourceObj.contains("ip"))
                s.setIp(boost::json::value_to<std::string>(sourceObj.at("ip")));

            if (sourceObj.contains("ws_port"))
                s.setWsPort(boost::json::value_to<std::string>(sourceObj.at("ws_port")));

            if (sourceObj.contains("grpc_port"))
                s.setGrpcPort(boost::json::value_to<std::string>(sourceObj.at("grpc_port")));

            sources.push_back(std::move(s));
        }
        etlInfo.setEtlSources(std::move(sources));
    }

    if (etlJson.contains("is_writer"))
        etlInfo.setIsWriter(etlJson.at("is_writer").as_int64() != 0);

    if (etlJson.contains("read_only"))
        etlInfo.setReadOnly(etlJson.at("read_only").as_int64() != 0);

    if (etlJson.contains("last_publish_age_seconds"))
        etlInfo.setLastPublishAgeSeconds(boost::json::value_to<std::string>(etlJson.at("last_publish_age_seconds")));

    return etlInfo;
}

Subscriptions
createSubscriptionsInfo(std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions)
{
    using namespace openapi_clio::model;

    auto subsReport = subscriptions->report();
    auto subscriptionsModel = Subscriptions{};

    static std::unordered_map<std::string, std::function<void(Subscriptions&, double)>> const subscriptionSetters = {
        {"ledger", [](Subscriptions& s, double val) { s.setLedger(val); }},
        {"transactions", [](Subscriptions& s, double val) { s.setTransactions(val); }},
        {"transactions_proposed", [](Subscriptions& s, double val) { s.setTransactionsProposed(val); }},
        {"manifests", [](Subscriptions& s, double val) { s.setManifests(val); }},
        {"validations", [](Subscriptions& s, double val) { s.setValidations(val); }},
        {"account", [](Subscriptions& s, double val) { s.setAccount(val); }},
        {"accounts_proposed", [](Subscriptions& s, double val) { s.setAccountsProposed(val); }},
        {"books", [](Subscriptions& s, double val) { s.setBooks(val); }},
        {"book_changes", [](Subscriptions& s, double val) { s.setBookChanges(val); }}
    };

    for (auto const& [type, value] : subsReport) {
        if (!value.is_number()) {
            continue;
        }

        auto count = boost::json::value_to<std::uint64_t>(value);
        auto it = subscriptionSetters.find(type);
        if (it != subscriptionSetters.end()) {
            it->second(subscriptionsModel, static_cast<double>(count));
        } else {
            LOG(util::LogService::debug()) << "Subscription type in report not found in OpenAPI spec: " << type;
        }
    }

    return subscriptionsModel;
}

Counters
createCountersInfo(
    std::reference_wrapper<rpc::Counters const> counters,
    std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions
)
{
    using namespace util;

    auto const report = counters.get().report();
    auto result = Counters{};

    // Process RPC methods
    if (report.contains(JS(rpc))) {
        auto const& rpcSection = report.at(JS(rpc)).as_object();
        auto rpc = RPC{};

        // Map of method names to corresponding setter methods on the RPC object
        std::unordered_map<std::string, std::function<void(std::optional<RPCMetrics>)>> methodSetters = {
            {"account_channels", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountChannels(m); }},
            {"account_currencies", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountCurrencies(m); }},
            {"account_info", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountInfo(m); }},
            {"account_lines", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountLines(m); }},
            {"account_nfts", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountNfts(m); }},
            {"account_objects", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountObjects(m); }},
            {"account_offers", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountOffers(m); }},
            {"account_tx", [&rpc](std::optional<RPCMetrics> m) { rpc.setAccountTx(m); }},
            {"amm_info", [&rpc](std::optional<RPCMetrics> m) { rpc.setAmmInfo(m); }},
            {"book_changes", [&rpc](std::optional<RPCMetrics> m) { rpc.setBookChanges(m); }},
            {"book_offers", [&rpc](std::optional<RPCMetrics> m) { rpc.setBookOffers(m); }},
            {"channel_authorize", [&rpc](std::optional<RPCMetrics> m) { rpc.setChannelAuthorize(m); }},
            {"channel_verify", [&rpc](std::optional<RPCMetrics> m) { rpc.setChannelVerify(m); }},
            {"deposit_authorized", [&rpc](std::optional<RPCMetrics> m) { rpc.setDepositAuthorized(m); }},
            {"feature", [&rpc](std::optional<RPCMetrics> m) { rpc.setFeature(m); }},
            {"fee", [&rpc](std::optional<RPCMetrics> m) { rpc.setFee(m); }},
            {"gateway_balances", [&rpc](std::optional<RPCMetrics> m) { rpc.setGatewayBalances(m); }},
            {"get_aggregate_price", [&rpc](std::optional<RPCMetrics> m) { rpc.setGetAggregatePrice(m); }},
            {"get_counts", [&rpc](std::optional<RPCMetrics> m) { rpc.setGetCounts(m); }},
            {"ledger", [&rpc](std::optional<RPCMetrics> m) { rpc.setLedger(m); }},
            {"ledger_closed", [&rpc](std::optional<RPCMetrics> m) { rpc.setLedgerClosed(m); }},
            {"ledger_current", [&rpc](std::optional<RPCMetrics> m) { rpc.setLedgerCurrent(m); }},
            {"ledger_data", [&rpc](std::optional<RPCMetrics> m) { rpc.setLedgerData(m); }},
            {"ledger_entry", [&rpc](std::optional<RPCMetrics> m) { rpc.setLedgerEntry(m); }},
            {"log_level", [&rpc](std::optional<RPCMetrics> m) { rpc.setLogLevel(m); }},
            {"logrotate", [&rpc](std::optional<RPCMetrics> m) { rpc.setLogrotate(m); }},
            {"manifest", [&rpc](std::optional<RPCMetrics> m) { rpc.setManifest(m); }},
            {"nft_buy_offers", [&rpc](std::optional<RPCMetrics> m) { rpc.setNftBuyOffers(m); }},
            {"nft_sell_offers", [&rpc](std::optional<RPCMetrics> m) { rpc.setNftSellOffers(m); }},
            {"noripple_check", [&rpc](std::optional<RPCMetrics> m) { rpc.setNorippleCheck(m); }},
            {"path_find", [&rpc](std::optional<RPCMetrics> m) { rpc.setPathFind(m); }},
            {"ping", [&rpc](std::optional<RPCMetrics> m) { rpc.setPing(m); }},
            {"ripple_path_find", [&rpc](std::optional<RPCMetrics> m) { rpc.setRipplePathFind(m); }},
            {"server_definitions", [&rpc](std::optional<RPCMetrics> m) { rpc.setServerDefinitions(m); }},
            {"server_info", [&rpc](std::optional<RPCMetrics> m) { rpc.setServerInfo(m); }},
            {"server_state", [&rpc](std::optional<RPCMetrics> m) { rpc.setServerState(m); }},
            {"sign", [&rpc](std::optional<RPCMetrics> m) { rpc.setSign(m); }},
            {"submit", [&rpc](std::optional<RPCMetrics> m) { rpc.setSubmit(m); }},
            {"submit_multisigned", [&rpc](std::optional<RPCMetrics> m) { rpc.setSubmitMultisigned(m); }},
            {"subscribe", [&rpc](std::optional<RPCMetrics> m) { rpc.setSubscribe(m); }},
            {"transaction_entry", [&rpc](std::optional<RPCMetrics> m) { rpc.setTransactionEntry(m); }},
            {"tx", [&rpc](std::optional<RPCMetrics> m) { rpc.setTx(m); }},
            {"unsubscribe", [&rpc](std::optional<RPCMetrics> m) { rpc.setUnsubscribe(m); }},
            {"version", [&rpc](std::optional<RPCMetrics> m) { rpc.setVersion(m); }}
        };

        // Process each RPC method in the report
        for (auto const& [method, countersObj] : rpcSection) {
            // Skip if not an object
            if (!countersObj.is_object()) {
                continue;
            }

            auto const& metrics = countersObj.as_object();
            auto rpcMetrics = RPCMetrics{};

            // Extract metrics values from the report
            if (metrics.contains(JS(started))) {
                rpcMetrics.setStarted(boost::json::value_to<std::string>(metrics.at(JS(started))));
            }

            if (metrics.contains(JS(finished))) {
                rpcMetrics.setFinished(boost::json::value_to<std::string>(metrics.at(JS(finished))));
            }

            if (metrics.contains(JS(errored))) {
                rpcMetrics.setErrored(boost::json::value_to<std::string>(metrics.at(JS(errored))));
            }

            if (metrics.contains(JS(duration_us))) {
                rpcMetrics.setDurationUs(boost::json::value_to<std::string>(metrics.at(JS(duration_us))));
            }

            // Find the method in our setter map
            auto it = methodSetters.find(method);
            if (it != methodSetters.end()) {
                // Set the metrics for this method
                it->second(std::move(rpcMetrics));
            } else {
                LOG(LogService::debug()) << "RPC method in report not found in OpenAPI spec: " << method;
            }
        }

        // Calculate and set the total metrics
        auto totalMetrics = RPCMetrics{};

        // Combine metrics from all methods
        uint64_t totalStarted = 0;
        uint64_t totalFinished = 0;
        uint64_t totalErrored = 0;
        uint64_t totalDuration = 0;

        for (auto const& [method, countersObj] : rpcSection) {
            if (!countersObj.is_object()) {
                continue;
            }

            auto const& metrics = countersObj.as_object();

            if (metrics.contains(JS(started))) {
                try {
                    totalStarted += std::stoull(boost::json::value_to<std::string>(metrics.at(JS(started))));
                } catch (...) {
                    // Ignore conversion errors
                }
            }

            if (metrics.contains(JS(finished))) {
                try {
                    totalFinished += std::stoull(boost::json::value_to<std::string>(metrics.at(JS(finished))));
                } catch (...) {
                    // Ignore conversion errors
                }
            }

            if (metrics.contains(JS(errored))) {
                try {
                    totalErrored += std::stoull(boost::json::value_to<std::string>(metrics.at(JS(errored))));
                } catch (...) {
                    // Ignore conversion errors
                }
            }

            if (metrics.contains(JS(duration_us))) {
                try {
                    totalDuration += std::stoull(boost::json::value_to<std::string>(metrics.at(JS(duration_us))));
                } catch (...) {
                    // Ignore conversion errors
                }
            }
        }

        totalMetrics.setStarted(std::to_string(totalStarted));
        totalMetrics.setFinished(std::to_string(totalFinished));
        totalMetrics.setErrored(std::to_string(totalErrored));
        totalMetrics.setDurationUs(std::to_string(totalDuration));

        rpc.setTotal(std::move(totalMetrics));

        // Set the RPC object in the result
        result.setRpc(std::move(rpc));
    }

    // Process WorkQueue
    if (report.contains("work_queue") && report.at("work_queue").is_object()) {
        auto const& workQueueData = report.at("work_queue").as_object();
        auto workQueue = WorkQueue{};

        if (workQueueData.contains("queued")) {
            auto value = boost::json::value_to<std::uint64_t>(workQueueData.at("queued"));
            workQueue.setQueued(static_cast<double>(value));
        }

        if (workQueueData.contains("queued_duration_us")) {
            auto value = boost::json::value_to<std::uint64_t>(workQueueData.at("queued_duration_us"));
            workQueue.setQueuedDurationUs(static_cast<double>(value));
        }

        if (workQueueData.contains("current_queue_size")) {
            auto value = boost::json::value_to<std::uint64_t>(workQueueData.at("current_queue_size"));
            workQueue.setCurrentQueueSize(static_cast<double>(value));
        }

        if (workQueueData.contains("max_queue_size")) {
            auto value = boost::json::value_to<std::uint64_t>(workQueueData.at("max_queue_size"));
            workQueue.setMaxQueueSize(static_cast<double>(value));
        }

        result.setWorkQueue(std::move(workQueue));
    }

    if (subscriptions)
        result.setSubscriptions(createSubscriptionsInfo(subscriptions));

    if (report.contains("too_busy_errors")) {
        try {
            auto value = std::stod(boost::json::value_to<std::string>(report.at("too_busy_errors")));
            result.setTooBusyErrors(value);
        } catch (...) {
            // Ignore conversion errors
        }
    }

    if (report.contains("not_ready_errors")) {
        try {
            auto value = std::stod(boost::json::value_to<std::string>(report.at("not_ready_errors")));
            result.setNotReadyErrors(value);
        } catch (...) {
            // Ignore conversion errors
        }
    }

    if (report.contains("bad_syntax_errors")) {
        try {
            auto value = std::stod(boost::json::value_to<std::string>(report.at("bad_syntax_errors")));
            result.setBadSyntaxErrors(value);
        } catch (...) {
            // Ignore conversion errors
        }
    }

    if (report.contains("unknown_command_errors")) {
        try {
            auto value = std::stod(boost::json::value_to<std::string>(report.at("unknown_command_errors")));
            result.setUnknownCommandErrors(value);
        } catch (...) {
            // Ignore conversion errors
        }
    }

    if (report.contains("internal_errors")) {
        try {
            auto value = std::stod(boost::json::value_to<std::string>(report.at("internal_errors")));
            result.setInternalErrors(value);
        } catch (...) {
            // Ignore conversion errors
        }
    }

    return result;
}

}  // namespace

namespace rpc::openapi {

ServerInfoHandlerImpl::ServerInfoHandlerImpl(
    std::shared_ptr<BackendInterface> const& backend,
    std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptions,
    std::shared_ptr<etlng::LoadBalancerInterface> const& balancer,
    std::shared_ptr<etlng::ETLServiceInterface const> const& etl,
    rpc::Counters const& counters
)
    : backend_(backend), subscriptions_(subscriptions), balancer_(balancer), etl_(etl), counters_(std::cref(counters))
{
}

std::expected<ServerInfoSuccessResponse, ServerInfoHandlerImpl::ErrorCodes>
ServerInfoHandlerImpl::process(ServerInfoRequestBase const&, rpc::Context const& ctx)
{
    using namespace openapi_clio::model;  // generated name of namespace can be adjusted in openapi
    using namespace rpc;
    using namespace std::chrono;
    using ripple::to_string;

    LOG(util::LogService::info()) << "+++ client ip: " << ctx.clientIp;

    auto const range = backend_->fetchLedgerRange();
    ASSERT(range.has_value(), "ServerInfo's ledger range must be available");

    auto const lgrInfo = backend_->fetchLedgerBySequence(range->maxSequence, ctx.yield);
    if (not lgrInfo.has_value())
        return std::unexpected(UniversalErrorResponseCodes::INTERNAL);

    auto const fees = backend_->fetchFees(lgrInfo->seq, ctx.yield);
    if (not fees.has_value())
        return std::unexpected(UniversalErrorResponseCodes::INTERNAL);

    auto info = Info{};
    auto const sinceEpoch = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    auto const age = static_cast<int32_t>(sinceEpoch) -
        static_cast<int32_t>(lgrInfo->closeTime.time_since_epoch().count()) - static_cast<int32_t>(kRIPPLE_EPOCH_START);

    info.setCompleteLedgers(fmt::format("{}-{}", range->minSequence, range->maxSequence));

    // if (ctx.isAdmin) {
    // info.setCounters(counters_.get());
    // TODO:
    // input.backendCounters ? std::make_optional(backend_->stats()) : std::nullopt,
    // subscriptions_->report(),
    //     jv.as_object()["etl"] = info.adminSection->etl;
    //     jv.as_object()[JS(counters)] = info.adminSection->counters;
    //     jv.as_object()[JS(counters)].as_object()["subscriptions"] = info.adminSection->subscriptions;
    //     if (info.adminSection->backendCounters.has_value()) {
    //         jv.as_object()[kBACKEND_COUNTERS_KEY] = *info.adminSection->backendCounters;
    //     }

    info.setCounters(createCountersInfo(counters_, subscriptions_));
    info.setEtl(createETLInfo(etl_->getInfo()));
    // }

    auto const serverInfoRippled =
        balancer_->forwardToRippled({{"command", "server_info"}}, ctx.clientIp, ctx.isAdmin, ctx.yield);

    if (serverInfoRippled && !serverInfoRippled->contains(JS(error))) {
        if (serverInfoRippled->contains(JS(result)) &&
            serverInfoRippled->at(JS(result)).as_object().contains(JS(info))) {
            using boost::json::value_to;
            auto const rippledInfo = serverInfoRippled->at(JS(result)).as_object().at(JS(info)).as_object();

            if (rippledInfo.contains(JS(load_factor)))
                info.setLoadFactor(value_to<double>(rippledInfo.at(JS(load_factor))));
            if (rippledInfo.contains(JS(validation_quorum)))
                info.setValidationQuorum(value_to<uint32_t>(rippledInfo.at(JS(validation_quorum))));
            if (rippledInfo.contains(JS(build_version)))
                info.setRippledVersion(value_to<std::string>(rippledInfo.at(JS(build_version))));
            if (rippledInfo.contains(JS(network_id)))
                info.setNetworkId(value_to<uint32_t>(rippledInfo.at(JS(network_id))));
        }
    }

    auto validatedLedger = ValidatedLedger{};
    validatedLedger.setAge(age < 0 ? 0 : age);
    validatedLedger.setHash(ripple::strHex(lgrInfo->hash));
    validatedLedger.setSeq(lgrInfo->seq);

    if (fees.has_value()) {
        validatedLedger.setBaseFeeXrp(fees->base.decimalXRP());
        validatedLedger.setReserveBaseXrp(fees->reserve.decimalXRP());
    }

    info.setValidatedLedger(std::move(validatedLedger));

    auto cacheInfo = CacheInfo{};
    cacheInfo.setIsEnabled(not backend_->cache().isDisabled());
    cacheInfo.setSize(backend_->cache().size());
    cacheInfo.setIsFull(backend_->cache().isFull());
    cacheInfo.setLatestLedgerSeq(backend_->cache().latestLedgerSequence());
    cacheInfo.setObjectHitRate(static_cast<double>(backend_->cache().getObjectHitRate()));
    cacheInfo.setSuccessorHitRate(static_cast<double>(backend_->cache().getSuccessorHitRate()));

    info.setCache(std::move(cacheInfo));

    info.setTime(to_string(std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now())));
    info.setUptime(counters_.get().uptime().count());
    info.setBuildVersion(util::build::getClioVersionString());
    info.setXrplVersion(ripple::BuildInfo::getVersionString());

    if (etl_->isAmendmentBlocked())
        info.setAmendmentBlocked(true);

    if (etl_->isCorruptionDetected())
        info.setCorruptionDetected(true);

    auto resp = ServerInfoSuccessResponse{};
    resp.setStatus(ServerInfoSuccessResponseBase::StatusEnum::SUCCESS);
    resp.setInfo(std::move(info));
    resp.setValidated(true);

    return resp;
}

}  // namespace rpc::openapi
