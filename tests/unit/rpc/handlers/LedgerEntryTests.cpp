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
#include "rpc/CredentialHelpers.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/handlers/LedgerEntry.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/NameGenerator.hpp"
#include "util/TestObject.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

namespace {

constexpr auto Index1 = "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD";
constexpr auto Account = "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn";
constexpr auto Account2 = "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun";
constexpr auto Account3 = "rhzcyub9SbyZ4YF1JYskN5rLrTDUuLZG6D";
constexpr auto RangeMin = 10;
constexpr auto RangeMax = 30;
constexpr auto LedgerHash = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto TokenID = "000827103B94ECBB7BF0A0A6ED62B3607801A27B65F4679F4AD1D4850000C0EA";
constexpr auto NftID = "00010000A7CAD27B688D14BA1A9FA5366554D6ADCF9CE0875B974D9F00000004";
constexpr auto TxnID = "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD";
constexpr auto CredentialType = "4B5943";

}  // namespace

class RPCLedgerEntryTest : public HandlerBaseTest {};

struct ParamTestCaseBundle {
    std::string testName;
    std::string testJson;
    std::string expectedError;
    std::string expectedErrorMessage;
};

// parameterized test cases for parameters check
struct LedgerEntryParameterTest : public RPCLedgerEntryTest, public WithParamInterface<ParamTestCaseBundle> {};

