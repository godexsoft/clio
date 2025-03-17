//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

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

#include "data/DBHelpers.hpp"
#include "data/Types.hpp"
#include "etlng/Models.hpp"
#include "etlng/impl/ext/Successor.hpp"
#include "util/BinaryTestObject.hpp"
#include "util/MockAssert.hpp"
#include "util/MockBackendTestFixture.hpp"
#include "util/MockLedgerCache.hpp"
#include "util/MockPrometheus.hpp"
#include "util/TestObject.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace etlng::impl;
using namespace data;

namespace {
constinit auto const kSEQ = 123u;
constinit auto const kLEDGER_HASH = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";

[[maybe_unused]] auto
createInitialTestData()
{
    auto transactions = std::vector{
        util::createTransaction(ripple::TxType::ttNFTOKEN_BURN),
        util::createTransaction(ripple::TxType::ttNFTOKEN_BURN),
        util::createTransaction(ripple::TxType::ttNFTOKEN_CREATE_OFFER),
    };

    auto const header = createLedgerHeader(kLEDGER_HASH, kSEQ);
    return etlng::model::LedgerData{
        .transactions = std::move(transactions),
        .objects = {},  // expected to be empty for onInitialData
        .successors = {},
        .edgeKeys = {{
            ripple::to_string(data::kFIRST_KEY),
            ripple::to_string(data::kLAST_KEY),
        }},  // must have some values
        .header = header,
        .rawHeader = {},
        .seq = kSEQ
    };
}

auto
createTestData(std::vector<etlng::model::Object> objects)
{
    auto transactions = std::vector{
        util::createTransaction(ripple::TxType::ttNFTOKEN_BURN),
        util::createTransaction(ripple::TxType::ttNFTOKEN_BURN),
        util::createTransaction(ripple::TxType::ttNFTOKEN_CREATE_OFFER),
    };

    auto const header = createLedgerHeader(kLEDGER_HASH, kSEQ);
    return etlng::model::LedgerData{
        .transactions = std::move(transactions),
        .objects = std::move(objects),
        .successors = {},
        .edgeKeys = {},
        .header = header,
        .rawHeader = {},
        .seq = kSEQ
    };
}

}  // namespace

struct SuccessorExtTests : util::prometheus::WithPrometheus, MockBackendTest {
protected:
    MockLedgerCache cache_;
    etlng::impl::SuccessorExt ext_{backend_, cache_};
};

TEST_F(SuccessorExtTests, OnLedgerDataLogicErrorIfCacheIsNotFullButSuccessorsNotPresent)
{
    auto const data = createTestData({});

    EXPECT_CALL(cache_, isFull()).WillRepeatedly(testing::Return(false));
    EXPECT_CALL(cache_, latestLedgerSequence()).WillRepeatedly(testing::Return(kSEQ));

    EXPECT_THROW(ext_.onLedgerData(data), std::logic_error);
}

TEST_F(SuccessorExtTests, OnLedgerDataLogicErrorIfCacheIsFullButLatestSeqDiffersAndSuccessorsNotPresent)
{
    auto const data = createTestData({});

    EXPECT_CALL(cache_, isFull()).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(cache_, latestLedgerSequence()).WillRepeatedly(testing::Return(kSEQ - 1));

    EXPECT_THROW(ext_.onLedgerData(data), std::logic_error);
}

TEST_F(SuccessorExtTests, OnLedgerDataWithDeletedObject)
{
    using namespace etlng::model;

    auto const objKey = "B00AA769C00726371689ED66A7CF57C2502F1BF4BDFF2ACADF67A2A7B5E8960D";
    auto const deletedObj = util::createObject(Object::ModType::Deleted, objKey);
    auto const data = createTestData({
        deletedObj,
        util::createObject(Object::ModType::Modified),
    });

    EXPECT_CALL(cache_, isFull()).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(cache_, latestLedgerSequence()).WillRepeatedly(testing::Return(kSEQ));

    EXPECT_CALL(cache_, getPredecessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));
    EXPECT_CALL(cache_, getSuccessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));

    EXPECT_CALL(*backend_, writeSuccessor(uint256ToString(data::kFIRST_KEY), kSEQ, uint256ToString(data::kLAST_KEY)));
    EXPECT_CALL(cache_, getDeleted(deletedObj.key, kSEQ - 1)).WillRepeatedly(testing::Return(Blob{'0'}));

    ext_.onLedgerData(data);
}

// TEST_F(SuccessorExtTests, OnLedgerDataWithDeletedObjectAssertsIfGetDeletedIsNotInCache)
// {
//     using namespace etlng::model;

