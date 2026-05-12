/** @file */
#pragma once

#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/SpecDumpWriter.hpp"

#include <tuple>

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

template <typename... Fields>
void
dumpRpcSpec(SpecDumpWriter& w, RpcSpec<Fields...> const& spec)
{
    std::apply([&](auto const&... f) { (dumpFieldSpec(w, f), ...); }, spec.fields);
}

}  // namespace rpc::spec
