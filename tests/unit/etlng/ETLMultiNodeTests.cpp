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

#include "data/Types.hpp"
#include "etl/SystemState.hpp"
#include "etlng/CacheLoaderInterface.hpp"
#include "etlng/CacheUpdaterInterface.hpp"
#include "etlng/ETLService.hpp"
#include "etlng/ExtractorInterface.hpp"
#include "etlng/InitialLoadObserverInterface.hpp"
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
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/context/BasicExecutionContext.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/config/ConfigValue.hpp"
#include "util/config/Types.hpp"

#include <boost/json/object.hpp>
#include <boost/signals2/connection.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <semaphore>
#include <string>
#include <vector>

using namespace etlng;
using namespace util::config;

namespace {

constexpr auto kSTART_SEQ = 100u;
// constexpr auto kNO_DB_UPDATE_TIMEOUT = std::chrono::milliseconds{10};

struct MockCacheLoader : etlng::CacheLoaderInterface {
    MOCK_METHOD(void, load, (uint32_t const seq), (override));
    MOCK_METHOD(void, stop, (), (noexcept, override));
    MOCK_METHOD(void, wait, (), (noexcept, override));
};
struct MockCacheUpdater : etlng::CacheUpdaterInterface {
    MOCK_METHOD(void, update, (model::LedgerData const&), (override));
    MOCK_METHOD(void, update, (uint32_t, std::vector<data::LedgerObject> const&), (override));
    MOCK_METHOD(void, update, (uint32_t, std::vector<model::Object> const&), (override));
    MOCK_METHOD(void, setFull, (), (override));
};

struct MockExtractor : etlng::ExtractorInterface {
    MOCK_METHOD(std::optional<model::LedgerData>, extractLedgerWithDiff, (uint32_t), (override));
    MOCK_METHOD(std::optional<model::LedgerData>, extractLedgerOnly, (uint32_t), (override));
};

struct MockLoader : etlng::LoaderInterface {
    using ExpectedType = std::expected<void, etlng::Error>;
    MOCK_METHOD(ExpectedType, load, (model::LedgerData const&), (override));
    MOCK_METHOD(std::optional<ripple::LedgerHeader>, loadInitialLedger, (model::LedgerData const&), (override));
};

struct MockInitialLoadObserver : etlng::InitialLoadObserverInterface {
    MOCK_METHOD(
        void,
        onInitialLoadGotMoreObjects,
        (uint32_t, std::vector<model::Object> const&, std::optional<std::string>),
        (override)
    );
};

struct MockTaskManagerProvider : etlng::TaskManagerProviderInterface {
    MOCK_METHOD(
        std::unique_ptr<TaskManagerInterface>,
        make,
        (util::async::AnyExecutionContext, std::reference_wrapper<MonitorInterface>, uint32_t),
        (override)
    );
};

struct MockMonitor : public etlng::MonitorInterface {
    MOCK_METHOD(void, notifySequenceLoaded, (uint32_t), (override));
    MOCK_METHOD(void, notifyWriteConflict, (uint32_t), (override));
    MOCK_METHOD(boost::signals2::scoped_connection, subscribe, (SignalType::slot_type const&), (override));
    MOCK_METHOD(
        boost::signals2::scoped_connection,
        subscribeToNoDbUpdate,
        (NoDbUpdateSignalType::slot_type const&),
        (override)
    );
    MOCK_METHOD(void, run, (std::chrono::steady_clock::duration), (override));
    MOCK_METHOD(void, stop, (), (override));
};

// Test fixture for multi-node ETL testing
struct ETLMultiNodeTests : util::prometheus::WithPrometheus, MockBackendTest {
    ETLMultiNodeTests()
    {
        // Set up shared components
        mockBalancer_ = std::make_shared<testing::NaggyMock<MockLoadBalancer>>();
        mockPublisher_ = std::make_shared<testing::NaggyMock<MockLedgerPublisher>>();
        mockCacheLoader_ = std::make_shared<testing::NaggyMock<MockCacheLoader>>();
        mockCacheUpdater_ = std::make_shared<testing::NaggyMock<MockCacheUpdater>>();
        mockExtractor_ = std::make_shared<testing::NaggyMock<MockExtractor>>();
        mockLoader_ = std::make_shared<testing::NaggyMock<MockLoader>>();
        mockInitialLoadObserver_ = std::make_shared<testing::NaggyMock<MockInitialLoadObserver>>();
        mockTaskManagerProvider_ = std::make_shared<testing::NaggyMock<MockTaskManagerProvider>>();

        data::LedgerRange range{.minSequence = kSTART_SEQ - 1, .maxSequence = kSTART_SEQ - 1};
        ON_CALL(*backend_, hardFetchLedgerRange(testing::_)).WillByDefault(testing::Return(range));
        ON_CALL(*mockLedgers_, getMostRecent()).WillByDefault(testing::Return(kSTART_SEQ));

        // Create system states (each ETL service needs its own state)
        state1_ = std::make_shared<etl::SystemState>();
        state2_ = std::make_shared<etl::SystemState>();
    }

