/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <string_view>
#include <tuple>

namespace rpc::spec {

template <typename Item>
MaybeError
callIfProcessor(Item const& item, boost::json::value& val, std::string_view key)
{
    if constexpr (SomeRequirement<Item>) {
        return item.verify(val, key);
    } else if constexpr (SomeModifier<Item>) {
        return item.modify(val, key);
    } else {
        return {};
    }
}

template <typename Item>
void
callIfChecker(Item const& item, boost::json::value const& val, std::string_view key, Warnings& out)
{
    if constexpr (SomeCheck<Item>) {
        if (auto w = item.check(val, key))
            out.push_back(std::move(*w));
    }
}

template <SomeFieldItem... Items>
struct FieldSpec {
    std::string_view key;
    std::tuple<Items...> items;

    consteval FieldSpec(std::string_view k, Items... i) : key{k}, items{i...}
    {
    }

    template <SomeFieldItem Item>
    [[nodiscard]] consteval auto
    operator|(Item item) const
    {
        return std::apply(
            [&](auto const&... existing) {
                return FieldSpec<Items..., Item>{key, existing..., item};
            },
            items
        );
    }

    [[nodiscard]] MaybeError
    process(boost::json::value& val) const
    {
        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                ((result = callIfProcessor(item, val, key), result.has_value()) && ...);
            },
            items
        );
        return result;
    }

    [[nodiscard]] Warnings
    check(boost::json::value const& val) const
    {
        Warnings out;
        std::apply([&](auto const&... item) { (callIfChecker(item, val, key, out), ...); }, items);
        return out;
    }
};

template <SomeFieldItem... Is>
FieldSpec(std::string_view, Is...) -> FieldSpec<Is...>;

consteval auto
field(std::string_view key)
{
    return FieldSpec<>{key};
}

template <SomeFieldItem... Items>
consteval auto
field(std::string_view key, Items... items)
{
    return FieldSpec{key, items...};
}

}  // namespace rpc::spec
