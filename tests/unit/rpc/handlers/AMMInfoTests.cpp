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
#include "rpc/Errors.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/handlers/AMMInfo.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"

#include <boost/json/parse.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

namespace {

constexpr auto Seq = 30;
constexpr auto WrongAmmAccount = "000S7XL6nxRAi7JcbJcn1Na179oF300000";
constexpr auto AmmAccount = "rLcS7XL6nxRAi7JcbJcn1Na179oF3vdfbh";
constexpr auto AmmAccount2 = "rnW8FAPgpQgA6VoESnVrUVJHBdq9QAtRZs";
constexpr auto LpIssueCurrency = "03930D02208264E2E40EC1B0C09E4DB96EE197B1";
constexpr auto NotfoundAccount = "rBdLS7RVLqkPwnWQCT2bC6HJd6xGoBizq8";
constexpr auto AmmID = 54321;
constexpr auto LedgerHash = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto Index1 = "1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC";
constexpr auto Index2 = "E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321";

}  // namespace

class RPCAMMInfoHandlerTest : public HandlerBaseTest {};

struct AMMInfoParamTestCaseBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

struct AMMInfoParameterTest : public RPCAMMInfoHandlerTest, public WithParamInterface<AMMInfoParamTestCaseBundle> {};

static auto
generateTestValuesForParametersTest()
{
    return std::vector<AMMInfoParamTestCaseBundle>{
        AMMInfoParamTestCaseBundle{"MissingAMMAccountOrAssets", "{}", "invalidParams", "Invalid parameters."},
        AMMInfoParamTestCaseBundle{
            "AMMAccountNotString", R"({"amm_account": 1})", "actMalformed", "Account malformed."
        },
        AMMInfoParamTestCaseBundle{"AccountNotString", R"({"account": 1})", "actMalformed", "Account malformed."},
        AMMInfoParamTestCaseBundle{
            "AMMAccountInvalid", R"({"amm_account": "xxx"})", "actMalformed", "Account malformed."
        },
        AMMInfoParamTestCaseBundle{"AccountInvalid", R"({"account": "xxx"})", "actMalformed", "Account malformed."},
        AMMInfoParamTestCaseBundle{
            "AMMAssetNotStringOrObject", R"({"asset": 1})", "issueMalformed", "Issue is malformed."
        },
        AMMInfoParamTestCaseBundle{"AMMAssetEmptyObject", R"({"asset": {}})", "issueMalformed", "Issue is malformed."},
        AMMInfoParamTestCaseBundle{
            "AMMAsset2NotStringOrObject", R"({"asset2": 1})", "issueMalformed", "Issue is malformed."
        },
        AMMInfoParamTestCaseBundle{
            "AMMAsset2EmptyObject", R"({"asset2": {}})", "issueMalformed", "Issue is malformed."
        },
    };
}

INSTANTIATE_TEST_CASE_P(
    RPCAMMInfoGroup1,
    AMMInfoParameterTest,
    ValuesIn(generateTestValuesForParametersTest()),
    tests::util::NameGenerator
);

TEST_P(AMMInfoParameterTest, InvalidParams)
{
    auto const testBundle = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{AMMInfoHandler{backend}};
        auto const req = json::parse(testBundle.testJson);
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), testBundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), testBundle.expectedErrorMessage);
    });
}

