#include "rpc/common/impl/HandlerRegistry.hpp"

#include "rpc/common/AnyHandler.hpp"
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

#include <array>
#include <span>

namespace rpc::impl {

constexpr auto kHANDLERS = std::to_array<HandlerEntry>({
    {.name = "account_channels",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountChannelsHandler{d.backend}; },
     .specFn  = &AccountChannelsHandler::spec},

    {.name = "account_currencies",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountCurrenciesHandler{d.backend}; },
     .specFn  = &AccountCurrenciesHandler::spec},

    {.name = "account_info",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountInfoHandler{d.backend, d.amendmentCenter}; },
     .specFn  = &AccountInfoHandler::spec},

    {.name = "account_lines",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountLinesHandler{d.backend}; },
     .specFn  = &AccountLinesHandler::spec},

    {.name = "account_mptoken_issuances",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountMPTokenIssuancesHandler{d.backend}; },
     .specFn  = &AccountMPTokenIssuancesHandler::spec,
     .isClioOnly = true},

    {.name = "account_mptokens",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountMPTokensHandler{d.backend}; },
     .specFn  = &AccountMPTokensHandler::spec,
     .isClioOnly = true},

    {.name = "account_nfts",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountNFTsHandler{d.backend}; },
     .specFn  = &AccountNFTsHandler::spec},

    {.name = "account_objects",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountObjectsHandler{d.backend}; },
     .specFn  = &AccountObjectsHandler::spec},

    {.name = "account_offers",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountOffersHandler{d.backend}; },
     .specFn  = &AccountOffersHandler::spec},

    {.name = "account_tx",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AccountTxHandler{d.backend, d.etl}; },
     .specFn  = &AccountTxHandler::spec},

    {.name = "amm_info",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return AMMInfoHandler{d.backend, d.amendmentCenter}; },
     .specFn  = &AMMInfoHandler::spec},

    {.name = "book_changes",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return BookChangesHandler{d.backend}; },
     .specFn  = &BookChangesHandler::spec},

    {.name = "book_offers",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return BookOffersHandler{d.backend, d.amendmentCenter}; },
     .specFn  = &BookOffersHandler::spec},

    {.name = "deposit_authorized",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return DepositAuthorizedHandler{d.backend}; },
     .specFn  = &DepositAuthorizedHandler::spec},

    {.name = "feature",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return FeatureHandler{d.backend, d.amendmentCenter}; },
     .specFn  = &FeatureHandler::spec},

    {.name = "gateway_balances",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return GatewayBalancesHandler{d.backend}; },
     .specFn  = &GatewayBalancesHandler::spec},

    {.name = "get_aggregate_price",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return GetAggregatePriceHandler{d.backend}; },
     .specFn  = &GetAggregatePriceHandler::spec},

    {.name = "ledger",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return LedgerHandler{d.backend, d.amendmentCenter}; },
     .specFn  = &LedgerHandler::spec},

    {.name = "ledger_data",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return LedgerDataHandler{d.backend}; },
     .specFn  = &LedgerDataHandler::spec},

    {.name = "ledger_entry",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return LedgerEntryHandler{d.backend}; },
     .specFn  = &LedgerEntryHandler::spec},

    {.name = "ledger_index",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return LedgerIndexHandler{d.backend}; },
     .specFn  = &LedgerIndexHandler::spec,
     .isClioOnly = true},

    {.name = "ledger_range",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return LedgerRangeHandler{d.backend}; },
     .specFn  = nullptr},

    {.name = "mpt_holders",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return MPTHoldersHandler{d.backend}; },
     .specFn  = &MPTHoldersHandler::spec,
     .isClioOnly = true},

    {.name = "nfts_by_issuer",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NFTsByIssuerHandler{d.backend}; },
     .specFn  = &NFTsByIssuerHandler::spec,
     .isClioOnly = true},

    {.name = "nft_history",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NFTHistoryHandler{d.backend}; },
     .specFn  = &NFTHistoryHandler::spec,
     .isClioOnly = true},

    {.name = "nft_buy_offers",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NFTBuyOffersHandler{d.backend}; },
     .specFn  = &NFTBuyOffersHandler::spec},

    {.name = "nft_info",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NFTInfoHandler{d.backend}; },
     .specFn  = &NFTInfoHandler::spec,
     .isClioOnly = true},

    {.name = "nft_sell_offers",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NFTSellOffersHandler{d.backend}; },
     .specFn  = &NFTSellOffersHandler::spec},

    {.name = "noripple_check",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return NoRippleCheckHandler{d.backend}; },
     .specFn  = &NoRippleCheckHandler::spec},

    {.name = "ping",
     .factory = [](HandlerDeps const&) -> AnyHandler { return PingHandler{}; },
     .specFn  = nullptr},

    {.name = "random",
     .factory = [](HandlerDeps const&) -> AnyHandler { return RandomHandler{}; },
     .specFn  = nullptr},

    {.name = "server_info",
     .factory = [](HandlerDeps const& d) -> AnyHandler {
         return ServerInfoHandler{d.backend, d.subscriptionManager, d.balancer, d.etl, d.counters};
     },
     .specFn  = &ServerInfoHandler::spec},

    {.name = "transaction_entry",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return TransactionEntryHandler{d.backend}; },
     .specFn  = &TransactionEntryHandler::spec},

    {.name = "tx",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return TxHandler{d.backend, d.etl}; },
     .specFn  = &TxHandler::spec},

    {.name = "subscribe",
     .factory = [](HandlerDeps const& d) -> AnyHandler {
         return SubscribeHandler{d.backend, d.amendmentCenter, d.subscriptionManager};
     },
     .specFn  = &SubscribeHandler::spec},

    {.name = "unsubscribe",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return UnsubscribeHandler{d.subscriptionManager}; },
     .specFn  = &UnsubscribeHandler::spec},

    {.name = "vault_info",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return VaultInfoHandler{d.backend}; },
     .specFn  = &VaultInfoHandler::spec},

    {.name = "version",
     .factory = [](HandlerDeps const& d) -> AnyHandler { return VersionHandler{d.config}; },
     .specFn  = nullptr},
});

std::span<HandlerEntry const>
handlerRegistry() noexcept
{
    return kHANDLERS;
}

}  // namespace rpc::impl
