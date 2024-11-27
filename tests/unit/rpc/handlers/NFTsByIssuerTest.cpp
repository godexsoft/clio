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
#include "rpc/handlers/NFTsByIssuer.hpp"
#include "util/HandlerBaseTestFixture.hpp"
#include "util/TestObject.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/json/parse.hpp>
#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

namespace {

constexpr auto Account = "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67";
constexpr auto LedgerHash = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr auto NftID1 = "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F0000099B00000000";  // taxon 0
constexpr auto NftID2 = "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F16E5DA9C00000001";  // taxon 0
constexpr auto NftID3 = "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F5B974D9E00000004";  // taxon 1

std::string const Nft1Out =
    R"({
        "nft_id": "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F0000099B00000000",
        "ledger_index": 29,
        "owner": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "is_burned": false,
        "uri": "757269",
        "flags": 8,
        "transfer_fee": 0,
        "issuer": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "nft_taxon": 0,
        "nft_serial": 0
    })";
std::string const Nft2Out =
    R"({
        "nft_id": "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F16E5DA9C00000001",
        "ledger_index": 29,
        "owner": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "is_burned": false,
        "uri": "757269",
        "flags": 8,
        "transfer_fee": 0,
        "issuer": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "nft_taxon": 0,
        "nft_serial": 1
    })";
std::string const Nft3Out =
    R"({
        "nft_id": "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F5B974D9E00000004",
        "ledger_index": 29,
        "owner": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "is_burned": false,
        "uri": "757269",
        "flags": 8,
        "transfer_fee": 0,
        "issuer": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
        "nft_taxon": 1,
        "nft_serial": 4
    })";

}  // namespace

class RPCNFTsByIssuerHandlerTest : public HandlerBaseTest {};

TEST_F(RPCNFTsByIssuerHandlerTest, NonHexLedgerHash)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
                "issuer": "{}", 
                "ledger_hash": "xxx"
            }})",
            Account
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledger_hashMalformed");
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, NonStringLedgerHash)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{
                "issuer": "{}", 
                "ledger_hash": 123
            }})",
            Account
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledger_hashNotString");
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, InvalidLedgerIndexString)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(fmt::format(
            R"({{ 
                "issuer": "{}", 
                "ledger_index": "notvalidated"
            }})",
            Account
        ));
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerIndexMalformed");
    });
}

// error case: issuer invalid format, length is incorrect
TEST_F(RPCNFTsByIssuerHandlerTest, NFTIssuerInvalidFormat)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(R"({ 
            "issuer": "xxx"
        })");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actMalformed");
        EXPECT_EQ(err.at("error_message").as_string(), "issuerMalformed");
    });
}

// error case: issuer missing
TEST_F(RPCNFTsByIssuerHandlerTest, NFTIssuerMissing)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(R"({})");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "Required field 'issuer' missing");
    });
}

// error case: issuer invalid format
TEST_F(RPCNFTsByIssuerHandlerTest, NFTIssuerNotString)
{
    runSpawn([this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const input = json::parse(R"({ 
            "issuer": 12
        })");
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "invalidParams");
        EXPECT_EQ(err.at("error_message").as_string(), "issuerNotString");
    });
}

// error case ledger non exist via hash
TEST_F(RPCNFTsByIssuerHandlerTest, NonExistLedgerViaLedgerHash)
{
    // mock fetchLedgerByHash return empty
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _))
        .WillByDefault(Return(std::optional<ripple::LedgerHeader>{}));

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "ledger_hash": "{}"
        }})",
        Account,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);

        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger non exist via index
TEST_F(RPCNFTsByIssuerHandlerTest, NonExistLedgerViaLedgerStringIndex)
{
    backend->setRange(10, 30);
    // mock fetchLedgerBySequence return empty
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(std::optional<ripple::LedgerHeader>{}));
    auto const input = json::parse(fmt::format(
        R"({{ 
            "issuer": "{}",
            "ledger_index": "4"
        }})",
        Account
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, NonExistLedgerViaLedgerIntIndex)
{
    backend->setRange(10, 30);
    // mock fetchLedgerBySequence return empty
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(std::optional<ripple::LedgerHeader>{}));
    auto const input = json::parse(fmt::format(
        R"({{ 
            "issuer": "{}",
            "ledger_index": 4
        }})",
        Account
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger > max seq via hash
// idk why this case will happen in reality
TEST_F(RPCNFTsByIssuerHandlerTest, NonExistLedgerViaLedgerHash2)
{
    backend->setRange(10, 30);
    // mock fetchLedgerByHash return ledger but seq is 31 > 30
    auto ledgerHeader = createLedgerHeader(LedgerHash, 31);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _)).WillByDefault(Return(ledgerHeader));
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    auto const input = json::parse(fmt::format(
        R"({{ 
            "issuer": "{}",
            "ledger_hash": "{}"
        }})",
        Account,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// error case ledger > max seq via index
TEST_F(RPCNFTsByIssuerHandlerTest, NonExistLedgerViaLedgerIndex2)
{
    backend->setRange(10, 30);
    // no need to check from db,call fetchLedgerBySequence 0 time
    // differ from previous logic
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(0);
    auto const input = json::parse(fmt::format(
        R"({{ 
            "issuer": "{}",
            "ledger_index": "31"
        }})",
        Account
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto const handler = AnyHandler{NFTsByIssuerHandler{backend}};
        auto const output = handler.process(input, Context{std::ref(yield)});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "lgrNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "ledgerNotFound");
    });
}

