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

#include "rpc/WorkQueue.hpp"
#include "util/Assert.hpp"
#include "util/Mutex.hpp"
#include "util/Spawn.hpp"
#include "util/config/Array.hpp"
#include "util/config/ConfigConstraints.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/config/ConfigValue.hpp"
#include "util/config/Types.hpp"
#include "util/log/Logger.hpp"
#include "util/prometheus/Counter.hpp"
#include "util/prometheus/Gauge.hpp"
#include "util/prometheus/Label.hpp"
#include "util/prometheus/Prometheus.hpp"

#include <benchmark/benchmark.h>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/json.hpp>
#include <boost/json/object.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <utility>

namespace old {

/**
 * @brief An asynchronous, thread-safe queue for RPC requests.
 */
class WorkQueue {
    // these are cumulative for the lifetime of the process
    std::reference_wrapper<util::prometheus::CounterInt> queued_;
    std::reference_wrapper<util::prometheus::CounterInt> durationUs_;

    std::reference_wrapper<util::prometheus::GaugeInt> curSize_;
    uint32_t maxSize_ = std::numeric_limits<uint32_t>::max();

    util::Logger log_{"RPC"};
    boost::asio::thread_pool ioc_;

    std::atomic_bool stopping_;

    class OneTimeCallable {
        std::function<void()> func_;
        bool called_{false};

    public:
        void
        setCallable(std::function<void()> func);

        void
        operator()();

        operator bool() const;
    };
    util::Mutex<OneTimeCallable> onQueueEmpty_;

public:
    /**
     * @brief Create an we instance of the work queue.
     *
     * @param numWorkers The amount of threads to spawn in the pool
     * @param maxSize The maximum capacity of the queue; 0 means unlimited
     */
    WorkQueue(std::uint32_t numWorkers, uint32_t maxSize = 0);
    ~WorkQueue();

    /**
     * @brief Put the work queue into a stopping state. This will prevent new jobs from being queued.
     *
     * @param onQueueEmpty A callback to run when the last task in the queue is completed
     */
    void
    stop(std::function<void()> onQueueEmpty);

    /**
     * @brief A factory function that creates the work queue based on a config.
     *
     * @param config The Clio config to use
     * @return The work queue
     */
    static WorkQueue
    makeWorkQueue(util::config::ClioConfigDefinition const& config);

    /**
     * @brief Submit a job to the work queue.
     *
     * The job will be rejected if isWhiteListed is set to false and the current size of the queue reached capacity.
     *
     * @tparam FnType The function object type
     * @param func The function object to queue as a job
     * @param isWhiteListed Whether the queue capacity applies to this job
     * @return true if the job was successfully queued; false otherwise
     */
    template <typename FnType>
    bool
    postCoro(FnType&& func, bool isWhiteListed)
    {
        if (stopping_) {
            LOG(log_.warn()) << "Queue is stopping, rejecting incoming task.";
            return false;
        }

        if (curSize_.get().value() >= maxSize_ && !isWhiteListed) {
            LOG(log_.warn()) << "Queue is full. rejecting job. current size = " << curSize_.get().value()
                             << "; max size = " << maxSize_;
            return false;
        }

        ++curSize_.get();

        // Each time we enqueue a job, we want to post a symmetrical job that will dequeue and run the job at the front
        // of the job queue.
        util::spawn(
            ioc_,
            [this, func = std::forward<FnType>(func), start = std::chrono::system_clock::now()](auto yield) mutable {
                auto const run = std::chrono::system_clock::now();
                auto const wait = std::chrono::duration_cast<std::chrono::microseconds>(run - start).count();

                ++queued_.get();
                durationUs_.get() += wait;
                LOG(log_.info()) << "WorkQueue wait time = " << wait << " queue size = " << curSize_.get().value();

                func(yield);
                --curSize_.get();
                if (curSize_.get().value() == 0 && stopping_) {
                    auto onTasksComplete = onQueueEmpty_.lock();
                    ASSERT(onTasksComplete->operator bool(), "onTasksComplete must be set when stopping is true.");
                    onTasksComplete->operator()();
                }
            }
        );

        return true;
    }

    /**
     * @brief Generate a report of the work queue state.
     *
     * @return The report as a JSON object.
     */
    boost::json::object
    report() const;

    /**
     * @brief Wait until all the jobs in the queue are finished.
     */
    void
    join();

    /**
     * @brief Get the size of the queue.
     *
     * @return The number of jobs in the queue.
     */
    size_t
    size() const;
};

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
{
    if (maxSize != 0)
        maxSize_ = maxSize;
}

WorkQueue::~WorkQueue()
{
    join();
}

void
WorkQueue::stop(std::function<void()> onQueueEmpty)
{
    auto handler = onQueueEmpty_.lock();
    handler->setCallable(std::move(onQueueEmpty));
    stopping_ = true;
    if (size() == 0) {
        handler->operator()();
    }
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
    ioc_.join();
}

size_t
WorkQueue::size() const
{
    return curSize_.get().value();
}

}  // namespace old

