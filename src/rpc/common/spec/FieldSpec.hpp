/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"

#include <string_view>
#include <tuple>

namespace rpc::spec {

template <typename Item, typename FA>
MaybeError
callIfProcessor(Item const& item, FA& fa)
{
    if constexpr (SomeRequirement<Item>) {
        return item.verify(fa);
    } else if constexpr (SomeModifier<Item>) {
        return item.modify(fa);
    } else {
        return {};
    }
}

template <typename Item, typename FA>
void
callIfChecker(Item const& item, FA const& fa, Warnings& out)
{
    if constexpr (SomeCheck<Item>) {
        if (auto w = item.check(fa))
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

    template <SomeRootAccess Root>
    [[nodiscard]] MaybeError
    process(Root& root) const
    {
        auto fa = root.child(key);
        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                (void)((result = callIfProcessor(item, fa), result.has_value()) && ...);
            },
            items
        );
        return result;
    }

    template <SomeRootAccess Root>
    [[nodiscard]] Warnings
    check(Root const& root) const
    {
        auto fa = root.child(key);
        Warnings out;
        std::apply([&](auto const&... item) { (callIfChecker(item, fa, out), ...); }, items);
        return out;
    }

    // Used by Section: create a child FA from a parent FA and apply this field's processors.
    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    processNested(FA& parentFa) const
    {
        auto childFa = parentFa.child(key);
        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                (void)((result = callIfProcessor(item, childFa), result.has_value()) && ...);
            },
            items
        );
        return result;
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] Warnings
    checkNested(FA const& parentFa) const
    {
        auto const childFa = parentFa.child(key);
        Warnings out;
        std::apply([&](auto const&... item) { (callIfChecker(item, childFa, out), ...); }, items);
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