//     auto const objKey = "B00AA769C00726371689ED66A7CF57C2502F1BF4BDFF2ACADF67A2A7B5E8960D";
//     auto const deletedObj = util::createObject(Object::ModType::Deleted, objKey);
//     auto const data = createTestData({
//         deletedObj,
//         util::createObject(Object::ModType::Modified),
//     });

//     EXPECT_CALL(cache_, isFull()).WillRepeatedly(testing::Return(true));
//     EXPECT_CALL(cache_, latestLedgerSequence()).WillRepeatedly(testing::Return(kSEQ));

//     EXPECT_CALL(cache_, getPredecessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));
//     EXPECT_CALL(cache_, getSuccessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));

//     EXPECT_CALL(*backend_, writeSuccessor(uint256ToString(data::kFIRST_KEY), kSEQ,
//     uint256ToString(data::kLAST_KEY))); EXPECT_CALL(cache_, getDeleted(deletedObj.key, kSEQ -
//     1)).WillRepeatedly(testing::Return(std::nullopt));

//     // filters all objects that are not Modified - one created one deleted
//     // if data.successors has value
//     //   writeSuccessor for each data.successors
//     //   writeSuccessor for filtered objects
//     // else if data.successors no value
//     //   isFull/latestLedgerSequence check -> could logic_error here
//     //   for each filtered object:
//     //     getPredcessor/getSuccessor for key
//     //     depending on delete or not, writeSuccessor once or twice
//     //     also depending on delete, getDeleted in cache is called
//     //     checkBookBase is determined, if is true a bunch more stuff happens
//     ext_.onLedgerData(data);
// }

// TEST_F(SuccessorExtTests, OnInitialDataWritesLedgerAndTransactions)
// {
//     auto const data = createInitialTestData();
//     auto const keys = std::vector{data::kFIRST_KEY, ripple::uint256{'1'}, ripple::uint256{'2'}, data::kLAST_KEY};

//     // start with FIRST_KEY, while returns non-null
//     // backend_ writeSuccessor if FIRST_KEY
//     // if is book dir
//     //   get book base, if bookbase NOT in cache
//     //      getSuccossor from cache
//     //      writeSuccessor on backend if keys match
//     // finally writeSuccessor for prev to last key
//     EXPECT_CALL(cache_, getSuccessor(testing::_, testing::_));
//     EXPECT_CALL(*backend_, writeLedger(testing::_, auto{data.rawHeader}));
//     EXPECT_CALL(*backend_, writeAccountTransaction).Times(data.transactions.size());
//     EXPECT_CALL(*backend_, writeTransaction).Times(data.transactions.size());

//     // write edge keys
//     // getSuccessor cache for each edgeKey
//     // if in cache, writeSuccessor on backend with that key from cache

//     ext_.onInitialData(data);
// }

TEST_F(SuccessorExtTests, OnInitialObjectsWritesLedgerObject)
{
}

struct SuccessorExtAssertTests : common::util::WithMockAssert, SuccessorExtTests {};

TEST_F(SuccessorExtAssertTests, OnLedgerDataWithDeletedObjectAssertsIfGetDeletedIsNotInCache)
{
    using namespace etlng::model;

    auto const objKey = "B00AA769C00726371689ED66A7CF57C2502F1BF4BDFF2ACADF67A2A7B5E8960D";
    auto const deletedObj = util::createObject(Object::ModType::Deleted, objKey);
    auto const data = createTestData({
        deletedObj,
        util::createObject(Object::ModType::Modified),
    });

    EXPECT_CALL(cache_, isFull()).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(cache_, latestLedgerSequence()).WillRepeatedly(testing::Return(kSEQ));

    EXPECT_CALL(cache_, getPredecessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));
    EXPECT_CALL(cache_, getSuccessor(testing::_, kSEQ)).WillRepeatedly(testing::Return(std::nullopt));

    EXPECT_CALL(*backend_, writeSuccessor(uint256ToString(data::kFIRST_KEY), kSEQ, uint256ToString(data::kLAST_KEY)));
    EXPECT_CALL(cache_, getDeleted(deletedObj.key, kSEQ - 1)).WillRepeatedly(testing::Return(std::nullopt));

    // filters all objects that are not Modified - one created one deleted
    // if data.successors has value
    //   writeSuccessor for each data.successors
    //   writeSuccessor for filtered objects
    // else if data.successors no value
    //   isFull/latestLedgerSequence check -> could logic_error here
    //   for each filtered object:
    //     getPredcessor/getSuccessor for key
    //     depending on delete or not, writeSuccessor once or twice
    //     also depending on delete, getDeleted in cache is called
    //     checkBookBase is determined, if is true a bunch more stuff happens
    EXPECT_CLIO_ASSERT_FAIL({ ext_.onLedgerData(data); });
}
