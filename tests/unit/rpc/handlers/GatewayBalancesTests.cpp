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
#include "rpc/handlers/GatewayBalances.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"

#include <boost/json/parse.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

namespace {

constexpr auto Account = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr auto Account2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr auto Account3 = "raHGBERMka3KZsfpTQUAtumxmvpqhFLyrk";
constexpr auto Issuer = "rK9DrarGKnVEo2nYp5MfVRXRYf5yRX3mwD";
constexpr auto LedgerHash = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto Index1 = "1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC";
constexpr auto Index2 = "E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321";
constexpr auto TxnID = "E3FE6EA3D48F0C2B639448020EA4F03D4F4F8FFDB243A852A0F59177921B4879";

}  // namespace

class RPCGatewayBalancesHandlerTest : public HandlerBaseTest {};

struct ParameterTestBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

struct ParameterTest : public RPCGatewayBalancesHandlerTest, public WithParamInterface<ParameterTestBundle> {};

TEST_P(ParameterTest, CheckError)
{
    auto bundle = GetParam();
    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(json::parse(bundle.testJson), Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), bundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), bundle.expectedErrorMessage);
    });
}

auto
generateParameterTestBundles()
{
    return std::vector<ParameterTestBundle>{
        ParameterTestBundle{
            "AccountNotString",
            R"({
                "account": 1213
            })",
            "invalidParams",
            "accountNotString"
        },
        ParameterTestBundle{
            "AccountMissing",
            R"({
            })",
            "invalidParams",
            "Required field 'account' missing"
        },
        ParameterTestBundle{
            "AccountInvalid",
            R"({
                "account": "1213"
            })",
            "actMalformed",
            "accountMalformed"
        },
        ParameterTestBundle{
            "LedgerIndexInvalid",
            fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_index": "meh"
                }})",
                Account
            ),
            "invalidParams",
            "ledgerIndexMalformed"
        },
        ParameterTestBundle{
            "LedgerHashInvalid",
            fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_hash": "meh"
                }})",
                Account
            ),
            "invalidParams",
            "ledger_hashMalformed"
        },
        ParameterTestBundle{
            "LedgerHashNotString",
            fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_hash": 12
                }})",
                Account
            ),
            "invalidParams",
            "ledger_hashNotString"
        },
        ParameterTestBundle{
            "WalletsNotStringOrArray",
            fmt::format(
                R"({{
                    "account": "{}",
                    "hotwallet": 12
                }})",
                Account
            ),
            "invalidParams",
            "hotwalletNotStringOrArray"
        },
        ParameterTestBundle{
            "WalletsNotStringAccount",
            fmt::format(
                R"({{
                    "account": "{}",
                    "hotwallet": [12]
                }})",
                Account
            ),
            "invalidParams",
            "hotwalletMalformed"
        },
        ParameterTestBundle{
            "WalletsInvalidAccount",
            fmt::format(
                R"({{
                    "account": "{}",
                    "hotwallet": ["12"]
                }})",
                Account
            ),
            "invalidParams",
            "hotwalletMalformed"
        },
        ParameterTestBundle{
            "WalletInvalidAccount",
            fmt::format(
                R"({{
                    "account": "{}",
                    "hotwallet": "12"
                }})",
                Account
            ),
            "invalidParams",
            "hotwalletMalformed"
        },
    };
}

INSTANTIATE_TEST_SUITE_P(
    RPCGatewayBalancesHandler,
    ParameterTest,
    testing::ValuesIn(generateParameterTestBundles()),
    tests::util::NameGenerator
);

TEST_F(RPCGatewayBalancesHandlerTest, LedgerNotFoundViaStringIndex)
{
    auto const seq = 123;

    backend->setRange(10, 300);
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend, fetchLedgerBySequence(seq, _)).WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_index": "{}"
                }})",
                Account,
                seq
            )),
            Context{yield}
        );
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCGatewayBalancesHandlerTest, LedgerNotFoundViaIntIndex)
{
    auto const seq = 123;

    backend->setRange(10, 300);
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend, fetchLedgerBySequence(seq, _)).WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_index": {}
                }})",
                Account,
                seq
            )),
            Context{yield}
        );
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCGatewayBalancesHandlerTest, LedgerNotFoundViaHash)
{
    backend->setRange(10, 300);
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    // return empty ledgerHeader
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _))
        .WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}",
                    "ledger_hash": "{}"
                }})",
                Account,
                LedgerHash
            )),
            Context{yield}
        );
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCGatewayBalancesHandlerTest, AccountNotFound)
{
    auto const seq = 300;

    backend->setRange(10, seq);
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, seq);
    ON_CALL(*backend, fetchLedgerBySequence(seq, _)).WillByDefault(Return(ledgerHeader));

    // return empty account
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, seq, _)).WillByDefault(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObject).Times(1);

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}"
                }})",
                Account
            )),
            Context{yield}
        );
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "accountNotFound");
    });
}

