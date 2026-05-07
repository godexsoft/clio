/** @file */
#pragma once

#include "rpc/common/spec/FieldAccess.hpp"
#include "rpc/common/spec/Types.hpp"

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpc::spec {

/**
 * @brief Interface contract for field access objects produced by makeFieldAccess backends.
 */
template <typename T>
concept SomeFieldAccess = requires(T f, T const cf) {
    { cf.key() } -> std::convertible_to<std::string_view>;
    { cf.present() } -> std::convertible_to<bool>;
    { cf.isInt64() } -> std::convertible_to<bool>;
    { cf.asInt64() } -> std::convertible_to<int64_t>;
    { cf.isBool() } -> std::convertible_to<bool>;
    { cf.asBool() } -> std::convertible_to<bool>;
    { cf.isString() } -> std::convertible_to<bool>;
    { cf.asString() } -> std::convertible_to<std::string_view>;
    { cf.isDouble() } -> std::convertible_to<bool>;
    { cf.asDouble() } -> std::convertible_to<double>;
    { cf.template is<int64_t>() } -> std::convertible_to<bool>;
    { cf.template is<bool>() } -> std::convertible_to<bool>;
    { cf.template is<std::string>() } -> std::convertible_to<bool>;
    { cf.template is<double>() } -> std::convertible_to<bool>;
    { f.set(int64_t{}) };
    { f.set(std::string_view{}) };
    { f.set(bool{}) };
    { f.set(double{}) };
};

static_assert(SomeFieldAccess<BoostJsonFieldAccess>);

// Validator concepts use BoostJsonFieldAccess as the archetype. Validators written
// as templates over SomeFieldAccess satisfy the concept automatically since
// BoostJsonFieldAccess satisfies SomeFieldAccess.

template <typename T>
concept SomeRequirement = requires(T const a, BoostJsonFieldAccess const& f) {
    { a.verify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeModifier = requires(T const a, BoostJsonFieldAccess& f) {
    { a.modify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeCheck = requires(T const a, BoostJsonFieldAccess const& f) {
    { a.check(f) } -> std::same_as<std::optional<Warning>>;
};

template <typename T>
concept SomeProcessor = SomeRequirement<T> || SomeModifier<T>;

template <typename T>
concept SomeFieldItem = SomeProcessor<T> || SomeCheck<T>;

}  // namespace rpc::spec