namespace current {

/**
 * @brief An interface for any class providing a report as json object
 */
struct Reportable {
    virtual ~Reportable() = default;

    /**
     * @brief Generate a report of the work queue state.
     *
     * @return The report as a JSON object.
     */
    [[nodiscard]] virtual boost::json::object
    report() const = 0;
};

/**
 * @brief An asynchronous, thread-safe queue for RPC requests.
 */
class WorkQueue : public Reportable {
    using TaskType = std::function<void(boost::asio::yield_context)>;
    using QueueType = std::queue<TaskType>;

public:
    /**
     * @brief Represents a task scheduling priority
     */
    enum class Priority : uint8_t {
        High,
        Default,
    };

private:
    struct DispatcherState {
        QueueType high;
        QueueType normal;

        bool isIdle = false;

        void
        push(Priority priority, auto&& task)
        {
            auto& queue = [this, priority] -> QueueType& {
                if (priority == Priority::High)
                    return high;
                return normal;
            }();
            queue.push(std::forward<decltype(task)>(task));
        }

        [[nodiscard]] bool
        empty() const
        {
            return high.empty() and normal.empty();
        }
    };

private:
    static constexpr auto kTAKE_HIGH_PRIO = 4uz;

    // these are cumulative for the lifetime of the process
    std::reference_wrapper<util::prometheus::CounterInt> queued_;
    std::reference_wrapper<util::prometheus::CounterInt> durationUs_;

    std::reference_wrapper<util::prometheus::GaugeInt> curSize_;
    uint32_t maxSize_ = std::numeric_limits<uint32_t>::max();

    util::Logger log_{"RPC"};
    boost::asio::thread_pool ioc_;
    boost::asio::strand<boost::asio::thread_pool::executor_type> strand_;
    bool hasDispatcher_ = false;

    std::atomic_bool stopping_;

    util::Mutex<std::function<void()>> onQueueEmpty_;
    util::Mutex<DispatcherState> dispatcherState_;
    boost::asio::steady_timer waitTimer_;

public:
    struct DontStartProcessingTag {};
    static constexpr DontStartProcessingTag kDONT_START_PROCESSING_TAG = {};

    /**
     * @brief Create an instance of the work queue.
     *
     * The work queue immediately starts to process tasks as they come.
     *
     * @param numWorkers The amount of threads to spawn in the pool
     * @param maxSize The maximum capacity of the queue; 0 means unlimited
     */
    WorkQueue(std::uint32_t numWorkers, uint32_t maxSize = 0);

    /**
     * @brief Create an instance of the work queue without starting the processing of events.
     *
     * Clients are expected to call `startProcessing` manually once ready to start processing tasks.
     *
     * @param numWorkers The amount of threads to spawn in the pool
     * @param maxSize The maximum capacity of the queue; 0 means unlimited
     */
    WorkQueue(DontStartProcessingTag, std::uint32_t numWorkers, uint32_t maxSize = 0);

    ~WorkQueue() override;

    /**
     * @brief Start processing of the enqueued tasks.
     */
    void
    startProcessing();

    /**
     * @brief Put the work queue into a stopping state. This will prevent new jobs from being queued.
     *
     * @param onQueueEmpty A callback to run when the last task in the queue is completed
     */
    void
    requestStop(std::function<void()> onQueueEmpty = [] {});

    /**
     * @brief Put the work queue into a stopping state and await workers to finish.
     */
    void
    stop();

    /**
     * @brief Submit a job to the work queue.
     *
     * The job will be rejected if isWhiteListed is set to false and the current size of the queue reached capacity.
     *
     * @param func The function object to queue as a job
     * @param isWhiteListed Whether the queue capacity applies to this job
     * @param priority The priority of the task
     * @return true if the job was successfully queued; false otherwise
     */
    bool
    postCoro(TaskType func, bool isWhiteListed, Priority priority = Priority::Default);

    /**
     * @brief Generate a report of the work queue state.
     *
     * @return The report as a JSON object.
     */
    [[nodiscard]] boost::json::object
    report() const override;

    /**
     * @brief Wait until all the jobs in the queue are finished.
     */
    void
    join();

    /**
     * @brief Get the size of the queue.
     *
     * @return The number of jobs in the queue.
     */
    [[nodiscard]] size_t
    size() const;

private:
    void
    dispatcherLoop(boost::asio::yield_context yield);
};

WorkQueue::WorkQueue(DontStartProcessingTag, std::uint32_t numWorkers, uint32_t maxSize)
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
}