TEST_F(RPCGatewayBalancesHandlerTest, InvalidHotWallet)
{
    auto const seq = 300;

    backend->setRange(10, seq);
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, seq);
    ON_CALL(*backend, fetchLedgerBySequence(seq, _)).WillByDefault(Return(ledgerHeader));

    // return valid account
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, seq, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    // return valid owner dir
    auto const ownerDir = createOwnerDirLedgerObject({ripple::uint256{Index2}}, Index1);
    auto const ownerDirKk = ripple::keylet::ownerDir(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(ownerDirKk, seq, _))
        .WillByDefault(Return(ownerDir.getSerializer().peekData()));
    EXPECT_CALL(*backend, doFetchLedgerObject).Times(2);

    // create a valid line, balance is 0
    auto const line1 = createRippleStateLedgerObject("USD", Issuer, 0, Account, 10, Account2, 20, TxnID, 123);
    std::vector<Blob> bbs;
    bbs.push_back(line1.getSerializer().peekData());
    ON_CALL(*backend, doFetchLedgerObjects).WillByDefault(Return(bbs));
    EXPECT_CALL(*backend, doFetchLedgerObjects).Times(1);

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}",
                    "hotwallet": "{}"
                }})",
                Account,
                Account2
            )),
            Context{yield}
        );
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidHotWallet");
        EXPECT_EQ(err.at("error_message").as_string(), "Invalid hot wallet.");
    });
}

struct NormalTestBundle {
    std::string testName;
    ripple::STObject mockedDir;
    std::vector<ripple::STObject> mockedObjects;
    std::string expectedJson;
    std::string hotwallet;
};

struct NormalPathTest : public RPCGatewayBalancesHandlerTest, public WithParamInterface<NormalTestBundle> {};

TEST_P(NormalPathTest, CheckOutput)
{
    auto const& bundle = GetParam();
    auto const seq = 300;

    backend->setRange(10, seq);
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, seq);
    ON_CALL(*backend, fetchLedgerBySequence(seq, _)).WillByDefault(Return(ledgerHeader));

    // return valid account
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, seq, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    // return valid owner dir
    auto const ownerDir = createOwnerDirLedgerObject({ripple::uint256{Index2}}, Index1);
    auto const ownerDirKk = ripple::keylet::ownerDir(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(ownerDirKk, seq, _))
        .WillByDefault(Return(bundle.mockedDir.getSerializer().peekData()));
    EXPECT_CALL(*backend, doFetchLedgerObject).Times(2);

    std::vector<Blob> bbs;
    std::transform(
        bundle.mockedObjects.begin(),
        bundle.mockedObjects.end(),
        std::back_inserter(bbs),
        [](auto const& obj) { return obj.getSerializer().peekData(); }
    );
    ON_CALL(*backend, doFetchLedgerObjects).WillByDefault(Return(bbs));
    EXPECT_CALL(*backend, doFetchLedgerObjects).Times(1);

    auto const handler = AnyHandler{GatewayBalancesHandler{backend}};
    runSpawn([&](auto yield) {
        auto const output = handler.process(
            json::parse(fmt::format(
                R"({{
                    "account": "{}",
                    {}
                }})",
                Account,
                bundle.hotwallet
            )),
            Context{yield}
        );
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value(), json::parse(bundle.expectedJson));
    });
}

