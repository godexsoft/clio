/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

namespace rpc::spec {

struct Required {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return std::unexpected{rpc::Status{std::string{f.key()} + ": required field missing"}};
        return {};
    }
};

template <typename T>
struct Type;

template <>
struct Type<int64_t> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isInt64())
            return std::unexpected{rpc::Status{std::string{f.key()} + ": expected integer"}};
        return {};
    }
};

template <>
struct Type<bool> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isBool())
            return std::unexpected{rpc::Status{std::string{f.key()} + ": expected bool"}};
        return {};
    }
};

template <>
struct Type<std::string> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString())
            return std::unexpected{rpc::Status{std::string{f.key()} + ": expected string"}};
        return {};
    }
};

template <typename T>
struct Min {
    T bound;
    consteval explicit Min(T v) : bound{v}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    verify(FA const& f) const
    {
        if (!f.present())
            return {};
        if constexpr (std::is_same_v<T, int64_t>) {
            if (!f.isInt64())
                return {};
            if (f.asInt64() < bound) {
                return std::unexpected{rpc::Status{std::string{f.key()} + ": value below minimum"}};
            }
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

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    modify(FA& f) const
    {
        if (!f.present())
            return {};
        if constexpr (std::is_same_v<T, int64_t>) {
            if (!f.isInt64())
                return {};
            f.set(std::clamp(f.asInt64(), lo, hi));
        }
        return {};
    }
};

template <typename T>
Clamp(T, T) -> Clamp<T>;

struct Deprecated {
    template <SomeFieldAccess FA>
    [[nodiscard]] static std::optional<Warning>
    check(FA const& f)
    {
        if (f.present())
            return Warning{.field = std::string{f.key()}, .message = "field is deprecated"};
        return std::nullopt;
    }
};

struct AccountFormat {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{
                rpc::Status{std::string{f.key()} + ": expected string for account"}
            };
        }
        auto const sv = f.asString();
        if (sv.empty() || sv.front() != 'r') {
            return std::unexpected{
                rpc::Status{std::string{f.key()} + ": not a valid account (must start with 'r')"}
            };
        }
        return {};
    }
};

}  // namespace rpc::spec
