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
#include "feed/impl/TransactionFeed.hpp"
#include "util/MockPrometheus.hpp"
#include "util/MockWsBase.hpp"
#include "util/SyncExecutionCtxFixture.hpp"
#include "util/TestObject.hpp"
#include "util/prometheus/Gauge.hpp"
#include "web/SubscriptionContextInterface.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <memory>
#include <vector>

constexpr static auto kACCOUN_T1 = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr static auto kACCOUN_T2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr static auto kLEDGERHASH = "1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC";
constexpr static auto kCURRENCY = "0158415500000000C1F76FF6ECB0BAC600000000";
constexpr static auto kISSUER = "rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD";
constexpr static auto kTXNID = "E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321";

constexpr static auto kTRAN_V1 =
    R"({
        "transaction":
        {
            "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "Amount":"1",
            "DeliverMax":"1",
            "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
            "Fee":"1",
            "Sequence":32,
            "SigningPubKey":"74657374",
            "TransactionType":"Payment",
            "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
            "date":0
        },
        "meta":
        {
            "AffectedNodes":
            [
                {
                    "ModifiedNode":
                    {
                        "FinalFields":
                        {
                            "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                            "Balance":"110"
                        },
                        "LedgerEntryType":"AccountRoot"
                    }
                },
                {
                    "ModifiedNode":
                    {
                        "FinalFields":
                        {
                            "Account":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                            "Balance":"30"
                        },
                        "LedgerEntryType":"AccountRoot"
                    }
                }
            ],
            "TransactionIndex":22,
            "TransactionResult":"tesSUCCESS",
            "delivered_amount":"unavailable"
        },
        "type":"transaction",
        "validated":true,
        "status":"closed",
        "ledger_index":33,
        "close_time_iso": "2000-01-01T00:00:00Z",
        "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
        "engine_result_code":0,
        "engine_result":"tesSUCCESS",
        "engine_result_message":"The transaction was applied. Only final in a validated ledger."
    })";

constexpr static auto kTRAN_V2 =
    R"({
        "tx_json":
        {
            "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "DeliverMax":"1",
            "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
            "Fee":"1",
            "Sequence":32,
            "SigningPubKey":"74657374",
            "TransactionType":"Payment",
            "date":0
        },
        "meta":
        {
            "AffectedNodes":
            [
                {
                    "ModifiedNode":{
                    "FinalFields":{
                        "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                        "Balance":"110"
                    },
                    "LedgerEntryType":"AccountRoot"
                    }
                },
                {
                    "ModifiedNode":{
                    "FinalFields":{
                        "Account":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                        "Balance":"30"
                    },
                    "LedgerEntryType":"AccountRoot"
                    }
                }
            ],
            "TransactionIndex":22,
            "TransactionResult":"tesSUCCESS",
            "delivered_amount":"unavailable"
        },
        "type":"transaction",
        "validated":true,
        "status":"closed",
        "ledger_index":33,
        "close_time_iso": "2000-01-01T00:00:00Z",
        "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
        "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
        "engine_result_code":0,
        "engine_result":"tesSUCCESS",
        "engine_result_message":"The transaction was applied. Only final in a validated ledger."
    })";

using namespace feed::impl;
using namespace util::prometheus;

using FeedTransactionTest = FeedBaseTest<TransactionFeed>;

TEST_F(FeedTransactionTest, SubTransactionV1)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(sessionPtr_);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);
}