WorkQueue::WorkQueue(std::uint32_t numWorkers, uint32_t maxSize)
    : WorkQueue(kDONT_START_PROCESSING_TAG, numWorkers, maxSize)
{
    startProcessing();
}

WorkQueue::~WorkQueue()
{
    stop();
}

void
WorkQueue::startProcessing()
{
    util::spawn(strand_, [this](auto yield) {
        ASSERT(not hasDispatcher_, "Dispatcher already running");

        hasDispatcher_ = true;
        dispatcherLoop(yield);
    });
}

bool
WorkQueue::postCoro(TaskType func, bool isWhiteListed, Priority priority)
{
    if (stopping_) {
        LOG(log_.warn()) << "Queue is stopping, rejecting incoming task.";
        return false;
    }

    if (size() >= maxSize_ && !isWhiteListed) {
        LOG(log_.warn()) << "Queue is full. rejecting job. current size = " << size() << "; max size = " << maxSize_;
        return false;
    }

    ++curSize_.get();
    auto needsWakeup = false;

    {
        auto state = dispatcherState_.lock();

        needsWakeup = std::exchange(state->isIdle, false);

        state->push(priority, std::move(func));
    }

    if (needsWakeup)
        boost::asio::post(strand_, [this] { waitTimer_.cancel(); });

    return true;
}

void
WorkQueue::dispatcherLoop(boost::asio::yield_context yield)
{
    LOG(log_.info()) << "WorkQueue dispatcher starting";

    // all ongoing tasks must be completed before stopping fully
    while (not stopping_ or size() > 0) {
        std::vector<TaskType> batch;

        {
            auto state = dispatcherState_.lock();

            if (state->empty()) {
                state->isIdle = true;
            } else {
                for (auto count = 0uz; count < kTAKE_HIGH_PRIO and not state->high.empty(); ++count) {
                    batch.push_back(std::move(state->high.front()));
                    state->high.pop();
                }

                if (not state->normal.empty()) {
                    batch.push_back(std::move(state->normal.front()));
                    state->normal.pop();
                }
            }
        }

        if (not stopping_ and batch.empty()) {
            waitTimer_.expires_at(std::chrono::steady_clock::time_point::max());
            boost::system::error_code ec;
            waitTimer_.async_wait(yield[ec]);
        } else {
            for (auto task : std::move(batch)) {
                util::spawn(
                    ioc_,
                    [this, spawnedAt = std::chrono::system_clock::now(), task = std::move(task)](auto yield) mutable {
                        auto const takenAt = std::chrono::system_clock::now();
                        auto const waited =
                            std::chrono::duration_cast<std::chrono::microseconds>(takenAt - spawnedAt).count();

                        ++queued_.get();
                        durationUs_.get() += waited;
                        LOG(log_.info()) << "WorkQueue wait time: " << waited << ", queue size: " << size();

                        task(yield);

                        --curSize_.get();
                    }
                );
            }

            boost::asio::post(ioc_.get_executor(), yield);  // yield back to avoid hijacking the thread
        }
    }

    LOG(log_.info()) << "WorkQueue dispatcher shutdown requested - time to execute onTasksComplete";

    {
        auto onTasksComplete = onQueueEmpty_.lock();
        ASSERT(onTasksComplete->operator bool(), "onTasksComplete must be set when stopping is true.");
        onTasksComplete->operator()();
    }

    LOG(log_.info()) << "WorkQueue dispatcher finished";
}

void
WorkQueue::requestStop(std::function<void()> onQueueEmpty)
{
    auto handler = onQueueEmpty_.lock();
    *handler = std::move(onQueueEmpty);

    stopping_ = true;
    auto needsWakeup = false;

    {
        auto state = dispatcherState_.lock();
        needsWakeup = std::exchange(state->isIdle, false);
    }

    if (needsWakeup)
        boost::asio::post(strand_, [this] { waitTimer_.cancel(); });
}

void
WorkQueue::stop()
{
    if (not stopping_.exchange(true))
        requestStop();

    ioc_.join();
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

size_t
WorkQueue::size() const
{
    return curSize_.get().value();
}

}  // namespace current

using namespace rpc;
using namespace util::config;

