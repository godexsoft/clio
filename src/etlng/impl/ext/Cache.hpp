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

#include "data/LedgerCache.hpp"
#include "etlng/Models.hpp"
#include "util/log/Logger.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etlng::impl {

class CacheExt {
    data::LedgerCache& cache_;

    util::Logger log_{"ETL"};

public:
    CacheExt(data::LedgerCache& cache) : cache_(cache)
    {
    }

    void
    onLedgerData(model::LedgerData const& data) const
    {
        cache_.update(data.objects, data.seq);
        LOG(log_.trace()) << "got data. objects cnt = " << data.objects.size();
    }

    void
    onInitialData(model::LedgerData const& data) const
    {
        LOG(log_.trace()) << "got initial data. objects cnt = " << data.objects.size();
        cache_.update(data.objects, data.seq);
        cache_.setFull();
    }

    void
    onInitialObjects(uint32_t seq, std::vector<model::Object> const& objs, [[maybe_unused]] std::string lastKey) const
    {
        LOG(log_.trace()) << "got initial objects cnt = " << objs.size();
        cache_.update(objs, seq);
    }
};

}  // namespace etlng::impl