TEST_F(FeedTransactionTest, SubTransactionForProposedTx)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsubProposed(sessionPtr_);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubTransactionV2)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubAccountV1)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);

    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubForProposedAccount)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);

    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsubProposed(account, sessionPtr_);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubAccountV2)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);

    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2)));
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubBothTransactionAndAccount)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);
    EXPECT_CALL(*mockSessionPtr_, onDisconnect).Times(2);
    testFeedPtr_->sub(account, sessionPtr_);
    testFeedPtr_->sub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);

    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).Times(2).WillRepeatedly(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(2);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);
    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubBookV1)
{
    auto const issue1 = getIssue(kCURRENCY, kISSUER);
    ripple::Book const book{ripple::xrpIssue(), issue1};

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    auto obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;

    auto metaObj = createMetaDataForBookChange(kCURRENCY, kISSUER, 22, 3, 1, 1, 3);
    trans1.metadata = metaObj.getSerializer().peekData();

    constexpr static auto kORDERBOOK_PUBLISH =
        R"({
            "transaction":
            {
                "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "Amount":"1",
                "DeliverMax":"1",
                "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "Fee":"1",
                "Sequence":32,
                "SigningPubKey":"74657374",
                "TransactionType":"Payment",
                "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
                "date":0
            },
            "meta":
            {
                "AffectedNodes":
                [
                    {
                        "ModifiedNode":
                        {
                            "FinalFields":
                            {
                                "TakerGets":"3",
                                "TakerPays":
                                {
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"1"
                                }
                            },
                            "LedgerEntryType":"Offer",
                            "PreviousFields":{
                                "TakerGets":"1",
                                "TakerPays":
                                {
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"3"
                                }
                            }
                        }
                    }
                ],
                "TransactionIndex":22,
                "TransactionResult":"tesSUCCESS",
                "delivered_amount":"unavailable"
            },
            "type":"transaction",
            "validated":true,
            "status":"closed",
            "ledger_index":33,
            "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "engine_result_code":0,
            "engine_result":"tesSUCCESS",
            "close_time_iso": "2000-01-01T00:00:00Z",
            "engine_result_message":"The transaction was applied. Only final in a validated ledger."
        })";

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kORDERBOOK_PUBLISH))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    // trigger by offer cancel meta data
    metaObj = createMetaDataForCancelOffer(kCURRENCY, kISSUER, 22, 3, 1);
    trans1.metadata = metaObj.getSerializer().peekData();

    constexpr static auto kORDERBOOK_CANCEL_PUBLISH =
        R"({
            "transaction":{
                "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "Amount":"1",
                "DeliverMax":"1",
                "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "Fee":"1",
                "Sequence":32,
                "SigningPubKey":"74657374",
                "TransactionType":"Payment",
                "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
                "date":0
            },
            "meta":{
                "AffectedNodes":
                [
                    {
                        "DeletedNode":
                        {
                            "FinalFields":
                            {
                                "TakerGets":"3",
                                "TakerPays":{
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"1"
                                }
                            },
                            "LedgerEntryType":"Offer"
                        }
                    }
                ],
                "TransactionIndex":22,
                "TransactionResult":"tesSUCCESS",
                "delivered_amount":"unavailable"
            },
            "type":"transaction",
            "validated":true,
            "status":"closed",
            "ledger_index":33,
            "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "engine_result_code":0,
            "engine_result":"tesSUCCESS",
            "close_time_iso": "2000-01-01T00:00:00Z",
            "engine_result_message":"The transaction was applied. Only final in a validated ledger."
        })";

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kORDERBOOK_CANCEL_PUBLISH))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    // trigger by offer create meta data
    constexpr static auto kORDERBOOK_CREATE_PUBLISH =
        R"({
            "transaction":
            {
                "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "Amount":"1",
                "DeliverMax":"1",
                "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "Fee":"1",
                "Sequence":32,
                "SigningPubKey":"74657374",
                "TransactionType":"Payment",
                "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
                "date":0
            },
            "meta":
            {
                "AffectedNodes":
                [
                    {
                        "CreatedNode":
                        {
                            "NewFields":{
                                "TakerGets":"3",
                                "TakerPays":
                                {
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"1"
                                }
                            },
                            "LedgerEntryType":"Offer"
                        }
                    }
                ],
                "TransactionIndex":22,
                "TransactionResult":"tesSUCCESS",
                "delivered_amount":"unavailable"
            },
            "type":"transaction",
            "validated":true,
            "status":"closed",
            "ledger_index":33,
            "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "engine_result_code":0,
            "engine_result":"tesSUCCESS",
            "close_time_iso": "2000-01-01T00:00:00Z",
            "engine_result_message":"The transaction was applied. Only final in a validated ledger."
        })";
    metaObj = createMetaDataForCreateOffer(kCURRENCY, kISSUER, 22, 3, 1);
    trans1.metadata = metaObj.getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kORDERBOOK_CREATE_PUBLISH))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubBookV2)
{
    auto const issue1 = getIssue(kCURRENCY, kISSUER);
    ripple::Book const book{ripple::xrpIssue(), issue1};

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    auto obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;

    auto const metaObj = createMetaDataForBookChange(kCURRENCY, kISSUER, 22, 3, 1, 1, 3);
    trans1.metadata = metaObj.getSerializer().peekData();

    constexpr static auto kORDERBOOK_PUBLISH =
        R"({
            "tx_json":
            {
                "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "DeliverMax":"1",
                "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "Fee":"1",
                "Sequence":32,
                "SigningPubKey":"74657374",
                "TransactionType":"Payment",
                "date":0
            },
            "meta":
            {
                "AffectedNodes":
                [
                    {
                        "ModifiedNode":
                        {
                            "FinalFields":
                            {
                                "TakerGets":"3",
                                "TakerPays":
                                {
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"1"
                                }
                            },
                            "LedgerEntryType":"Offer",
                            "PreviousFields":
                            {
                                "TakerGets":"1",
                                "TakerPays":
                                {
                                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                                    "value":"3"
                                }
                            }
                        }
                    }
                ],
                "TransactionIndex":22,
                "TransactionResult":"tesSUCCESS",
                "delivered_amount":"unavailable"
            },
            "type":"transaction",
            "validated":true,
            "status":"closed",
            "ledger_index":33,
            "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "engine_result_code":0,
            "engine_result":"tesSUCCESS",
            "close_time_iso": "2000-01-01T00:00:00Z",
            "hash":"51D2AAA6B8E4E16EF22F6424854283D8391B56875858A711B8CE4D5B9A422CC2",
            "engine_result_message":"The transaction was applied. Only final in a validated ledger."
        })";

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kORDERBOOK_PUBLISH))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, TransactionContainsBothAccountsSubed)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);

    auto const account2 = getAccountIdWithString(kACCOUN_T2);
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account2, sessionPtr_);

    EXPECT_EQ(testFeedPtr_->accountSubCount(), 2);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account2, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubAccountRepeatWithDifferentVersion)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);

    auto const account2 = getAccountIdWithString(kACCOUN_T2);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account2, sessionPtr_);

    EXPECT_EQ(testFeedPtr_->accountSubCount(), 2);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account2, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubTransactionRepeatWithDifferentVersion)
{
    // sub version 1 first
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    // sub version 2 later
    testFeedPtr_->sub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(2));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V2))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubRepeat)
{
    auto const session2 = std::make_shared<MockSession>();

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    EXPECT_CALL(*session2, onDisconnect);
    testFeedPtr_->sub(session2);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 2);

    testFeedPtr_->sub(sessionPtr_);
    testFeedPtr_->sub(session2);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 2);

    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);
    testFeedPtr_->unsub(session2);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);
    testFeedPtr_->unsub(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    auto const account = getAccountIdWithString(kACCOUN_T1);
    auto const account2 = getAccountIdWithString(kACCOUN_T2);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);

    EXPECT_CALL(*session2, onDisconnect);
    testFeedPtr_->sub(account2, session2);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 2);

    testFeedPtr_->sub(account, sessionPtr_);
    testFeedPtr_->sub(account2, session2);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 2);

    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);
    testFeedPtr_->unsub(account2, session2);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);
    testFeedPtr_->unsub(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    auto const issue1 = getIssue(kCURRENCY, kISSUER);
    ripple::Book const book{ripple::xrpIssue(), issue1};

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 1);

    EXPECT_CALL(*session2, onDisconnect);
    testFeedPtr_->sub(book, session2);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 2);

    testFeedPtr_->unsub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 1);
    testFeedPtr_->unsub(book, session2);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 0);
    testFeedPtr_->unsub(book, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->bookSubCount(), 0);
}

