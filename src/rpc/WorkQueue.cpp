//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2022, the clio developers.

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

#include "rpc/WorkQueue.hpp"

#include "util/Assert.hpp"
#include "util/Spawn.hpp"
#include "util/log/Logger.hpp"
#include "util/prometheus/Label.hpp"
#include "util/prometheus/Prometheus.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/json/object.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace rpc {

void
WorkQueue::OneTimeCallable::setCallable(std::function<void()> func)
{
    func_ = func;
}

void
WorkQueue::OneTimeCallable::operator()()
{
    if (not called_) {
        func_();
        called_ = true;
    }
}

WorkQueue::OneTimeCallable::
operator bool() const
{
    return func_.operator bool();
}

WorkQueue::WorkQueue(std::uint32_t numWorkers, uint32_t maxSize)
    : queued_{PrometheusService::counterInt(
          "work_queue_queued_total_number",
          util::prometheus::Labels(),
          "The total number of tasks queued for processing"
      )}
    , durationUs_{PrometheusService::counterInt(
          "work_queue_cumulative_tasks_duration_us",
          util::prometheus::Labels(),
          "The total number of microseconds tasks were waiting to be executed"
      )}
    , curSize_{PrometheusService::gaugeInt(
          "work_queue_current_size",
          util::prometheus::Labels(),
          "The current number of tasks in the queue"
      )}
    , ioc_{numWorkers}
    , strand_{ioc_.get_executor()}
    , waitTimer_(ioc_)
{
    if (maxSize != 0)
        maxSize_ = maxSize;

    util::spawn(strand_, [this](auto yield) { dispatcherLoop(yield); });
}

WorkQueue::~WorkQueue()
{
    join();
}

void
WorkQueue::dispatcherLoop(boost::asio::yield_context yield)
{
    LOG(log_.debug()) << "WorkQueue dispatcher starting";

    // all ongoing tasks must be completed before stopping fully
    while (not stopping_ or size() > 0) {
        std::vector<std::function<void(boost::asio::yield_context)>> batch;
        auto shouldWait = false;

        {
            auto state = dispatcherState_.lock();

            if (state->empty()) {
                shouldWait = true;
                state->isIdle = true;
            } else {
                auto highPrioCount = 0uz;
                while (highPrioCount < kHIGH_PRIO_RATIO and not state->high.empty()) {
                    batch.push_back(std::move(state->high.front()));
                    state->high.pop();
                    ++highPrioCount;
                }

                if (not state->normal.empty()) {
                    batch.push_back(std::move(state->normal.front()));
                    state->normal.pop();
                }
            }
        }

        if (not stopping_ and shouldWait) {
            waitTimer_.expires_at(std::chrono::steady_clock::time_point::max());
            boost::system::error_code ec;
            waitTimer_.async_wait(yield[ec]);
        } else {
            for (auto task : std::move(batch)) {
                util::spawn(
                    ioc_, [this, start = std::chrono::system_clock::now(), task = std::move(task)](auto yield) mutable {
                        auto const run = std::chrono::system_clock::now();
                        auto const wait = std::chrono::duration_cast<std::chrono::microseconds>(run - start).count();

                        ++queued_.get();
                        durationUs_.get() += wait;
                        LOG(log_.info()) << "WorkQueue wait time: " << wait << ", queue size: " << size();

                        task(yield);

                        --curSize_.get();
                    }
                );
            }

            boost::asio::post(ioc_.get_executor(), yield);  // yield back to avoid hijacking the thread
        }
    }

    LOG(log_.info()) << "WorkQueue dispatcher shutdown requested - time to execute onTasksComplete";

    auto onTasksComplete = onQueueEmpty_.lock();
    ASSERT(onTasksComplete->operator bool(), "onTasksComplete must be set when stopping is true.");
    onTasksComplete->operator()();

    LOG(log_.debug()) << "WorkQueue dispatcher finished";
}

void
WorkQueue::stop(std::function<void()> onQueueEmpty)
{
    auto handler = onQueueEmpty_.lock();
    handler->setCallable(std::move(onQueueEmpty));

    stopping_ = true;
    auto needsWakeup = false;

    {
        auto state = dispatcherState_.lock();
        if (state->isIdle) {
            needsWakeup = true;
            state->isIdle = false;
        }
    }

    if (needsWakeup)
        boost::asio::post(strand_, [this] { waitTimer_.cancel(); });
}

WorkQueue
WorkQueue::makeWorkQueue(util::config::ClioConfigDefinition const& config)
{
    static util::Logger const log{"RPC"};  // NOLINT(readability-identifier-naming)
    auto const serverConfig = config.getObject("server");
    auto const numThreads = config.get<uint32_t>("workers");
    auto const maxQueueSize = serverConfig.get<uint32_t>("max_queue_size");

    LOG(log.info()) << "Number of workers = " << numThreads << ". Max queue size = " << maxQueueSize;
    return WorkQueue{numThreads, maxQueueSize};
}

boost::json::object
WorkQueue::report() const
{
    auto obj = boost::json::object{};

    obj["queued"] = queued_.get().value();
    obj["queued_duration_us"] = durationUs_.get().value();
    obj["current_queue_size"] = curSize_.get().value();
    obj["max_queue_size"] = maxSize_;

    return obj;
}

void
WorkQueue::join()
{
    // TODO: maybe this is not the best place or some renaming needs to be done
    if (not stopping_)
        stop();

    ioc_.join();
}

size_t
WorkQueue::size() const
{
    return curSize_.get().value();
}

}  // namespace rpc
