/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/IfType.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "rpc/common/spec/WithCustomError.hpp"

#include <string_view>

namespace rpc::spec {

// NOLINTBEGIN(readability-identifier-naming)
inline constexpr auto required = Required{};
inline constexpr auto deprecated = Deprecated{};
inline constexpr auto account = AccountFormat{};

template <typename T>
inline constexpr auto type = Type<T>{};
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

}  // namespace rpc::spec
