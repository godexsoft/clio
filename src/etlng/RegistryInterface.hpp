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

#include <org/xrpl/rpc/v1/ledger.pb.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace etlng {

/**
 * @brief The interface for a registry that can dispatch transactions and objects to extensions.
 *
 * This class defines the interface for dispatching data through to extensions.
 */
struct RegistryInterface {
    using RawLedgerObjectType = org::xrpl::rpc::v1::RawLedgerObject;
    using GetLedgerResponseType = org::xrpl::rpc::v1::GetLedgerResponse;
    using OptionalGetLedgerResponseType = std::optional<GetLedgerResponseType>;

    virtual ~RegistryInterface() = default;

    /**
     * @brief Dispatch initial objects.
     *
     * These objects are received during initial ledger load.
     *
     * @param seq The sequence
     * @param data The objects to dispatch
     */
    virtual void
    dispatchInitialObjects(uint32_t seq, std::vector<model::Object> const& data) noexcept(false) = 0;

    /**
     * @brief Dispatch initial transactions.
     *
     * These transactions are received during initial ledger load.
     *
     * @param seq The sequence
     * @param data The transactions to dispatch
     */
    virtual void
    dispatchInitialData(uint32_t seq, std::vector<model::Transaction> const& data) noexcept(false) = 0;

    /**
     * @brief Dispatch an entire ledger diff.
     *
     * This is used to dispatch incoming diffs to the extensions.
     *
     * @param data The data to dispatch
     */
    virtual void
    dispatch(model::LedgerData const& data) noexcept(false) = 0;
};

}  // namespace etlng