static auto const kCONFIG = ClioConfigDefinition{
    {"prometheus.compress_reply", ConfigValue{ConfigType::Boolean}.defaultValue(true)},
    {"prometheus.enabled", ConfigValue{ConfigType::Boolean}.defaultValue(true)},
    {"log.channels.[].channel", Array{ConfigValue{ConfigType::String}}},
    {"log.channels.[].level", Array{ConfigValue{ConfigType::String}}},
    {"log.level", ConfigValue{ConfigType::String}.defaultValue("info")},
    {"log.format", ConfigValue{ConfigType::String}.defaultValue(R"(%Y-%m-%d %H:%M:%S.%f %^%3!l:%n%$ - %v)")},
    {"log.is_async", ConfigValue{ConfigType::Boolean}.defaultValue(false)},
    {"log.enable_console", ConfigValue{ConfigType::Boolean}.defaultValue(false)},
    {"log.directory", ConfigValue{ConfigType::String}.optional()},
    {"log.rotation_size", ConfigValue{ConfigType::Integer}.defaultValue(2048).withConstraint(gValidateUint32)},
    {"log.directory_max_files", ConfigValue{ConfigType::Integer}.defaultValue(25).withConstraint(gValidateUint32)},
    {"log.tag_style", ConfigValue{ConfigType::String}.defaultValue("none")},
};

// this should be a fixture but it did not work with Args very well
static void
init()
{
    static std::once_flag kONCE;
    std::call_once(kONCE, [] {
        PrometheusService::init(kCONFIG);
        (void)util::LogService::init(kCONFIG);
    });
}

static void
benchmarkWorkQueueOld(benchmark::State& state)
{
    init();

    auto const kTOTAL = static_cast<size_t>(state.range(0));
    auto const numThreads = static_cast<uint32_t>(state.range(1));
    auto const maxSize = static_cast<uint32_t>(state.range(2));

    for (auto _ : state) {
        std::atomic_size_t total = 0uz;

        state.PauseTiming();
        old::WorkQueue queue(numThreads, maxSize);
        state.ResumeTiming();

        for (auto i = 0uz; i < kTOTAL; ++i) {
            total += static_cast<int>(queue.postCoro(
                [](auto yield) {
                    boost::asio::steady_timer timer(yield.get_executor(), std::chrono::milliseconds{10});
                    timer.async_wait(yield);
                },
                /* isWhiteListed = */ false
            ));
        }

        queue.join();

        // std::cout << "total: " << total << '\n';
        ASSERT(total <= kTOTAL && total >= maxSize, "Totals don't match");
    }
}

static void
benchmarkWorkQueueNew(benchmark::State& state)
{
    init();

    auto const kTOTAL = static_cast<size_t>(state.range(0));
    auto const numThreads = static_cast<uint32_t>(state.range(1));
    auto const maxSize = static_cast<uint32_t>(state.range(2));

    for (auto _ : state) {
        std::atomic_size_t total = 0uz;

        state.PauseTiming();
        WorkQueue queue(numThreads, maxSize);
        state.ResumeTiming();

        for (auto i = 0uz; i < kTOTAL; ++i) {
            total += static_cast<int>(queue.postCoro(
                [](auto yield) {
                    boost::asio::steady_timer timer(yield.get_executor(), std::chrono::milliseconds{10});
                    timer.async_wait(yield);
                },
                /* isWhiteListed = */ false
            ));
        }

        queue.stop();

        // std::cout << "total: " << total << '\n';
        ASSERT(total <= kTOTAL && total >= maxSize, "Totals don't match");
    }
}

static void
benchmarkWorkQueueCurrent(benchmark::State& state)
{
    init();

    auto const kTOTAL = static_cast<size_t>(state.range(0));
    auto const numThreads = static_cast<uint32_t>(state.range(1));
    auto const maxSize = static_cast<uint32_t>(state.range(2));

    for (auto _ : state) {
        std::atomic_size_t total = 0uz;

        state.PauseTiming();
        current::WorkQueue queue(numThreads, maxSize);
        state.ResumeTiming();

        for (auto i = 0uz; i < kTOTAL; ++i) {
            total += static_cast<int>(queue.postCoro(
                [](auto yield) {
                    boost::asio::steady_timer timer(yield.get_executor(), std::chrono::milliseconds{10});
                    timer.async_wait(yield);
                },
                /* isWhiteListed = */ false
            ));
        }

        queue.stop();

        // std::cout << "total: " << total << '\n';
        ASSERT(total <= kTOTAL && total >= maxSize, "Totals don't match");
    }
}

// BENCHMARK(benchmarkWorkQueueOld)->ArgsProduct({{1000, 10000, 100000}, {1, 2, 4, 8}})->Unit(benchmark::kMillisecond);
// BENCHMARK(benchmarkWorkQueueNew)->ArgsProduct({{1000, 10000, 100000}, {1, 2, 4, 8}})->Unit(benchmark::kMillisecond);
BENCHMARK(benchmarkWorkQueueNew)->ArgsProduct({{10000}, {4}, {0}})->Unit(benchmark::kMillisecond);
BENCHMARK(benchmarkWorkQueueCurrent)->ArgsProduct({{10000}, {4}, {0}})->Unit(benchmark::kMillisecond);
BENCHMARK(benchmarkWorkQueueOld)->ArgsProduct({{10000}, {4}, {0}})->Unit(benchmark::kMillisecond);
