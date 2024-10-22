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
#include "etlng/RegistryInterface.hpp"

#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <vector>

namespace etlng::impl {

template <ripple::TxType... Types>
struct Spec {
    constexpr static bool
    wants(ripple::TxType t)
    {
        return ((Types == t) || ...);
    }
};

template <typename... Ps>
class Registry : public RegistryInterface {
    std::tuple<Ps...> store_;

public:
    Registry(Ps&&... exts) : store_(std::forward<Ps>(exts)...)
    {
    }

    void
    dispatch(model::LedgerData const& data) override
    {
        // send entire batch of data (for objects etc.)
        {
            auto const expand = [&](auto& p) {
                if constexpr (requires { p.onLedgerData(data); }) {
                    p.onLedgerData(data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send filtered tx path
        {
            auto const expand = [&]<typename P>(P& p, model::Transaction const& t) {
                if constexpr (requires { p.onTransaction(t); }) {
                    if (P::spec::wants(t.type))
                        p.onTransaction(t);
                }
            };

            for (auto const& t : data.transactions) {
                std::apply([&expand, &t](auto&&... xs) { (expand(xs, t), ...); }, store_);
            }
        }
    }

    void
    dispatchInitialObjects(uint32_t seq, std::vector<model::Object> const& data) override
    {
        // send entire vector path
        {
            auto const expand = [&](auto&& p) {
                if constexpr (requires { p.onInitialObjects(seq, data); }) {
                    p.onInitialObjects(seq, data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send per object path
        {
            auto const expand = [&]<typename P>(P&& p, model::Object const& o) {
                if constexpr (requires { p.onInitialObject(seq, o); }) {
                    p.onInitialObject(seq, o);
                }
            };

            for (auto const& obj : data) {
                std::apply([&expand, &obj](auto&&... xs) { (expand(xs, obj), ...); }, store_);
            }
        }
    }

    void
    dispatchInitialData(uint32_t seq, std::vector<model::Transaction> const& data) override
    {
        // send entire vector path
        {
            auto const expand = [&](auto&& p) {
                if constexpr (requires { p.onInitialData(seq, data); }) {
                    p.onInitialData(seq, data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send per object path
        {
            auto const expand = [&]<typename P>(P&& p, model::Transaction const& tx) {
                if constexpr (requires { p.onInitialTransaction(seq, tx); }) {
                    p.onInitialTransaction(seq, tx);
                }
            };

            for (auto const& obj : data) {
                std::apply([&expand, &obj](auto&&... xs) { (expand(xs, obj), ...); }, store_);
            }
        }
    }
};

}  // namespace etlng::impl