TEST_F(FeedTransactionTest, PubTransactionWithOwnerFund)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createCreateOfferTransactionObject(kACCOUN_T1, 1, 32, kCURRENCY, kISSUER, 1, 3);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    ripple::STArray const metaArray{0};
    ripple::STObject metaObj(ripple::sfTransactionMetaData);
    metaObj.setFieldArray(ripple::sfAffectedNodes, metaArray);
    metaObj.setFieldU8(ripple::sfTransactionResult, ripple::tesSUCCESS);
    metaObj.setFieldU32(ripple::sfTransactionIndex, 22);
    trans1.metadata = metaObj.getSerializer().peekData();

    ripple::STObject line(ripple::sfIndexes);
    line.setFieldU16(ripple::sfLedgerEntryType, ripple::ltRIPPLE_STATE);
    line.setFieldAmount(ripple::sfLowLimit, ripple::STAmount(10, false));
    line.setFieldAmount(ripple::sfHighLimit, ripple::STAmount(100, false));
    line.setFieldH256(ripple::sfPreviousTxnID, ripple::uint256{kTXNID});
    line.setFieldU32(ripple::sfPreviousTxnLgrSeq, 3);
    line.setFieldU32(ripple::sfFlags, 0);
    auto const issue2 = getIssue(kCURRENCY, kISSUER);
    line.setFieldAmount(ripple::sfBalance, ripple::STAmount(issue2, 100));

    EXPECT_CALL(*backend_, doFetchLedgerObject).Times(3);
    auto const issueAccount = getAccountIdWithString(kISSUER);
    auto const kk = ripple::keylet::account(issueAccount).key;
    ON_CALL(*backend_, doFetchLedgerObject(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(line.getSerializer().peekData()));
    ripple::STObject const accountRoot = createAccountRootObject(kISSUER, 0, 1, 10, 2, kTXNID, 3);
    ON_CALL(*backend_, doFetchLedgerObject(kk, testing::_, testing::_))
        .WillByDefault(testing::Return(accountRoot.getSerializer().peekData()));

    constexpr static auto kTRANSACTION_FOR_OWNER_FUND =
        R"({
            "transaction":
            {
                "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "Fee":"1",
                "Sequence":32,
                "SigningPubKey":"74657374",
                "TakerGets":
                {
                    "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                    "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                    "value":"1"
                },
                "TakerPays":"3",
                "TransactionType":"OfferCreate",
                "hash":"EE8775B43A67F4803DECEC5E918E0EA9C56D8ED93E512EBE9F2891846509AAAB",
                "date":0,
                "owner_funds":"100"
            },
            "meta":
            {
                "AffectedNodes":[],
                "TransactionIndex":22,
                "TransactionResult":"tesSUCCESS"
            },
            "type":"transaction",
            "validated":true,
            "status":"closed",
            "ledger_index":33,
            "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "engine_result_code":0,
            "close_time_iso": "2000-01-01T00:00:00Z",
            "engine_result":"tesSUCCESS",
            "engine_result_message":"The transaction was applied. Only final in a validated ledger."
        })";

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRANSACTION_FOR_OWNER_FUND))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