    // Create two ETL services with independent task managers
    std::unique_ptr<etlng::ETLService>
    createETLService(std::shared_ptr<etl::SystemState> state, ClioConfigDefinition& config)
    {
        static auto kCTX = util::async::CoroExecutionContext(8);
        return std::make_unique<etlng::ETLService>(
            kCTX,
            config,
            backend_,
            mockBalancer_,
            mockLedgers_,
            mockPublisher_,
            mockCacheLoader_,
            mockCacheUpdater_,
            mockExtractor_,
            mockLoader_,
            mockInitialLoadObserver_,
            mockTaskManagerProvider_,
            state
        );
    }

protected:
    // Shared mocks for both ETL services
    std::shared_ptr<testing::NaggyMock<MockLoadBalancer>> mockBalancer_;
    StrictMockNetworkValidatedLedgersPtr mockLedgers_;
    std::shared_ptr<testing::NaggyMock<MockLedgerPublisher>> mockPublisher_;
    std::shared_ptr<testing::NaggyMock<MockCacheLoader>> mockCacheLoader_;
    std::shared_ptr<testing::NaggyMock<MockCacheUpdater>> mockCacheUpdater_;
    std::shared_ptr<testing::NaggyMock<MockExtractor>> mockExtractor_;
    std::shared_ptr<testing::NaggyMock<MockLoader>> mockLoader_;
    std::shared_ptr<testing::NaggyMock<MockInitialLoadObserver>> mockInitialLoadObserver_;
    std::shared_ptr<testing::NaggyMock<MockTaskManagerProvider>> mockTaskManagerProvider_;

    ClioConfigDefinition mockConfig1_{
        {"read_only", ConfigValue{ConfigType::Boolean}.defaultValue(false)},
        {"extractor_threads", ConfigValue{ConfigType::Integer}.defaultValue(4)},
        {"io_threads", ConfigValue{ConfigType::Integer}.defaultValue(2)},
        {"cache.num_diffs", ConfigValue{ConfigType::Integer}.defaultValue(32)},
        {"cache.num_markers", ConfigValue{ConfigType::Integer}.defaultValue(48)},
        {"cache.num_cursors_from_diff", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.num_cursors_from_account", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.page_fetch_size", ConfigValue{ConfigType::Integer}.defaultValue(512)},
        {"cache.load", ConfigValue{ConfigType::String}.defaultValue("async")}
    };

    ClioConfigDefinition mockConfig2_{
        {"read_only", ConfigValue{ConfigType::Boolean}.defaultValue(false)},
        {"extractor_threads", ConfigValue{ConfigType::Integer}.defaultValue(4)},
        {"io_threads", ConfigValue{ConfigType::Integer}.defaultValue(2)},
        {"cache.num_diffs", ConfigValue{ConfigType::Integer}.defaultValue(32)},
        {"cache.num_markers", ConfigValue{ConfigType::Integer}.defaultValue(48)},
        {"cache.num_cursors_from_diff", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.num_cursors_from_account", ConfigValue{ConfigType::Integer}.defaultValue(0)},
        {"cache.page_fetch_size", ConfigValue{ConfigType::Integer}.defaultValue(512)},
        {"cache.load", ConfigValue{ConfigType::String}.defaultValue("async")}
    };

    std::shared_ptr<etl::SystemState> state1_;
    std::shared_ptr<etl::SystemState> state2_;
};

// Create a test ledger data object
model::LedgerData
createTestLedgerData(uint32_t seq)
{
    return etlng::model::LedgerData{
        .transactions = {},
        .objects = {},
        .successors = {},
        .edgeKeys = {},
        .header = ripple::LedgerHeader{},
        .rawHeader = {},
        .seq = seq
    };
}

// Mock implementation of TaskManagerInterface that tracks calls and state
class FakeTaskManager : public etlng::TaskManagerInterface {
public:
    FakeTaskManager(uint32_t seq) : startSeq_(seq)
    {
    }

