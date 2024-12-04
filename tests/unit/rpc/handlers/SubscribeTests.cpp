//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

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
#include "rpc/Errors.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/handlers/Subscribe.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/MockSubscriptionManager.hpp"
#include "util/MockWsBase.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"
#include "web/SubscriptionContextInterface.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;
using std::chrono::milliseconds;

constexpr static auto kMINSEQ = 10;
constexpr static auto kMAXSEQ = 30;
constexpr static auto kACCOUNT = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr static auto kACCOUN_T2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr static auto kPAY_S20_USDGET_S10_XRPBOOKDIR =
    "43B83ADC452B85FCBADA6CAEAC5181C255A213630D58FFD455071AFD498D0000";
constexpr static auto kPAY_S20_XRPGET_S10_USDBOOKDIR =
    "7B1767D41DBCE79D9585CF9D0262A5FEC45E5206FF524F8B55071AFD498D0000";
constexpr static auto kINDE_X1 = "1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC";
constexpr static auto kINDE_X2 = "E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321";

struct RPCSubscribeHandlerTest : HandlerBaseTest {
    web::SubscriptionContextPtr session = std::make_shared<MockSession>();
    MockSession* mockSession = dynamic_cast<MockSession*>(session.get());
    StrictMockSubscriptionManagerSharedPtr mockSubscriptionManagerPtr;
};

struct SubscribeParamTestCaseBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

// parameterized test cases for parameters check
struct SubscribeParameterTest : public RPCSubscribeHandlerTest,
                                public WithParamInterface<SubscribeParamTestCaseBundle> {};