// TODO: because we extract the error generation from the handler to framework
// the error messages need one round fine tuning
static auto
generateTestValuesForParametersTest()
{
    return std::vector<ParamTestCaseBundle>{
        ParamTestCaseBundle{
            "InvalidBinaryType",
            R"({
                "index":
                "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
                "binary": "invalid"
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidAccountRootFormat",
            R"({
                "account_root": "invalid"
            })",
            "malformedAddress",
            "Malformed address."
        },

        ParamTestCaseBundle{
            "InvalidDidFormat",
            R"({
                "did": "invalid"
            })",
            "malformedAddress",
            "Malformed address."
        },

        ParamTestCaseBundle{
            "InvalidAccountRootNotString",
            R"({
                "account_root": 123
            })",
            "invalidParams",
            "account_rootNotString"
        },

        ParamTestCaseBundle{
            "InvalidLedgerIndex",
            R"({
                "ledger_index": "wrong"
            })",
            "invalidParams",
            "ledgerIndexMalformed"
        },

        ParamTestCaseBundle{"UnknownOption", R"({})", "invalidParams", "Invalid parameters."},

        ParamTestCaseBundle{
            "InvalidDepositPreauthType",
            R"({
                "deposit_preauth": 123
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthString",
            R"({
                "deposit_preauth": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthEmtpyJson",
            R"({
                "deposit_preauth": {}
            })",
            "invalidParams",
            "Required field 'owner' missing"
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthJsonWrongAccount",
            R"({
                "deposit_preauth": {
                    "owner": "invalid",
                    "authorized": "invalid"
                }
            })",
            "malformedOwner",
            "Malformed owner."
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthJsonOwnerNotString",
            R"({
                "deposit_preauth": {
                    "owner": 123,
                    "authorized": 123
                }
            })",
            "malformedOwner",
            "Malformed owner."
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthJsonAuthorizedNotString",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized": 123
                    }}
                }})",
                Account
            ),
            "invalidParams",
            "authorizedNotString"
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthJsonAuthorizeCredentialsNotArray",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": "asdf"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "authorized_credentials not array"
        },

        ParamTestCaseBundle{
            "InvalidDepositPreauthJsonAuthorizeCredentialsMalformedString",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": ["C2F2A19C8D0D893D18F18FDCFE13A3ECB41767E48422DF07F2455CDA08FDF09B"]
                    }}
                }})",
                Account
            ),
            "malformedAuthorizedCredentials",
            "authorized_credentials elements in array are not objects."
        },

        ParamTestCaseBundle{
            "DepositPreauthBothAuthAndAuthCredentialsDoesNotExists",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Must have one of authorized or authorized_credentials."
        },

        ParamTestCaseBundle{
            "DepositPreauthBothAuthAndAuthCredentialsExists",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized": "{}",
                        "authorized_credentials": [
                           {{
                                "issuer": "{}",
                                "credential_type": "{}"
                            }}
                        ]
                    }}
                }})",
                Account,
                Account2,
                Account3,
                CredentialType
            ),
            "malformedRequest",
            "Must have one of authorized or authorized_credentials."
        },

        ParamTestCaseBundle{
            "DepositPreauthEmptyAuthorizeCredentials",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        ]
                    }}
                }})",
                Account
            ),
            "malformedAuthorizedCredentials",
            "Requires at least one element in authorized_credentials array."
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsMissingCredentialType",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                            {{
                                "issuer": "{}"
                            }}
                        ]
                    }}
                }})",
                Account,
                Account2
            ),
            "malformedAuthorizedCredentials",
            "Field 'CredentialType' is required but missing."
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsMissingIssuer",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "credential_type": "{}"
                        }}
                        ]
                    }}
                }})",
                Account,
                CredentialType
            ),
            "malformedAuthorizedCredentials",
            "Field 'Issuer' is required but missing."
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsIncorrectIssuerType",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "issuer": 123,
                            "credential_type": "{}"
                        }}
                        ]
                    }}
                }})",
                Account,
                CredentialType
            ),
            "malformedAuthorizedCredentials",
            "issuer NotString"
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsIncorrectCredentialType",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "issuer": "{}",
                            "credential_type": 432
                        }}
                        ]
                    }}
                }})",
                Account,
                Account2
            ),
            "malformedAuthorizedCredentials",
            "credential_type NotString"
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsCredentialTypeNotHex",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "issuer": "{}",
                            "credential_type": "hello world"
                        }}
                        ]
                    }}
                }})",
                Account,
                Account2
            ),
            "malformedAuthorizedCredentials",
            "credential_type NotHexString"
        },

        ParamTestCaseBundle{
            "DepositPreauthAuthorizeCredentialsCredentialTypeEmpty",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "issuer": "{}",
                            "credential_type": ""
                        }}
                        ]
                    }}
                }})",
                Account,
                Account2
            ),
            "malformedAuthorizedCredentials",
            "credential_type is empty"
        },

        ParamTestCaseBundle{
            "DepositPreauthDuplicateAuthorizeCredentials",
            fmt::format(
                R"({{
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized_credentials": [
                        {{
                            "issuer": "{}",
                            "credential_type": "{}"
                        }},
                        {{
                            "issuer": "{}",
                            "credential_type": "{}"
                        }}
                        ]
                    }}
                }})",
                Account,
                Account2,
                CredentialType,
                Account2,
                CredentialType
            ),
            "malformedAuthorizedCredentials",
            "duplicates in credentials."
        },

        ParamTestCaseBundle{
            "InvalidTicketType",
            R"({
                "ticket": 123
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidTicketIndex",
            R"({
                "ticket": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidTicketEmptyJson",
            R"({
                "ticket": {}
            })",
            "invalidParams",
            "Required field 'account' missing"
        },

        ParamTestCaseBundle{
            "InvalidTicketJsonAccountNotString",
            R"({
                "ticket": {
                    "account": 123,
                    "ticket_seq": 123
                }
            })",
            "invalidParams",
            "accountNotString"
        },

        ParamTestCaseBundle{
            "InvalidTicketJsonAccountInvalid",
            R"({
                "ticket": {
                    "account": "123",
                    "ticket_seq": 123
                }
            })",
            "malformedAddress",
            "Malformed address."
        },

        ParamTestCaseBundle{
            "InvalidTicketJsonSeqNotInt",
            fmt::format(
                R"({{
                    "ticket": {{
                        "account": "{}",
                        "ticket_seq": "123"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidOfferType",
            R"({
                "offer": 123
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidOfferIndex",
            R"({
                "offer": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidOfferEmptyJson",
            R"({
                "offer": {}
            })",
            "invalidParams",
            "Required field 'account' missing"
        },

        ParamTestCaseBundle{
            "InvalidOfferJsonAccountNotString",
            R"({
                "ticket": {
                    "account": 123,
                    "seq": 123
                }
            })",
            "invalidParams",
            "accountNotString"
        },

        ParamTestCaseBundle{
            "InvalidOfferJsonAccountInvalid",
            R"({
                "ticket": {
                    "account": "123",
                    "seq": 123
                }
            })",
            "malformedAddress",
            "Malformed address."
        },

        ParamTestCaseBundle{
            "InvalidOfferJsonSeqNotInt",
            fmt::format(
                R"({{
                    "offer": {{
                        "account": "{}",
                        "seq": "123"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidEscrowType",
            R"({
                "escrow": 123
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidEscrowIndex",
            R"({
                "escrow": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidEscrowEmptyJson",
            R"({
                "escrow": {}
            })",
            "invalidParams",
            "Required field 'owner' missing"
        },

        ParamTestCaseBundle{
            "InvalidEscrowJsonAccountNotString",
            R"({
                "escrow": {
                    "owner": 123,
                    "seq": 123
                }
            })",
            "malformedOwner",
            "Malformed owner."
        },

        ParamTestCaseBundle{
            "InvalidEscrowJsonAccountInvalid",
            R"({
                "escrow": {
                    "owner": "123",
                    "seq": 123
                }
            })",
            "malformedOwner",
            "Malformed owner."
        },

        ParamTestCaseBundle{
            "InvalidEscrowJsonSeqNotInt",
            fmt::format(
                R"({{
                    "escrow": {{
                        "owner": "{}",
                        "seq": "123"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidRippleStateType",
            R"({
                "ripple_state": "123"
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidRippleStateMissField",
            R"({
                "ripple_state": {
                    "currency": "USD"
                }
            })",
            "invalidParams",
            "Required field 'accounts' missing"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateEmtpyJson",
            R"({
                "ripple_state": {}
            })",
            "invalidParams",
            "Required field 'accounts' missing"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateOneAccount",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}"]
                    }}
                }})",
                Account
            ),
            "invalidParams",
            "malformedAccounts"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateSameAccounts",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}","{}"],
                        "currency": "USD"
                    }}
                }})",
                Account,
                Account
            ),
            "invalidParams",
            "malformedAccounts"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateWrongAccountsNotString",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}",123],
                        "currency": "USD"
                    }}
                }})",
                Account
            ),
            "invalidParams",
            "malformedAccounts"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateWrongAccountsFormat",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}","123"],
                        "currency": "USD"
                    }}
                }})",
                Account
            ),
            "malformedAddress",
            "malformedAddresses"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateWrongCurrency",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}","{}"],
                        "currency": "XXXX"
                    }}
                }})",
                Account,
                Account2
            ),
            "malformedCurrency",
            "malformedCurrency"
        },

        ParamTestCaseBundle{
            "InvalidRippleStateWrongCurrencyNotString",
            fmt::format(
                R"({{
                    "ripple_state": {{
                        "accounts" : ["{}","{}"],
                        "currency": 123
                    }}
                }})",
                Account,
                Account2
            ),
            "invalidParams",
            "currencyNotString"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryType",
            R"({
                "directory": 123
            })",
            "invalidParams",
            "Invalid parameters."
        },

        ParamTestCaseBundle{
            "InvalidDirectoryIndex",
            R"({
                "directory": "123"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidDirectoryEmtpyJson",
            R"({
                "directory": {}
            })",
            "invalidParams",
            "missingOwnerOrDirRoot"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryWrongOwnerNotString",
            R"({
                "directory": {
                    "owner": 123
                }
            })",
            "invalidParams",
            "ownerNotString"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryWrongOwnerFormat",
            R"({
                "directory": {
                    "owner": "123"
                }
            })",
            "malformedAddress",
            "Malformed address."
        },

        ParamTestCaseBundle{
            "InvalidDirectoryWrongDirFormat",
            R"({
                "directory": {
                    "dir_root": "123"
                }
            })",
            "invalidParams",
            "dir_rootMalformed"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryWrongDirNotString",
            R"({
                "directory": {
                    "dir_root": 123
                }
            })",
            "invalidParams",
            "dir_rootNotString"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryDirOwnerConflict",
            fmt::format(
                R"({{
                    "directory": {{
                        "dir_root": "{}",
                        "owner": "{}"
                    }}
                }})",
                Index1,
                Account
            ),
            "invalidParams",
            "mayNotSpecifyBothDirRootAndOwner"
        },

        ParamTestCaseBundle{
            "InvalidDirectoryDirSubIndexNotInt",
            fmt::format(
                R"({{
                    "directory": {{
                        "dir_root": "{}",
                        "sub_index": "not int"
                    }}
                }})",
                Index1
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "InvalidAMMStringIndex",
            R"({
                "amm": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "EmptyAMMJson",
            R"({
                "amm": {}
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "NonObjectAMMJsonAsset",
            R"({
                "amm": {
                    "asset": 123,
                    "asset2": 123
                }
            })",
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "EmptyAMMAssetJson",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset":{{}},
                        "asset2":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "EmptyAMMAsset2Json",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":{{}},
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "MissingAMMAsset2Json",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "MissingAMMAssetJson",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "AMMAssetNotJson",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset": "invalid",
                        "asset2":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "AMMAsset2NotJson",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2": "invalid",
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "WrongAMMAssetCurrency",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency":"XRP"
                        }},
                        "asset":
                        {{
                            "currency" : "USD2",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "WrongAMMAssetIssuer",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency":"XRP"
                        }},
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "aa{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "MissingAMMAssetIssuerForNonXRP",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency":"JPY"
                        }},
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "AMMAssetHasIssuerForXRP",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency":"XRP",
                            "issuer":"{}"
                        }},
                        "asset":
                        {{
                            "currency" : "USD",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account,
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },

        ParamTestCaseBundle{
            "MissingAMMAssetCurrency",
            fmt::format(
                R"({{
                    "amm": 
                    {{
                        "asset2":
                        {{
                            "currency":"XRP"
                        }},
                        "asset":
                        {{
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeMissingBridgeAccount",
            fmt::format(
                R"({{
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeCurrencyIsNumber",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": {},
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account,
                1,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssuerIsNumber",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": {}
                        }}
                    }}
                }})",
                Account,
                Account,
                Account,
                "JPY",
                2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssuingChainIssueIsNotObject",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue": 1
                    }}
                }})",
                Account,
                Account,
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeWithInvalidBridgeAccount",
            fmt::format(
                R"({{
                    "bridge_account": "abcd",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeDoorInvalid",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "abcd",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssuerInvalid",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "invalid"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account,
                "JPY"
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssueCurrencyInvalid",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "JPJPJP",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account2,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssueXRPCurrencyInvalid",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP",
                            "issuer": "{}"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "JPY",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account2,
                Account2,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeIssueJPYCurrencyInvalid",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "JPY"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeMissingLockingChainDoor",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP",
                            "issuer": "{}"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "JPY",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account2,
                Account2,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeMissingIssuingChainDoor",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "LockingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "JPY",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeMissingLockingChainIssue",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "IssuingChainDoor": "{}",
                        "LockingChainDoor": "{}",
                        "IssuingChainIssue":
                        {{
                            "currency": "JPY",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeMissingIssuingChainIssue",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": 
                    {{
                        "IssuingChainDoor": "{}",
                        "LockingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "JPY",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "BridgeInvalidType",
            fmt::format(
                R"({{
                    "bridge_account": "{}",
                    "bridge": "invalid"
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdInvalidType",
            R"({
                "xchain_owned_claim_id": 123
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdJsonMissingClaimId",
            fmt::format(
                R"({{
                    "xchain_owned_claim_id": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdJsonMissingDoor",
            fmt::format(
                R"({{
                    "xchain_owned_claim_id": 
                    {{
                        "xchain_owned_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdJsonMissingIssue",
            fmt::format(
                R"({{
                    "xchain_owned_claim_id": 
                    {{
                        "xchain_owned_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }}
                    }}
                }})",
                Account,
                Account
            ),

            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdJsonInvalidDoor",
            fmt::format(
                R"({{
                    "xchain_owned_claim_id": 
                    {{
                        "xchain_owned_claim_id": 10,
                        "LockingChainDoor": "abcd",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedClaimIdJsonInvalidIssue",
            fmt::format(
                R"({{
                    "xchain_owned_claim_id": 
                    {{
                        "xchain_owned_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY"
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdInvalidType",
            R"({
                    "xchain_owned_create_account_claim_id": 123
                    })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdJsonMissingClaimId",
            fmt::format(
                R"({{
                    "xchain_owned_create_account_claim_id": 
                    {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdJsonMissingDoor",
            fmt::format(
                R"({{
                    "xchain_owned_create_account_claim_id": 
                    {{
                        "xchain_owned_create_account_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdJsonMissingIssue",
            fmt::format(
                R"({{
                    "xchain_owned_create_account_claim_id": 
                    {{
                        "xchain_owned_create_account_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }}
                    }}
                }})",
                Account,
                Account
            ),

            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdJsonInvalidDoor",
            fmt::format(
                R"({{
                    "xchain_owned_create_account_claim_id": 
                    {{
                        "xchain_owned_create_account_claim_id": 10,
                        "LockingChainDoor": "abcd",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                Account,
                "JPY",
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OwnedCreateAccountClaimIdJsonInvalidIssue",
            fmt::format(
                R"({{
                    "xchain_owned_create_account_claim_id": 
                    {{
                        "xchain_owned_create_account_claim_id": 10,
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue":
                        {{
                            "currency": "XRP"
                        }},
                        "IssuingChainIssue":
                        {{
                            "currency": "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                "JPY"
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdMissing",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidNegative",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": -1
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidTypeString",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": "invalid"
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidTypeDouble",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": 3.21
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidTypeObject",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": {{}}
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidTypeArray",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": []
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectDocumentIdInvalidTypeNull",
            fmt::format(
                R"({{
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": null
                    }}
                }})",
                Account
            ),
            "malformedDocumentID",
            "Malformed oracle_document_id."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountMissing",
            R"({
                "oracle": {
                    "oracle_document_id": 1
                }
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidTypeInteger",
            R"({
                "oracle": {
                    "account": 123,
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidTypeDouble",
            R"({
                "oracle": {
                    "account": 123.45,
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidTypeNull",
            R"({
                "oracle": {
                    "account": null,
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidTypeObject",
            R"({
                "oracle": {
                    "account": {"test": "test"},
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidTypeArray",
            R"({
                "oracle": {
                    "account": [{"test": "test"}],
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleObjectAccountInvalidFormat",
            R"({
                "oracle": {
                    "account": "NotHex",
                    "oracle_document_id": 1
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleStringInvalidFormat",
            R"({
                "oracle": "NotHex"
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "OracleStringInvalidTypeInteger",
            R"({
                "oracle": 123
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleStringInvalidTypeDouble",
            R"({
                "oracle": 123.45
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleStringInvalidTypeArray",
            R"({
                "oracle": [{"test": "test"}]
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "OracleStringInvalidTypeNull",
            R"({
                "oracle": null
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "CredentialInvalidSubjectType",
            R"({
                "credential": {
                    "subject": 123
                }
            })",
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "CredentialInvalidIssuerType",
            fmt::format(
                R"({{
                "credential": {{
                    "issuer": ["{}"]
                }}
            }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "InvalidMPTIssuanceStringIndex",
            R"({
                "mpt_issuance": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "InvalidMPTIssuanceType",
            R"({
                "mpt_issuance": 0
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "InvalidMPTokenStringIndex",
            R"({
                "mptoken": "invalid"
            })",
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "InvalidMPTokenObject",
            fmt::format(
                R"({{
                    "mptoken": {{}}
                }})"
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "MissingMPTokenID",
            fmt::format(
                R"({{
                    "mptoken": {{
                        "account": "{}"
                    }}
                }})",
                Account
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "CredentialInvalidCredentialType",
            fmt::format(
                R"({{
                "credential": {{
                    "subject": "{}",
                    "issuer": "{}",
                    "credential_type": 1234
                }}
            }})",
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "CredentialMissingIssuerField",
            fmt::format(
                R"({{
                "credential": {{
                    "subject": "{}",
                    "credential_type": "1234"
                }}
            }})",
                Account,
                Account2
            ),
            "malformedRequest",
            "Malformed request."
        },
        ParamTestCaseBundle{
            "InvalidMPTokenAccount",
            fmt::format(
                R"({{
                    "mptoken": {{
                        "mpt_issuance_id": "0000019315EABA24E6135A4B5CE2899E0DA791206413B33D",
                        "account": 1
                    }}
                }})"
            ),
            "malformedAddress",
            "Malformed address."
        },
        ParamTestCaseBundle{
            "InvalidMPTokenType",
            fmt::format(
                R"({{
                    "mptoken": 0
                }})"
            ),
            "malformedRequest",
            "Malformed request."
        }
    };
}

