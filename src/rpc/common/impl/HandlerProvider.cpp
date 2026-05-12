#include "rpc/common/impl/HandlerProvider.hpp"

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "etl/ETLServiceInterface.hpp"
#include "etl/LoadBalancerInterface.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "rpc/Counters.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/SpecDumpWriter.hpp"
#include "rpc/handlers/AMMInfo.hpp"
#include "rpc/handlers/AccountChannels.hpp"
#include "rpc/handlers/AccountCurrencies.hpp"
#include "rpc/handlers/AccountInfo.hpp"
#include "rpc/handlers/AccountLines.hpp"
#include "rpc/handlers/AccountMPTokenIssuances.hpp"
#include "rpc/handlers/AccountMPTokens.hpp"
#include "rpc/handlers/AccountNFTs.hpp"
#include "rpc/handlers/AccountObjects.hpp"
#include "rpc/handlers/AccountOffers.hpp"
#include "rpc/handlers/AccountTx.hpp"
#include "rpc/handlers/BookChanges.hpp"
#include "rpc/handlers/BookOffers.hpp"
#include "rpc/handlers/DepositAuthorized.hpp"
#include "rpc/handlers/Feature.hpp"
#include "rpc/handlers/GatewayBalances.hpp"
#include "rpc/handlers/GetAggregatePrice.hpp"
#include "rpc/handlers/Ledger.hpp"
#include "rpc/handlers/LedgerData.hpp"
#include "rpc/handlers/LedgerEntry.hpp"
#include "rpc/handlers/LedgerIndex.hpp"
#include "rpc/handlers/LedgerRange.hpp"
#include "rpc/handlers/MPTHolders.hpp"
#include "rpc/handlers/NFTBuyOffers.hpp"
#include "rpc/handlers/NFTHistory.hpp"
#include "rpc/handlers/NFTInfo.hpp"
#include "rpc/handlers/NFTSellOffers.hpp"
#include "rpc/handlers/NFTsByIssuer.hpp"
#include "rpc/handlers/NoRippleCheck.hpp"
#include "rpc/handlers/Ping.hpp"
#include "rpc/handlers/Random.hpp"
#include "rpc/handlers/ServerInfo.hpp"
#include "rpc/handlers/Subscribe.hpp"
#include "rpc/handlers/TransactionEntry.hpp"
#include "rpc/handlers/Tx.hpp"
#include "rpc/handlers/Unsubscribe.hpp"
#include "rpc/handlers/VaultInfo.hpp"
#include "rpc/handlers/VersionHandler.hpp"
#include "util/config/ConfigDefinition.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace rpc::impl {

ProductionHandlerProvider::ProductionHandlerProvider(
    util::config::ClioConfigDefinition const& config,
    std::shared_ptr<BackendInterface> const& backend,
    std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptionManager,
    std::shared_ptr<etl::LoadBalancerInterface> const& balancer,
    std::shared_ptr<etl::ETLServiceInterface const> const& etl,
    std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter,
    Counters const& counters
)
    : handlerMap_{
          {"account_channels", {.handler = AccountChannelsHandler{backend}}},
          {"account_currencies", {.handler = AccountCurrenciesHandler{backend}}},
          {"account_info", {.handler = AccountInfoHandler{backend, amendmentCenter}}},
          {"account_lines", {.handler = AccountLinesHandler{backend}}},
          {"account_mptoken_issuances",
           {.handler = AccountMPTokenIssuancesHandler{backend}, .isClioOnly = true}},  // clio only
          {"account_mptokens",
           {.handler = AccountMPTokensHandler{backend}, .isClioOnly = true}},  // clio only
          {"account_nfts", {.handler = AccountNFTsHandler{backend}}},
          {"account_objects", {.handler = AccountObjectsHandler{backend}}},
          {"account_offers", {.handler = AccountOffersHandler{backend}}},
          {"account_tx", {.handler = AccountTxHandler{backend, etl}}},
          {"amm_info", {.handler = AMMInfoHandler{backend, amendmentCenter}}},
          {"book_changes", {.handler = BookChangesHandler{backend}}},
          {"book_offers", {.handler = BookOffersHandler{backend, amendmentCenter}}},
          {"deposit_authorized", {.handler = DepositAuthorizedHandler{backend}}},
          {"feature", {.handler = FeatureHandler{backend, amendmentCenter}}},
          {"gateway_balances", {.handler = GatewayBalancesHandler{backend}}},
          {"get_aggregate_price", {.handler = GetAggregatePriceHandler{backend}}},
          {"ledger", {.handler = LedgerHandler{backend, amendmentCenter}}},
          {"ledger_data", {.handler = LedgerDataHandler{backend}}},
          {"ledger_entry", {.handler = LedgerEntryHandler{backend}}},
          {"ledger_index",
           {.handler = LedgerIndexHandler{backend}, .isClioOnly = true}},  // clio only
          {"ledger_range", {.handler = LedgerRangeHandler{backend}}},
          {"mpt_holders",
           {.handler = MPTHoldersHandler{backend}, .isClioOnly = true}},  // clio only
          {"nfts_by_issuer",
           {.handler = NFTsByIssuerHandler{backend}, .isClioOnly = true}},  // clio only
          {"nft_history",
           {.handler = NFTHistoryHandler{backend}, .isClioOnly = true}},  // clio only
          {"nft_buy_offers", {.handler = NFTBuyOffersHandler{backend}}},
          {"nft_info", {.handler = NFTInfoHandler{backend}, .isClioOnly = true}},  // clio only
          {"nft_sell_offers", {.handler = NFTSellOffersHandler{backend}}},
          {"noripple_check", {.handler = NoRippleCheckHandler{backend}}},
          {"ping", {.handler = PingHandler{}}},
          {"random", {.handler = RandomHandler{}}},
          {"server_info",
           {.handler = ServerInfoHandler{backend, subscriptionManager, balancer, etl, counters}}},
          {"transaction_entry", {.handler = TransactionEntryHandler{backend}}},
          {"tx", {.handler = TxHandler{backend, etl}}},
          {"subscribe",
           {.handler = SubscribeHandler{backend, amendmentCenter, subscriptionManager}}},
          {"unsubscribe", {.handler = UnsubscribeHandler{subscriptionManager}}},
          {"vault_info", {.handler = VaultInfoHandler{backend}}},
          {"version", {.handler = VersionHandler{config}}},
      }
{
}

