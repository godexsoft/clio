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
#include "etl/ETLHelpers.hpp"
#include "etl/impl/GrpcSource.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/impl/GrpcSource.hpp"
#include "etlng/impl/Loading.hpp"
#include "util/Assert.hpp"
#include "util/LoggerFixtures.hpp"
#include "util/MockBackend.hpp"
#include "util/MockPrometheus.hpp"
#include "util/MockXrpLedgerAPIService.hpp"
#include "util/TestObject.hpp"
#include "util/config/Config.hpp"

#include <gmock/gmock.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <gtest/gtest.h>
#include <org/xrpl/rpc/v1/get_ledger.pb.h>
#include <org/xrpl/rpc/v1/get_ledger_data.pb.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace etl::impl;

struct MockLoadObserver : etlng::InitialLoadObserverInterface {
    MOCK_METHOD(
        void,
        onInitialLoadGotMoreObjects,
        (uint32_t, std::vector<etlng::model::Object> const&, std::string),
        (override)
    );
};

struct GrpcSourceNgTests : NoLoggerFixture, util::prometheus::WithPrometheus, tests::util::WithMockXrpLedgerAPIService {
    GrpcSourceNgTests()
        : WithMockXrpLedgerAPIService("localhost:0"), grpcSource_("localhost", std::to_string(getXRPLMockPort()))
    {
    }

    testing::StrictMock<MockLoadObserver> loader_;
    testing::StrictMock<etlng::impl::GrpcSource> grpcSource_;
};

TEST_F(GrpcSourceNgTests, fetchLedger)
{
    uint32_t const sequence = 123;
    bool const getObjects = true;
    bool const getObjectNeighbors = false;

    EXPECT_CALL(mockXrpLedgerAPIService, GetLedger)
        .WillOnce([&](grpc::ServerContext* /*context*/,
                      org::xrpl::rpc::v1::GetLedgerRequest const* request,
                      org::xrpl::rpc::v1::GetLedgerResponse* response) {
            EXPECT_EQ(request->ledger().sequence(), sequence);
            EXPECT_TRUE(request->transactions());
            EXPECT_TRUE(request->expand());
            EXPECT_EQ(request->get_objects(), getObjects);
            EXPECT_EQ(request->get_object_neighbors(), getObjectNeighbors);
            EXPECT_EQ(request->user(), "ETL");
            response->set_validated(true);
            response->set_is_unlimited(false);
            response->set_object_neighbors_included(false);
            return grpc::Status{};
        });
    auto const [status, response] = grpcSource_.fetchLedger(sequence, getObjects, getObjectNeighbors);
    ASSERT_TRUE(status.ok());
    EXPECT_TRUE(response.validated());
    EXPECT_FALSE(response.is_unlimited());
    EXPECT_FALSE(response.object_neighbors_included());
}

struct GrpcSourceLoadInitialLedgerTests : GrpcSourceNgTests {
    uint32_t const sequence_ = 123;
    uint32_t const numMarkers_ = 4;
    bool const cacheOnly_ = false;
};

TEST_F(GrpcSourceLoadInitialLedgerTests, GetLedgerDataFailed)
{
    EXPECT_CALL(mockXrpLedgerAPIService, GetLedgerData)
        .Times(numMarkers_)
        .WillRepeatedly([&](grpc::ServerContext* /*context*/,
                            org::xrpl::rpc::v1::GetLedgerDataRequest const* request,
                            org::xrpl::rpc::v1::GetLedgerDataResponse* /*response*/) {
            EXPECT_EQ(request->ledger().sequence(), sequence_);
            EXPECT_EQ(request->user(), "ETL");
            return grpc::Status{grpc::StatusCode::NOT_FOUND, "Not found"};
        });

    auto const [data, success] = grpcSource_.loadInitialLedger(sequence_, numMarkers_, loader_);
    EXPECT_TRUE(data.empty());
    EXPECT_FALSE(success);
}