TEST_F(RPCAMMInfoHandlerTest, AccountNotFound)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    auto const missingAccountKey = getAccountKey(NotfoundAccount);
    auto const accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto const accountKey = getAccountKey(AmmAccount);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(missingAccountKey, testing::_, testing::_))
        .WillByDefault(Return(std::optional<Blob>{}));
    ON_CALL(*backend, doFetchLedgerObject(accountKey, testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}",
            "account": "{}"
        }})",
        AmmAccount,
        NotfoundAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "Account not found.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, AMMAccountNotExist)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject).WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        WrongAmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actMalformed");
        EXPECT_EQ(err.at("error_message").as_string(), "Account malformed.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, AMMAccountNotInDBIsMalformed)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject).WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actMalformed");
        EXPECT_EQ(err.at("error_message").as_string(), "Account malformed.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, AMMAccountNotFoundMissingAmmField)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    auto const accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject).WillByDefault(Return(accountRoot.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "Account not found.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, AMMAccountAmmBlobNotFound)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    auto const accountKey = getAccountKey(AmmAccount);
    auto const ammId = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammId);

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj = createAmmObject(AmmAccount2, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2);
    accountRoot.setFieldH256(ripple::sfAMMID, ripple::uint256{AmmID});

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(accountKey, testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "Account not found.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, AMMAccountAccBlobNotFound)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, 30);
    auto const accountKey = getAccountKey(AmmAccount);
    auto const account2Key = getAccountKey(AmmAccount2);
    auto const ammId = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammId);

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto const ammObj = createAmmObject(AmmAccount2, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2);
    accountRoot.setFieldH256(ripple::sfAMMID, ammId);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(accountKey, testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(account2Key, testing::_, testing::_))
        .WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "Account not found.");
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathMinimalFirstXRPNoTrustline)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj =
        createAmmObject(AmmAccount, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2, LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _)).WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": "193",
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithAccount)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account2, account1, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const account2Root = createAccountRootObject(AmmAccount2, 0, 2, 300, 2, Index1, 2);
    auto const ammObj =
        createAmmObject(AmmAccount2, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount, LpIssueCurrency);
    auto const lptCurrency = createLptCurrency("XRP", "JPY");
    auto const accountHoldsKeylet = ripple::keylet::line(account2, account2, lptCurrency);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    auto const trustline =
        createRippleStateLedgerObject(LpIssueCurrency, AmmAccount, 12, AmmAccount2, 1000, AmmAccount, 2000, Index1, 2);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(account2Root.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _)).WillByDefault(Return(std::optional<Blob>{}));
    ON_CALL(*backend, doFetchLedgerObject(accountHoldsKeylet.key, Seq, _))
        .WillByDefault(Return(trustline.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}",
            "account": "{}"
        }})",
        AmmAccount,
        AmmAccount2
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto const expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "12"
                    }},
                    "amount": "293",
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount2,
            "JPY",
            AmmAccount,
            AmmAccount2,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathMinimalSecondXRPNoTrustline)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj =
        createAmmObject(AmmAccount, "JPY", AmmAccount2, "XRP", ripple::toBase58(ripple::xrpAccount()), LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _)).WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto const expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "amount2": "193",
                    "account": "{}",
                    "trading_fee": 5,
                    "asset_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathNonXRPNoTrustlines)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj = createAmmObject(AmmAccount, "USD", AmmAccount, "JPY", AmmAccount2, LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _)).WillByDefault(Return(std::optional<Blob>{}));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto const expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset_frozen": false,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "USD",
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathFrozen)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue1LineKey = ripple::keylet::line(account1, account1, ripple::to_currency("USD")).key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj = createAmmObject(AmmAccount, "USD", AmmAccount, "JPY", AmmAccount2, LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);

    // note: frozen flag will not be used for trustline1 because issuer == account
    auto const trustline1BalanceFrozen = createRippleStateLedgerObject(
        "USD", AmmAccount, 8, AmmAccount, 1000, AmmAccount2, 2000, Index1, 2, ripple::lsfGlobalFreeze
    );
    auto const trustline2BalanceFrozen = createRippleStateLedgerObject(
        "JPY", AmmAccount, 12, AmmAccount2, 1000, AmmAccount, 2000, Index1, 2, ripple::lsfGlobalFreeze
    );

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue1LineKey, Seq, _))
        .WillByDefault(Return(trustline1BalanceFrozen.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _))
        .WillByDefault(Return(trustline2BalanceFrozen.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto const expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "8"
                    }},
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "-12"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset_frozen": false,
                    "asset2_frozen": true
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "USD",
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathFrozenIssuer)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue1LineKey = ripple::keylet::line(account1, account1, ripple::to_currency("USD")).key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    // asset1 will be frozen because flag set here
    auto accountRoot = createAccountRootObject(AmmAccount, ripple::lsfGlobalFreeze, 2, 200, 2, Index1, 2);
    auto ammObj = createAmmObject(AmmAccount, "USD", AmmAccount, "JPY", AmmAccount2, LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);

    // note: frozen flag will not be used for trustline1 because issuer == account
    auto const trustline1BalanceFrozen = createRippleStateLedgerObject(
        "USD", AmmAccount, 8, AmmAccount, 1000, AmmAccount2, 2000, Index1, 2, ripple::lsfGlobalFreeze
    );
    auto const trustline2BalanceFrozen = createRippleStateLedgerObject(
        "JPY", AmmAccount, 12, AmmAccount2, 1000, AmmAccount, 2000, Index1, 2, ripple::lsfGlobalFreeze
    );

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue1LineKey, Seq, _))
        .WillByDefault(Return(trustline1BalanceFrozen.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _))
        .WillByDefault(Return(trustline2BalanceFrozen.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto const expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "8"
                    }},
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "-12"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset_frozen": true,
                    "asset2_frozen": true
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "USD",
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithTrustline)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj =
        createAmmObject(AmmAccount, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2, LpIssueCurrency);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    auto const trustlineBalance =
        createRippleStateLedgerObject("JPY", AmmAccount2, -8, AmmAccount, 1000, AmmAccount2, 2000, Index2, 2, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _))
        .WillByDefault(Return(trustlineBalance.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": "193",
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "8"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithVoteSlots)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj =
        createAmmObject(AmmAccount, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2, LpIssueCurrency);
    ammAddVoteSlot(ammObj, account1, 2, 4);
    ammAddVoteSlot(ammObj, account2, 4, 2);
    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    auto const trustlineBalance =
        createRippleStateLedgerObject("JPY", AmmAccount2, -8, AmmAccount, 1000, AmmAccount2, 2000, Index2, 2, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _))
        .WillByDefault(Return(trustlineBalance.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": "193",
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "8"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "vote_slots": [
                        {{
                            "account": "{}",
                            "trading_fee": 2,
                            "vote_weight": 4
                        }},
                        {{
                            "account": "{}",
                            "trading_fee": 4,
                            "vote_weight": 2
                        }}
                    ],
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            AmmAccount,
            AmmAccount2,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithAuctionSlot)
{
    backend->setRange(10, 30);

    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const ammKey = ripple::uint256{AmmID};
    auto const ammKeylet = ripple::keylet::amm(ammKey);
    auto const feesKey = ripple::keylet::fees().key;
    auto const issue2LineKey = ripple::keylet::line(account1, account2, ripple::to_currency("JPY")).key;

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj =
        createAmmObject(AmmAccount, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", AmmAccount2, LpIssueCurrency);
    ammSetAuctionSlot(
        ammObj, account2, ripple::amountFromString(ripple::xrpIssue(), "100"), 2, 25 * 3600, {account1, account2}
    );

    accountRoot.setFieldH256(ripple::sfAMMID, ammKey);
    auto const feesObj = createLegacyFeeSettingBlob(1, 2, 3, 4, 0);
    auto const trustlineBalance =
        createRippleStateLedgerObject("JPY", AmmAccount2, -8, AmmAccount, 1000, AmmAccount2, 2000, Index2, 2, 0);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(feesKey, Seq, _)).WillByDefault(Return(feesObj));
    ON_CALL(*backend, doFetchLedgerObject(issue2LineKey, Seq, _))
        .WillByDefault(Return(trustlineBalance.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "amm_account": "{}"
        }})",
        AmmAccount
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": "193",
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "8"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "auction_slot": {{
                        "time_interval": 20,
                        "price": "100",
                        "discounted_fee": 2,
                        "account": "{}",
                        "expiration": "2000-01-02T01:00:00+0000",
                        "auth_accounts": [
                            {{
                                "account": "{}"
                            }},
                            {{
                                "account": "{}"
                            }}
                        ]
                    }},
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            AmmAccount2,
            AmmAccount,
            AmmAccount2,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithAssetsMatchingInputOrder)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const issue1 = ripple::Issue(ripple::to_currency("JPY"), account1);
    auto const issue2 = ripple::Issue(ripple::to_currency("USD"), account2);
    auto const ammKeylet = ripple::keylet::amm(issue1, issue2);

    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto ammObj = createAmmObject(AmmAccount, "JPY", AmmAccount, "USD", AmmAccount2, LpIssueCurrency);
    auto const auctionIssue = ripple::Issue{ripple::Currency{LpIssueCurrency}, account1};
    ammSetAuctionSlot(
        ammObj, account2, ripple::amountFromString(auctionIssue, "100"), 2, 25 * 3600, {account1, account2}
    );
    accountRoot.setFieldH256(ripple::sfAMMID, ammKeylet.key);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "asset": {{
                "currency": "JPY", 
                "issuer": "{}"
            }},
            "asset2": {{
                "currency": "USD",
                "issuer": "{}"
            }}
        }})",
        AmmAccount,
        AmmAccount2
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "auction_slot": {{
                        "time_interval": 20,
                        "price": {{
                            "currency": "{}",
                            "issuer": "{}",
                            "value": "100"
                        }},
                        "discounted_fee": 2,
                        "account": "{}",
                        "expiration": "2000-01-02T01:00:00+0000",
                        "auth_accounts": [
                            {{
                                "account": "{}"
                            }},
                            {{
                                "account": "{}"
                            }}
                        ]
                    }},
                    "asset_frozen": false,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "JPY",
            AmmAccount,
            "USD",
            AmmAccount2,
            AmmAccount,
            LpIssueCurrency,
            AmmAccount,
            AmmAccount2,
            AmmAccount,
            AmmAccount2,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}

