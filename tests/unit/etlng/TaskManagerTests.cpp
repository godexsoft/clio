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

#include "etlng/ExtractorInterface.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/SchedulerInterface.hpp"
#include "etlng/impl/Loading.hpp"
#include "etlng/impl/TaskManager.hpp"
#include "util/LoggerFixtures.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/context/BasicExecutionContext.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/protocol/LedgerHeader.h>

#include <cstdint>
#include <memory>
#include <optional>

using namespace etlng::model;
using namespace etlng::impl;

namespace {

struct MockScheduler : etlng::SchedulerInterface {
    MOCK_METHOD(std::optional<Task>, next, (), (override));
};

struct MockExtractor : etlng::ExtractorInterface {
    MOCK_METHOD(std::optional<LedgerData>, extractLedgerWithDiff, (uint32_t), (override));
    MOCK_METHOD(std::optional<LedgerData>, extractLedgerOnly, (uint32_t), (override));
};

struct MockLoader : etlng::LoaderInterface {
    MOCK_METHOD(void, load, (LedgerData const&), (override));
    MOCK_METHOD(std::optional<ripple::LedgerHeader>, loadInitialLedger, (LedgerData const&), (override));
};

struct TaskManagerTests : NoLoggerFixture {
protected:
    util::async::CoroExecutionContext ctx_;
    std::shared_ptr<MockScheduler> mockSchedulerPtr_ = std::make_shared<MockScheduler>();
    std::shared_ptr<MockExtractor> mockExtractorPtr_ = std::make_shared<MockExtractor>();
    std::shared_ptr<MockLoader> mockLoaderPtr_ = std::make_shared<MockLoader>();

    TaskManager taskManager_{ctx_, *mockSchedulerPtr_, *mockExtractorPtr_, *mockLoaderPtr_};
};

}  // namespace

TEST_F(TaskManagerTests, First)
{
}