auto
generateNormalPathTestBundles()
{
    auto frozenState = createRippleStateLedgerObject("JPY", Issuer, -50, Account, 10, Account3, 20, TxnID, 123);
    frozenState.setFieldU32(ripple::sfFlags, ripple::lsfLowFreeze);

    auto overflowState = createRippleStateLedgerObject("JPY", Issuer, 50, Account, 10, Account3, 20, TxnID, 123);
    int64_t const min64 = -9922966390934554;
    overflowState.setFieldAmount(ripple::sfBalance, ripple::STAmount(getIssue("JPY", Issuer), min64, 80));
    return std::vector<NormalTestBundle>{
        NormalTestBundle{
            "AllBranches",
            createOwnerDirLedgerObject(
                {ripple::uint256{Index2},
                 ripple::uint256{Index2},
                 ripple::uint256{Index2},
                 ripple::uint256{Index2},
                 ripple::uint256{Index2},
                 ripple::uint256{Index2}},
                Index1
            ),
            std::vector{// hotwallet
                        createRippleStateLedgerObject("USD", Issuer, -10, Account, 100, Account2, 200, TxnID, 123),
                        // hotwallet
                        createRippleStateLedgerObject("CNY", Issuer, -20, Account, 100, Account2, 200, TxnID, 123),
                        // positive balance -> asset
                        createRippleStateLedgerObject("EUR", Issuer, 30, Account, 100, Account3, 200, TxnID, 123),
                        // positive balance -> asset
                        createRippleStateLedgerObject("JPY", Issuer, 40, Account, 100, Account3, 200, TxnID, 123),
                        // obligation
                        createRippleStateLedgerObject("JPY", Issuer, -50, Account, 10, Account3, 20, TxnID, 123),
                        frozenState

            },
            fmt::format(
                R"({{
                    "obligations":{{
                        "JPY":"50"
                    }},
                    "balances":{{
                        "{}":[
                            {{
                                "currency":"USD",
                                "value":"10"
                            }},
                            {{
                                "currency":"CNY",
                                "value":"20"
                            }}
                        ]
                    }},
                    "frozen_balances":{{
                        "{}":[
                            {{
                                "currency":"JPY",
                                "value":"50"
                            }}
                        ]
                    }},
                    "assets":{{
                        "{}":[
                            {{
                                "currency":"EUR",
                                "value":"30"
                            }},
                            {{
                                "currency":"JPY",
                                "value":"40"
                            }}
                        ]
                    }},
                    "account":"{}",
                    "ledger_index":300,
                    "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652"
                }})",
                Account2,
                Account3,
                Account3,
                Account
            ),
            fmt::format(R"("hotwallet": "{}")", Account2)
        },
        NormalTestBundle{
            "NoHotwallet",
            createOwnerDirLedgerObject({ripple::uint256{Index2}}, Index1),
            std::vector{createRippleStateLedgerObject("JPY", Issuer, -50, Account, 10, Account3, 20, TxnID, 123)},
            fmt::format(
                R"({{
                    "obligations":{{
                        "JPY":"50"
                    }},
                    "account":"{}",
                    "ledger_index":300,
                    "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652"
                }})",
                Account
            ),
            R"("ledger_index" : "validated")"
        },
        NormalTestBundle{
            "ObligationOverflow",
            createOwnerDirLedgerObject({ripple::uint256{Index2}, ripple::uint256{Index2}}, Index1),
            std::vector{overflowState, overflowState},
            fmt::format(
                R"({{
                    "obligations":{{
                        "JPY":"9999999999999999e80"
                    }},
                    "account":"{}",
                    "ledger_index":300,
                    "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652"
                }})",
                Account
            ),
            R"("ledger_index" : "validated")"
        },
        NormalTestBundle{
            "HighID",
            createOwnerDirLedgerObject(
                {ripple::uint256{Index2}, ripple::uint256{Index2}, ripple::uint256{Index2}, ripple::uint256{Index2}},
                Index1
            ),
            std::vector{// hotwallet
                        createRippleStateLedgerObject("USD", Issuer, 10, Account2, 100, Account, 200, TxnID, 123),
                        // hotwallet
                        createRippleStateLedgerObject("CNY", Issuer, 20, Account2, 100, Account, 200, TxnID, 123),
                        createRippleStateLedgerObject("EUR", Issuer, 30, Account3, 100, Account, 200, TxnID, 123),
                        createRippleStateLedgerObject("JPY", Issuer, -50, Account3, 10, Account, 20, TxnID, 123)
            },
            fmt::format(
                R"({{
                    "obligations":{{
                        "EUR":"30"
                    }},
                    "balances":{{
                        "{}":[
                            {{
                                "currency":"USD",
                                "value":"10"
                            }},
                            {{
                                "currency":"CNY",
                                "value":"20"
                            }}
                        ]
                    }},
                    "assets":{{
                        "{}":[
                            {{
                                "currency":"JPY",
                                "value":"50"
                            }}
                        ]
                    }},
                    "account":"{}",
                    "ledger_index":300,
                    "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652"
                }})",
                Account2,
                Account3,
                Account
            ),
            fmt::format(R"("hotwallet": "{}")", Account2)
        },
        NormalTestBundle{
            "HotWalletArray",
            createOwnerDirLedgerObject(
                {ripple::uint256{Index2}, ripple::uint256{Index2}, ripple::uint256{Index2}}, Index1
            ),
            std::vector{
                createRippleStateLedgerObject("USD", Issuer, -10, Account, 100, Account2, 200, TxnID, 123),
                createRippleStateLedgerObject("CNY", Issuer, -20, Account, 100, Account2, 200, TxnID, 123),
                createRippleStateLedgerObject("EUR", Issuer, -30, Account, 100, Account3, 200, TxnID, 123)

            },
            fmt::format(
                R"({{
                    "balances":{{
                        "{}":[
                            {{
                                "currency":"EUR",
                                "value":"30"
                            }}
                        ],
                        "{}":[
                            {{
                                "currency":"USD",
                                "value":"10"
                            }},
                            {{
                                "currency":"CNY",
                                "value":"20"
                            }}
                        ]
                    }},
                    "account":"{}",
                    "ledger_index":300,
                    "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652"
                }})",
                Account3,
                Account2,
                Account
            ),
            fmt::format(R"("hotwallet": ["{}", "{}"])", Account2, Account3)
        },
    };
}

INSTANTIATE_TEST_SUITE_P(
    RPCGatewayBalancesHandler,
    NormalPathTest,
    testing::ValuesIn(generateNormalPathTestBundles()),
    tests::util::NameGenerator
);
