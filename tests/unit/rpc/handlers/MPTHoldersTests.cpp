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
#include "rpc/handlers/MPTHolders.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/TestObject.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/json/parse.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/LedgerHeader.h>
#include <xrpl/basics/strHex.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

namespace {

constexpr auto HoldeR1Account = "rrnAZCqMahreZrKMcZU3t2DZ6yUndT4ubN";
constexpr auto HoldeR2Account = "rEiNkzogdHEzUxPfsri5XSMqtXUixf2Yx";
constexpr auto LedgerHash = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto MptID = "000004C463C52827307480341125DA0577DEFC38405B0E3E";

std::string const MptOut1 =
    R"({
        "account": "rrnAZCqMahreZrKMcZU3t2DZ6yUndT4ubN",
        "flags": 0,
        "mpt_amount": "1",
        "mptoken_index": "D137F2E5A5767A06CB7A8F060ADE442A30CFF95028E1AF4B8767E3A56877205A"
    })";

std::string const MptOut2 =
    R"({
        "account": "rEiNkzogdHEzUxPfsri5XSMqtXUixf2Yx",
        "flags": 0,
        "mpt_amount": "1",
        "mptoken_index": "36D91DEE5EFE4A93119A8B84C944A528F2B444329F3846E49FE921040DE17E65"
    })";

}  // namespace

class RPCMPTHoldersHandlerTest : public HandlerBaseTest {};

TEST_F(RPCMPTHoldersHandlerTest, NonHexLedgerHash)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
                "mpt_issuance_id": "{}", 
                "ledger_hash": "xxx"
            }})",
            MptID
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledger_hashMalformed");
    });
}

TEST_F(RPCMPTHoldersHandlerTest, NonStringLedgerHash)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{
                "mpt_issuance_id": "{}", 
                "ledger_hash": 123
            }})",
            MptID
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledger_hashNotString");
    });
}

TEST_F(RPCMPTHoldersHandlerTest, InvalidLedgerIndexString)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
                "mpt_issuance_id": "{}", 
                "ledger_index": "notvalidated"
            }})",
            MptID
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerIndexMalformed");
    });
}

// error case: issuer invalid format, length is incorrect
TEST_F(RPCMPTHoldersHandlerTest, MPTIDInvalidFormat)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(R"({ 
            "mpt_issuance_id": "xxx"
        })");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "mpt_issuance_idMalformed");
    });
}

// error case: issuer missing
TEST_F(RPCMPTHoldersHandlerTest, MPTIDMissing)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(R"({})");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "Required field 'mpt_issuance_id' missing");
    });
}

// error case: issuer invalid format
TEST_F(RPCMPTHoldersHandlerTest, MPTIDNotString)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(R"({ 
            "mpt_issuance_id": 12
        })");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "mpt_issuance_idNotString");
    });
}

// error case: invalid marker format
TEST_F(RPCMPTHoldersHandlerTest, MarkerInvalidFormat)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
            "mpt_issuance_id": "{}",
            "marker": "xxx"
        }})",
            MptID
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "markerMalformed");
    });
}

// error case: invalid marker type
TEST_F(RPCMPTHoldersHandlerTest, MarkerNotString)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
            "mpt_issuance_id": "{}",
            "marker": 1
        }})",
            MptID
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "markerNotString");
    });
}

// error case ledger non exist via hash
TEST_F(RPCMPTHoldersHandlerTest, NonExistLedgerViaLedgerHash)
{
    // mock fetchLedgerByHash return empty
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _))
        .WillByDefault(Return(std::optional<ripple::LedgerInfo>{}));

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}",
            "ledger_hash": "{}"
        }})",
        MptID,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger non exist via index