TEST_F(RPCAMMInfoHandlerTest, HappyPathWithAssetsPreservesInputOrder)
{
    backend->setRange(10, 30);

    auto const lgrInfo = createLedgerHeader(LedgerHash, Seq);
    auto const account1 = getAccountIdWithString(AmmAccount);
    auto const account2 = getAccountIdWithString(AmmAccount2);
    auto const issue1 = ripple::Issue(ripple::to_currency("USD"), account1);
    auto const issue2 = ripple::Issue(ripple::to_currency("JPY"), account2);
    auto const ammKeylet = ripple::keylet::amm(issue1, issue2);

    // Note: order in the AMM object is different from the input
    auto ammObj = createAmmObject(AmmAccount, "JPY", AmmAccount, "USD", AmmAccount2, LpIssueCurrency);
    auto accountRoot = createAccountRootObject(AmmAccount, 0, 2, 200, 2, Index1, 2);
    auto const auctionIssue = ripple::Issue{ripple::Currency{LpIssueCurrency}, account1};
    ammSetAuctionSlot(
        ammObj, account2, ripple::amountFromString(auctionIssue, "100"), 2, 25 * 3600, {account1, account2}
    );
    accountRoot.setFieldH256(ripple::sfAMMID, ammKeylet.key);

    ON_CALL(*backend, fetchLedgerBySequence).WillByDefault(Return(lgrInfo));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account1), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(getAccountKey(account2), testing::_, testing::_))
        .WillByDefault(Return(accountRoot.getSerializer().peekData()));
    ON_CALL(*backend, doFetchLedgerObject(ammKeylet.key, testing::_, testing::_))
        .WillByDefault(Return(ammObj.getSerializer().peekData()));

    auto static const Input = json::parse(fmt::format(
        R"({{
            "asset": {{
                "currency": "USD",
                "issuer": "{}"
            }},
            "asset2": {{
                "currency": "JPY", 
                "issuer": "{}"
            }}
        }})",
        AmmAccount,
        AmmAccount2
    ));

    auto const handler = AnyHandler{AMMInfoHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(Input, Context{yield});
        auto expectedResult = json::parse(fmt::format(
            R"({{
                "amm": {{
                    "lp_token": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "100"
                    }},
                    "amount": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "amount2": {{
                        "currency": "{}",
                        "issuer": "{}",
                        "value": "0"
                    }},
                    "account": "{}",
                    "trading_fee": 5,
                    "auction_slot": {{
                        "time_interval": 20,
                        "price": {{
                            "currency": "{}",
                            "issuer": "{}",
                            "value": "100"
                        }},
                        "discounted_fee": 2,
                        "account": "{}",
                        "expiration": "2000-01-02T01:00:00+0000",
                        "auth_accounts": [
                            {{
                                "account": "{}"
                            }},
                            {{
                                "account": "{}"
                            }}
                        ]
                    }},
                    "asset_frozen": false,
                    "asset2_frozen": false
                }},
                "ledger_index": 30,
                "ledger_hash": "{}",
                "validated": true
            }})",
            LpIssueCurrency,
            AmmAccount,
            "USD",
            AmmAccount,
            "JPY",
            AmmAccount2,
            AmmAccount,
            LpIssueCurrency,
            AmmAccount,
            AmmAccount2,
            AmmAccount,
            AmmAccount2,
            LedgerHash
        ));

        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), expectedResult);
    });
}
