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

namespace detail {

// Archetype satisfying SomeFieldAccess. Used as the witness type for
// validator/modifier/checker concepts so they aren't coupled to any backend.
// Never instantiated; declarations only.
struct FieldAccessArchetype {
    [[nodiscard]] std::string_view
    key() const noexcept;
    [[nodiscard]] bool
    present() const noexcept;
    [[nodiscard]] bool
    isInt64() const noexcept;
    [[nodiscard]] int64_t
    asInt64() const;
    [[nodiscard]] bool
    isBool() const noexcept;
    [[nodiscard]] bool
    asBool() const;
    [[nodiscard]] bool
    isString() const noexcept;
    [[nodiscard]] std::string_view
    asString() const;
    [[nodiscard]] bool
    isDouble() const noexcept;
    [[nodiscard]] double
    asDouble() const;
    template <typename T>
    [[nodiscard]] bool
    is() const noexcept;
    void
    set(int64_t);
    void set(std::string_view);
    void
    set(bool);
    void
    set(double);
};

}  // namespace detail

static_assert(SomeFieldAccess<detail::FieldAccessArchetype>);

// Validator concepts use detail::FieldAccessArchetype as the witness type so they
// are decoupled from any concrete backend. Validators written as templates over
// SomeFieldAccess satisfy these concepts automatically.

template <typename T>
concept SomeRequirement = requires(T const a, detail::FieldAccessArchetype const& f) {
    { a.verify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeModifier = requires(T const a, detail::FieldAccessArchetype& f) {
    { a.modify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeCheck = requires(T const a, detail::FieldAccessArchetype const& f) {
    { a.check(f) } -> std::same_as<std::optional<Warning>>;
};

template <typename T>
concept SomeProcessor = SomeRequirement<T> || SomeModifier<T>;

template <typename T>
concept SomeFieldItem = SomeProcessor<T> || SomeCheck<T>;

}  // namespace rpc::spec