INSTANTIATE_TEST_CASE_P(
    RPCLedgerEntryGroup1,
    LedgerEntryParameterTest,
    ValuesIn(generateTestValuesForParametersTest()),
    tests::util::NameGenerator
);

TEST_P(LedgerEntryParameterTest, InvalidParams)
{
    auto const testBundle = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(testBundle.testJson);
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), testBundle.expectedError);
        EXPECT_EQ(err.at("error_message").as_string(), testBundle.expectedErrorMessage);
    });
}

// parameterized test cases for index
struct IndexTest : public HandlerBaseTest, public WithParamInterface<std::string> {
    struct NameGenerator {
        template <class ParamType>
        std::string
        operator()(testing::TestParamInfo<ParamType> const& info) const
        {
            return static_cast<std::string>(info.param);
        }
    };
};

// content of index, payment_channel, nft_page and check fields is ledger index.
INSTANTIATE_TEST_CASE_P(
    RPCLedgerEntryGroup3,
    IndexTest,
    Values("index", "nft_page", "payment_channel", "check"),
    IndexTest::NameGenerator{}
);

TEST_P(IndexTest, InvalidIndexUint256)
{
    auto const index = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "{}": "invalid"
            }})",
            index
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "malformedRequest");
        EXPECT_EQ(err.at("error_message").as_string(), "Malformed request.");
    });
}

