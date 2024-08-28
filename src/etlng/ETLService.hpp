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
#include "etl/ETLState.hpp"
#include "etlng/ETLServiceInterface.hpp"
#include "util/config/Config.hpp"
#include "util/log/Logger.hpp"

#include <boost/json/object.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace etlng {

class ETLService : public ETLServiceInterface {
    util::Logger log_{"ETL"};

    std::shared_ptr<BackendInterface> backend_;

public:
    ETLService([[maybe_unused]] util::Config const& config)
    {
        // start monitor mode
        // extractors, loaders, plugins all that jazz
        // if we are a writer node, attempt to become a writer
        LOG(log_.info()) << "Starting ETLng...";
    }

    ~ETLService() override
    {
        LOG(log_.debug()) << "Stopping ETL";
    }

    void
    run() override
    {
        LOG(log_.info()) << "run() in ETLng...";
    }

    boost::json::object
    getInfo() const override
    {
        return {{"ok", true}};
    }

    bool
    isAmendmentBlocked() const override
    {
        return false;
    }

    bool
    isCorruptionDetected() const override
    {
        return false;
    }

    std::optional<etl::ETLState>
    getETLState() const override
    {
        return std::nullopt;
    }

    /**
     * @brief Get time passed since last ledger close, in seconds.
     *
     * @return Time passed since last ledger close
     */
    std::uint32_t
    lastCloseAgeSeconds() const override
    {
        return 0;
    }

private:
};
}  // namespace etlng
