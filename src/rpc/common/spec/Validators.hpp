/** @file */
#pragma once

#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace rpc::spec {

struct Required {
    [[nodiscard]] static MaybeError
    verify(boost::json::value const& val, std::string_view key)
    {
        if (!val.is_object() || !val.as_object().contains(key))
            return std::unexpected{std::string{key} + ": required field missing"};
        return {};
    }
};

template <typename T>
struct Type;

template <>
struct Type<int64_t> {
    [[nodiscard]] static MaybeError
    verify(boost::json::value const& val, std::string_view key)
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if (!it->value().is_int64() && !it->value().is_uint64())
            return std::unexpected{std::string{key} + ": expected integer"};
        return {};
    }
};

template <>
struct Type<bool> {
    [[nodiscard]] static MaybeError
    verify(boost::json::value const& val, std::string_view key)
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if (!it->value().is_bool())
            return std::unexpected{std::string{key} + ": expected bool"};
        return {};
    }
};

template <>
struct Type<std::string> {
    [[nodiscard]] static MaybeError
    verify(boost::json::value const& val, std::string_view key)
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if (!it->value().is_string())
            return std::unexpected{std::string{key} + ": expected string"};
        return {};
    }
};

template <typename T>
struct Min {
    T bound;
    consteval explicit Min(T v) : bound{v}
    {
    }

    [[nodiscard]] MaybeError
    verify(boost::json::value const& val, std::string_view key) const
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if constexpr (std::is_same_v<T, int64_t>) {
            if (!it->value().is_int64() && !it->value().is_uint64())
                return {};
            int64_t const v = it->value().is_int64()
                ? it->value().as_int64()
                : static_cast<int64_t>(it->value().as_uint64());
            if (v < bound)
                return std::unexpected{std::string{key} + ": value below minimum"};
        }
        return {};
    }
};

template <typename T>
Min(T) -> Min<T>;

template <typename T>
struct Clamp {
    T lo, hi;
    consteval Clamp(T l, T h) : lo{l}, hi{h}
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
        if constexpr (std::is_same_v<T, int64_t>) {
            if (!it->value().is_int64() && !it->value().is_uint64())
                return {};
            int64_t const v = it->value().is_int64()
                ? it->value().as_int64()
                : static_cast<int64_t>(it->value().as_uint64());
            it->value() = std::clamp(v, lo, hi);
        }
        return {};
    }
};

template <typename T>
Clamp(T, T) -> Clamp<T>;

struct Deprecated {
    [[nodiscard]] static std::optional<Warning>
    check(boost::json::value const& val, std::string_view key)
    {
        if (val.is_object() && val.as_object().contains(key))
            return Warning{.field = std::string{key}, .message = "field is deprecated"};
        return std::nullopt;
    }
};

struct AccountFormat {
    [[nodiscard]] static MaybeError
    verify(boost::json::value const& val, std::string_view key)
    {
        if (!val.is_object())
            return {};
        auto it = val.as_object().find(key);
        if (it == val.as_object().end())
            return {};
        if (!it->value().is_string())
            return std::unexpected{std::string{key} + ": expected string for account"};
        auto const sv = it->value().as_string();
        if (sv.empty() || sv.front() != 'r') {
            return std::unexpected{
                std::string{key} + ": not a valid account (must start with 'r')"
            };
        }
        return {};
    }
};

}  // namespace rpc::spec