constexpr static auto kTRAN_FROZEN =
    R"({
        "transaction":
        {
            "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "Fee":"1",
            "Sequence":32,
            "SigningPubKey":"74657374",
            "TakerGets":
            {
                "currency":"0158415500000000C1F76FF6ECB0BAC600000000",
                "issuer":"rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD",
                "value":"1"
            },
            "TakerPays":"3",
            "TransactionType":"OfferCreate",
            "hash":"EE8775B43A67F4803DECEC5E918E0EA9C56D8ED93E512EBE9F2891846509AAAB",
            "date":0,
            "owner_funds":"0"
        },
        "meta":{
            "AffectedNodes":[],
            "TransactionIndex":22,
            "TransactionResult":"tesSUCCESS"
        },
        "type":"transaction",
        "validated":true,
        "status":"closed",
        "ledger_index":33,
        "close_time_iso": "2000-01-01T00:00:00Z",
        "ledger_hash":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
        "engine_result_code":0,
        "engine_result":"tesSUCCESS",
        "engine_result_message":"The transaction was applied. Only final in a validated ledger."
    })";

TEST_F(FeedTransactionTest, PubTransactionOfferCreationFrozenLine)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createCreateOfferTransactionObject(kACCOUN_T1, 1, 32, kCURRENCY, kISSUER, 1, 3);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    ripple::STArray const metaArray{0};
    ripple::STObject metaObj(ripple::sfTransactionMetaData);
    metaObj.setFieldArray(ripple::sfAffectedNodes, metaArray);
    metaObj.setFieldU8(ripple::sfTransactionResult, ripple::tesSUCCESS);
    metaObj.setFieldU32(ripple::sfTransactionIndex, 22);
    trans1.metadata = metaObj.getSerializer().peekData();

    ripple::STObject line(ripple::sfIndexes);
    line.setFieldU16(ripple::sfLedgerEntryType, ripple::ltRIPPLE_STATE);
    line.setFieldAmount(ripple::sfLowLimit, ripple::STAmount(10, false));
    line.setFieldAmount(ripple::sfHighLimit, ripple::STAmount(100, false));
    line.setFieldH256(ripple::sfPreviousTxnID, ripple::uint256{kTXNID});
    line.setFieldU32(ripple::sfPreviousTxnLgrSeq, 3);
    line.setFieldU32(ripple::sfFlags, ripple::lsfHighFreeze);
    line.setFieldAmount(ripple::sfBalance, ripple::STAmount(getIssue(kCURRENCY, kISSUER), 100));

    EXPECT_CALL(*backend_, doFetchLedgerObject).Times(3);
    auto const issueAccount = getAccountIdWithString(kISSUER);
    auto const kk = ripple::keylet::account(issueAccount).key;
    ON_CALL(*backend_, doFetchLedgerObject(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(line.getSerializer().peekData()));
    ripple::STObject const accountRoot = createAccountRootObject(kISSUER, 0, 1, 10, 2, kTXNID, 3);
    ON_CALL(*backend_, doFetchLedgerObject(kk, testing::_, testing::_))
        .WillByDefault(testing::Return(accountRoot.getSerializer().peekData()));

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_FROZEN))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubTransactionOfferCreationGlobalFrozen)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createCreateOfferTransactionObject(kACCOUN_T1, 1, 32, kCURRENCY, kISSUER, 1, 3);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    ripple::STArray const metaArray{0};
    ripple::STObject metaObj(ripple::sfTransactionMetaData);
    metaObj.setFieldArray(ripple::sfAffectedNodes, metaArray);
    metaObj.setFieldU8(ripple::sfTransactionResult, ripple::tesSUCCESS);
    metaObj.setFieldU32(ripple::sfTransactionIndex, 22);
    trans1.metadata = metaObj.getSerializer().peekData();

    ripple::STObject line(ripple::sfIndexes);
    line.setFieldU16(ripple::sfLedgerEntryType, ripple::ltRIPPLE_STATE);
    line.setFieldAmount(ripple::sfLowLimit, ripple::STAmount(10, false));
    line.setFieldAmount(ripple::sfHighLimit, ripple::STAmount(100, false));
    line.setFieldH256(ripple::sfPreviousTxnID, ripple::uint256{kTXNID});
    line.setFieldU32(ripple::sfPreviousTxnLgrSeq, 3);
    line.setFieldU32(ripple::sfFlags, ripple::lsfHighFreeze);
    auto const issueAccount = getAccountIdWithString(kISSUER);
    line.setFieldAmount(ripple::sfBalance, ripple::STAmount(getIssue(kCURRENCY, kISSUER), 100));

    EXPECT_CALL(*backend_, doFetchLedgerObject).Times(2);
    auto const kk = ripple::keylet::account(issueAccount).key;
    ON_CALL(*backend_, doFetchLedgerObject(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Return(line.getSerializer().peekData()));
    ripple::STObject const accountRoot = createAccountRootObject(kISSUER, ripple::lsfGlobalFreeze, 1, 10, 2, kTXNID, 3);
    ON_CALL(*backend_, doFetchLedgerObject(kk, testing::_, testing::_))
        .WillByDefault(testing::Return(accountRoot.getSerializer().peekData()));

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_FROZEN))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubBothProposedAndValidatedAccount)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(account, sessionPtr_);
    testFeedPtr_->unsubProposed(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubBothProposedAndValidated)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 1);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).Times(2).WillRepeatedly(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1))).Times(2);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    testFeedPtr_->unsub(sessionPtr_);
    testFeedPtr_->unsubProposed(sessionPtr_);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubProposedDisconnect)
{
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(sessionPtr_);
    EXPECT_EQ(testFeedPtr_->transactionSubCount(), 0);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    sessionPtr_.reset();
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

TEST_F(FeedTransactionTest, SubProposedAccountDisconnect)
{
    auto const account = getAccountIdWithString(kACCOUN_T1);

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->subProposed(account, sessionPtr_);
    EXPECT_EQ(testFeedPtr_->accountSubCount(), 0);

    auto const ledgerHeader = createLedgerHeader(kLEDGERHASH, 33);
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    trans1.metadata =
        createPaymentTransactionMetaObject(kACCOUN_T1, kACCOUN_T2, 110, 30, 22).getSerializer().peekData();

    EXPECT_CALL(*mockSessionPtr_, apiSubversion).WillOnce(testing::Return(1));
    EXPECT_CALL(*mockSessionPtr_, send(sharedStringJsonEq(kTRAN_V1))).Times(1);
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);

    sessionPtr_.reset();
    testFeedPtr_->pub(trans1, ledgerHeader, backend_);
}

