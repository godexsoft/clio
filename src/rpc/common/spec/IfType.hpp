/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace rpc::spec {

template <typename T>
bool
jsonTypeMatches(boost::json::value const& fieldVal)
{
    if constexpr (std::is_same_v<T, int64_t>) {
        return fieldVal.is_int64() || fieldVal.is_uint64();
    } else if constexpr (std::is_same_v<T, bool>) {
        return fieldVal.is_bool();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return fieldVal.is_string();
    } else if constexpr (std::is_same_v<T, double>) {
        return fieldVal.is_double();
    }
    return false;
}

/**
 * @brief Runs sub-processors only when the field's JSON type matches T at runtime.
 *
 * Satisfies SomeModifier so it receives a mutable reference, enabling both
 * requirement and modifier sub-items. Checkers are excluded from sub-items;
 * hang them directly on the FieldSpec if conditional warning emission is needed.
 */
template <typename T, SomeProcessor... SubItems>
struct IfType {
    std::tuple<SubItems...> subItems;

    consteval explicit IfType(SubItems... s) : subItems{s...}
    {
    }

    [[nodiscard]] MaybeError
    modify(boost::json::value& val, std::string_view key) const
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if (!jsonTypeMatches<T>(it->value()))
            return {};

        if constexpr (sizeof...(SubItems) == 0) {
            return {};
        } else {
            MaybeError result{};
            std::apply(
                [&](auto const&... item) {
                    ((result = callIfProcessor(item, val, key), result.has_value()) && ...);
                },
                subItems
            );
            return result;
        }
    }
};

}  // namespace rpc::spec