TEST_P(IndexTest, InvalidIndexNotString)
{
    auto const index = GetParam();
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "{}": 123
            }})",
            index
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "malformedRequest");
        EXPECT_EQ(err.at("error_message").as_string(), "Malformed request.");
    });
}

TEST_F(RPCLedgerEntryTest, LedgerEntryNotFound)
{
    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerHeader));

    // return null for ledger entry
    auto const key = ripple::keylet::account(getAccountIdWithString(Account)).key;
    EXPECT_CALL(*backend, doFetchLedgerObject(key, RangeMax, _)).WillRepeatedly(Return(std::optional<Blob>{}));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "account_root": "{}"
            }})",
            Account
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "entryNotFound");
    });
}

struct NormalPathTestBundle {
    std::string testName;
    std::string testJson;
    ripple::uint256 expectedIndex;
    ripple::STObject mockedEntity;
};

struct RPCLedgerEntryNormalPathTest : public RPCLedgerEntryTest, public WithParamInterface<NormalPathTestBundle> {};

static auto
generateTestValuesForNormalPathTest()
{
    auto account1 = getAccountIdWithString(Account);
    auto account2 = getAccountIdWithString(Account2);
    ripple::Currency currency;
    ripple::to_currency(currency, "USD");

    return std::vector<NormalPathTestBundle>{
        NormalPathTestBundle{
            "Index",
            fmt::format(
                R"({{
                    "binary": true,
                    "index": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createAccountRootObject(Account2, ripple::lsfGlobalFreeze, 1, 10, 2, Index1, 3)
        },
        NormalPathTestBundle{
            "Payment_channel",
            fmt::format(
                R"({{
                    "binary": true,
                    "payment_channel": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createPaymentChannelLedgerObject(Account, Account2, 100, 200, 300, Index1, 400)
        },
        NormalPathTestBundle{
            "Nft_page",
            fmt::format(
                R"({{
                    "binary": true,
                    "nft_page": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createNftTokenPage(
                std::vector{std::make_pair<std::string, std::string>(TokenID, "www.ok.com")}, std::nullopt
            )
        },
        NormalPathTestBundle{
            "Check",
            fmt::format(
                R"({{
                    "binary": true,
                    "check": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createCheckLedgerObject(Account, Account2)
        },
        NormalPathTestBundle{
            "DirectoryIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "directory": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createOwnerDirLedgerObject(std::vector<ripple::uint256>{ripple::uint256{Index1}}, Index1)
        },
        NormalPathTestBundle{
            "OfferIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "offer": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createOfferLedgerObject(
                Account, 100, 200, "USD", "XRP", Account2, ripple::toBase58(ripple::xrpAccount()), Index1
            )
        },
        NormalPathTestBundle{
            "EscrowIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "escrow": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createEscrowLedgerObject(Account, Account2)
        },
        NormalPathTestBundle{
            "TicketIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "ticket": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createTicketLedgerObject(Account, 0)
        },
        NormalPathTestBundle{
            "DepositPreauthIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "deposit_preauth": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createDepositPreauthLedgerObjectByAuth(Account, Account2)
        },
        NormalPathTestBundle{
            "AccountRoot",
            fmt::format(
                R"({{
                    "binary": true,
                    "account_root": "{}"
                }})",
                Account
            ),
            ripple::keylet::account(getAccountIdWithString(Account)).key,
            createAccountRootObject(Account, 0, 1, 1, 1, Index1, 1)
        },
        NormalPathTestBundle{
            "DID",
            fmt::format(
                R"({{
                    "binary": true,
                    "did": "{}"
                }})",
                Account
            ),
            ripple::keylet::did(getAccountIdWithString(Account)).key,
            createDidObject(Account, "mydocument", "myURI", "mydata")
        },
        NormalPathTestBundle{
            "DirectoryViaDirRoot",
            fmt::format(
                R"({{
                    "binary": true,
                    "directory": {{
                        "dir_root": "{}",
                        "sub_index": 2
                    }}
                }})",
                Index1
            ),
            ripple::keylet::page(ripple::uint256{Index1}, 2).key,
            createOwnerDirLedgerObject(std::vector<ripple::uint256>{ripple::uint256{Index1}}, Index1)
        },
        NormalPathTestBundle{
            "DirectoryViaOwner",
            fmt::format(
                R"({{
                    "binary": true,
                    "directory": {{
                        "owner": "{}",
                        "sub_index": 2
                    }}
                }})",
                Account
            ),
            ripple::keylet::page(ripple::keylet::ownerDir(account1), 2).key,
            createOwnerDirLedgerObject(std::vector<ripple::uint256>{ripple::uint256{Index1}}, Index1)
        },
        NormalPathTestBundle{
            "DirectoryViaDefaultSubIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "directory": {{
                        "owner": "{}"
                    }}
                }})",
                Account
            ),
            // default sub_index is 0
            ripple::keylet::page(ripple::keylet::ownerDir(account1), 0).key,
            createOwnerDirLedgerObject(std::vector<ripple::uint256>{ripple::uint256{Index1}}, Index1)
        },
        NormalPathTestBundle{
            "Escrow",
            fmt::format(
                R"({{
                    "binary": true,
                    "escrow": {{
                        "owner": "{}",
                        "seq": 1
                    }}
                }})",
                Account
            ),
            ripple::keylet::escrow(account1, 1).key,
            createEscrowLedgerObject(Account, Account2)
        },
        NormalPathTestBundle{
            "DepositPreauthByAuth",
            fmt::format(
                R"({{
                    "binary": true,
                    "deposit_preauth": {{
                        "owner": "{}",
                        "authorized": "{}"
                    }}
                }})",
                Account,
                Account2
            ),
            ripple::keylet::depositPreauth(account1, account2).key,
            createDepositPreauthLedgerObjectByAuth(Account, Account2)
        },
        NormalPathTestBundle{
            "DepositPreauthByAuthCredentials",
            fmt::format(
                R"({{
                       "binary": true,
                       "deposit_preauth": {{
                           "owner": "{}",
                           "authorized_credentials": [
                               {{
                                    "issuer": "{}",
                                    "credential_type": "{}"
                               }}
                           ]
                       }}
                   }})",
                Account,
                Account2,
                CredentialType
            ),
            ripple::keylet::depositPreauth(
                account1,
                credentials::createAuthCredentials(createAuthCredentialArray(
                    std::vector<std::string_view>{Account2}, std::vector<std::string_view>{CredentialType}
                ))
            )
                .key,
            createDepositPreauthLedgerObjectByAuthCredentials(Account, Account2, CredentialType)
        },
        NormalPathTestBundle{
            "Credentials",
            fmt::format(
                R"({{
                    "binary": true,
                    "credential": {{
                        "subject": "{}",
                        "issuer": "{}",
                        "credential_type": "{}"
                    }}
                }})",
                Account,
                Account2,
                CredentialType
            ),
            ripple::keylet::credential(
                account1,
                account2,
                ripple::Slice(ripple::strUnHex(CredentialType)->data(), ripple::strUnHex(CredentialType)->size())
            )
                .key,
            createCredentialObject(Account, Account2, CredentialType)
        },
        NormalPathTestBundle{
            "RippleState",
            fmt::format(
                R"({{
                    "binary": true,
                    "ripple_state": {{
                        "accounts": ["{}","{}"],
                        "currency": "USD"
                    }}
                }})",
                Account,
                Account2
            ),
            ripple::keylet::line(account1, account2, currency).key,
            createRippleStateLedgerObject("USD", Account2, 100, Account, 10, Account2, 20, Index1, 123, 0)
        },
        NormalPathTestBundle{
            "Ticket",
            fmt::format(
                R"({{
                    "binary": true,
                    "ticket": {{
                        "account": "{}",
                        "ticket_seq": 2
                    }}
                }})",
                Account
            ),
            ripple::getTicketIndex(account1, 2),
            createTicketLedgerObject(Account, 0)
        },
        NormalPathTestBundle{
            "Offer",
            fmt::format(
                R"({{
                    "binary": true,
                    "offer": {{
                        "account": "{}",
                        "seq": 2
                    }}
                }})",
                Account
            ),
            ripple::keylet::offer(account1, 2).key,
            createOfferLedgerObject(
                Account, 100, 200, "USD", "XRP", Account2, ripple::toBase58(ripple::xrpAccount()), Index1
            )
        },
        NormalPathTestBundle{
            "AMMViaIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "amm": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createAmmObject(Account, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", Account2)
        },
        NormalPathTestBundle{
            "AMMViaJson",
            fmt::format(
                R"({{
                    "binary": true,
                    "amm": {{
                        "asset": {{
                            "currency": "XRP"
                        }},
                        "asset2": {{
                            "currency": "{}",
                            "issuer": "{}"
                        }}
                    }}
                }})",
                "JPY",
                Account2
            ),
            ripple::keylet::amm(getIssue("XRP", ripple::toBase58(ripple::xrpAccount())), getIssue("JPY", Account2)).key,
            createAmmObject(Account, "XRP", ripple::toBase58(ripple::xrpAccount()), "JPY", Account2)
        },
        NormalPathTestBundle{
            "BridgeLocking",
            fmt::format(
                R"({{
                    "binary": true,
                    "bridge_account": "{}",
                    "bridge": {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue": {{
                            "currency" : "XRP"
                        }},
                        "IssuingChainIssue": {{
                            "currency" : "JPY",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account,
                Account,
                Account2,
                Account3
            ),
            ripple::keylet::bridge(
                ripple::STXChainBridge(
                    getAccountIdWithString(Account),
                    ripple::xrpIssue(),
                    getAccountIdWithString(Account2),
                    getIssue("JPY", Account3)
                ),
                ripple::STXChainBridge::ChainType::locking
            )
                .key,
            createBridgeObject(Account, Account, Account2, "JPY", Account3)
        },
        NormalPathTestBundle{
            "BridgeIssuing",
            fmt::format(
                R"({{
                    "binary": true,
                    "bridge_account": "{}",
                    "bridge": {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue": {{
                            "currency" : "XRP"
                        }},
                        "IssuingChainIssue": {{
                            "currency" : "JPY",
                            "issuer" : "{}"
                        }}
                    }}
                }})",
                Account2,
                Account,
                Account2,
                Account3
            ),
            ripple::keylet::bridge(
                ripple::STXChainBridge(
                    getAccountIdWithString(Account),
                    ripple::xrpIssue(),
                    getAccountIdWithString(Account2),
                    getIssue("JPY", Account3)
                ),
                ripple::STXChainBridge::ChainType::issuing
            )
                .key,
            createBridgeObject(Account, Account, Account2, "JPY", Account3)
        },
        NormalPathTestBundle{
            "XChainOwnedClaimId",
            fmt::format(
                R"({{
                    "binary": true,
                    "xchain_owned_claim_id": {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue": {{
                            "currency" : "XRP"
                        }},
                        "IssuingChainIssue": {{
                            "currency" : "JPY",
                            "issuer" : "{}"
                        }},
                        "xchain_owned_claim_id": 10
                    }}
                }})",
                Account,
                Account2,
                Account3
            ),
            ripple::keylet::xChainClaimID(
                ripple::STXChainBridge(
                    getAccountIdWithString(Account),
                    ripple::xrpIssue(),
                    getAccountIdWithString(Account2),
                    getIssue("JPY", Account3)
                ),
                10
            )
                .key,
            createChainOwnedClaimIdObject(Account, Account, Account2, "JPY", Account3, Account)
        },
        NormalPathTestBundle{
            "XChainOwnedCreateAccountClaimId",
            fmt::format(
                R"({{
                    "binary": true,
                    "xchain_owned_create_account_claim_id": {{
                        "LockingChainDoor": "{}",
                        "IssuingChainDoor": "{}",
                        "LockingChainIssue": {{
                            "currency" : "XRP"
                        }},
                        "IssuingChainIssue": {{
                            "currency" : "JPY",
                            "issuer" : "{}"
                        }},
                        "xchain_owned_create_account_claim_id": 10
                    }}
                }})",
                Account,
                Account2,
                Account3
            ),
            ripple::keylet::xChainCreateAccountClaimID(
                ripple::STXChainBridge(
                    getAccountIdWithString(Account),
                    ripple::xrpIssue(),
                    getAccountIdWithString(Account2),
                    getIssue("JPY", Account3)
                ),
                10
            )
                .key,
            createChainOwnedClaimIdObject(Account, Account, Account2, "JPY", Account3, Account)
        },
        NormalPathTestBundle{
            "OracleEntryFoundViaIntOracleDocumentId",
            fmt::format(
                R"({{
                    "binary": true,
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": 1
                    }}
                }})",
                Account
            ),
            ripple::keylet::oracle(getAccountIdWithString(Account), 1).key,
            createOracleObject(
                Account,
                "70726F7669646572",
                32u,
                1234u,
                ripple::Blob(8, 's'),
                ripple::Blob(8, 's'),
                RangeMax - 2,
                ripple::uint256{"E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321"},
                createPriceDataSeries(
                    {createOraclePriceData(2e4, ripple::to_currency("XRP"), ripple::to_currency("USD"), 3)}
                )
            )
        },
        NormalPathTestBundle{
            "OracleEntryFoundViaStrOracleDocumentId",
            fmt::format(
                R"({{
                    "binary": true,
                    "oracle": {{
                        "account": "{}",
                        "oracle_document_id": "1"
                    }}
                }})",
                Account
            ),
            ripple::keylet::oracle(getAccountIdWithString(Account), 1).key,
            createOracleObject(
                Account,
                "70726F7669646572",
                32u,
                1234u,
                ripple::Blob(8, 's'),
                ripple::Blob(8, 's'),
                RangeMax - 2,
                ripple::uint256{"E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321"},
                createPriceDataSeries(
                    {createOraclePriceData(2e4, ripple::to_currency("XRP"), ripple::to_currency("USD"), 3)}
                )
            )
        },
        NormalPathTestBundle{
            "OracleEntryFoundViaString",
            fmt::format(
                R"({{
                    "binary": true,
                    "oracle": "{}"
                }})",
                ripple::to_string(ripple::keylet::oracle(getAccountIdWithString(Account), 1).key)
            ),
            ripple::keylet::oracle(getAccountIdWithString(Account), 1).key,
            createOracleObject(
                Account,
                "70726F7669646572",
                64u,
                4321u,
                ripple::Blob(8, 'a'),
                ripple::Blob(8, 'a'),
                RangeMax - 4,
                ripple::uint256{"E6DBAFC99223B42257915A63DFC6B0C032D4070F9A574B255AD97466726FC321"},
                createPriceDataSeries(
                    {createOraclePriceData(1e3, ripple::to_currency("USD"), ripple::to_currency("XRP"), 2)}
                )
            )
        },
        NormalPathTestBundle{
            "MPTIssuance",
            fmt::format(
                R"({{
                    "binary": true,
                    "mpt_issuance": "{}"
                }})",
                ripple::to_string(ripple::makeMptID(2, account1))
            ),
            ripple::keylet::mptIssuance(ripple::makeMptID(2, account1)).key,
            createMptIssuanceObject(Account, 2, "metadata")
        },
        NormalPathTestBundle{
            "MPTokenViaIndex",
            fmt::format(
                R"({{
                    "binary": true,
                    "mptoken": "{}"
                }})",
                Index1
            ),
            ripple::uint256{Index1},
            createMpTokenObject(Account, ripple::makeMptID(2, account1))
        },
        NormalPathTestBundle{
            "MPTokenViaObject",
            fmt::format(
                R"({{
                    "binary": true,
                    "mptoken": {{
                        "account": "{}",
                        "mpt_issuance_id": "{}"
                    }}
                }})",
                Account,
                ripple::to_string(ripple::makeMptID(2, account1))
            ),
            ripple::keylet::mptoken(ripple::makeMptID(2, account1), account1).key,
            createMpTokenObject(Account, ripple::makeMptID(2, account1))
        },
    };
}