static auto
generateTestValuesForParametersTest()
{
    return std::vector<SubscribeParamTestCaseBundle>{
        SubscribeParamTestCaseBundle{
            "AccountsNotArray",
            R"({"accounts": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"})",
            "invalidParams",
            "accountsNotArray"
        },
        SubscribeParamTestCaseBundle{
            "AccountsItemNotString", R"({"accounts": [123]})", "invalidParams", "accounts'sItemNotString"
        },
        SubscribeParamTestCaseBundle{
            "AccountsItemInvalidString", R"({"accounts": ["123"]})", "actMalformed", "accounts'sItemMalformed"
        },
        SubscribeParamTestCaseBundle{
            "AccountsEmptyArray", R"({"accounts": []})", "actMalformed", "accounts malformed."
        },
        SubscribeParamTestCaseBundle{
            "AccountsProposedNotArray",
            R"({"accounts_proposed": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"})",
            "invalidParams",
            "accounts_proposedNotArray"
        },
        SubscribeParamTestCaseBundle{
            "AccountsProposedItemNotString",
            R"({"accounts_proposed": [123]})",
            "invalidParams",
            "accounts_proposed'sItemNotString"
        },
        SubscribeParamTestCaseBundle{
            "AccountsProposedItemInvalidString",
            R"({"accounts_proposed": ["123"]})",
            "actMalformed",
            "accounts_proposed'sItemMalformed"
        },
        SubscribeParamTestCaseBundle{
            "AccountsProposedEmptyArray", R"({"accounts_proposed": []})", "actMalformed", "accounts_proposed malformed."
        },
        SubscribeParamTestCaseBundle{"StreamsNotArray", R"({"streams": 1})", "invalidParams", "streamsNotArray"},
        SubscribeParamTestCaseBundle{"StreamNotString", R"({"streams": [1]})", "invalidParams", "streamNotString"},
        SubscribeParamTestCaseBundle{"StreamNotValid", R"({"streams": ["1"]})", "malformedStream", "Stream malformed."},
        SubscribeParamTestCaseBundle{
            "StreamPeerStatusNotSupport", R"({"streams": ["peer_status"]})", "notSupported", "Operation not supported."
        },
        SubscribeParamTestCaseBundle{
            "StreamConsensusNotSupport", R"({"streams": ["consensus"]})", "notSupported", "Operation not supported."
        },
        SubscribeParamTestCaseBundle{
            "StreamServerNotSupport", R"({"streams": ["server"]})", "notSupported", "Operation not supported."
        },
        SubscribeParamTestCaseBundle{"BooksNotArray", R"({"books": "1"})", "invalidParams", "booksNotArray"},
        SubscribeParamTestCaseBundle{
            "BooksItemNotObject", R"({"books": ["1"]})", "invalidParams", "booksItemNotObject"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemMissingTakerPays",
            R"({"books": [{"taker_gets": {"currency": "XRP"}}]})",
            "invalidParams",
            "Missing field 'taker_pays'"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemMissingTakerGets",
            R"({"books": [{"taker_pays": {"currency": "XRP"}}]})",
            "invalidParams",
            "Missing field 'taker_gets'"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsNotObject",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": "USD"
                    }
                ]
            })",
            "invalidParams",
            "Field 'taker_gets' is not an object"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysNotObject",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": "USD"
                    }
                ]
            })",
            "invalidParams",
            "Field 'taker_pays' is not an object"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysMissingCurrency",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {}
                    }
                ]
            })",
            "srcCurMalformed",
            "Source currency is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsMissingCurrency",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {}
                    }
                ]
            })",
            "dstAmtMalformed",
            "Destination amount/currency/issuer is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysCurrencyNotString",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {
                            "currency": 1,
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "srcCurMalformed",
            "Source currency is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsCurrencyNotString",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": 1,
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "dstAmtMalformed",
            "Destination amount/currency/issuer is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysInvalidCurrency",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {
                            "currency": "XXXXXX",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "srcCurMalformed",
            "Source currency is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsInvalidCurrency",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "xxxxxxx",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "dstAmtMalformed",
            "Destination amount/currency/issuer is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysMissingIssuer",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {
                            "currency": "USD"
                        }
                    }
                ]
            })",
            "srcIsrMalformed",
            "Invalid field 'taker_pays.issuer', expected non-XRP issuer."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsMissingIssuer",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD"
                        }
                    }
                ]
            })",
            "dstIsrMalformed",
            "Invalid field 'taker_gets.issuer', expected non-XRP issuer."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysIssuerNotString",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {
                            "currency": "USD",
                            "issuer": 1
                        }
                    }
                ]
            })",
            "invalidParams",
            "takerPaysIssuerNotString"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsIssuerNotString",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": 1
                        }
                    }
                ]
            })",
            "invalidParams",
            "taker_gets.issuer should be string"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysInvalidIssuer",
            R"({
                "books": [
                    {
                        "taker_gets": 
                        {
                            "currency": "XRP"
                        },
                        "taker_pays": {
                            "currency": "USD",
                            "issuer": "123"
                        }
                    }
                ]
            })",
            "srcIsrMalformed",
            "Source issuer is malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsInvalidIssuer",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "123"
                        }
                    }
                ]
            })",
            "dstIsrMalformed",
            "Invalid field 'taker_gets.issuer', bad issuer."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerGetsXRPHasIssuer",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "taker_gets": {
                            "currency": "XRP",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "dstIsrMalformed",
            "Unneeded field 'taker_gets.issuer' for XRP currency specification."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemTakerPaysXRPHasIssuer",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        }
                    }
                ]
            })",
            "srcIsrMalformed",
            "Unneeded field 'taker_pays.issuer' for XRP currency specification."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemBadMartket",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "XRP"
                        }
                    }
                ]
            })",
            "badMarket",
            "badMarket"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemInvalidSnapshot",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "snapshot": 0
                    }
                ]
            })",
            "invalidParams",
            "snapshotNotBool"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemInvalidBoth",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "both": 0
                    }
                ]
            })",
            "invalidParams",
            "bothNotBool"
        },
        SubscribeParamTestCaseBundle{
            "BooksItemInvalidTakerNotString",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "taker": 0
                    }
                ]
            })",
            "badIssuer",
            "Issuer account malformed."
        },
        SubscribeParamTestCaseBundle{
            "BooksItemInvalidTaker",
            R"({
                "books": [
                    {
                        "taker_pays": 
                        {
                            "currency": "XRP"
                        },
                        "taker_gets": {
                            "currency": "USD",
                            "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
                        },
                        "taker": "xxxxxxx"
                    }
                ]
            })",
            "badIssuer",
            "Issuer account malformed."
        },
    };
}

INSTANTIATE_TEST_CASE_P(
    RPCSubscribe,
    SubscribeParameterTest,
    ValuesIn(generateTestValuesForParametersTest()),
    tests::util::kNAME_GENERATOR
);

