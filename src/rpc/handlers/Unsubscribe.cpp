#include "rpc/handlers/Unsubscribe.hpp"

#include "feed/SubscriptionManagerInterface.hpp"
#include "feed/Types.hpp"
#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/Aliases.hpp>
#include <rpcspec/FieldSpec.hpp>
#include <rpcspec/RpcSpec.hpp>
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/Validators.hpp>
#include "util/Assert.hpp"

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rpc {

UnsubscribeHandler::UnsubscribeHandler(
    std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptions
)
    : subscriptions_(subscriptions)
{
}

rpc::spec::RpcSpecView
UnsubscribeHandler::spec([[maybe_unused]] uint32_t apiVersion)
{
    using namespace spec;

    // Validates an array of account identifiers (base58 or hex pubkey).
    // Errors mirror subscribeAccountsValidator from the old system exactly:
    //   - not array → RpcInvalidParams + key + "NotArray"
    //   - empty array → RpcActMalformed + key + " malformed."
    //   - element not string → RpcInvalidParams + key + "'sItemNotString"
    //   - element invalid account → RpcActMalformed + key + "'sItemMalformed"
    static constexpr auto kSUBSCRIBE_ACCOUNTS_VALIDATOR =
        spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
            if (!f.isArray()) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::RpcInvalidParams, std::string{f.key()} + "NotArray"
                }};
            }
            if (f.arraySize() == 0) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::RpcActMalformed, std::string{f.key()} + " malformed."
                }};
            }
            for (std::size_t i = 0; i < f.arraySize(); ++i) {
                auto const elem = f.element(i);
                if (!elem.isString()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams,
                        std::string{f.key()} + "'sItemNotString"
                    }};
                }
                if (!rpc::accountFromStringStrict(std::string{elem.asString()})) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcActMalformed,
                        std::string{f.key()} + "'sItemMalformed"
                    }};
                }
            }
            return {};
        }};

    // Validates the streams field: must be an array of known stream name strings.
    // Errors mirror subscribeStreamValidator from the old system exactly:
    //   - not array → RpcInvalidParams + key + "NotArray"
    //   - element not string → RpcInvalidParams + "streamNotString"
    //   - element in NOT_SUPPORT set → RpcNotSupported
    //   - element not in VALID set → RpcStreamMalformed
    static constexpr auto kSUBSCRIBE_STREAM_VALIDATOR =
        spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
            if (!f.isArray()) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::RpcInvalidParams, std::string{f.key()} + "NotArray"
                }};
            }
            static std::unordered_set<std::string> const kVALID_STREAMS = {
                "ledger",
                "transactions",
                "transactions_proposed",
                "book_changes",
                "manifests",
                "validations"
            };
            static std::unordered_set<std::string> const kNOT_SUPPORT_STREAMS = {
                "peer_status", "consensus", "server"
            };
            for (std::size_t i = 0; i < f.arraySize(); ++i) {
                auto const elem = f.element(i);
                if (!elem.isString()) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::RpcInvalidParams, "streamNotString"}
                    };
                }
                auto const str = std::string{elem.asString()};
                if (kNOT_SUPPORT_STREAMS.contains(str)) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcNotSupported}};
                }
                if (!kVALID_STREAMS.contains(str)) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcStreamMalformed}};
                }
            }
            return {};
        }};

    // Validates the books field: must be an array of valid book objects.
    // Errors mirror the old kBOOKS_VALIDATOR lambda exactly (including all parseBook errors).
    // Note: Unsubscribe does NOT check snapshot (no snapshot field in unsubscribe).
    static constexpr auto kBOOKS_VALIDATOR =
        spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
            if (!f.isArray()) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::RpcInvalidParams, std::string{f.key()} + "NotArray"
                }};
            }
            for (std::size_t i = 0; i < f.arraySize(); ++i) {
                auto const book = f.element(i);
                if (!book.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams, std::string{f.key()} + "ItemNotObject"
                    }};
                }

                auto const bothFa = book.child("both");
                if (bothFa.present() && !bothFa.isBool()) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::RpcInvalidParams, "bothNotBool"}
                    };
                }

                // Replicate parseBook(book.as_object()) errors inline using FA child API.
                auto const takerPaysFa = book.child("taker_pays");
                if (!takerPaysFa.present()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams, "Missing field 'taker_pays'"
                    }};
                }
                if (!takerPaysFa.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams, "Field 'taker_pays' is not an object"
                    }};
                }

                auto const takerGetsFa = book.child("taker_gets");
                if (!takerGetsFa.present()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams, "Missing field 'taker_gets'"
                    }};
                }
                if (!takerGetsFa.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcInvalidParams, "Field 'taker_gets' is not an object"
                    }};
                }

                // taker_pays currency
                auto const paysCurFa = takerPaysFa.child("currency");
                if (!paysCurFa.present() || !paysCurFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcSrcCurMalformed}};
                }
                xrpl::Currency payCurrency;
                if (!xrpl::toCurrency(payCurrency, std::string{paysCurFa.asString()})) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcSrcCurMalformed}};
                }

                // taker_gets currency
                auto const getsCurFa = takerGetsFa.child("currency");
                if (!getsCurFa.present() || !getsCurFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcDstAmtMalformed}};
                }
                xrpl::Currency getCurrency;
                if (!xrpl::toCurrency(getCurrency, std::string{getsCurFa.asString()})) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcDstAmtMalformed}};
                }

                // book-level domain (mirrors parseBook): must be string if present
                auto const domainFa = book.child("domain");
                if (domainFa.present() && !domainFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::RpcDomainMalformed}};
                }

                // taker_pays issuer
                xrpl::AccountID payIssuer;
                auto const paysIssuerFa = takerPaysFa.child("issuer");
                if (paysIssuerFa.present()) {
                    if (!paysIssuerFa.isString()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::RpcInvalidParams, "takerPaysIssuerNotString"
                        }};
                    }
                    if (!xrpl::toIssuer(payIssuer, std::string{paysIssuerFa.asString()})) {
                        return std::unexpected{
                            rpc::Status{rpc::RippledError::RpcSrcIsrMalformed}
                        };
                    }
                    if (payIssuer == xrpl::noAccount()) {
                        return std::unexpected{
                            rpc::Status{rpc::RippledError::RpcSrcIsrMalformed}
                        };
                    }
                } else {
                    payIssuer = xrpl::xrpAccount();
                }

                if (xrpl::isXRP(payCurrency) && !xrpl::isXRP(payIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcSrcIsrMalformed,
                        "Unneeded field 'taker_pays.issuer' for XRP currency specification."
                    }};
                }
                if (!xrpl::isXRP(payCurrency) && xrpl::isXRP(payIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcSrcIsrMalformed,
                        "Invalid field 'taker_pays.issuer', expected non-XRP issuer."
                    }};
                }

                // taker_gets issuer
                xrpl::AccountID getIssuer;
                auto const getsIssuerFa = takerGetsFa.child("issuer");
                if (getsIssuerFa.present()) {
                    if (!getsIssuerFa.isString()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::RpcInvalidParams,
                            "taker_gets.issuer should be string"
                        }};
                    }
                    if (!xrpl::toIssuer(getIssuer, std::string{getsIssuerFa.asString()})) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::RpcDstIsrMalformed,
                            "Invalid field 'taker_gets.issuer', bad issuer."
                        }};
                    }
                    if (getIssuer == xrpl::noAccount()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::RpcDstIsrMalformed,
                            "Invalid field 'taker_gets.issuer', bad issuer account one."
                        }};
                    }
                } else {
                    getIssuer = xrpl::xrpAccount();
                }

                if (xrpl::isXRP(getCurrency) && !xrpl::isXRP(getIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcDstIsrMalformed,
                        "Unneeded field 'taker_gets.issuer' for XRP currency specification."
                    }};
                }
                if (!xrpl::isXRP(getCurrency) && xrpl::isXRP(getIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::RpcDstIsrMalformed,
                        "Invalid field 'taker_gets.issuer', expected non-XRP issuer."
                    }};
                }

                if (payCurrency == getCurrency && payIssuer == getIssuer) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::RpcBadMarket, "badMarket"}
                    };
                }

                // book-level domain (mirrors inner parseBook overload): must parse as hex
                if (domainFa.present()) {
                    xrpl::uint256 dom;
                    if (!dom.parseHex(std::string{domainFa.asString()})) {
                        return std::unexpected{rpc::Status{rpc::RippledError::RpcDomainMalformed}};
                    }
                }
            }
            return {};
        }};

    static constexpr auto kRPC_SPEC = spec::RpcSpec{
        field(JS(streams)) | kSUBSCRIBE_STREAM_VALIDATOR,
        field(JS(accounts)) | kSUBSCRIBE_ACCOUNTS_VALIDATOR,
        field(JS(accounts_proposed)) | kSUBSCRIBE_ACCOUNTS_VALIDATOR,
        field(JS(books)) | kBOOKS_VALIDATOR,
        field(JS(url)) | deprecated,
        field(JS(rt_accounts)) | deprecated,
        field("rt_transactions") | deprecated
    };

    return kRPC_SPEC;
}

