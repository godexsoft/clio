/** @file */
#pragma once

#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <concepts>
#include <optional>
#include <string_view>

namespace rpc::spec {

template <typename T>
concept SomeRequirement = requires(T const a, boost::json::value const& v, std::string_view k) {
    { a.verify(v, k) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeModifier = requires(T const a, boost::json::value& v, std::string_view k) {
    { a.modify(v, k) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeCheck = requires(T const a, boost::json::value const& v, std::string_view k) {
    { a.check(v, k) } -> std::same_as<std::optional<Warning>>;
};

template <typename T>
concept SomeProcessor = SomeRequirement<T> || SomeModifier<T>;

template <typename T>
concept SomeFieldItem = SomeProcessor<T> || SomeCheck<T>;

}  // namespace rpc::spec