bool
ProductionHandlerProvider::contains(std::string const& command) const
{
    return handlerMap_.contains(command);
}

std::optional<AnyHandler>
ProductionHandlerProvider::getHandler(std::string const& command) const
{
    if (!handlerMap_.contains(command))
        return {};

    return handlerMap_.at(command).handler;
}

bool
ProductionHandlerProvider::isClioOnly(std::string const& command) const
{
    return handlerMap_.contains(command) && handlerMap_.at(command).isClioOnly;
}

std::unordered_set<std::string>
ProductionHandlerProvider::handlerNames() const
{
    std::unordered_set<std::string> result;
    for (auto const& [name, handler] : handlerMap_)
        result.insert(name);
    return result;
}

void
dumpAllRpcSpecs(std::ostream& os, uint32_t apiVersion)
{
    using SpecFn = rpc::spec::RpcSpecView (*)(uint32_t);

    struct Entry {
        std::string_view name;
        SpecFn specFn;  // nullptr -> no-input handler; emits "(no inputs)".
    };

    // Mirrors ProductionHandlerProvider's handler list. Keep in sync.
    // Handlers with no input spec (ping, random, ledger_range, version) use nullptr.
    constexpr auto kHANDLERS = std::to_array<Entry>({
        {.name = "account_channels", .specFn = &AccountChannelsHandler::spec},
        {.name = "account_currencies", .specFn = &AccountCurrenciesHandler::spec},
        {.name = "account_info", .specFn = &AccountInfoHandler::spec},
        {.name = "account_lines", .specFn = &AccountLinesHandler::spec},
        {.name = "account_mptoken_issuances", .specFn = &AccountMPTokenIssuancesHandler::spec},
        {.name = "account_mptokens", .specFn = &AccountMPTokensHandler::spec},
        {.name = "account_nfts", .specFn = &AccountNFTsHandler::spec},
        {.name = "account_objects", .specFn = &AccountObjectsHandler::spec},
        {.name = "account_offers", .specFn = &AccountOffersHandler::spec},
        {.name = "account_tx", .specFn = &AccountTxHandler::spec},
        {.name = "amm_info", .specFn = &AMMInfoHandler::spec},
        {.name = "book_changes", .specFn = &BookChangesHandler::spec},
        {.name = "book_offers", .specFn = &BookOffersHandler::spec},
        {.name = "deposit_authorized", .specFn = &DepositAuthorizedHandler::spec},
        {.name = "feature", .specFn = &FeatureHandler::spec},
        {.name = "gateway_balances", .specFn = &GatewayBalancesHandler::spec},
        {.name = "get_aggregate_price", .specFn = &GetAggregatePriceHandler::spec},
        {.name = "ledger", .specFn = &LedgerHandler::spec},
        {.name = "ledger_data", .specFn = &LedgerDataHandler::spec},
        {.name = "ledger_entry", .specFn = &LedgerEntryHandler::spec},
        {.name = "ledger_index", .specFn = &LedgerIndexHandler::spec},
        {.name = "ledger_range", .specFn = nullptr},
        {.name = "mpt_holders", .specFn = &MPTHoldersHandler::spec},
        {.name = "nfts_by_issuer", .specFn = &NFTsByIssuerHandler::spec},
        {.name = "nft_history", .specFn = &NFTHistoryHandler::spec},
        {.name = "nft_buy_offers", .specFn = &NFTBuyOffersHandler::spec},
        {.name = "nft_info", .specFn = &NFTInfoHandler::spec},
        {.name = "nft_sell_offers", .specFn = &NFTSellOffersHandler::spec},
        {.name = "noripple_check", .specFn = &NoRippleCheckHandler::spec},
        {.name = "ping", .specFn = nullptr},
        {.name = "random", .specFn = nullptr},
        {.name = "server_info", .specFn = &ServerInfoHandler::spec},
        {.name = "transaction_entry", .specFn = &TransactionEntryHandler::spec},
        {.name = "tx", .specFn = &TxHandler::spec},
        {.name = "subscribe", .specFn = &SubscribeHandler::spec},
        {.name = "unsubscribe", .specFn = &UnsubscribeHandler::spec},
        {.name = "vault_info", .specFn = &VaultInfoHandler::spec},
        {.name = "version", .specFn = nullptr},
    });

    auto sorted = kHANDLERS;
    std::ranges::sort(sorted, [](auto const& a, auto const& b) { return a.name < b.name; });

    rpc::spec::SpecDumpWriter writer{os};
    os << "apiVersion: " << apiVersion << "\nhandlers:\n";
    writer.push();
    for (auto const& entry : sorted) {
        writer.bulletGroup(entry.name, [&] {
            if (entry.specFn != nullptr) {
                entry.specFn(apiVersion).dump(writer);
            } else {
                writer.line("(no inputs)");
            }
        });
    }
    writer.pop();
}

}  // namespace rpc::impl
