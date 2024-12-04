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

#include "data/Types.hpp"
#include "feed/FeedTestUtil.hpp"
#include "feed/impl/BookChangesFeed.hpp"
#include "feed/impl/ForwardFeed.hpp"
#include "util/TestObject.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/protocol/STObject.h>

#include <vector>

using namespace feed::impl;

constexpr static auto kLEDGERHASH = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr static auto kACCOUN_T1 = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr static auto kACCOUN_T2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr static auto kCURRENCY = "0158415500000000C1F76FF6ECB0BAC600000000";
constexpr static auto kISSUER = "rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD";

using FeedBookChangeTest = FeedBaseTest<BookChangesFeed>;

TEST_F(FeedBookChangeTest, Pub)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->count(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 32);
    auto transactions = std::vector<TransactionAndMetadata>{};
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    ripple::STObject const metaObj = createMetaDataForBookChange(kCURRENCY, kISSUER, 22, 1, 3, 3, 1);
    trans1.metadata = metaObj.getSerializer().peekData();
    transactions.push_back(trans1);

    constexpr static auto kBOOK_CHANGE_PUBLISH =
        R"({
            "type":"bookChanges",
            "ledger_index":32,
            "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
            "ledger_time":0,
            "changes":
            [
                {
                    "currency_a":"XRP_drops",
                    "currency_b":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD/0158415500000000C1F76FF6ECB0BAC600000000",
                    "volume_a":"2",
                    "volume_b":"2",
                    "high":"-1",
                    "low":"-1",
                    "open":"-1",
                    "close":"-1"
                }
            ]
        })";

    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kBOOK_CHANGE_PUBLISH))).Times(1);
    testFeedPtr_->pub(ledgerHeader, transactions);

    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->count(), 0);
    testFeedPtr_->pub(ledgerHeader, transactions);
}
