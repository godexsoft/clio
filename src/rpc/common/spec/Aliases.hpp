/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/IfType.hpp"
#include "rpc/common/spec/Section.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "rpc/common/spec/WithCustomError.hpp"

#include <array>
#include <string>
#include <string_view>

namespace rpc::spec {

// NOLINTBEGIN(readability-identifier-naming)
inline constexpr auto required = Required{};
inline constexpr auto deprecated = Deprecated{};
inline constexpr auto account = AccountFormat{};

template <typename... Ts>
inline constexpr auto type = Type<Ts...>{};
// NOLINTEND(readability-identifier-naming)

template <typename T>
consteval auto
min(T v)
{
    return Min{v};
}

template <typename T>
consteval auto
clamp(T lo, T hi)
{
    return Clamp{lo, hi};
}

/**
 * @brief Coerces an integer-valued field into the inclusive range of `Target`.
 *
 * Mirrors the old `validation::Type<Target>{}` silent-clamp behaviour. Pair with `type<int64_t>`
 * or `type<uint32_t>` when downstream code reads the value as a narrower integer.
 *
 * Example: `field(JS(ledger_index_min), type<int64_t>, clampAs<int32_t>)`
 */
// NOLINTBEGIN(readability-identifier-naming)
template <typename Target>
inline constexpr auto clampAs = ClampAs<Target>{};
// NOLINTEND(readability-identifier-naming)

/**
 * @brief Factory for IfType — T must be specified explicitly, SubItems are deduced.
 *
 * Example: spec::ifType<int64_t>(spec::min(int64_t{0}), spec::clamp(int64_t{10}, int64_t{400}))
 */
template <typename T, SomeProcessor... SubItems>
consteval auto
ifType(SubItems... items)
{
    return IfType<T, SubItems...>{items...};
}

/**
 * @brief Factory for WithCustomError — wraps a requirement or modifier with a custom error.
 *
 * Examples:
 *   spec::withCustomError(spec::required, rpc::RippledError::rpcINVALID_PARAMS)
 *   spec::withCustomError(spec::required, rpc::RippledError::rpcINVALID_PARAMS, "missing marker")
 */
template <typename Wrapped>
consteval auto
withCustomError(Wrapped w, rpc::CombinedError code, std::string_view message = {})
{
    return WithCustomError<Wrapped>{w, code, message};
}

/**
 * @brief Factory for TimeFormatValidator.
 *
 * Example: spec::timeFormat("%Y-%m-%dT%TZ")
 */
consteval auto
timeFormat(std::string_view format)
{
    return TimeFormatValidator{format};
}

/**
 * @brief Factory for Section — validates named sub-fields within an object field.
 *
 * Example:
 *   spec::field("taker_pays", spec::section(
 *       spec::field("currency", spec::required),
 *       spec::field("issuer",   spec::account)
 *   ))
 */
template <typename... SubFields>
consteval auto
section(SubFields... sf)
{
    return Section<SubFields...>{sf...};
}

// NOLINTBEGIN(readability-identifier-naming)
inline constexpr auto ledgerIndex = LedgerIndexValidator{};
inline constexpr auto accountBase58 = AccountBase58Validator{};
inline constexpr auto currency = CurrencyValidator{};
inline constexpr auto issuer = IssuerValidator{};
inline constexpr auto currencyIssue = CurrencyIssueValidator{};
inline constexpr auto credentialType = CredentialTypeValidator{};
inline constexpr auto authorizeCredential = AuthorizeCredentialValidator{};
inline constexpr auto toNumber = ToNumberModifier{};
inline constexpr auto uint256Hex = Uint256HexStringValidator{};
inline constexpr auto uint192Hex = Uint192HexStringValidator{};
inline constexpr auto uint160Hex = Uint160HexStringValidator{};
inline constexpr auto notSupported = NotSupported{};

/**
 * @brief Factory for `NotSupportedIfEqual` — rejects a field only when its value matches.
 *
 * Example: `spec::notSupportedIf(true)` rejects `field: true` but accepts `field: false`.
 */
template <typename T>
consteval auto
notSupportedIf(T value)
{
    return NotSupportedIfEqual{value};
}

inline constexpr auto toLower = ToLowerModifier{};
inline constexpr auto hex256Array = Hex256ArrayValidator{};
inline constexpr auto accountMarker = AccountMarkerValidator{};
inline constexpr auto accountType = AccountTypeValidator{};
inline constexpr auto ledgerType = LedgerEntryTypeValidator{};
// NOLINTEND(readability-identifier-naming)

/**
 * @brief Factory for OneOfValidator — validates that a string field is one of the given values.
 *
 * Example: spec::oneOf<std::string>("gateway", "user")
 */
template <typename T = std::string, typename... Strings>
consteval auto
oneOf(Strings... vals)
{
    return OneOfValidator<sizeof...(Strings)>{
        std::array<std::string_view, sizeof...(Strings)>{std::string_view{vals}...}
    };
}

/**
 * @brief Factory for Between — validates a numeric field is in [lo, hi].
 *
 * Example: spec::between(uint32_t{1}, uint32_t{25})
 */
template <typename T>
consteval auto
between(T lo, T hi)
{
    return Between{lo, hi};
}

/**
 * @brief Factory for CustomModifier — wraps a captureless lambda as a modifier.
 *
 * Example: spec::customModifier([](auto& f) -> rpc::MaybeError { ... })
 */
template <typename Fn>
consteval auto
customModifier(Fn f)
{
    return CustomModifier<Fn>{f};
}

}  // namespace rpc::spec
