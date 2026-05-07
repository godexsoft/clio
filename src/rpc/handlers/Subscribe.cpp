#include "rpc/handlers/Subscribe.hpp"

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "data/Types.hpp"
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

#include <boost/asio/spawn.hpp>
#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpc {

SubscribeHandler::SubscribeHandler(
    std::shared_ptr<BackendInterface> sharedPtrBackend,
    std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter,
    std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptions
)
    : sharedPtrBackend_(std::move(sharedPtrBackend))
    , amendmentCenter_(amendmentCenter)
    , subscriptions_(subscriptions)
{
}

rpc::spec::RpcSpecView
SubscribeHandler::spec([[maybe_unused]] uint32_t apiVersion)
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

                auto const snapshotFa = book.child("snapshot");
                if (snapshotFa.present() && !snapshotFa.isBool()) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "snapshotNotBool"}
                    };
                }

                auto const takerFa = book.child("taker");
                if (takerFa.present()) {
                    // Mirror: meta::WithCustomError(accountValidator, rpcBAD_ISSUER + "Issuer
                    // account malformed.")
                    if (!takerFa.isString() ||
                        !rpc::accountFromStringStrict(std::string{takerFa.asString()})) {
                        return std::unexpected{rpc::Status{
                            rpc::RippledError::rpcBAD_ISSUER, "Issuer account malformed."
                        }};
                    }
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
        field("user") | deprecated,
        field(JS(password)) | deprecated,
        field(JS(rt_accounts)) | deprecated
    };

    return kRPC_SPEC;
}

SubscribeHandler::Result
SubscribeHandler::process(Input const& input, Context const& ctx) const
{
    auto output = Output{};

    // Mimic rippled. No matter what the request is, the api version changes for the whole session
    ctx.session->setApiSubversion(ctx.apiVersion);

    if (input.streams) {
        auto const ledger = subscribeToStreams(ctx.yield, *(input.streams), ctx.session);
        if (!ledger.empty())
            output.ledger = ledger;
    }

    if (input.accounts)
        subscribeToAccounts(*(input.accounts), ctx.session);

    if (input.accountsProposed)
        subscribeToAccountsProposed(*(input.accountsProposed), ctx.session);

    if (input.books)
        subscribeToBooks(*(input.books), ctx.session, ctx.yield, output);

    return output;
}

boost::json::object
SubscribeHandler::subscribeToStreams(
    boost::asio::yield_context yield,
    std::vector<std::string> const& streams,
    feed::SubscriberSharedPtr const& session
) const
{
    auto response = boost::json::object{};

    for (auto const& stream : streams) {
        if (stream == "ledger") {
            response = subscriptions_->subLedger(yield, session);
        } else if (stream == "transactions") {
            subscriptions_->subTransactions(session);
        } else if (stream == "transactions_proposed") {
            subscriptions_->subProposedTransactions(session);
        } else if (stream == "validations") {
            subscriptions_->subValidation(session);
        } else if (stream == "manifests") {
            subscriptions_->subManifest(session);
        } else if (stream == "book_changes") {
            subscriptions_->subBookChanges(session);
        }
    }

    return response;
}

void
SubscribeHandler::subscribeToAccountsProposed(
    std::vector<std::string> const& accounts,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& account : accounts) {
        auto const accountID = accountFromStringStrict(account);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        subscriptions_->subProposedAccount(*accountID, session);
    }
}

void
SubscribeHandler::subscribeToAccounts(
    std::vector<std::string> const& accounts,
    feed::SubscriberSharedPtr const& session
) const
{
    for (auto const& account : accounts) {
        auto const accountID = accountFromStringStrict(account);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        subscriptions_->subAccount(*accountID, session);
    }
}

void
SubscribeHandler::subscribeToBooks(
    std::vector<OrderBook> const& books,
    feed::SubscriberSharedPtr const& session,
    boost::asio::yield_context yield,
    Output& output
) const
{
    static constexpr auto kFETCH_LIMIT = 200;

    std::optional<data::LedgerRange> rng;

    for (auto const& internalBook : books) {
        if (internalBook.snapshot) {
            if (!rng) {
                rng = sharedPtrBackend_->fetchLedgerRange();
                ASSERT(rng.has_value(), "Subscribe's ledger range must be available");
            }

            auto const getOrderBook = [&](auto const& book, auto& snapshots) {
                auto const bookBase = getBookBase(book);
                auto const [offers, _] = sharedPtrBackend_->fetchBookOffers(
                    bookBase, rng->maxSequence, kFETCH_LIMIT, yield
                );

                // the taker is not really used, same issue with
                // https://github.com/XRPLF/xrpl-dev-portal/issues/1818
                auto const takerID = internalBook.taker
                    ? accountFromStringStrict(*(internalBook.taker))
                    : beast::zero;

                auto const orderBook = postProcessOrderBook(
                    offers,
                    book,
                    *takerID,
                    *sharedPtrBackend_,
                    *amendmentCenter_,
                    rng->maxSequence,
                    yield
                );
                std::copy(orderBook.begin(), orderBook.end(), std::back_inserter(snapshots));
            };

            if (internalBook.both) {
                if (!output.bids)
                    output.bids = boost::json::array();
                if (!output.asks)
                    output.asks = boost::json::array();
                getOrderBook(internalBook.book, *(output.bids));
                getOrderBook(ripple::reversed(internalBook.book), *(output.asks));
            } else {
                if (!output.offers)
                    output.offers = boost::json::array();
                getOrderBook(internalBook.book, *(output.offers));
            }
        }

        subscriptions_->subBook(internalBook.book, session);

        if (internalBook.both)
            subscriptions_->subBook(ripple::reversed(internalBook.book), session);
    }
}

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    SubscribeHandler::Output const& output
)
{
    jv = output.ledger ? *(output.ledger) : boost::json::object();

    if (output.offers)
        jv.as_object().emplace(JS(offers), *(output.offers));
    if (output.asks)
        jv.as_object().emplace(JS(asks), *(output.asks));
    if (output.bids)
        jv.as_object().emplace(JS(bids), *(output.bids));
}

SubscribeHandler::Input
tag_invoke(boost::json::value_to_tag<SubscribeHandler::Input>, boost::json::value const& jv)
{
    auto input = SubscribeHandler::Input{};
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
        input.books = std::vector<SubscribeHandler::OrderBook>();
        for (auto const& book : books->value().as_array()) {
            auto internalBook = SubscribeHandler::OrderBook{};
            auto const& bookObject = book.as_object();

            if (auto const taker = bookObject.find(JS(taker)); taker != bookObject.end())
                internalBook.taker = boost::json::value_to<std::string>(taker->value());

            if (auto const both = bookObject.find(JS(both)); both != bookObject.end())
                internalBook.both = both->value().as_bool();

            if (auto const snapshot = bookObject.find(JS(snapshot)); snapshot != bookObject.end())
                internalBook.snapshot = snapshot->value().as_bool();

            auto const parsedBookMaybe = parseBook(book.as_object());
            ASSERT(parsedBookMaybe.has_value(), "Book parsing failed");
            internalBook.book = *parsedBookMaybe;
            input.books->push_back(internalBook);
        }
    }

    return input;
}

}  // namespace rpc
