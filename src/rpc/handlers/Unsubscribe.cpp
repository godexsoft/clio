#include "rpc/handlers/Unsubscribe.hpp"

#include "feed/SubscriptionManagerInterface.hpp"
#include "feed/Types.hpp"
#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/Validators.hpp"
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
    //   - not array → rpcINVALID_PARAMS + key + "NotArray"
    //   - empty array → rpcACT_MALFORMED + key + " malformed."
    //   - element not string → rpcINVALID_PARAMS + key + "'sItemNotString"
    //   - element invalid account → rpcACT_MALFORMED + key + "'sItemMalformed"
    static constexpr auto kSUBSCRIBE_ACCOUNTS_VALIDATOR =
        spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
            if (!f.isArray()) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotArray"
                }};
            }
            if (f.arraySize() == 0) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::rpcACT_MALFORMED, std::string{f.key()} + " malformed."
                }};
            }
            for (std::size_t i = 0; i < f.arraySize(); ++i) {
                auto const elem = f.element(i);
                if (!elem.isString()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS,
                        std::string{f.key()} + "'sItemNotString"
                    }};
                }
                if (!rpc::accountFromStringStrict(std::string{elem.asString()})) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcACT_MALFORMED,
                        std::string{f.key()} + "'sItemMalformed"
                    }};
                }
            }
            return {};
        }};

    // Validates the streams field: must be an array of known stream name strings.
    // Errors mirror subscribeStreamValidator from the old system exactly:
    //   - not array → rpcINVALID_PARAMS + key + "NotArray"
    //   - element not string → rpcINVALID_PARAMS + "streamNotString"
    //   - element in NOT_SUPPORT set → rpcNOT_SUPPORTED
    //   - element not in VALID set → rpcSTREAM_MALFORMED
    static constexpr auto kSUBSCRIBE_STREAM_VALIDATOR =
        spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
            if (!f.isArray()) {
                return std::unexpected{rpc::Status{
                    rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotArray"
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
                        rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "streamNotString"}
                    };
                }
                auto const str = std::string{elem.asString()};
                if (kNOT_SUPPORT_STREAMS.contains(str)) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcNOT_SUPPORTED}};
                }
                if (!kVALID_STREAMS.contains(str)) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcSTREAM_MALFORMED}};
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
                    rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotArray"
                }};
            }
            for (std::size_t i = 0; i < f.arraySize(); ++i) {
                auto const book = f.element(i);
                if (!book.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "ItemNotObject"
                    }};
                }

                auto const bothFa = book.child("both");
                if (bothFa.present() && !bothFa.isBool()) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "bothNotBool"}
                    };
                }

                // Replicate parseBook(book.as_object()) errors inline using FA child API.
                auto const takerPaysFa = book.child("taker_pays");
                if (!takerPaysFa.present()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS, "Missing field 'taker_pays'"
                    }};
                }
                if (!takerPaysFa.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS, "Field 'taker_pays' is not an object"
                    }};
                }

                auto const takerGetsFa = book.child("taker_gets");
                if (!takerGetsFa.present()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS, "Missing field 'taker_gets'"
                    }};
                }
                if (!takerGetsFa.isObject()) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcINVALID_PARAMS, "Field 'taker_gets' is not an object"
                    }};
                }

                // taker_pays currency
                auto const paysCurFa = takerPaysFa.child("currency");
                if (!paysCurFa.present() || !paysCurFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcSRC_CUR_MALFORMED}};
                }
                ripple::Currency payCurrency;
                if (!ripple::to_currency(payCurrency, std::string{paysCurFa.asString()})) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcSRC_CUR_MALFORMED}};
                }

                // taker_gets currency
                auto const getsCurFa = takerGetsFa.child("currency");
                if (!getsCurFa.present() || !getsCurFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcDST_AMT_MALFORMED}};
                }
                ripple::Currency getCurrency;
                if (!ripple::to_currency(getCurrency, std::string{getsCurFa.asString()})) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcDST_AMT_MALFORMED}};
                }

                // book-level domain (mirrors parseBook): must be string if present
                auto const domainFa = book.child("domain");
                if (domainFa.present() && !domainFa.isString()) {
                    return std::unexpected{rpc::Status{rpc::RippledError::rpcDOMAIN_MALFORMED}};
                }

                // taker_pays issuer
                ripple::AccountID payIssuer;
                auto const paysIssuerFa = takerPaysFa.child("issuer");
                if (paysIssuerFa.present()) {
                    if (!paysIssuerFa.isString()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::rpcINVALID_PARAMS, "takerPaysIssuerNotString"
                        }};
                    }
                    if (!ripple::to_issuer(payIssuer, std::string{paysIssuerFa.asString()})) {
                        return std::unexpected{
                            rpc::Status{rpc::RippledError::rpcSRC_ISR_MALFORMED}
                        };
                    }
                    if (payIssuer == ripple::noAccount()) {
                        return std::unexpected{
                            rpc::Status{rpc::RippledError::rpcSRC_ISR_MALFORMED}
                        };
                    }
                } else {
                    payIssuer = ripple::xrpAccount();
                }

                if (ripple::isXRP(payCurrency) && !ripple::isXRP(payIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcSRC_ISR_MALFORMED,
                        "Unneeded field 'taker_pays.issuer' for XRP currency specification."
                    }};
                }
                if (!ripple::isXRP(payCurrency) && ripple::isXRP(payIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcSRC_ISR_MALFORMED,
                        "Invalid field 'taker_pays.issuer', expected non-XRP issuer."
                    }};
                }

                // taker_gets issuer
                ripple::AccountID getIssuer;
                auto const getsIssuerFa = takerGetsFa.child("issuer");
                if (getsIssuerFa.present()) {
                    if (!getsIssuerFa.isString()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::rpcINVALID_PARAMS,
                            "taker_gets.issuer should be string"
                        }};
                    }
                    if (!ripple::to_issuer(getIssuer, std::string{getsIssuerFa.asString()})) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::rpcDST_ISR_MALFORMED,
                            "Invalid field 'taker_gets.issuer', bad issuer."
                        }};
                    }
                    if (getIssuer == ripple::noAccount()) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::rpcDST_ISR_MALFORMED,
                            "Invalid field 'taker_gets.issuer', bad issuer account one."
                        }};
                    }
                } else {
                    getIssuer = ripple::xrpAccount();
                }

                if (ripple::isXRP(getCurrency) && !ripple::isXRP(getIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcDST_ISR_MALFORMED,
                        "Unneeded field 'taker_gets.issuer' for XRP currency specification."
                    }};
                }
                if (!ripple::isXRP(getCurrency) && ripple::isXRP(getIssuer)) {
                    return std::unexpected{rpc::Status{
                        rpc::RippledError::rpcDST_ISR_MALFORMED,
                        "Invalid field 'taker_gets.issuer', expected non-XRP issuer."
                    }};
                }

                if (payCurrency == getCurrency && payIssuer == getIssuer) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::rpcBAD_MARKET, "badMarket"}
                    };
                }

                // book-level domain (mirrors inner parseBook overload): must parse as hex
                if (domainFa.present()) {
                    ripple::uint256 dom;
                    if (!dom.parseHex(std::string{domainFa.asString()})) {
                        return std::unexpected{rpc::Status{rpc::RippledError::rpcDOMAIN_MALFORMED}};
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
            subscriptions_->unsubBook(ripple::reversed(orderBook.book), session);
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
