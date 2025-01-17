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
#include "util/log/Logger.hpp"

#include <xrpl/protocol/TxFormats.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace etlng::impl {

Monitor::Monitor(
    std::shared_ptr<BackendInterface> backend,
    std::shared_ptr<etl::NetworkValidatedLedgersInterface> ledgers,
    uint32_t startSequence
)
    : backend_(std::move(backend)), ledgers_(std::move(ledgers)), nextSequence_(startSequence)
{
}

std::optional<uint32_t>
Monitor::awaitNextSequenceWithTimeout(std::size_t timeout)  // TODO: this should become chrono duration
{
    // the idea is to always look for new ledgers in the DB.
    // this way if we are readonly node we can still publish next ledgers.
    // note that etl already publishes ledgers as it writes them.
    // so this is only really needed to support readonly mode and transition between writer and passive ETL node.

    std::optional<uint32_t> out;

    if (auto rng = backend_->hardFetchLedgerRangeNoThrow(); rng && rng->maxSequence >= nextSequence_) {
        out.emplace(nextSequence_);

        LOG(log_.info()) << "Ledger " << nextSequence_ << " is detected in DB. publish!";
        ++nextSequence_;
    } else if (ledgers_->waitUntilValidatedByNetwork(nextSequence_, timeout)) {
        LOG(log_.info()) << "Ledger with sequence = " << nextSequence_ << " has been validated by the network.";
    }

    return out;
}

}  // namespace etlng::impl
