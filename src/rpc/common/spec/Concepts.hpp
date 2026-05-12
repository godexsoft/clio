/** @file */
#pragma once

#include "rpc/common/spec/Types.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpc::spec {

/**
 * @brief Non-owning view of a single resolved field within a JSON document.
 *
 * Implemented by a backend type (e.g. the boost::json adapter). Validators and
 * modifiers receive instances of any such type through a template parameter,
 * so they never depend on a concrete JSON library.
 */
template <typename T>
concept SomeFieldView = requires(T f, T const cf) {
    { cf.key() } -> std::convertible_to<std::string_view>;
    { cf.present() } -> std::convertible_to<bool>;
    { cf.isInt64() } -> std::convertible_to<bool>;
    { cf.asInt64() } -> std::convertible_to<int64_t>;
    { cf.isUint32() } -> std::convertible_to<bool>;
    { cf.asUint32() } -> std::convertible_to<uint32_t>;
    { cf.isBool() } -> std::convertible_to<bool>;
    { cf.asBool() } -> std::convertible_to<bool>;
    { cf.isString() } -> std::convertible_to<bool>;
    { cf.asString() } -> std::convertible_to<std::string_view>;
    { cf.isDouble() } -> std::convertible_to<bool>;
    { cf.asDouble() } -> std::convertible_to<double>;
    { cf.isObject() } -> std::convertible_to<bool>;
    { cf.isArray() } -> std::convertible_to<bool>;
    { cf.arraySize() } -> std::convertible_to<std::size_t>;
    { cf.template is<JsonObject>() } -> std::convertible_to<bool>;
    { cf.template is<JsonArray>() } -> std::convertible_to<bool>;
    { cf.template is<int64_t>() } -> std::convertible_to<bool>;
    { cf.template is<uint32_t>() } -> std::convertible_to<bool>;
    { cf.template is<bool>() } -> std::convertible_to<bool>;
    { cf.template is<std::string>() } -> std::convertible_to<bool>;
    { cf.template is<double>() } -> std::convertible_to<bool>;
    { cf.child(std::string_view{}) } -> std::same_as<T>;
    { cf.element(std::size_t{}) } -> std::same_as<T>;
    { f.set(int64_t{}) };
    { f.set(uint32_t{}) };
    { f.set(std::string_view{}) };
    { f.set(bool{}) };
    { f.set(double{}) };
};

namespace detail {

// Archetype satisfying SomeFieldView. Used as the witness type for
// validator/modifier/checker concepts so they aren't coupled to any backend.
// Never instantiated; declarations only.
struct FieldViewArchetype {
    [[nodiscard]] std::string_view
    key() const noexcept;
    [[nodiscard]] bool
    present() const noexcept;
    [[nodiscard]] bool
    isInt64() const noexcept;
    [[nodiscard]] int64_t
    asInt64() const;
    [[nodiscard]] bool
    isUint32() const noexcept;
    [[nodiscard]] uint32_t
    asUint32() const;
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
    [[nodiscard]] bool
    isObject() const noexcept;
    [[nodiscard]] bool
    isArray() const noexcept;
    [[nodiscard]] std::size_t
    arraySize() const noexcept;
    template <typename T>
    [[nodiscard]] bool
    is() const noexcept;
    [[nodiscard]] FieldViewArchetype child(std::string_view) const noexcept;
    [[nodiscard]] FieldViewArchetype
    element(std::size_t) const noexcept;
    void
    set(int64_t);
    void
    set(uint32_t);
    void set(std::string_view);
    void
    set(bool);
    void
    set(double);
};

}  // namespace detail

static_assert(SomeFieldView<detail::FieldViewArchetype>);

/**
 * @brief Non-owning view of the document root, which is always an object/dict.
 *
 * Distinct from SomeFieldView: the root has no name, is always present, cannot
 * be set, and is only used by RpcSpec/FieldSpec to navigate into named fields via
 * child(). Keeping the type distinct prevents passing a keyless field view into
 * validators.
 */
template <typename T>
concept SomeObjectView = requires(T const cr, T& mr) {
    { cr.isObject() } -> std::convertible_to<bool>;
    { cr.isArray() } -> std::convertible_to<bool>;
    { mr.child(std::string_view{}) } -> SomeFieldView;
    { cr.child(std::string_view{}) } -> SomeFieldView;
};

namespace detail {

// Archetype satisfying SomeObjectView. Used as the witness type for spec-level
// concepts so they aren't coupled to any backend. Never instantiated.
struct ObjectViewArchetype {
    [[nodiscard]] bool
    isObject() const noexcept;
    [[nodiscard]] bool
    isArray() const noexcept;
    [[nodiscard]] FieldViewArchetype child(std::string_view) noexcept;
    [[nodiscard]] FieldViewArchetype child(std::string_view) const noexcept;
};

}  // namespace detail

static_assert(SomeObjectView<detail::ObjectViewArchetype>);

// Validator concepts use detail::FieldViewArchetype as the witness type so they
// are decoupled from any concrete backend. Validators written as templates over
// SomeFieldView satisfy these concepts automatically.

template <typename T>
concept SomeRequirement = requires(T const a, detail::FieldViewArchetype const& f) {
    { a.verify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeModifier = requires(T const a, detail::FieldViewArchetype& f) {
    { a.modify(f) } -> std::same_as<MaybeError>;
};

template <typename T>
concept SomeCheck = requires(T const a, detail::FieldViewArchetype const& f) {
    { a.check(f) } -> std::same_as<std::optional<Warning>>;
};

template <typename T>
concept SomeProcessor = SomeRequirement<T> || SomeModifier<T>;

template <typename T>
concept SomeFieldItem = SomeProcessor<T> || SomeCheck<T>;

}  // namespace rpc::spec
