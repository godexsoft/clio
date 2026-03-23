//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

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

#include "feed/Types.hpp"
#include "feed/impl/TrackableSignal.hpp"
#include "feed/impl/TrackableSignalMap.hpp"
#include "feed/impl/Util.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyStrand.hpp"
#include "util/log/Logger.hpp"
#include "util/prometheus/Gauge.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/json/object.hpp>
#include <fmt/format.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace feed::impl {

/**
 * @brief Feed that publishes the Proposed Transactions.
 */
class ProposedTransactionFeed {
    // Hold two versions of transaction messages: [0] = v1, [1] = v2
    using AllVersionMsgsType = std::array<std::shared_ptr<std::string>, 2>;

    struct ProposedTransactionSlot {
        std::reference_wrapper<ProposedTransactionFeed> feed;
        std::weak_ptr<Subscriber> subscriptionContextWeakPtr;

        ProposedTransactionSlot(ProposedTransactionFeed& feed, SubscriberSharedPtr const& connection)
            : feed(feed), subscriptionContextWeakPtr(connection)
        {
        }

        void
        operator()(AllVersionMsgsType const& allVersionMsgs) const;
    };

    util::Logger logger_{"Subscriptions"};

    std::unordered_set<SubscriberPtr> notified_;  // Used by slots to prevent double notifications
                                                  // if tx contains multiple subscribed accounts
    util::async::AnyStrand strand_;
    std::reference_wrapper<util::prometheus::GaugeInt> subAllCount_;
    std::reference_wrapper<util::prometheus::GaugeInt> subAccountCount_;

    TrackableSignalMap<ripple::AccountID, Subscriber, AllVersionMsgsType const&> accountSignal_;
    TrackableSignal<Subscriber, AllVersionMsgsType const&> signal_;

public:
    /**
     * @brief Move constructor is deleted because ProposedTransactionSlot takes ProposedTransactionFeed by reference.
     */
    ProposedTransactionFeed(ProposedTransactionFeed&&) = delete;

    /**
     * @brief Construct a Proposed Transaction Feed object.
     * @param executionCtx The actual publish will be called in the strand of this.
     */
    ProposedTransactionFeed(util::async::AnyExecutionContext& executionCtx)
        : strand_(executionCtx.makeStrand())
        , subAllCount_(getSubscriptionsGaugeInt("tx_proposed"))
        , subAccountCount_(getSubscriptionsGaugeInt("account_proposed"))

    {
    }

    /**
     * @brief Subscribe to the proposed transaction feed.
     * @param subscriber
     */
    void
    sub(SubscriberSharedPtr const& subscriber);

    /**
     * @brief Subscribe to the proposed transaction feed, only receive the feed when particular
     * account is affected.
     * @param subscriber
     * @param account The account to watch.
     */
    void
    sub(ripple::AccountID const& account, SubscriberSharedPtr const& subscriber);

    /**
     * @brief Unsubscribe to the proposed transaction feed.
     * @param subscriber
     */
    void
    unsub(SubscriberSharedPtr const& subscriber);

    /**
     * @brief Unsubscribe to the proposed transaction feed for particular account.
     * @param subscriber
     * @param account The account to unsubscribe.
     */
    void
    unsub(ripple::AccountID const& account, SubscriberSharedPtr const& subscriber);

    /**
     * @brief Publishes the proposed transaction feed.
     * @param receivedTxJson The proposed transaction json.
     */
    void
    pub(boost::json::object const& receivedTxJson);

    /**
     * @brief Get the number of subscribers of the proposed transaction feed.
     */
    std::uint64_t
    transactionSubcount() const;

    /**
     * @brief Get the number of accounts subscribers.
     */
    std::uint64_t
    accountSubCount() const;

private:
    void
    unsubInternal(SubscriberPtr subscriber);

    void
    unsubInternal(ripple::AccountID const& account, SubscriberPtr subscriber);
};
}  // namespace feed::impl
