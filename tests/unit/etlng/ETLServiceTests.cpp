//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025 the clio developers.

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
#include "etl/ETLState.hpp"
#include "etlng/ETLService.hpp"
#include "etlng/MonitorInterface.hpp"
#include "util/MockBackendTestFixture.hpp"
#include "util/MockLoadBalancer.hpp"
#include "util/MockNetworkValidatedLedgers.hpp"
#include "util/MockPrometheus.hpp"
#include "util/MockSubscriptionManager.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/config/ConfigValue.hpp"
#include "util/config/Types.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/json/object.hpp>
#include <boost/signals2/connection.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

using namespace util::config;

class MockMonitor : public etlng::MonitorInterface {
public:
    MOCK_METHOD(void, notifyLedgerLoaded, (uint32_t), (override));
    MOCK_METHOD(boost::signals2::scoped_connection, subscribe, (SignalType::slot_type const&), (override));
    MOCK_METHOD(void, run, (std::chrono::steady_clock::duration), (override));
    MOCK_METHOD(void, stop, (), (override));
};

struct ETLServiceTests : util::prometheus::WithPrometheus, MockBackendTest {
    boost::asio::io_context ioc;
    util::config::ClioConfigDefinition config{
        {"extractor_threads", ConfigValue{ConfigType::Integer}.defaultValue(4)},
        {"io_threads", ConfigValue{ConfigType::Integer}.defaultValue(2)},
        {"cache.num_diffs", ConfigValue{ConfigType::Integer}.defaultValue(32)},
        {"cache.num_markers", ConfigValue{ConfigType::Integer}.defaultValue(48)},
        {"cache.num_cursors_from_diff", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.num_cursors_from_account", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.page_fetch_size", ConfigValue{ConfigType::Integer}.defaultValue(512)},
        {"cache.load", ConfigValue{ConfigType::String}.defaultValue("async")}
    };
    StrictMockSubscriptionManagerSharedPtr subscriptions;
    std::shared_ptr<MockLoadBalancer> balancer = std::make_shared<MockLoadBalancer>();
    MockNetworkValidatedLedgersPtr ledgers;

    void
    setupEmptyDatabaseExpectations(uint32_t mostRecentSeq)
    {
        EXPECT_CALL(*backend_, hardFetchLedgerRange(testing::_)).WillOnce(testing::Return(std::nullopt));
        EXPECT_CALL(*ledgers, getMostRecent()).WillOnce(testing::Return(mostRecentSeq));
    }

    void
    setupPopulatedDatabaseExpectations(uint32_t maxSequence)
    {
        EXPECT_CALL(*backend_, hardFetchLedgerRange(testing::_))
            .WillOnce(testing::Return(data::LedgerRange{.minSequence = 1, .maxSequence = maxSequence}));
    }
};

TEST_F(ETLServiceTests, GetInfo)
{
    EXPECT_CALL(*balancer, toJson()).WillOnce(testing::Return(boost::json::object{{"test", "value"}}));

    etlng::ETLService service(ioc, config, backend_, subscriptions, balancer, ledgers);
    auto result = service.getInfo();

    EXPECT_TRUE(result.contains("etl_sources"));
    EXPECT_TRUE(result.contains("is_writer"));
    EXPECT_TRUE(result.contains("read_only"));
}

TEST_F(ETLServiceTests, IsAmendmentBlocked)
{
    etlng::ETLService service(ioc, config, backend_, subscriptions, balancer, ledgers);
    EXPECT_FALSE(service.isAmendmentBlocked());
}

TEST_F(ETLServiceTests, IsCorruptionDetected)
{
    etlng::ETLService service(ioc, config, backend_, subscriptions, balancer, ledgers);
    EXPECT_FALSE(service.isCorruptionDetected());
}

TEST_F(ETLServiceTests, GetETLState)
{
    EXPECT_CALL(*balancer, getETLState()).WillOnce(testing::Return(etl::ETLState{}));

    etlng::ETLService service(ioc, config, backend_, subscriptions, balancer, ledgers);
    auto result = service.getETLState();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ETLServiceTests, LastCloseAgeSeconds)
{
    etlng::ETLService service(ioc, config, backend_, subscriptions, balancer, ledgers);
    auto result = service.lastCloseAgeSeconds();
    EXPECT_GE(result, 0);
}