INSTANTIATE_TEST_CASE_P(
    RPCLedgerEntryGroup2,
    RPCLedgerEntryNormalPathTest,
    ValuesIn(generateTestValuesForNormalPathTest()),
    tests::util::NameGenerator
);

// Test for normal path
// Check the index in response matches the computed index accordingly
TEST_P(RPCLedgerEntryNormalPathTest, NormalPath)
{
    auto const testBundle = GetParam();

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerHeader));

    EXPECT_CALL(*backend, doFetchLedgerObject(testBundle.expectedIndex, RangeMax, _))
        .WillRepeatedly(Return(testBundle.mockedEntity.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(testBundle.testJson);
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(output.result.value().at("ledger_hash").as_string(), LedgerHash);
        EXPECT_EQ(output.result.value().at("ledger_index").as_uint64(), RangeMax);
        EXPECT_EQ(
            output.result.value().at("node_binary").as_string(),
            ripple::strHex(testBundle.mockedEntity.getSerializer().peekData())
        );
        EXPECT_EQ(
            ripple::uint256(boost::json::value_to<std::string>(output.result.value().at("index")).data()),
            testBundle.expectedIndex
        );
    });
}

// this testcase will test the deserialization of ledger entry
TEST_F(RPCLedgerEntryTest, BinaryFalse)
{
    static constexpr auto Out = R"({
        "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index":30,
        "validated":true,
        "index":"05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "node":{
            "Account":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "Amount":"100",
            "Balance":"200",
            "Destination":"rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
            "Flags":0,
            "LedgerEntryType":"PayChannel",
            "OwnerNode":"0",
            "PreviousTxnID":"05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
            "PreviousTxnLgrSeq":400,
            "PublicKey":"020000000000000000000000000000000000000000000000000000000000000000",
            "SettleDelay":300,
            "index":"05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
        }
    })";

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerHeader));

    // return valid ledger entry which can be deserialized
    auto const ledgerEntry = createPaymentChannelLedgerObject(Account, Account2, 100, 200, 300, Index1, 400);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillRepeatedly(Return(ledgerEntry.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "payment_channel": "{}"
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

TEST_F(RPCLedgerEntryTest, UnexpectedLedgerType)
{
    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerHeader));

    // return valid ledger entry which can be deserialized
    auto const ledgerEntry = createPaymentChannelLedgerObject(Account, Account2, 100, 200, 300, Index1, 400);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillRepeatedly(Return(ledgerEntry.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "check": "{}"
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "unexpectedLedgerType");
    });
}