TEST_F(GrpcSourceLoadInitialLedgerTests, worksFine)
{
    auto const key = ripple::uint256{4};
    std::string const keyStr{reinterpret_cast<char const*>(key.data()), ripple::uint256::size()};
    auto const object = CreateTicketLedgerObject("rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", sequence_);
    auto const objectData = object.getSerializer().peekData();

    EXPECT_CALL(mockXrpLedgerAPIService, GetLedgerData)
        .Times(numMarkers_)
        .WillRepeatedly([&](grpc::ServerContext* /*context*/,
                            org::xrpl::rpc::v1::GetLedgerDataRequest const* request,
                            org::xrpl::rpc::v1::GetLedgerDataResponse* response) {
            EXPECT_EQ(request->ledger().sequence(), sequence_);
            EXPECT_EQ(request->user(), "ETL");

            response->set_is_unlimited(true);
            auto newObject = response->mutable_ledger_objects()->add_objects();
            newObject->set_key(reinterpret_cast<char const*>(key.data()), ripple::uint256::size());
            newObject->set_data(objectData.data(), objectData.size());

            return grpc::Status{};
        });

    EXPECT_CALL(loader_, onInitialLoadGotMoreObjects)
        .Times(numMarkers_)
        .WillRepeatedly([&](uint32_t, std::vector<etlng::model::Object> const& data, std::string lastKey) {
            EXPECT_TRUE(lastKey.empty());
            EXPECT_EQ(data.size(), 1);
        });

    auto const [data, success] = grpcSource_.loadInitialLedger(sequence_, numMarkers_, loader_);

    EXPECT_TRUE(success);
    EXPECT_EQ(data.size(), numMarkers_);

    EXPECT_EQ(data, std::vector<std::string>(4, keyStr));
}

TEST_F(GrpcSourceLoadInitialLedgerTests, worksFine2)
{
    auto const totalKeys = 256uz;
    auto const markers = etl::getMarkers(numMarkers_);
    auto const totalPerMarker = totalKeys / numMarkers_;
    auto const batchSize = totalPerMarker / 32uz;
    auto const batchesPerMarker = totalPerMarker / batchSize;

    auto allKeys = etl::getMarkers(totalKeys);
    auto keysFor = std::map<ripple::uint256, std::queue<ripple::uint256>>();

    for (auto mi = 0uz; mi < markers.size(); ++mi) {
        for (auto i = 0uz; i < totalPerMarker; ++i) {
            keysFor[markers.at(mi)].push(allKeys.at(mi * totalPerMarker + i));
        }
    }

    std::mutex mtx;
    auto nextKey = [&mtx, &keysFor](auto const& marker) {
        std::scoped_lock lock(mtx);

        auto& k = keysFor.at(ripple::uint256(marker));
        ASSERT(not k.empty(), "Can't be empty");

        auto t = k.front();
        k.pop();

        return t;
    };
    auto moreAvailable = [&mtx, &keysFor](auto const& marker) {
        std::scoped_lock lock(mtx);
        return not keysFor.at(ripple::uint256(marker)).empty();
    };

    auto const object = CreateTicketLedgerObject("rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", sequence_);
    auto const objectData = object.getSerializer().peekData();

    for (auto [k, q] : keysFor) {
        std::cout << "{" << ripple::strHex(k) << "} " << q.size() << '\n';

        while (not q.empty()) {
            std::cout << "{" << ripple::strHex(k) << "} " << ripple::strHex(q.front()) << '\n';
            q.pop();
        }
    }

    EXPECT_CALL(mockXrpLedgerAPIService, GetLedgerData)
        .Times(numMarkers_ * batchesPerMarker)
        .WillRepeatedly([&](grpc::ServerContext* /*context*/,
                            org::xrpl::rpc::v1::GetLedgerDataRequest const* request,
                            org::xrpl::rpc::v1::GetLedgerDataResponse* response) {
            EXPECT_EQ(request->ledger().sequence(), sequence_);
            EXPECT_EQ(request->user(), "ETL");

            response->set_is_unlimited(true);
            std::cout << "called GetLedgerData for " << ripple::strHex(request->marker()) << '\n';
            auto lastKey = data::firstKey;

            for (auto i = 0uz; i < batchSize; ++i) {
                auto const key = nextKey(request->marker());
                lastKey = key;

                auto newObject = response->mutable_ledger_objects()->add_objects();
                newObject->set_key(reinterpret_cast<char const*>(key.data()), ripple::uint256::size());
                newObject->set_data(objectData.data(), objectData.size());
                std::cout << " - add object with key " << ripple::strHex(key) << '\n';
            }
            if (moreAvailable(request->marker()))
                response->set_marker(ripple::strHex(lastKey));

            return grpc::Status{};
        });

    std::atomic_int callCount = 0;
    EXPECT_CALL(loader_, onInitialLoadGotMoreObjects)
        .Times(numMarkers_ * batchesPerMarker)
        .WillRepeatedly([&](uint32_t, std::vector<etlng::model::Object> const& data, std::string lastKey) {
            ++callCount;
            if (callCount > 4) {
                EXPECT_FALSE(lastKey.empty());
            } else {
                EXPECT_TRUE(lastKey.empty());
            }

            std::cout << "called onInitialLoadGotMoreObjects with " << data.size() << " batch\n";
            EXPECT_EQ(data.size(), batchSize);
        });
    auto const [data, success] = grpcSource_.loadInitialLedger(sequence_, numMarkers_, loader_);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    EXPECT_TRUE(success);
    EXPECT_EQ(data.size(), numMarkers_);
}