TEST_F(RPCMPTHoldersHandlerTest, NonExistLedgerViaLedgerStringIndex)
{
    backend->setRange(10, 30);
    // mock fetchLedgerBySequence return empty
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(std::optional<ripple::LedgerInfo>{}));
    auto const input = json::parse(fmt::format(
        R"({{ 
            "mpt_issuance_id": "{}",
            "ledger_index": "4"
        }})",
        MptID
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCMPTHoldersHandlerTest, NonExistLedgerViaLedgerIntIndex)
{
    backend->setRange(10, 30);
    // mock fetchLedgerBySequence return empty
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(std::optional<ripple::LedgerInfo>{}));
    auto const input = json::parse(fmt::format(
        R"({{ 
            "mpt_issuance_id": "{}",
            "ledger_index": 4
        }})",
        MptID
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger > max seq via hash
// idk why this case will happen in reality
TEST_F(RPCMPTHoldersHandlerTest, NonExistLedgerViaLedgerHash2)
{
    backend->setRange(10, 30);
    // mock fetchLedgerByHash return ledger but seq is 31 > 30
    auto ledgerinfo = createLedgerHeader(LedgerHash, 31);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _)).WillByDefault(Return(ledgerinfo));
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    auto const input = json::parse(fmt::format(
        R"({{ 
            "mpt_issuance_id": "{}",
            "ledger_hash": "{}"
        }})",
        MptID,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger > max seq via index
TEST_F(RPCMPTHoldersHandlerTest, NonExistLedgerViaLedgerIndex2)
{
    backend->setRange(10, 30);
    // no need to check from db,call fetchLedgerBySequence 0 time
    // differ from previous logic
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(0);
    auto const input = json::parse(fmt::format(
        R"({{ 
            "mpt_issuance_id": "{}",
            "ledger_index": "31"
        }})",
        MptID
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{MPTHoldersHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// normal case when MPT does not exist
TEST_F(RPCMPTHoldersHandlerTest, MPTNotFound)
{
    backend->setRange(10, 30);
    auto ledgerinfo = createLedgerHeader(LedgerHash, 30);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _)).WillByDefault(Return(ledgerinfo));
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    ON_CALL(*backend, doFetchLedgerObject).WillByDefault(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObject).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}",
            "ledger_hash": "{}"
        }})",
        MptID,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "objectNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "objectNotFound");
    });
}