TEST_F(RPCLedgerEntryTest, LedgerNotExistViaIntSequence)
{
    backend->setRange(RangeMin, RangeMax);

    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(std::nullopt));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "check": "{}",
                "ledger_index": {}
            }})",
            Index1,
            RangeMax
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCLedgerEntryTest, LedgerNotExistViaStringSequence)
{
    backend->setRange(RangeMin, RangeMax);

    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(std::nullopt));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "check": "{}",
                "ledger_index": "{}"
            }})",
            Index1,
            RangeMax
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCLedgerEntryTest, LedgerNotExistViaHash)
{
    backend->setRange(RangeMin, RangeMax);

    EXPECT_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _)).WillRepeatedly(Return(std::nullopt));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "check": "{}",
                "ledger_hash": "{}"
            }})",
            Index1,
            LedgerHash
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCLedgerEntryTest, InvalidEntryTypeVersion2)
{
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(R"({})");
        auto const output = handler.process(req, Context{.yield = yield, .apiVersion = 2});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "Invalid parameters.");
    });
}

TEST_F(RPCLedgerEntryTest, InvalidEntryTypeVersion1)
{
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(R"({})");
        auto const output = handler.process(req, Context{.yield = yield, .apiVersion = 1});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "unknownOption");
        EXPECT_EQ(err.at("error_message").as_string(), "Unknown option.");
    });
}