TEST_P(SubscribeParameterTest, InvalidParams)
{
    auto const testBundle = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        auto const req = json::parse(testBundle.testJson);
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), testBundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), testBundle.expectedErrorMessage);
    });
}

TEST_F(RPCSubscribeHandlerTest, EmptyResponse)
{
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(json::parse(R"({})"), Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, StreamsWithoutLedger)
{
    // these streams don't return response
    auto const input = json::parse(
        R"({
            "streams": ["transactions_proposed","transactions","validations","manifests","book_changes"]
        })"
    );
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subTransactions);
        EXPECT_CALL(*mockSubscriptionManagerPtr, subValidation);
        EXPECT_CALL(*mockSubscriptionManagerPtr, subManifest);
        EXPECT_CALL(*mockSubscriptionManagerPtr, subBookChanges);
        EXPECT_CALL(*mockSubscriptionManagerPtr, subProposedTransactions);

        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, StreamsLedger)
{
    static auto constexpr kEXPECTED_OUTPUT =
        R"({      
            "validated_ledgers":"10-30",
            "ledger_index":30,
            "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
            "ledger_time":0,
            "fee_base":1,
            "reserve_base":3,
            "reserve_inc":2
        })";

    auto const input = json::parse(
        R"({
            "streams": ["ledger"]
        })"
    );
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};

        EXPECT_CALL(*mockSubscriptionManagerPtr, subLedger)
            .WillOnce(testing::Return(boost::json::parse(kEXPECTED_OUTPUT).as_object()));

        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result->as_object(), json::parse(kEXPECTED_OUTPUT));
    });
}

TEST_F(RPCSubscribeHandlerTest, Accounts)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "accounts": ["{}","{}","{}"]
        }})",
        kACCOUNT,
        kACCOUN_T2,
        kACCOUN_T2
    ));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};

        EXPECT_CALL(*mockSubscriptionManagerPtr, subAccount(getAccountIdWithString(kACCOUNT), session));
        EXPECT_CALL(*mockSubscriptionManagerPtr, subAccount(getAccountIdWithString(kACCOUN_T2), session)).Times(2);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, AccountsProposed)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "accounts_proposed": ["{}","{}","{}"]
        }})",
        kACCOUNT,
        kACCOUN_T2,
        kACCOUN_T2
    ));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};

        EXPECT_CALL(*mockSubscriptionManagerPtr, subProposedAccount(getAccountIdWithString(kACCOUNT), session));
        EXPECT_CALL(*mockSubscriptionManagerPtr, subProposedAccount(getAccountIdWithString(kACCOUN_T2), session))
            .Times(2);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, JustBooks)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "books": 
            [
                {{
                    "taker_pays": 
                    {{
                        "currency": "XRP"
                    }},
                    "taker_gets": 
                    {{
                        "currency": "USD",
                        "issuer": "{}"
                    }}
                }}
            ]
        }})",
        kACCOUNT
    ));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subBook);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, BooksBothSet)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "books": 
            [
                {{
                    "taker_pays": 
                    {{
                        "currency": "XRP"
                    }},
                    "taker_gets": 
                    {{
                        "currency": "USD",
                        "issuer": "{}"
                    }},
                    "both": true
                }}
            ]
        }})",
        kACCOUNT
    ));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subBook).Times(2);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_TRUE(output.result->as_object().empty());
    });
}

