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
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/subscribe/Spec.hpp>
#include <rpcspec/handlers/subscribe/Types.hpp>
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
    return rpc::spec::handlers::subscribe::kSpec;
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
    static constexpr auto kFetchLimit = 200;

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
                    bookBase, rng->maxSequence, kFetchLimit, yield
                );

                // the taker is not really used, same issue with
                // https://github.com/XRPLF/xrpl-dev-portal/issues/1818
                auto const takerID = internalBook.taker
                    ? accountFromStringStrict(*(internalBook.taker))
                    : beast::kZero;

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
                getOrderBook(xrpl::reversed(internalBook.book), *(output.asks));
            } else {
                if (!output.offers)
                    output.offers = boost::json::array();
                getOrderBook(internalBook.book, *(output.offers));
            }
        }

        subscriptions_->subBook(internalBook.book, session);

        if (internalBook.both)
            subscriptions_->subBook(xrpl::reversed(internalBook.book), session);
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

}  // namespace rpc

namespace rpc::spec::handlers::subscribe {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv)
{
    auto input = Input{};
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
        input.books = std::vector<OrderBook>();
        for (auto const& book : books->value().as_array()) {
            auto internalBook = OrderBook{};
            auto const& bookObject = book.as_object();

            if (auto const taker = bookObject.find(JS(taker)); taker != bookObject.end())
                internalBook.taker = boost::json::value_to<std::string>(taker->value());

            if (auto const both = bookObject.find(JS(both)); both != bookObject.end())
                internalBook.both = both->value().as_bool();

            if (auto const snapshot = bookObject.find(JS(snapshot)); snapshot != bookObject.end())
                internalBook.snapshot = snapshot->value().as_bool();

            auto const parsedBookMaybe = rpc::parseBook(book.as_object());
            ASSERT(parsedBookMaybe.has_value(), "Book parsing failed");
            internalBook.book = *parsedBookMaybe;
            input.books->push_back(internalBook);
        }
    }

    return input;
}

}  // namespace rpc::spec::handlers::subscribe