struct TransactionFeedMockPrometheusTest : WithMockPrometheus, SyncExecutionCtxFixture {
protected:
    web::SubscriptionContextPtr sessionPtr_ = std::make_shared<MockSession>();
    std::shared_ptr<TransactionFeed> testFeedPtr_ = std::make_shared<TransactionFeed>(ctx_);
    MockSession* mockSessionPtr_ = dynamic_cast<MockSession*>(sessionPtr_.get());
};

TEST_F(TransactionFeedMockPrometheusTest, subUnsub)
{
    auto& counterTx = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"tx\"}");
    auto& counterAccount = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"account\"}");
    auto& counterBook = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"book\"}");

    EXPECT_CALL(counterTx, add(1));
    EXPECT_CALL(counterTx, add(-1));
    EXPECT_CALL(counterAccount, add(1));
    EXPECT_CALL(counterAccount, add(-1));
    EXPECT_CALL(counterBook, add(1));
    EXPECT_CALL(counterBook, add(-1));

    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(sessionPtr_);
    testFeedPtr_->unsub(sessionPtr_);

    auto const account = getAccountIdWithString(kACCOUN_T1);
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(account, sessionPtr_);
    testFeedPtr_->unsub(account, sessionPtr_);

    auto const issue1 = getIssue(kCURRENCY, kISSUER);
    ripple::Book const book{ripple::xrpIssue(), issue1};
    EXPECT_CALL(*mockSessionPtr_, onDisconnect);
    testFeedPtr_->sub(book, sessionPtr_);
    testFeedPtr_->unsub(book, sessionPtr_);
}