TEST_F(RPCSubscribeHandlerTest, BooksBothSnapshotSet)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "books": 
            [
                {{
                    "taker_gets": 
                    {{
                        "currency": "XRP"
                    }},
                    "taker_pays": 
                    {{
                        "currency": "USD",
                        "issuer": "{}"
                    }},
                    "both": true,
                    "snapshot": true
                }}
            ]
        }})",
        kACCOUNT
    ));
    backend_->setRange(kMINSEQ, kMAXSEQ);

    auto const issuer = getAccountIdWithString(kACCOUNT);

    auto const getsXRPPaysUSDBook = getBookBase(std::get<ripple::Book>(
        rpc::parseBook(ripple::to_currency("USD"), issuer, ripple::xrpCurrency(), ripple::xrpAccount())
    ));

    auto const reversedBook = getBookBase(std::get<ripple::Book>(
        rpc::parseBook(ripple::xrpCurrency(), ripple::xrpAccount(), ripple::to_currency("USD"), issuer)
    ));

    ON_CALL(*backend_, doFetchSuccessorKey(getsXRPPaysUSDBook, kMAXSEQ, _))
        .WillByDefault(Return(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}));

    ON_CALL(*backend_, doFetchSuccessorKey(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(std::nullopt));

    ON_CALL(*backend_, doFetchSuccessorKey(reversedBook, kMAXSEQ, _))
        .WillByDefault(Return(ripple::uint256{kPAY_S20_XRPGET_S10_USDBOOKDIR}));

    EXPECT_CALL(*backend_, doFetchSuccessorKey).Times(4);

    // 2 book dirs + 2 issuer global freeze + 2 transferRate + 1 owner root + 1 fee
    EXPECT_CALL(*backend_, doFetchLedgerObject).Times(8);

    auto const indexes = std::vector<ripple::uint256>(10, ripple::uint256{kINDE_X2});
    ON_CALL(*backend_, doFetchLedgerObject(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(createOwnerDirLedgerObject(indexes, kINDE_X1).getSerializer().peekData()));

    // for reverse
    auto const indexes2 = std::vector<ripple::uint256>(10, ripple::uint256{kINDE_X1});
    ON_CALL(*backend_, doFetchLedgerObject(ripple::uint256{kPAY_S20_XRPGET_S10_USDBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(createOwnerDirLedgerObject(indexes2, kINDE_X2).getSerializer().peekData()));

    // offer owner account root
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::account(getAccountIdWithString(kACCOUN_T2)).key, kMAXSEQ, _))
        .WillByDefault(Return(createAccountRootObject(kACCOUN_T2, 0, 2, 200, 2, kINDE_X1, 2).getSerializer().peekData())
        );

    // issuer account root
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::account(getAccountIdWithString(kACCOUNT)).key, kMAXSEQ, _))
        .WillByDefault(Return(createAccountRootObject(kACCOUNT, 0, 2, 200, 2, kINDE_X1, 2).getSerializer().peekData()));

    // fee
    auto feeBlob = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::fees().key, kMAXSEQ, _)).WillByDefault(Return(feeBlob));

    auto const gets10XRPPays20USDOffer = createOfferLedgerObject(
        kACCOUN_T2,
        10,
        20,
        ripple::to_string(ripple::xrpCurrency()),
        ripple::to_string(ripple::to_currency("USD")),
        toBase58(ripple::xrpAccount()),
        kACCOUNT,
        kPAY_S20_USDGET_S10_XRPBOOKDIR
    );

    // for reverse
    // offer owner is USD issuer
    auto const gets10USDPays20XRPOffer = createOfferLedgerObject(
        kACCOUNT,
        10,
        20,
        ripple::to_string(ripple::to_currency("USD")),
        ripple::to_string(ripple::xrpCurrency()),
        kACCOUNT,
        toBase58(ripple::xrpAccount()),
        kPAY_S20_XRPGET_S10_USDBOOKDIR
    );

    std::vector<Blob> const bbs(10, gets10XRPPays20USDOffer.getSerializer().peekData());
    ON_CALL(*backend_, doFetchLedgerObjects(indexes, kMAXSEQ, _)).WillByDefault(Return(bbs));

    // for reverse
    std::vector<Blob> const bbs2(10, gets10USDPays20XRPOffer.getSerializer().peekData());
    ON_CALL(*backend_, doFetchLedgerObjects(indexes2, kMAXSEQ, _)).WillByDefault(Return(bbs2));

    EXPECT_CALL(*backend_, doFetchLedgerObjects).Times(2);

    static auto const kEXPECTED_OFFER = fmt::format(
        R"({{
            "Account":"{}",
            "BookDirectory":"{}",
            "BookNode":"0",
            "Flags":0,
            "LedgerEntryType":"Offer",
            "OwnerNode":"0",
            "PreviousTxnID":"0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq":0,
            "Sequence":0,
            "TakerGets":"10",
            "TakerPays":
            {{
                "currency":"USD",
                "issuer":"{}",
                "value":"20"
            }},
            "index":"E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321",
            "owner_funds":"193",
            "quality":"2"
        }})",
        kACCOUN_T2,
        kPAY_S20_USDGET_S10_XRPBOOKDIR,
        kACCOUNT
    );
    static auto const kEXPECTED_REVERSED_OFFER = fmt::format(
        R"({{
            "Account":"{}",
            "BookDirectory":"{}",
            "BookNode":"0",
            "Flags":0,
            "LedgerEntryType":"Offer",
            "OwnerNode":"0",
            "PreviousTxnID":"0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq":0,
            "Sequence":0,
            "TakerGets":
            {{
                "currency":"USD",
                "issuer":"{}",
                "value":"10"
            }},
            "TakerPays":"20",
            "index":"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC",
            "owner_funds":"10",
            "quality":"2"
        }})",
        kACCOUNT,
        kPAY_S20_XRPGET_S10_USDBOOKDIR,
        kACCOUNT
    );
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subBook).Times(2);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result->as_object().at("bids").as_array().size(), 10);
        EXPECT_EQ(output.result->as_object().at("asks").as_array().size(), 10);
        EXPECT_EQ(output.result->as_object().at("bids").as_array()[0].as_object(), json::parse(kEXPECTED_OFFER));
        EXPECT_EQ(
            output.result->as_object().at("asks").as_array()[0].as_object(), json::parse(kEXPECTED_REVERSED_OFFER)
        );
    });
}