UnsubscribeHandler::Result
UnsubscribeHandler::process(Input const& input, Context const& ctx) const
{
    if (input.streams)
        unsubscribeFromStreams(*(input.streams), ctx.session);

    if (input.accounts)
        unsubscribeFromAccounts(*(input.accounts), ctx.session);

    if (input.accountsProposed)
        unsubscribeFromProposedAccounts(*(input.accountsProposed), ctx.session);

    if (input.books)
        unsubscribeFromBooks(*(input.books), ctx.session);

    return Output{};
}

void
UnsubscribeHandler::unsubscribeFromStreams(
    std::vector<std::string> const& streams,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& stream : streams) {
        if (stream == "ledger") {
            subscriptions_->unsubLedger(session);
        } else if (stream == "transactions") {
            subscriptions_->unsubTransactions(session);
        } else if (stream == "transactions_proposed") {
            subscriptions_->unsubProposedTransactions(session);
        } else if (stream == "validations") {
            subscriptions_->unsubValidation(session);
        } else if (stream == "manifests") {
            subscriptions_->unsubManifest(session);
        } else if (stream == "book_changes") {
            subscriptions_->unsubBookChanges(session);
        } else {
            ASSERT(false, "Unknown stream: {}", stream);
        }
    }
}

void
UnsubscribeHandler::unsubscribeFromAccounts(
    std::vector<std::string> accounts,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& account : accounts) {
        auto const accountID = accountFromStringStrict(account);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        subscriptions_->unsubAccount(*accountID, session);
    }
}