// normal case when issuer does not exist or has no NFTs
TEST_F(RPCNFTsByIssuerHandlerTest, AccountNotFound)
{
    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    ON_CALL(*backend, fetchLedgerByHash(ripple::uint256{LedgerHash}, _)).WillByDefault(Return(ledgerHeader));
    EXPECT_CALL(*backend, fetchLedgerByHash).Times(1);
    ON_CALL(*backend, doFetchLedgerObject).WillByDefault(Return(std::optional<Blob>{}));
    EXPECT_CALL(*backend, doFetchLedgerObject).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "ledger_hash": "{}"
        }})",
        Account,
        LedgerHash
    ));
    runSpawn([&, this](boost::asio::yield_context yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.result.error());
        EXPECT_EQ(err.at("error").as_string(), "actNotFound");
        EXPECT_EQ(err.at("error_message").as_string(), "accountNotFound");
    });
}

// normal case when issuer has a single nft
TEST_F(RPCNFTsByIssuerHandlerTest, DefaultParameters)
{
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit": 50,
            "ledger_index": 30,
            "nfts": [{}],
            "validated": true
        }})",
        Account,
        Nft1Out
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {createNft(NftID1, Account, 29)};
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, {}}));
    EXPECT_CALL(*backend, fetchNFTsByIssuer(account, Eq(std::nullopt), Const(30), _, Eq(std::nullopt), _)).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}"
        }})",
        Account
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, SpecificLedgerIndex)
{
    auto const specificLedger = 20;
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit": 50,
            "ledger_index": {},
            "nfts": [{{
                "nft_id": "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F0000099B00000000",
                "ledger_index": 20,
                "owner": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
                "is_burned": false,
                "uri": "757269",
                "flags": 8,
                "transfer_fee": 0,
                "issuer": "r4X6JLsBfhNK4UnquNkCxhVHKPkvbQff67",
                "nft_taxon": 0,
                "nft_serial": 0
            }}],
            "validated": true
        }})",
        Account,
        specificLedger
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, specificLedger);
    ON_CALL(*backend, fetchLedgerBySequence(specificLedger, _)).WillByDefault(Return(ledgerHeader));
    EXPECT_CALL(*backend, fetchLedgerBySequence).Times(1);
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, specificLedger, _))
        .WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {createNft(NftID1, Account, specificLedger)};
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, {}}));
    EXPECT_CALL(*backend, fetchNFTsByIssuer(account, Eq(std::nullopt), Const(specificLedger), _, Eq(std::nullopt), _))
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "ledger_index": {}
        }})",
        Account,
        specificLedger
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, TaxonParameter)
{
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit": 50,
            "ledger_index": 30,
            "nfts": [{}],
            "validated": true,
            "nft_taxon": 0
        }})",
        Account,
        Nft1Out
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {createNft(NftID1, Account, 29)};
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, {}}));
    EXPECT_CALL(*backend, fetchNFTsByIssuer(account, Optional(0), Const(30), _, Eq(std::nullopt), _)).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "nft_taxon": 0
        }})",
        Account
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, MarkerParameter)
{
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit": 50,
            "ledger_index": 30,
            "nfts": [{}],
            "validated": true,
            "marker": "00080000EC28C2910FD1C454A51598AAB91C8876286B2E7F5B974D9E00000004"
        }})",
        Account,
        Nft3Out
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {createNft(NftID3, Account, 29)};
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, ripple::uint256{NftID3}}));
    EXPECT_CALL(*backend, fetchNFTsByIssuer(account, _, Const(30), _, Eq(ripple::uint256{NftID1}), _)).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "marker": "{}"
        }})",
        Account,
        NftID1
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, MultipleNFTs)
{
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit":50,
            "ledger_index": 30,
            "nfts": [{}, {}, {}],
            "validated": true
        }})",
        Account,
        Nft1Out,
        Nft2Out,
        Nft3Out
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {
        createNft(NftID1, Account, 29), createNft(NftID2, Account, 29), createNft(NftID3, Account, 29)
    };
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, {}}));
    EXPECT_CALL(*backend, fetchNFTsByIssuer(account, Eq(std::nullopt), Const(30), _, Eq(std::nullopt), _)).Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}"
        }})",
        Account
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}

TEST_F(RPCNFTsByIssuerHandlerTest, LimitMoreThanMax)
{
    auto const currentOutput = fmt::format(
        R"({{
            "issuer": "{}",
            "limit": 100,
            "ledger_index": 30,
            "nfts": [{}],
            "validated": true
        }})",
        Account,
        Nft1Out
    );

    backend->setRange(10, 30);
    auto ledgerHeader = createLedgerHeader(LedgerHash, 30);
    EXPECT_CALL(*backend, fetchLedgerBySequence).WillOnce(Return(ledgerHeader));
    auto const accountKk = ripple::keylet::account(getAccountIdWithString(Account)).key;
    ON_CALL(*backend, doFetchLedgerObject(accountKk, 30, _)).WillByDefault(Return(Blob{'f', 'a', 'k', 'e'}));

    std::vector<NFT> const nfts = {createNft(NftID1, Account, 29)};
    auto const account = getAccountIdWithString(Account);
    ON_CALL(*backend, fetchNFTsByIssuer).WillByDefault(Return(NFTsAndCursor{nfts, {}}));
    EXPECT_CALL(
        *backend,
        fetchNFTsByIssuer(account, Eq(std::nullopt), Const(30), NFTsByIssuerHandler::limitMax, Eq(std::nullopt), _)
    )
        .Times(1);

    auto const input = json::parse(fmt::format(
        R"({{
            "issuer": "{}",
            "limit": {}
        }})",
        Account,
        NFTsByIssuerHandler::limitMax + 1
    ));
    runSpawn([&, this](auto& yield) {
        auto handler = AnyHandler{NFTsByIssuerHandler{this->backend}};
        auto const output = handler.process(input, Context{yield});
        ASSERT_TRUE(output);
        EXPECT_EQ(json::parse(currentOutput), *output.result);
    });
}