TEST_F(RPCSubscribeHandlerTest, BooksBothUnsetSnapshotSet)
{
    auto const input = json::parse(fmt::format(
        R"({{
            "books": 
            [
                {{
                    "taker_gets": 
                    {{
                        "currency": "XRP"
                    }},
                    "taker_pays": 
                    {{
                        "currency": "USD",
                        "issuer": "{}"
                    }},
                    "snapshot": true
                }}
            ]
        }})",
        kACCOUNT
    ));
    backend_->setRange(kMINSEQ, kMAXSEQ);

    auto const issuer = getAccountIdWithString(kACCOUNT);

    auto const getsXRPPaysUSDBook = getBookBase(std::get<ripple::Book>(
        rpc::parseBook(ripple::to_currency("USD"), issuer, ripple::xrpCurrency(), ripple::xrpAccount())
    ));

    auto const reversedBook = getBookBase(std::get<ripple::Book>(
        rpc::parseBook(ripple::xrpCurrency(), ripple::xrpAccount(), ripple::to_currency("USD"), issuer)
    ));

    ON_CALL(*backend_, doFetchSuccessorKey(getsXRPPaysUSDBook, kMAXSEQ, _))
        .WillByDefault(Return(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}));

    ON_CALL(*backend_, doFetchSuccessorKey(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(std::nullopt));

    ON_CALL(*backend_, doFetchSuccessorKey(reversedBook, kMAXSEQ, _))
        .WillByDefault(Return(ripple::uint256{kPAY_S20_XRPGET_S10_USDBOOKDIR}));

    EXPECT_CALL(*backend_, doFetchSuccessorKey).Times(2);

    EXPECT_CALL(*backend_, doFetchLedgerObject).Times(5);

    auto const indexes = std::vector<ripple::uint256>(10, ripple::uint256{kINDE_X2});
    ON_CALL(*backend_, doFetchLedgerObject(ripple::uint256{kPAY_S20_USDGET_S10_XRPBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(createOwnerDirLedgerObject(indexes, kINDE_X1).getSerializer().peekData()));

    // for reverse
    auto const indexes2 = std::vector<ripple::uint256>(10, ripple::uint256{kINDE_X1});
    ON_CALL(*backend_, doFetchLedgerObject(ripple::uint256{kPAY_S20_XRPGET_S10_USDBOOKDIR}, kMAXSEQ, _))
        .WillByDefault(Return(createOwnerDirLedgerObject(indexes2, kINDE_X2).getSerializer().peekData()));

    // offer owner account root
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::account(getAccountIdWithString(kACCOUN_T2)).key, kMAXSEQ, _))
        .WillByDefault(Return(createAccountRootObject(kACCOUN_T2, 0, 2, 200, 2, kINDE_X1, 2).getSerializer().peekData())
        );

    // issuer account root
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::account(getAccountIdWithString(kACCOUNT)).key, kMAXSEQ, _))
        .WillByDefault(Return(createAccountRootObject(kACCOUNT, 0, 2, 200, 2, kINDE_X1, 2).getSerializer().peekData()));

    // fee
    auto feeBlob = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    ON_CALL(*backend_, doFetchLedgerObject(ripple::keylet::fees().key, kMAXSEQ, _)).WillByDefault(Return(feeBlob));

    auto const gets10XRPPays20USDOffer = createOfferLedgerObject(
        kACCOUN_T2,
        10,
        20,
        ripple::to_string(ripple::xrpCurrency()),
        ripple::to_string(ripple::to_currency("USD")),
        toBase58(ripple::xrpAccount()),
        kACCOUNT,
        kPAY_S20_USDGET_S10_XRPBOOKDIR
    );

    // for reverse
    // offer owner is USD issuer
    auto const gets10USDPays20XRPOffer = createOfferLedgerObject(
        kACCOUNT,
        10,
        20,
        ripple::to_string(ripple::to_currency("USD")),
        ripple::to_string(ripple::xrpCurrency()),
        kACCOUNT,
        toBase58(ripple::xrpAccount()),
        kPAY_S20_XRPGET_S10_USDBOOKDIR
    );

    std::vector<Blob> const bbs(10, gets10XRPPays20USDOffer.getSerializer().peekData());
    ON_CALL(*backend_, doFetchLedgerObjects(indexes, kMAXSEQ, _)).WillByDefault(Return(bbs));

    // for reverse
    std::vector<Blob> const bbs2(10, gets10USDPays20XRPOffer.getSerializer().peekData());
    ON_CALL(*backend_, doFetchLedgerObjects(indexes2, kMAXSEQ, _)).WillByDefault(Return(bbs2));

    EXPECT_CALL(*backend_, doFetchLedgerObjects);

    static auto const kEXPECTED_OFFER = fmt::format(
        R"({{
            "Account":"{}",
            "BookDirectory":"{}",
            "BookNode":"0",
            "Flags":0,
            "LedgerEntryType":"Offer",
            "OwnerNode":"0",
            "PreviousTxnID":"0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq":0,
            "Sequence":0,
            "TakerGets":"10",
            "TakerPays":
            {{
                "currency":"USD",
                "issuer":"{}",
                "value":"20"
            }},
            "index":"E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321",
            "owner_funds":"193",
            "quality":"2"
        }})",
        kACCOUN_T2,
        kPAY_S20_USDGET_S10_XRPBOOKDIR,
        kACCOUNT
    );

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subBook);
        EXPECT_CALL(*mockSession, setApiSubversion(0));
        auto const output = handler.process(input, Context{yield, session});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result->as_object().at("offers").as_array().size(), 10);
        EXPECT_EQ(output.result->as_object().at("offers").as_array()[0].as_object(), json::parse(kEXPECTED_OFFER));
    });
}

