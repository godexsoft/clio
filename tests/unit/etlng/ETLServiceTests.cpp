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
#include "etl/SystemState.hpp"
#include "etlng/CacheLoaderInterface.hpp"
#include "etlng/CacheUpdaterInterface.hpp"
#include "etlng/ETLService.hpp"
#include "etlng/ExtractorInterface.hpp"
#include "etlng/InitialLoadObserverInterface.hpp"
#include "etlng/LedgerPublisherInterface.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/MonitorInterface.hpp"
#include "etlng/TaskManagerInterface.hpp"
#include "etlng/TaskManagerProviderInterface.hpp"
#include "util/MockBackendTestFixture.hpp"
#include "util/MockLedgerPublisher.hpp"
#include "util/MockLoadBalancer.hpp"
#include "util/MockNetworkValidatedLedgers.hpp"
#include "util/MockPrometheus.hpp"
#include "util/MockSubscriptionManager.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/context/BasicExecutionContext.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/config/ConfigValue.hpp"
#include "util/config/Types.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/json/object.hpp>
#include <boost/signals2/connection.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace util::config;

class MockMonitor : public etlng::MonitorInterface {
public:
    MOCK_METHOD(void, notifyLedgerLoaded, (uint32_t), (override));
    MOCK_METHOD(boost::signals2::scoped_connection, subscribe, (SignalType::slot_type const&), (override));
    MOCK_METHOD(void, run, (std::chrono::steady_clock::duration), (override));
    MOCK_METHOD(void, stop, (), (override));
};

struct MockExtractor : etlng::ExtractorInterface {
    MOCK_METHOD(std::optional<etlng::model::LedgerData>, extractLedgerWithDiff, (uint32_t), (override));
    MOCK_METHOD(std::optional<etlng::model::LedgerData>, extractLedgerOnly, (uint32_t), (override));
};

struct MockLoader : etlng::LoaderInterface {
    MOCK_METHOD(void, load, (etlng::model::LedgerData const&), (override));
    MOCK_METHOD(std::optional<ripple::LedgerHeader>, loadInitialLedger, (etlng::model::LedgerData const&), (override));
};

struct MockCacheLoader : etlng::CacheLoaderInterface {
    MOCK_METHOD(void, load, (uint32_t), (override));
    MOCK_METHOD(void, stop, (), (noexcept, override));
    MOCK_METHOD(void, wait, (), (noexcept, override));
};

struct MockCacheUpdater : etlng::CacheUpdaterInterface {
    MOCK_METHOD(void, update, (etlng::model::LedgerData const&), (override));
    MOCK_METHOD(void, update, (uint32_t, std::vector<data::LedgerObject> const&), (override));
    MOCK_METHOD(void, update, (uint32_t, std::vector<etlng::model::Object> const&), (override));
    MOCK_METHOD(void, setFull, (), (override));
};

struct MockInitialLoadObserver : etlng::InitialLoadObserverInterface {
    MOCK_METHOD(
        void,
        onInitialLoadGotMoreObjects,
        (uint32_t, std::vector<etlng::model::Object> const&, std::optional<std::string>),
        (override)
    );
};

struct MockTaskManager : etlng::TaskManagerInterface {
    MOCK_METHOD(void, run, (etlng::TaskManagerInterface::Settings), (override));
    MOCK_METHOD(void, stop, (), (override));
};

struct MockTaskManagerProvider : etlng::TaskManagerProviderInterface {
    MOCK_METHOD(
        std::unique_ptr<etlng::TaskManagerInterface>,
        make,
        (util::async::AnyExecutionContext, std::reference_wrapper<etlng::MonitorInterface>, uint32_t),
        (override)
    );
};

struct ETLServiceTests : util::prometheus::WithPrometheus, MockBackendTest {
    util::async::CoroExecutionContext ctx{2};
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
    std::shared_ptr<MockLedgerPublisher> publisher = std::make_shared<MockLedgerPublisher>();
    std::shared_ptr<MockCacheLoader> cacheLoader = std::make_shared<MockCacheLoader>();
    std::shared_ptr<MockCacheUpdater> cacheUpdater = std::make_shared<MockCacheUpdater>();
    std::shared_ptr<MockExtractor> extractor = std::make_shared<MockExtractor>();
    std::shared_ptr<MockLoader> loader = std::make_shared<MockLoader>();
    std::shared_ptr<MockInitialLoadObserver> initialLoadObserver = std::make_shared<MockInitialLoadObserver>();
    std::shared_ptr<MockTaskManagerProvider> taskManagerProvider = std::make_shared<MockTaskManagerProvider>();
    std::shared_ptr<etl::SystemState> systemState = std::make_shared<etl::SystemState>();

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
    EXPECT_CALL(*publisher, lastPublishAgeSeconds()).WillRepeatedly(testing::Return(0));

    etlng::ETLService service(
        ctx,
        config,
        backend_,
        balancer,
        ledgers,
        publisher,
        cacheLoader,
        cacheUpdater,
        extractor,
        loader,
        initialLoadObserver,
        taskManagerProvider,
        systemState
    );
    auto result = service.getInfo();

    EXPECT_TRUE(result.contains("etl_sources"));
    EXPECT_TRUE(result.contains("is_writer"));
    EXPECT_TRUE(result.contains("read_only"));
}

TEST_F(ETLServiceTests, IsAmendmentBlocked)
{
    etlng::ETLService service(
        ctx,
        config,
        backend_,
        balancer,
        ledgers,
        publisher,
        cacheLoader,
        cacheUpdater,
        extractor,
        loader,
        initialLoadObserver,
        taskManagerProvider,
        systemState
    );
    EXPECT_FALSE(service.isAmendmentBlocked());
}

TEST_F(ETLServiceTests, IsCorruptionDetected)
{
    etlng::ETLService service(
        ctx,
        config,
        backend_,
        balancer,
        ledgers,
        publisher,
        cacheLoader,
        cacheUpdater,
        extractor,
        loader,
        initialLoadObserver,
        taskManagerProvider,
        systemState
    );
    EXPECT_FALSE(service.isCorruptionDetected());
}

TEST_F(ETLServiceTests, GetETLState)
{
    EXPECT_CALL(*balancer, getETLState()).WillOnce(testing::Return(etl::ETLState{}));

    etlng::ETLService service(
        ctx,
        config,
        backend_,
        balancer,
        ledgers,
        publisher,
        cacheLoader,
        cacheUpdater,
        extractor,
        loader,
        initialLoadObserver,
        taskManagerProvider,
        systemState
    );
    auto result = service.getETLState();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ETLServiceTests, LastCloseAgeSeconds)
{
    EXPECT_CALL(*publisher, lastCloseAgeSeconds()).WillOnce(testing::Return(10));

    etlng::ETLService service(
        ctx,
        config,
        backend_,
        balancer,
        ledgers,
        publisher,
        cacheLoader,
        cacheUpdater,
        extractor,
        loader,
        initialLoadObserver,
        taskManagerProvider,
        systemState
    );
    auto result = service.lastCloseAgeSeconds();
    EXPECT_GE(result, 0);
}
