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

#include "etlng/impl/Monitor.hpp"

#include "data/BackendInterface.hpp"
#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "util/Assert.hpp"
#include "util/async/AnyExecutionContext.hpp"
#include "util/async/AnyOperation.hpp"
#include "util/log/Logger.hpp"

#include <boost/signals2/connection.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace etlng::impl {
Monitor::Monitor(
    util::async::AnyExecutionContext ctx,
    std::shared_ptr<BackendInterface> backend,
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> validatedLedgers,
    uint32_t startSequence,
    std::chrono::steady_clock::duration noDbUpdateTimeout
)
    : strand_(ctx.makeStrand())
    , backend_(std::move(backend))
    , validatedLedgers_(std::move(validatedLedgers))
    , nextSequence_(startSequence)
    , noDbUpdateTimeout_(noDbUpdateTimeout)
    , lastDbProgressTime_(std::chrono::steady_clock::now())
    , lastSeenMaxSeqInDb_(startSequence > 0 ? startSequence - 1 : 0)
{
}

Monitor::~Monitor()
{
    stop();
}

void
Monitor::notifyLedgerLoaded(uint32_t seq)
{
    LOG(log_.debug()) << "Loader notified Monitor about newly committed ledger " << seq;
    {
        lastSeenMaxSeqInDb_ = std::max(seq, lastSeenMaxSeqInDb_);
        lastDbProgressTime_ = std::chrono::steady_clock::now();
    }
    repeatedTask_->invoke();  // force-invoke doWork immediately
};

void
Monitor::run(std::chrono::steady_clock::duration repeatInterval)
{
    ASSERT(not repeatedTask_.has_value(), "Monitor attempted to run more than once");
    LOG(log_.debug()) << "Starting monitor with repeat interval: "
                      << std::chrono::duration_cast<std::chrono::seconds>(repeatInterval).count()
                      << "s and no DB update timeout: "
                      << std::chrono::duration_cast<std::chrono::seconds>(noDbUpdateTimeout_).count() << "s";

    repeatedTask_ = strand_.executeRepeatedly(repeatInterval, std::bind_front(&Monitor::doWork, this));
    subscription_ = validatedLedgers_->subscribe(std::bind_front(&Monitor::onNextSequence, this));
}

void
Monitor::stop()
{
    if (repeatedTask_.has_value())
        repeatedTask_->abort();

    repeatedTask_ = std::nullopt;
}

boost::signals2::scoped_connection
Monitor::subscribe(SignalType::slot_type const& subscriber)
{
    return notificationChannel_.connect(subscriber);
}

boost::signals2::scoped_connection
Monitor::subscribeToNoDbUpdate(NoDbUpdateSignalType::slot_type const& subscriber)
{
    return noDbUpdateChannel_.connect(subscriber);
}

void
Monitor::onNextSequence(uint32_t seq)
{
    LOG(log_.debug()) << "rippled published sequence " << seq;
    repeatedTask_->invoke();  // force-invoke immediately
}

void
Monitor::doWork()
{
    auto rng = backend_->hardFetchLedgerRangeNoThrow();
    bool dbProgressedThisCycle = false;

    if (rng) {
        if (rng->maxSequence > lastSeenMaxSeqInDb_) {
            LOG(log_.info()) << "DB progressed. Old max seq = " << lastSeenMaxSeqInDb_
                             << ", new max seq = " << rng->maxSequence;
            lastSeenMaxSeqInDb_ = rng->maxSequence;
            dbProgressedThisCycle = true;
        }

        while (lastSeenMaxSeqInDb_ >= nextSequence_) {
            LOG(log_.info()) << "Publishing from Monitor::doWork. nextSequence_ = " << nextSequence_
                             << ", lastSeenMaxSeqInDb_ = " << lastSeenMaxSeqInDb_;
            notificationChannel_(nextSequence_++);
            dbProgressedThisCycle = true;
        }
    } else {
        LOG(log_.trace()) << "DB range is not available or empty. lastSeenMaxSeqInDb_ = " << lastSeenMaxSeqInDb_
                          << ", nextSequence_ = " << nextSequence_;
    }

    if (dbProgressedThisCycle) {
        lastDbProgressTime_ = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - lastDbProgressTime_ > noDbUpdateTimeout_) {
        LOG(log_.warn()) << "No DB update detected for "
                         << std::chrono::duration_cast<std::chrono::seconds>(noDbUpdateTimeout_).count()
                         << " seconds. Firing noDbUpdateChannel. Last seen max seq in DB: " << lastSeenMaxSeqInDb_
                         << ". Expecting next: " << nextSequence_;
        noDbUpdateChannel_();
        lastDbProgressTime_ = std::chrono::steady_clock::now();
    }
}

}  // namespace etlng::impl