TEST_F(RPCSubscribeHandlerTest, APIVersion)
{
    auto const input = json::parse(
        R"({
            "streams": ["transactions_proposed"]
        })"
    );
    auto const apiVersion = 2;
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{SubscribeHandler{backend_, mockSubscriptionManagerPtr}};
        EXPECT_CALL(*mockSubscriptionManagerPtr, subProposedTransactions);
        EXPECT_CALL(*mockSession, setApiSubversion(apiVersion));
        auto const output =
            handler.process(input, Context{.yield = yield, .session = session, .apiVersion = apiVersion});
        ASSERT_TRUE(output);
        // EXPECT_EQ(session_->apiSubVersion, apiVersion);
    });
}

TEST(RPCSubscribeHandlerSpecTest, DeprecatedFields)
{
    boost::json::value const json{
        {"streams", kACCOUNT},
        {"accounts", {123}},
        {"accounts_proposed", "abc"},
        {"books", "1"},
        {"user", "some"},
        {"password", "secret"},
        {"rt_accounts", true}
    };
    auto const spec = SubscribeHandler::spec(2);
    auto const warnings = spec.check(json);
    ASSERT_EQ(warnings.size(), 1);
    auto const& warning = warnings[0];
    ASSERT_TRUE(warning.is_object());
    auto const obj = warning.as_object();
    ASSERT_TRUE(obj.contains("id"));
    ASSERT_TRUE(obj.contains("message"));
    EXPECT_EQ(obj.at("id").as_int64(), static_cast<int64_t>(WarningCode::WarnRpcDeprecated));
    auto const& message = obj.at("message").as_string();
    for (auto const& field : {"user", "password", "rt_accounts"}) {
        EXPECT_NE(message.find(fmt::format("Field '{}' is deprecated", field)), std::string::npos) << message;
    }
}
