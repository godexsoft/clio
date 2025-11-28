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
#include "util/config/ConfigDefinition.hpp"
#include "util/log/Logger.hpp"

#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
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
    handleSignal(int /* signal */)
    {
        ASSERT(installedHandler != nullptr, "SignalsHandler is not initialized");
        installedHandler->signalCount_.fetch_add(1, std::memory_order_seq_cst);
    }

    static void
    handleSecondSignal(int /* signal */)
    {
        ASSERT(installedHandler != nullptr, "SignalsHandler is not initialized");
        installedHandler->secondSignalReceived_.store(true, std::memory_order_seq_cst);
        installedHandler->signalCount_.fetch_add(1, std::memory_order_seq_cst);
    }
};

SignalsHandler* SignalsHandlerStatic::installedHandler = nullptr;

}  // namespace impl

SignalsHandler::SignalsHandler(config::ClioConfigDefinition const& config, std::function<void()> forceExitHandler)
    : gracefulPeriod_(0)
    , context_(1)
    , stopHandler_([this, forceExitHandler] mutable {
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
    , secondSignalHandler_([this, forceExitHandler = std::move(forceExitHandler)] {
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
    setHandler();
    impl::SignalsHandlerStatic::resetHandler();  // This is needed mostly for tests to reset static state

    // We can only await in destructor as the signal handlers are called on the same thread as the scheduled timer
    // and a deadlock is unavoidable if we await there.
    cancelTimer(/* await= */ true);

    if (signalMonitorOperation_.has_value()) {
        signalMonitorOperation_->requestStop();
        signalMonitorOperation_->wait();
    }
}

void
SignalsHandler::cancelTimer(bool await)
{
    std::lock_guard<std::mutex> lock(timerMutex_);
    if (timer_.has_value()) {
        timer_->cancel();
        if (await) {
            timer_->wait();
            timer_ = std::nullopt;
        }
    }
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
        while (not stopRequested) {
            // Check if we have any signals to process
            auto signalCount = signalCount_.exchange(0, std::memory_order_seq_cst);
            auto isSecondSignal = secondSignalReceived_.exchange(false, std::memory_order_seq_cst);

            if (signalCount > 0) {
                if (isSecondSignal) {
                    secondSignalHandler_();
                } else {
                    stopHandler_();
                }
            } else {
                // Only yield when there are no signals to process
                // This makes signal processing more responsive
                std::this_thread::yield();
            }
        }
    }));
}

}  // namespace util
