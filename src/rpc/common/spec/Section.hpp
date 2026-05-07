/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <tuple>

namespace rpc::spec {

/**
 * @brief Validates a sub-object's fields using a list of FieldSpecs.
 *
 * Satisfies SomeModifier so it participates in the process() path and can run
 * both requirements and modifiers on nested fields. When the field is absent the
 * Section is a no-op; when the field is present but not an object it returns
 * rpcINVALID_PARAMS. Nested FieldSpecs are applied via processNested() which
 * navigates into the object using FA::child().
 *
 * Example:
 *   field("taker_pays", section(
 *       field("currency", required, type<std::string>),
 *       field("issuer",   account)
 *   ))
 */
template <typename... SubFields>
struct Section {
    std::tuple<SubFields...> subFields;

    consteval explicit Section(SubFields... sf) : subFields{sf...}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
    {
        if (!fa.present())
            return {};
        if (!fa.isObject())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};

        MaybeError result{};
        std::apply(
            [&](auto const&... subSpec) {
                (void)((result = subSpec.processNested(fa), result.has_value()) && ...);
            },
            subFields
        );
        return result;
    }
};

template <typename... Fs>
Section(Fs...) -> Section<Fs...>;

/**
 * @brief Runs sub-processors only when the field holds a JSON object.
 *
 * The sub-processors receive the object field FA directly (not its children),
 * so they are typically Section instances or other object-aware validators.
 *
 * Example:
 *   field("entry", ifObject(section(field("a", required), field("b", type<std::string>))))
 */
template <SomeProcessor... SubItems>
struct IfObject {
    std::tuple<SubItems...> subItems;

    consteval explicit IfObject(SubItems... s) : subItems{s...}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
    {
        if (!fa.present() || !fa.isObject())
            return {};

        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                (void)((result = callIfProcessor(item, fa), result.has_value()) && ...);
            },
            subItems
        );
        return result;
    }
};

template <typename... Ss>
IfObject(Ss...) -> IfObject<Ss...>;

/**
 * @brief Runs sub-processors only when the field holds a JSON array.
 *
 * Sub-processors receive the array field FA directly. Use FA::element(idx) inside
 * a custom processor to access individual array elements.
 *
 * Example:
 *   field("ids", ifArray(someArrayValidator))
 */
template <SomeProcessor... SubItems>
struct IfArray {
    std::tuple<SubItems...> subItems;

    consteval explicit IfArray(SubItems... s) : subItems{s...}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
    {
        if (!fa.present() || !fa.isArray())
            return {};

        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                (void)((result = callIfProcessor(item, fa), result.has_value()) && ...);
            },
            subItems
        );
        return result;
    }
};

template <typename... Ss>
IfArray(Ss...) -> IfArray<Ss...>;

}  // namespace rpc::spec