TEST(RPCLedgerEntrySpecTest, DeprecatedFields)
{
    boost::json::value const json{{"ledger", 2}};
    auto const spec = LedgerEntryHandler::spec(2);
    auto const warnings = spec.check(json);
    ASSERT_EQ(warnings.size(), 1);
    ASSERT_TRUE(warnings[0].is_object());
    auto const& warning = warnings[0].as_object();
    ASSERT_TRUE(warning.contains("id"));
    ASSERT_TRUE(warning.contains("message"));
    EXPECT_EQ(warning.at("id").as_int64(), static_cast<int64_t>(rpc::WarningCode::WarnRpcDeprecated));
    EXPECT_NE(warning.at("message").as_string().find("Field 'ledger' is deprecated."), std::string::npos) << warning;
}

// Same as BinaryFalse with include_deleted set to true
// Expected Result: same as BinaryFalse
TEST_F(RPCLedgerEntryTest, BinaryFalseIncludeDeleted)
{
    static constexpr auto Out = R"({
        "ledger_hash": "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index": 30,
        "validated": true,
        "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "node": {
            "Account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "Amount": "100",
            "Balance": "200",
            "Destination": "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
            "Flags": 0,
            "LedgerEntryType": "PayChannel",
            "OwnerNode": "0",
            "PreviousTxnID": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
            "PreviousTxnLgrSeq": 400,
            "PublicKey": "020000000000000000000000000000000000000000000000000000000000000000",
            "SettleDelay": 300,
            "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
        }
    })";

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerinfo
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));

    // return valid ledger entry which can be deserialized
    auto const ledgerEntry = createPaymentChannelLedgerObject(Account, Account2, 100, 200, 300, Index1, 400);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillRepeatedly(Return(ledgerEntry.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

// Test for object is deleted in the latest sequence
// Expected Result: return the latest object that is not deleted
TEST_F(RPCLedgerEntryTest, LedgerEntryDeleted)
{
    static constexpr auto Out = R"({
        "ledger_hash": "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index": 30,
        "validated": true,
        "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "deleted_ledger_index": 30,
        "node": {
            "Amount": "123",
            "Flags": 0,
            "LedgerEntryType": "NFTokenOffer",
            "NFTokenID": "00010000A7CAD27B688D14BA1A9FA5366554D6ADCF9CE0875B974D9F00000004",
            "NFTokenOfferNode": "0",
            "Owner": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "OwnerNode": "0",
            "PreviousTxnID": "0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq": 0,
            "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
            }
        })";
    backend->setRange(RangeMin, RangeMax);
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));
    // return valid ledger entry which can be deserialized
    auto const offer = createNftBuyOffer(NftID, Account);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObjectSeq(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(uint32_t{RangeMax}));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax - 1, _))
        .WillOnce(Return(offer.getSerializer().peekData()));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

// Test for object not exist in database
// Expected Result: return entryNotFound error
TEST_F(RPCLedgerEntryTest, LedgerEntryNotExist)
{
    backend->setRange(RangeMin, RangeMax);
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObjectSeq(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(uint32_t{RangeMax}));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax - 1, _))
        .WillOnce(Return(std::optional<Blob>{}));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        auto const myerr = err.at("error").as_string();
        EXPECT_EQ(myerr, "entryNotFound");
    });
}

