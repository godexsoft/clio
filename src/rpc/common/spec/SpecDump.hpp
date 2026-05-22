/** @file */
#pragma once

#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/SpecDumpWriter.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>

namespace rpc::spec {

template <typename T>
concept HasSubFields = requires(T const& t) { t.subFields; };

template <typename T>
concept HasSubItems = requires(T const& t) { t.subItems; };

template <typename T>
concept HasWrapped = requires(T const& t) {
    { t.wrapped() };
};

template <typename T>
concept HasKName = requires {
    { T::kNAME };
};

template <typename T, typename Writer>
concept HasDescribeParams = requires(T const& t, Writer& w) { t.describeParams(w); };

template <typename Item>
void
dumpItem(SpecDumpWriter& w, Item const& item);

template <typename... Items>
void
dumpFieldSpec(SpecDumpWriter& w, FieldSpec<Items...> const& f);

template <typename... Fields>
void
dumpRpcSpec(SpecDumpWriter& w, RpcSpec<Fields...> const& spec);

template <typename Item>
void
dumpItem(SpecDumpWriter& w, Item const& item)
{
    if constexpr (HasSubFields<Item>) {
        w.bulletGroup(Item::kNAME, [&] {
            std::apply([&](auto const&... sf) { (dumpFieldSpec(w, sf), ...); }, item.subFields);
        });
    } else if constexpr (HasSubItems<Item>) {
        w.bulletGroup(Item::kNAME, [&] {
            if constexpr (HasDescribeParams<Item, SpecDumpWriter>)
                item.describeParams(w);
            std::apply([&](auto const&... it) { (dumpItem(w, it), ...); }, item.subItems);
        });
    } else if constexpr (HasWrapped<Item>) {
        w.bulletGroup(Item::kNAME, [&] {
            auto const msg = item.message();
            if (!msg.empty())
                w.param("message", msg);
            dumpItem(w, item.wrapped());
        });
    } else if constexpr (HasKName<Item>) {
        if constexpr (HasDescribeParams<Item, SpecDumpWriter>) {
            w.bulletGroup(Item::kNAME, [&] { item.describeParams(w); });
        } else {
            w.bullet(Item::kNAME, [] {});
        }
    } else {
        w.bullet("custom", [] {});
    }
}

template <typename... Items>
void
dumpFieldSpec(SpecDumpWriter& w, FieldSpec<Items...> const& f)
{
    w.bulletGroup(f.key, [&] {
        std::apply([&](auto const&... it) { (dumpItem(w, it), ...); }, f.items);
    });
}

namespace impl {

template <typename... Fields, std::size_t... Is>
void
dumpRpcSpec(SpecDumpWriter& w, RpcSpec<Fields...> const& spec, std::index_sequence<Is...>)
{
    if constexpr (sizeof...(Is) == 0) {
        return;
    } else {
        using FieldsTuple = typename RpcSpec<Fields...>::FieldsTuple;
        constexpr auto kN = sizeof...(Is);
        std::array<std::string_view, kN> const keys{std::get<Is>(spec.fields).key...};
        auto const plan = buildOverridePlan(keys);

        using DumpFn = void (*)(SpecDumpWriter&, FieldsTuple const&);
        static constexpr std::array<DumpFn, kN> kDISPATCH{
            +[](SpecDumpWriter& wr, FieldsTuple const& t) { dumpFieldSpec(wr, std::get<Is>(t)); }...
        };

        for (std::size_t i = 0; i < kN; ++i) {
            if (!plan.shouldRun[i])
                continue;
            kDISPATCH[plan.effectiveIdx[i]](w, spec.fields);
        }
    }
}

}  // namespace impl

template <typename... Fields>
void
dumpRpcSpec(SpecDumpWriter& w, RpcSpec<Fields...> const& spec)
{
    impl::dumpRpcSpec(w, spec, std::index_sequence_for<Fields...>{});
}

}  // namespace rpc::spec