    void
    run(std::size_t numExtractors) override
    {
        running_ = true;
        runCalled_ = true;
        extractors_ = numExtractors;
    }

    void
    stop() override
    {
        running_ = false;
        stopCalled_ = true;
    }

    bool
    isRunning() const
    {
        return running_;
    }
    bool
    wasRunCalled() const
    {
        return runCalled_;
    }
    bool
    wasStopCalled() const
    {
        return stopCalled_;
    }
    std::size_t
    getExtractors() const
    {
        return extractors_;
    }
    uint32_t
    getStartSeq() const
    {
        return startSeq_;
    }

private:
    uint32_t startSeq_;
    bool running_ = false;
    bool runCalled_ = false;
    bool stopCalled_ = false;
    std::size_t extractors_ = 0;
};

}  // namespace

TEST_F(ETLMultiNodeTests, WriteConflictCausesTransitionToReadonly)
{
    // We'll track which service got a write conflict
    std::atomic<bool> service1GotConflict = false;
    std::atomic<bool> service2GotConflict = false;
    std::atomic<int> loadCounter = 0;
    std::binary_semaphore conflictHappened(0);

    EXPECT_CALL(*mockTaskManagerProvider_, make(testing::_, testing::_, kSTART_SEQ))
        .WillRepeatedly([&](auto, auto, auto) { return std::make_unique<FakeTaskManager>(kSTART_SEQ); });

    // Setup mock loader to track conflicts
    EXPECT_CALL(*mockLoader_, load(testing::_))
        .WillRepeatedly([&](model::LedgerData const& data) -> std::expected<void, etlng::Error> {
            if (loadCounter > 0) {
                if (data.seq == kSTART_SEQ + 2 && state1_->isWriting && !state1_->writeConflict) {
                    state1_->writeConflict = true;
                    service1GotConflict = true;
                    conflictHappened.release();
                    return std::unexpected("write conflict");
                }

                if (data.seq == kSTART_SEQ + 2 && state2_->isWriting && !state2_->writeConflict) {
                    state2_->writeConflict = true;
                    service2GotConflict = true;
                    conflictHappened.release();
                    return std::unexpected("write conflict");
                }
            }

            ++loadCounter;
            return {};
        });

    EXPECT_CALL(*mockExtractor_, extractLedgerWithDiff(testing::_))
        .WillRepeatedly([](uint32_t seq) -> std::optional<model::LedgerData> { return createTestLedgerData(seq); });
    EXPECT_CALL(*mockLedgers_, getMostRecent()).WillRepeatedly(testing::Return(kSTART_SEQ));
    EXPECT_CALL(*mockLedgers_, subscribe(testing::_)).WillRepeatedly([](auto&&) {
        return boost::signals2::scoped_connection();
    });

    auto service1 = createETLService(state1_, mockConfig1_);
    auto service2 = createETLService(state2_, mockConfig2_);

    state1_->isWriting = true;
    state2_->isWriting = true;

    service1->run();
    service2->run();

    mockLoader_->load(createTestLedgerData(kSTART_SEQ + 1));
    mockLoader_->load(createTestLedgerData(kSTART_SEQ + 2));  // this should trigger conflict

    conflictHappened.acquire();

    EXPECT_TRUE(service1GotConflict || service2GotConflict);
    EXPECT_FALSE(service1GotConflict && service2GotConflict);

    if (service1GotConflict)
        EXPECT_TRUE(state1_->writeConflict);

    if (service2GotConflict)
        EXPECT_TRUE(state2_->writeConflict);
}
