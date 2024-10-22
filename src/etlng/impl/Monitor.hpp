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

#include "data/BackendInterface.hpp"
#include "etl/NetworkValidatedLedgersInterface.hpp"
#include "util/Constants.hpp"
#include "util/log/Logger.hpp"

#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace etlng::impl {

class Monitor {
    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers_;
    // std::shared_ptr<etl::impl::LedgerPublisher<data::LedgerCache>> publisher_;

    uint32_t nextSequence_;
    util::Logger log_{"ETL"};

public:
    Monitor(
        std::shared_ptr<BackendInterface> backend,
        std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers,
        // std::shared_ptr<etl::impl::LedgerPublisher<data::LedgerCache>> publisher,
        uint32_t startSequence
    )
        : backend_(std::move(backend))
        , ledgers_(std::move(ledgers))
        // , publisher_(std::move(publisher))
        , nextSequence_(startSequence)
    {
    }

    void
    publishNextWhenAvailable()
    {
        // the idea is to always look for new ledgers in the DB. this way if we are readonly node this should still
        // publish next ledgers
        // note that etl already publishes the ledger as it writes it. this is only really needed to support readonly
        // mode and transition between writer and passive ETL node
        if (auto rng = backend_->hardFetchLedgerRangeNoThrow(); rng && rng->maxSequence >= nextSequence_) {
            // ledgerPublisher_.publish(nextSequence, {});
            LOG(log_.info()) << "Ledger " << nextSequence_ << " is detected in DB. publish!";
            ++nextSequence_;
        } else if (ledgers_->waitUntilValidatedByNetwork(nextSequence_, util::MILLISECONDS_PER_SECOND)) {
            LOG(log_.info()) << "Ledger with sequence = " << nextSequence_ << " has been validated by the network. "
                             << "Attempting to find in database and publish";
        }
    }
};

}  // namespace etlng::impl