// Same as BinaryFalse with include_deleted set to false
// Expected Result: same as BinaryFalse
TEST_F(RPCLedgerEntryTest, BinaryFalseIncludeDeleteFalse)
{
    static constexpr auto Out = R"({
        "ledger_hash": "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index": 30,
        "validated": true,
        "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "node": {
            "Account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "Amount": "100",
            "Balance": "200",
            "Destination": "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
            "Flags": 0,
            "LedgerEntryType": "PayChannel",
            "OwnerNode": "0",
            "PreviousTxnID": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
            "PreviousTxnLgrSeq": 400,
            "PublicKey": "020000000000000000000000000000000000000000000000000000000000000000",
            "SettleDelay": 300,
            "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
        }
    })";

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerinfo
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));

    // return valid ledger entry which can be deserialized
    auto const ledgerEntry = createPaymentChannelLedgerObject(Account, Account2, 100, 200, 300, Index1, 400);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillRepeatedly(Return(ledgerEntry.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "payment_channel": "{}",
                "include_deleted": false
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

// Test when an object is updated and include_deleted is set to true
// Expected Result: return the latest object that is not deleted (latest object in this test)
TEST_F(RPCLedgerEntryTest, ObjectUpdateIncludeDelete)
{
    static constexpr auto Out = R"({
        "ledger_hash": "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index": 30,
        "validated": true,
        "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "node": {
            "Balance": {
                "currency": "USD",
                "issuer": "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "value": "10"
            },
            "Flags": 0,
            "HighLimit": {
                "currency": "USD",
                "issuer": "rLEsXccBGNR3UPuPu2hUXPjziKC3qKSBun",
                "value": "200"
            },
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "USD",
                "issuer": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
                "value": "100"
            },
            "PreviousTxnID": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
            "PreviousTxnLgrSeq": 123,
            "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
            }
        })";

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerinfo
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));

    // return valid ledger entry which can be deserialized
    auto const line1 = createRippleStateLedgerObject("USD", Account2, 10, Account, 100, Account2, 200, TxnID, 123);
    auto const line2 = createRippleStateLedgerObject("USD", Account, 10, Account2, 100, Account, 200, TxnID, 123);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillRepeatedly(Return(line1.getSerializer().peekData()));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax - 1, _))
        .WillRepeatedly(Return(line2.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

// Test when an object is deleted several sequence ago and include_deleted is set to true
// Expected Result: return the latest object that is not deleted
TEST_F(RPCLedgerEntryTest, ObjectDeletedPreviously)
{
    static constexpr auto Out = R"({
        "ledger_hash": "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index": 30,
        "validated": true,
        "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD",
        "deleted_ledger_index": 26,
        "node": {
            "Amount": "123",
            "Flags": 0,
            "LedgerEntryType": "NFTokenOffer",
            "NFTokenID": "00010000A7CAD27B688D14BA1A9FA5366554D6ADCF9CE0875B974D9F00000004",
            "NFTokenOfferNode": "0",
            "Owner": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "OwnerNode": "0",
            "PreviousTxnID": "0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq": 0,
            "index": "05FB0EB4B899F056FA095537C5817163801F544BAFCEA39C995D76DB4D16F9DD"
            }
        })";
    backend->setRange(RangeMin, RangeMax);
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));
    // return valid ledger entry which can be deserialized
    auto const offer = createNftBuyOffer(NftID, Account);
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObjectSeq(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(uint32_t{RangeMax - 4}));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax - 5, _))
        .WillOnce(Return(offer.getSerializer().peekData()));
    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

// Test for object seq not exist in database
// Expected Result: return entryNotFound error
TEST_F(RPCLedgerEntryTest, ObjectSeqNotExist)
{
    backend->setRange(RangeMin, RangeMax);
    auto const ledgerinfo = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerinfo));
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::uint256{Index1}, RangeMax, _))
        .WillOnce(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObjectSeq(ripple::uint256{Index1}, RangeMax, _)).WillOnce(Return(std::nullopt));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}",
                "include_deleted": true
            }})",
            Index1
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        auto const myerr = err.at("error").as_string();
        EXPECT_EQ(myerr, "entryNotFound");
    });
}

// this testcase will test the if response includes synthetic mpt_issuance_id
TEST_F(RPCLedgerEntryTest, SyntheticMPTIssuanceID)
{
    static constexpr auto Out = R"({
        "ledger_hash":"4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652",
        "ledger_index":30,
        "validated":true,
        "index":"FD7E7EFAE2A20E75850D0E0590B205E2F74DC472281768CD6E03988069816336",
        "node":{
            "Flags":0,
            "Issuer":"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
            "LedgerEntryType":"MPTokenIssuance",
            "MPTokenMetadata":"6D65746164617461",
            "MaximumAmount":"0",
            "OutstandingAmount":"0",
            "OwnerNode":"0",
            "PreviousTxnID":"0000000000000000000000000000000000000000000000000000000000000000",
            "PreviousTxnLgrSeq":0,
            "Sequence":2,
            "index":"FD7E7EFAE2A20E75850D0E0590B205E2F74DC472281768CD6E03988069816336",
            "mpt_issuance_id":"000000024B4E9C06F24296074F7BC48F92A97916C6DC5EA9"
        }
    })";

    auto const mptId = ripple::makeMptID(2, getAccountIdWithString(Account));

    backend->setRange(RangeMin, RangeMax);
    // return valid ledgerHeader
    auto const ledgerHeader = createLedgerHeader(LedgerHash, RangeMax);
    EXPECT_CALL(*backend, fetchLedgerBySequence(RangeMax, _)).WillRepeatedly(Return(ledgerHeader));

    // return valid ledger entry which can be deserialized
    auto const ledgerEntry = createMptIssuanceObject(Account, 2, "metadata");
    EXPECT_CALL(*backend, doFetchLedgerObject(ripple::keylet::mptIssuance(mptId).key, RangeMax, _))
        .WillRepeatedly(Return(ledgerEntry.getSerializer().peekData()));

    runSpawn([&, this](auto yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "mpt_issuance": "{}"
            }})",
            ripple::to_string(mptId)
        ));
        auto const output = handler.process(req, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(*output.result, json::parse(Out));
    });
}

using RPCLedgerEntryDeathTest = RPCLedgerEntryTest;

TEST_F(RPCLedgerEntryDeathTest, RangeNotAvailable)
{
    boost::asio::io_context ctx;
    bool checkCalled = false;
    spawn(ctx, [&, _unused = make_work_guard(ctx)](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{LedgerEntryHandler{backend}};
        auto const req = json::parse(fmt::format(
            R"({{
                "index": "{}"
            }})",
            Index1
        ));
        checkCalled = true;
        EXPECT_DEATH(
            { [[maybe_unused]] auto _unused2 = handler.process(req, Context{yield}); }, "Ledger range must be available"
        );
    });

    ctx.run();
    ASSERT_TRUE(checkCalled);
}