void
UnsubscribeHandler::unsubscribeFromProposedAccounts(
    std::vector<std::string> accountsProposed,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& account : accountsProposed) {
        auto const accountID = accountFromStringStrict(account);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        subscriptions_->unsubProposedAccount(*accountID, session);
    }
}
void
UnsubscribeHandler::unsubscribeFromBooks(
    std::vector<OrderBook> const& books,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& orderBook : books) {
        subscriptions_->unsubBook(orderBook.book, session);

        if (orderBook.both)
            subscriptions_->unsubBook(xrpl::reversed(orderBook.book), session);
    }
}

UnsubscribeHandler::Input
tag_invoke(boost::json::value_to_tag<UnsubscribeHandler::Input>, boost::json::value const& jv)
{
    auto input = UnsubscribeHandler::Input{};
    auto const& jsonObject = jv.as_object();

    if (auto const& streams = jsonObject.find(JS(streams)); streams != jsonObject.end()) {
        input.streams = std::vector<std::string>();
        for (auto const& stream : streams->value().as_array())
            input.streams->push_back(boost::json::value_to<std::string>(stream));
    }
    if (auto const& accounts = jsonObject.find(JS(accounts)); accounts != jsonObject.end()) {
        input.accounts = std::vector<std::string>();
        for (auto const& account : accounts->value().as_array())
            input.accounts->push_back(boost::json::value_to<std::string>(account));
    }
    if (auto const& accountsProposed = jsonObject.find(JS(accounts_proposed));
        accountsProposed != jsonObject.end()) {
        input.accountsProposed = std::vector<std::string>();
        for (auto const& account : accountsProposed->value().as_array())
            input.accountsProposed->push_back(boost::json::value_to<std::string>(account));
    }
    if (auto const& books = jsonObject.find(JS(books)); books != jsonObject.end()) {
        input.books = std::vector<UnsubscribeHandler::OrderBook>();
        for (auto const& book : books->value().as_array()) {
            auto internalBook = UnsubscribeHandler::OrderBook{};
            auto const& bookObject = book.as_object();

            if (auto const& both = bookObject.find(JS(both)); both != bookObject.end())
                internalBook.both = both->value().as_bool();

            auto const parsedBookMaybe = parseBook(book.as_object());
            ASSERT(parsedBookMaybe.has_value(), "Invalid book format");
            internalBook.book = *parsedBookMaybe;
            input.books->push_back(internalBook);
        }
    }

    return input;
}
}  // namespace rpc
