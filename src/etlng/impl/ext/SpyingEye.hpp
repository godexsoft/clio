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

#include "etlng/Models.hpp"
#include "util/log/Logger.hpp"

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>

#include <cstdint>

namespace etlng::impl {

class SpyExt {
    util::Logger log_{"ETL"};

public:
    void
    onLedgerData(model::LedgerData const& data) const
    {
        LOG(log_.info()) << "SPY Loading ledger data for " << data.seq;
        // for (auto const& tx : data.transactions) {
        // }
        // for (auto const& obj : data.objects) {
        // }
    }

    void
    onInitialData(model::LedgerData const& data) const
    {
        LOG(log_.info()) << "SPY Loading initial ledger data for " << data.seq;
        // for (auto const& tx : data.transactions) {
        // }
        // for (auto const& obj : data.objects) {
        // }
    }

    void
    onInitialObject(uint32_t seq, model::Object const& obj) const
    {
        LOG(log_.info()) << "SPY got initial OBJ = " << obj.key << " for seq " << seq << '\n'
                         << "DATA: " << ripple::strHex(obj.data);
    }

    void
    onObject(uint32_t seq, model::Object const& obj) const
    {
        LOG(log_.info()) << "SPY got OBJ = " << obj.key << " for seq " << seq << '\n'
                         << "DATA: " << ripple::strHex(obj.data);
    }
};

}  // namespace etlng::impl