// normal case when mpt has one holder
TEST_F(RPCMPTHoldersHandlerTest, DefaultParameters)
{
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":50,
        "ledger_index": 30,
        "mptokens": [{}],
        "validated": true
    }})",
        MptID,
        MptOut1
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerInfo));
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken = createMpTokenObject(HoldeR1Account, ripple::uint192(MptID));
    std::vector<Blob> const mpts = {mptoken.getSerializer().peekData()};
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, {}}));
    EXPECT_CALL(
        *backend, fetchMPTHolders(ripple::uint192(MptID), testing::_, testing::Eq(std::nullopt), Const(30), testing::_)
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}"
        }})",
        MptID
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCMPTHoldersHandlerTest, CustomAmounts)
{
    // it's not possible to have locked_amount to be greater than mpt_amount,
    // we are simply testing the response parsing of the api
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":50,
        "ledger_index": 30,
        "mptokens": [{{
            "account": "rrnAZCqMahreZrKMcZU3t2DZ6yUndT4ubN",
            "flags": 0,
            "mpt_amount": "0",
            "mptoken_index": "D137F2E5A5767A06CB7A8F060ADE442A30CFF95028E1AF4B8767E3A56877205A"
        }}],
        "validated": true
    }})",
        MptID
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerInfo));
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken = createMpTokenObject(HoldeR1Account, ripple::uint192(MptID), 0);
    std::vector<Blob> const mpts = {mptoken.getSerializer().peekData()};
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, {}}));
    EXPECT_CALL(
        *backend, fetchMPTHolders(ripple::uint192(MptID), testing::_, testing::Eq(std::nullopt), Const(30), testing::_)
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}"
        }})",
        MptID
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCMPTHoldersHandlerTest, SpecificLedgerIndex)
{
    auto const specificLedger = 20;
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":50,
        "ledger_index": {},
        "mptokens": [{}],
        "validated": true
    }})",
        MptID,
        specificLedger,
        MptOut1
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, specificLedger);
    ON_CALL(*backend, fetchLedgerBySequence(specificLedger, _)).WillByDefault(Return(ledgerInfo));
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, specificLedger, _))
        .WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken = createMpTokenObject(HoldeR1Account, ripple::uint192(MptID));
    std::vector<Blob> const mpts = {mptoken.getSerializer().peekData()};
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, {}}));
    EXPECT_CALL(
        *backend,
        fetchMPTHolders(
            ripple::uint192(MptID), testing::_, testing::Eq(std::nullopt), Const(specificLedger), testing::_
        )
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}",
            "ledger_index": {}
        }})",
        MptID,
        specificLedger
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCMPTHoldersHandlerTest, MarkerParameter)
{
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":50,
        "ledger_index": 30,
        "mptokens": [{}],
        "validated": true,
        "marker": "{}"
    }})",
        MptID,
        MptOut2,
        ripple::strHex(getAccountIdWithString(HoldeR1Account))
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerInfo));
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken = createMpTokenObject(HoldeR2Account, ripple::uint192(MptID));
    std::vector<Blob> const mpts = {mptoken.getSerializer().peekData()};
    auto const marker = getAccountIdWithString(HoldeR1Account);
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, marker}));
    EXPECT_CALL(
        *backend, fetchMPTHolders(ripple::uint192(MptID), testing::_, testing::Eq(marker), Const(30), testing::_)
    )
        .Times(1);

    auto const Holder1AccountID = ripple::strHex(getAccountIdWithString(HoldeR1Account));
    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}",
            "marker": "{}"
        }})",
        MptID,
        Holder1AccountID
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCMPTHoldersHandlerTest, MultipleMPTs)
{
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":50,
        "ledger_index": 30,
        "mptokens": [{}, {}],
        "validated": true
    }})",
        MptID,
        MptOut1,
        MptOut2
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerInfo));
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken1 = createMpTokenObject(HoldeR1Account, ripple::uint192(MptID));
    auto const mptoken2 = createMpTokenObject(HoldeR2Account, ripple::uint192(MptID));
    std::vector<Blob> const mpts = {mptoken1.getSerializer().peekData(), mptoken2.getSerializer().peekData()};
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, {}}));
    EXPECT_CALL(
        *backend, fetchMPTHolders(ripple::uint192(MptID), testing::_, testing::Eq(std::nullopt), Const(30), testing::_)
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}"
        }})",
        MptID
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCMPTHoldersHandlerTest, LimitMoreThanMAx)
{
    auto const currentOutput = fmt::format(
        R"({{
        "mpt_issuance_id": "{}",
        "limit":100,
        "ledger_index": 30,
        "mptokens": [{}],
        "validated": true
    }})",
        MptID,
        MptOut1
    );

    backend->setRange(10, 30);
    auto ledgerInfo = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerInfo));
    auto const issuanceKk = ripple::keylet::mptIssuance(ripple::uint192(MptID)).key;
    ON_CALL(*backend, doFetchLedgerObject(issuanceKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    auto const mptoken = createMpTokenObject(HoldeR1Account, ripple::uint192(MptID));
    std::vector<Blob> const mpts = {mptoken.getSerializer().peekData()};
    ON_CALL(*backend, fetchMPTHolders).WillByDefault(Return(MPTHoldersAndCursor{mpts, {}}));
    EXPECT_CALL(
        *backend,
        fetchMPTHolders(
            ripple::uint192(MptID), Const(MPTHoldersHandler::limitMax), testing::Eq(std::nullopt), Const(30), testing::_
        )
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "mpt_issuance_id": "{}",
            "limit": {}
        }})",
        MptID,
        MPTHoldersHandler::limitMax + 1
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{MPTHoldersHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}
