#include "rpc/handlers/Unsubscribe.hpp"

#include "feed/SubscriptionManagerInterface.hpp"
#include "feed/Types.hpp"
#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/unsubscribe/Spec.hpp>
#include <rpcspec/handlers/unsubscribe/Types.hpp>
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
    return rpc::spec::handlers::unsubscribe::kSpec;
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

}  // namespace rpc

namespace rpc::spec::handlers::unsubscribe {

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

            if (auto const& both = bookObject.find(JS(both)); both != bookObject.end())
                internalBook.both = both->value().as_bool();

            auto const parsedBookMaybe = rpc::parseBook(book.as_object());
            ASSERT(parsedBookMaybe.has_value(), "Invalid book format");
            internalBook.book = *parsedBookMaybe;
            input.books->push_back(internalBook);
        }
    }

    return input;
}

}  // namespace rpc::spec::handlers::unsubscribe
