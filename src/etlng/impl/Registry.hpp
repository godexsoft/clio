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

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace etlng::impl {

consteval auto
checkNoDuplicates(auto&&... types)
{
    auto store = std::array{types...};
    auto end = store.end();
    std::ranges::sort(store);
    return (std::unique(std::begin(store), end) == end);
}

template <ripple::TxType... Types>
    requires(checkNoDuplicates(Types...))
struct Spec {
    static constexpr bool SpecTag = true;

    constexpr static bool
    wants(ripple::TxType t)
    {
        return ((Types == t) || ...);
    }
};

template <typename T>
concept HasLedgerDataHook = requires(T p) {
    { p.onLedgerData(std::declval<etlng::model::LedgerData>()) } -> std::same_as<void>;
};

template <typename T>
concept HasTransactionHook = requires(T p) {
    { p.onTransaction(uint32_t{}, std::declval<etlng::model::Transaction>()) } -> std::same_as<void>;
};

template <typename T>
concept HasInitialTransactionsHook = requires(T p) {
    {
        p.onInitialTransactions(uint32_t{}, std::declval<std::vector<etlng::model::Transaction>>())
    } -> std::same_as<void>;
};

template <typename T>
concept HasInitialTransactionHook = requires(T p) {
    { p.onInitialTransaction(uint32_t{}, std::declval<etlng::model::Transaction>()) } -> std::same_as<void>;
};

template <typename T>
concept HasInitialObjectsHook = requires(T p) {
    { p.onInitialObjects(uint32_t{}, std::declval<std::vector<etlng::model::Object>>()) } -> std::same_as<void>;
};

template <typename T>
concept HasInitialObjectHook = requires(T p) {
    { p.onInitialObject(uint32_t{}, std::declval<etlng::model::Object>()) } -> std::same_as<void>;
};

template <typename T>
concept ContainsSpec = std::decay_t<T>::spec::SpecTag;

template <typename T>
concept ContainsValidHook =
    HasLedgerDataHook<T> or (HasTransactionHook<T> and ContainsSpec<T>) or HasInitialTransactionsHook<T> or
    (HasInitialTransactionHook<T> and ContainsSpec<T>) or HasInitialObjectsHook<T> or HasInitialObjectHook<T>;

template <typename T>
concept NoTwoOfKind = not(HasLedgerDataHook<T> and HasTransactionHook<T>) and
    not(HasInitialTransactionsHook<T> and HasInitialTransactionHook<T>) and
    not(HasInitialObjectsHook<T> and HasInitialObjectHook<T>);

template <typename T>
concept SomeExtension = NoTwoOfKind<T> and ContainsValidHook<T>;

// Add spec stuff for non (s) versions

template <SomeExtension... Ps>
class Registry : public RegistryInterface {
    std::tuple<Ps...> store_;

    static_assert(
        (((not HasTransactionHook<std::decay_t<Ps>>) or ContainsSpec<std::decay_t<Ps>>) and ...),
        "Spec must be specified when 'onTransaction' function exists."
    );

    static_assert(
        (((not HasInitialTransactionHook<std::decay_t<Ps>>) or ContainsSpec<std::decay_t<Ps>>) and ...),
        "Spec must be specified when 'onInitialTransaction' function exists."
    );

public:
    Registry(SomeExtension auto&&... exts)
        requires(std::is_same_v<std::decay_t<decltype(exts)>, std::decay_t<Ps>> and ...)
        : store_(std::forward<Ps>(exts)...)
    {
    }

    void
    dispatch(model::LedgerData const& data) override
    {
        // send entire batch of data at once
        {
            auto const expand = [&](auto& p) {
                if constexpr (requires { p.onLedgerData(data); }) {
                    p.onLedgerData(data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send filtered transactions
        {
            auto const expand = [&]<typename P>(P& p, model::Transaction const& t) {
                if constexpr (requires { p.onTransaction(data.seq, t); }) {
                    if (std::decay_t<P>::spec::wants(t.type))
                        p.onTransaction(data.seq, t);
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
                if constexpr (requires { p.onInitialTransactions(seq, data); }) {
                    p.onInitialTransactions(seq, data);
                }
            };

            std::apply([&expand](auto&&... xs) { (expand(xs), ...); }, store_);
        }

        // send per object path
        {
            auto const expand = [&]<typename P>(P&& p, model::Transaction const& tx) {
                if constexpr (requires { p.onInitialTransaction(seq, tx); }) {
                    if (std::decay_t<P>::spec::wants(tx.type))
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