TEST_F(TransactionFeedMockPrometheusTest, AutoDisconnect)
{
    auto& counterTx = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"tx\"}");
    auto& counterAccount = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"account\"}");
    auto& counterBook = makeMock<GaugeInt>("subscriptions_current_number", "{stream=\"book\"}");

    EXPECT_CALL(counterTx, add(1));
    EXPECT_CALL(counterTx, add(-1));
    EXPECT_CALL(counterAccount, add(1));
    EXPECT_CALL(counterAccount, add(-1));
    EXPECT_CALL(counterBook, add(1));
    EXPECT_CALL(counterBook, add(-1));

    std::vector<web::SubscriptionContextInterface::OnDisconnectSlot> onDisconnectSlots;

    EXPECT_CALL(*mockSessionPtr_, onDisconnect).Times(3).WillRepeatedly([&onDisconnectSlots](auto const& slot) {
        onDisconnectSlots.push_back(slot);
    });
    testFeedPtr_->sub(sessionPtr_);

    auto const account = getAccountIdWithString(kACCOUN_T1);
    testFeedPtr_->sub(account, sessionPtr_);

    auto const issue1 = getIssue(kCURRENCY, kISSUER);
    ripple::Book const book{ripple::xrpIssue(), issue1};
    testFeedPtr_->sub(book, sessionPtr_);

    // Emulate onDisconnect signal is called
    for (auto const& slot : onDisconnectSlots)
        slot(sessionPtr_.get());

    sessionPtr_.reset();
}
