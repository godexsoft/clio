/** @file */
#pragma once

#include "rpc/Errors.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace rpc::spec {

/**
 * @brief Result type returned by validators and modifiers.
 */
using MaybeError = std::expected<void, rpc::Status>;

/**
 * @brief A single non-blocking warning emitted by a checker (e.g. field deprecation).
 */
struct Warning {
    rpc::WarningCode code;  // grouping key for the wire-format converter
    std::string field;      // identifier for the field that triggered the warning
    std::string message;    // extra context appended to the standard message for `code`
};

using Warnings = std::vector<Warning>;

// Marker types for use with Type<T> and is<T>() — keeps validators decoupled from boost::json.
struct JsonObject {};
struct JsonArray {};

/**
 * @brief Human-readable name for a JSON/scalar type tag, used by the spec dumper.
 *
 * Primary template intentionally undefined so an unsupported instantiation fails to link/compile
 * — that's a signal to add a specialization here rather than silently emitting a generic name.
 */
template <typename T>
constexpr std::string_view
typeNameOf() noexcept;

// clang-format off
template <> constexpr std::string_view typeNameOf<int64_t>()     noexcept { return "int64"; }
template <> constexpr std::string_view typeNameOf<int32_t>()     noexcept { return "int32"; }
template <> constexpr std::string_view typeNameOf<uint32_t>()    noexcept { return "uint32"; }
template <> constexpr std::string_view typeNameOf<uint64_t>()    noexcept { return "uint64"; }
template <> constexpr std::string_view typeNameOf<bool>()        noexcept { return "bool"; }
template <> constexpr std::string_view typeNameOf<double>()      noexcept { return "double"; }
template <> constexpr std::string_view typeNameOf<std::string>() noexcept { return "string"; }
template <> constexpr std::string_view typeNameOf<JsonObject>()  noexcept { return "object"; }
template <> constexpr std::string_view typeNameOf<JsonArray>()   noexcept { return "array"; }
// clang-format on

}  // namespace rpc::spec
