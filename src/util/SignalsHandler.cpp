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

#include "util/SignalsHandler.hpp"

#include "util/Assert.hpp"
#include "util/async/AnyStopToken.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/log/Logger.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace util {
namespace impl {

class SignalsHandlerStatic {
    static SignalsHandler* installedHandler;

public:
    static void
    registerHandler(SignalsHandler& handler)
    {
        ASSERT(installedHandler == nullptr, "There could be only one instance of SignalsHandler");
        installedHandler = &handler;
    }

    static void
    resetHandler()
    {
        installedHandler = nullptr;
    }

    static void
    handleSignal(int /*signal*/)
    {
        // This runs in signal context - only async-signal-safe operations allowed
        if (installedHandler != nullptr) {
            // Increment signal counter (async-signal-safe)
            installedHandler->signalCount_.fetch_add(1, std::memory_order_release);

            // Notify the monitor (async-signal-safe)
            installedHandler->signalCondition_.notify_one();
        }
    }

    static void
    handleSecondSignal(int /*signal*/)
    {
        // This runs in signal context - only async-signal-safe operations allowed
        if (installedHandler != nullptr) {
            // Mark as second signal and increment counter (async-signal-safe)
            installedHandler->secondSignalReceived_.store(true, std::memory_order_release);
            installedHandler->signalCount_.fetch_add(1, std::memory_order_release);

            // Notify the monitor (async-signal-safe)
            installedHandler->signalCondition_.notify_one();
        }
    }
};

SignalsHandler* SignalsHandlerStatic::installedHandler = nullptr;

}  // namespace impl

SignalsHandler::SignalsHandler(config::ClioConfigDefinition const& config, std::function<void()> forceExitHandler)
    : gracefulPeriod_(0)
    , context_(1)
    , stopHandler_([this, forceExitHandler](int) mutable {
        LOG(LogService::info()) << "Got stop signal. Stopping Clio. Graceful period is "
                                << std::chrono::duration_cast<std::chrono::milliseconds>(gracefulPeriod_).count()
                                << " milliseconds.";
        setHandler(impl::SignalsHandlerStatic::handleSecondSignal);
        {
            std::lock_guard<std::mutex> lock(timerMutex_);
            timer_.emplace(context_.scheduleAfter(
                gracefulPeriod_, [forceExitHandler = std::move(forceExitHandler)](auto&& stopRequested, bool canceled) {
                    // TODO: Update this after https://github.com/XRPLF/clio/issues/1380
                    if (not stopRequested and not canceled) {
                        LOG(LogService::warn()) << "Force exit at the end of graceful period.";
                        forceExitHandler();
                    }
                }
            ));
        }
        stopSignal_();
    })
    , secondSignalHandler_([this, forceExitHandler = std::move(forceExitHandler)](int) {
        LOG(LogService::warn()) << "Force exit on second signal.";
        forceExitHandler();
        cancelTimer();
        setHandler();
    })
{
    impl::SignalsHandlerStatic::registerHandler(*this);

    gracefulPeriod_ = util::config::ClioConfigDefinition::toMilliseconds(config.get<float>("graceful_period"));

    startSignalMonitoring();
    setHandler(impl::SignalsHandlerStatic::handleSignal);
}

SignalsHandler::~SignalsHandler()
{
    cancelTimer();

    // Clean up signal monitoring
    if (signalMonitorOperation_.has_value()) {
        signalMonitorOperation_->requestStop();
        // Wake up the monitor so it can exit cleanly
        signalCondition_.notify_one();
    }

    setHandler();
    impl::SignalsHandlerStatic::resetHandler();  // This is needed mostly for tests to reset static state
}

void
SignalsHandler::cancelTimer()
{
    std::lock_guard<std::mutex> lock(timerMutex_);
    if (timer_.has_value())
        timer_->abort();
}

void
SignalsHandler::setHandler(void (*handler)(int))
{
    for (int const signal : kHANDLED_SIGNALS) {
        std::signal(signal, handler == nullptr ? SIG_DFL : handler);
    }
}

void
SignalsHandler::startSignalMonitoring()
{
    signalMonitorOperation_.emplace(context_.execute([this](auto stopRequested) {
        processSignals(std::move(stopRequested));
    }));
}

void
SignalsHandler::processSignals(async::AnyStopToken stopRequested)
{
    while (not stopRequested) {
        std::unique_lock<std::mutex> lock(signalMutex_);
        signalCondition_.wait(lock, [this, &stopRequested]() {
            return stopRequested or signalCount_.load(std::memory_order_acquire) > 0;
        });

        auto signalCount = signalCount_.exchange(0, std::memory_order_acquire);
        auto isSecondSignal = secondSignalReceived_.exchange(false, std::memory_order_acquire);

        if (signalCount > 0) {
            lock.unlock();

            if (isSecondSignal) {
                secondSignalHandler_(SIGINT);  // Assuming SIGINT for now
            } else {
                stopHandler_(SIGINT);  // Assuming SIGINT for now
            }
        }
    }
}

}  // namespace util
