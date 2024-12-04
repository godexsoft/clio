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
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/handlers/BookChanges.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"

#include <boost/json/parse.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STObject.h>

#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

constexpr static auto kCURRENCY = "0158415500000000C1F76FF6ECB0BAC600000000";
constexpr static auto kISSUER = "rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD";
constexpr static auto kACCOUN_T1 = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr static auto kACCOUN_T2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr static auto kLEDGERHASH = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr static auto kMAXSEQ = 30;
constexpr static auto kMINSEQ = 10;

struct RPCBookChangesHandlerTest : HandlerBaseTest {
    RPCBookChangesHandlerTest()
    {
        backend_->setRange(kMINSEQ, kMAXSEQ);
    }
};

struct BookChangesParamTestCaseBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

// parameterized test cases for parameters check
struct BookChangesParameterTest : public RPCBookChangesHandlerTest,
                                  public WithParamInterface<BookChangesParamTestCaseBundle> {};

static auto
generateTestValuesForParametersTest()
{
    return std::vector<BookChangesParamTestCaseBundle>{
        BookChangesParamTestCaseBundle{
            "LedgerHashInvalid", R"({"ledger_hash":"1"})", "invalidParams", "ledger_hashMalformed"
        },
        BookChangesParamTestCaseBundle{
            "LedgerHashNotString", R"({"ledger_hash":1})", "invalidParams", "ledger_hashNotString"
        },
        BookChangesParamTestCaseBundle{
            "LedgerIndexInvalid", R"({"ledger_index":"a"})", "invalidParams", "ledgerIndexMalformed"
        },
    };
}

INSTANTIATE_TEST_CASE_P(
    RPCBookChangesGroup1,
    BookChangesParameterTest,
    ValuesIn(generateTestValuesForParametersTest()),
    tests::util::kNAME_GENERATOR
);

TEST_P(BookChangesParameterTest, InvalidParams)
{
    auto const testBundle = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{BookChangesHandler{backend_}};
        auto const req = json::parse(testBundle.testJson);
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), testBundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), testBundle.expectedErrorMessage);
    });
}

TEST_F(RPCBookChangesHandlerTest, LedgerNonExistViaIntSequence)
{
    EXPECT_CALL(*backend_, fetchLedgerBySequence).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend_, fetchLedgerBySequence(kMAXSEQ, _)).WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto static const kINPUT = json::parse(R"({"ledger_index":30})");
    auto const handler = AnyHandler{BookChangesHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(kINPUT, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCBookChangesHandlerTest, LedgerNonExistViaStringSequence)
{
    EXPECT_CALL(*backend_, fetchLedgerBySequence).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend_, fetchLedgerBySequence(kMAXSEQ, _)).WillByDefault(Return(std::nullopt));

    auto static const kINPUT = json::parse(R"({"ledger_index":"30"})");
    auto const handler = AnyHandler{BookChangesHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(kINPUT, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCBookChangesHandlerTest, LedgerNonExistViaHash)
{
    EXPECT_CALL(*backend_, fetchLedgerByHash).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend_, fetchLedgerByHash(ripple::uint256{kLEDGERHASH}, _))
        .WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto static const kINPUT = json::parse(fmt::format(
        R"({{
            "ledger_hash":"{}"
        }})",
        kLEDGERHASH
    ));
    auto const handler = AnyHandler{BookChangesHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(kINPUT, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCBookChangesHandlerTest, NormalPath)
{
    static auto constexpr kEXPECTED_OUT =
        R"({
            "type":"bookChanges",
            "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
            "ledger_index":30,
            "ledger_time":0,
            "validated":true,
            "changes":[
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

    EXPECT_CALL(*backend_, fetchLedgerBySequence).Times(1);
    ON_CALL(*backend_, fetchLedgerBySequence(kMAXSEQ, _))
        .WillByDefault(Return(createLedgerHeader(kLEDGERHASH, kMAXSEQ)));

    auto transactions = std::vector<TransactionAndMetadata>{};
    auto trans1 = TransactionAndMetadata();
    ripple::STObject const obj = createPaymentTransactionObject(kACCOUN_T1, kACCOUN_T2, 1, 1, 32);
    trans1.transaction = obj.getSerializer().peekData();
    trans1.ledgerSequence = 32;
    ripple::STObject const metaObj = createMetaDataForBookChange(kCURRENCY, kISSUER, 22, 1, 3, 3, 1);
    trans1.metadata = metaObj.getSerializer().peekData();
    transactions.push_back(trans1);

    EXPECT_CALL(*backend_, fetchAllTransactionsInLedger).Times(1);
    ON_CALL(*backend_, fetchAllTransactionsInLedger(kMAXSEQ, _)).WillByDefault(Return(transactions));

    auto const handler = AnyHandler{BookChangesHandler{backend_}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(json::parse("{}"), Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(kEXPECTED_OUT));
    });
}
