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

#pragma once

#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "etlng/SchedulerInterface.hpp"
#include "util/log/Logger.hpp"

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>

namespace etlng::impl {

struct ForwardScheduler : SchedulerInterface {
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers_;

    uint32_t startSeq_;
    std::optional<uint32_t> maxSeq_;
    std::atomic_uint32_t seq_;

    std::chrono::steady_clock::time_point last;
    std::chrono::steady_clock::duration delta = std::chrono::milliseconds{100};

    util::Logger log_{"ETL"};

    ForwardScheduler(ForwardScheduler const& other)
        : ledgers_(other.ledgers_), startSeq_(other.startSeq_), maxSeq_(other.maxSeq_), seq_(other.seq_.load())
    {
    }

    ForwardScheduler(
        std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers,
        uint32_t ss,
        std::optional<uint32_t> ms = std::nullopt
    )
        : ledgers_(std::move(ledgers)), startSeq_(ss), maxSeq_(ms), seq_(ss)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        if (maxSeq_.has_value() && maxSeq_.value() <= seq_)
            return std::nullopt;

        if (ledgers_->getMostRecent() == seq_ + 1) {
            LOG(log_.info()) << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! "
                             << seq_ + 1;
            return {{.priority = 1, .seq = seq_++}};
        }

        return std::nullopt;
    }
};

struct BackfillScheduler : SchedulerInterface {
    uint32_t startSeq_;
    uint32_t minSeq_ = 0u;

    std::atomic_uint32_t seq_;

    BackfillScheduler(BackfillScheduler const& other)
        : startSeq_(other.startSeq_), minSeq_(other.minSeq_), seq_(other.seq_.load())
    {
    }

    BackfillScheduler(uint32_t ss, std::optional<uint32_t> ms) : startSeq_(ss), minSeq_(ms.value_or(0)), seq_(ss)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        if (seq_ == minSeq_)
            return std::nullopt;

        return {{.priority = 0, .seq = seq_--}};
    }
};

template <typename... Schedulers>
class SchedulerChain : public SchedulerInterface {
    std::tuple<Schedulers...> schedulers_;

public:
    template <typename... Ts>
    SchedulerChain(Ts&&... schedulers) : schedulers_(std::forward<Ts>(schedulers)...)
    {
    }

    [[nodiscard]] std::optional<Task>
    next() override
    {
        std::optional<Task> task;
        auto const expand = [&](auto& s) {
            if (task.has_value())
                return false;

            task = s.next();
            return task.has_value();
        };

        std::apply([&expand](auto&&... xs) { (... || expand(xs)); }, schedulers_);

        // if in the end of it we did not have any new task, let's wait a bit to avoid busy spinning
        if (!task.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        return task;
    }
};

}  // namespace etlng::impl
