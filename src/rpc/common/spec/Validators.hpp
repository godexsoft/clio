/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"
#include "util/TimeUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace rpc::spec {

struct Required {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS,
                "Required field '" + std::string{f.key()} + "' missing"
            }};
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
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
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
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
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
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

template <>
struct Type<double> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isDouble())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

template <>
struct Type<uint32_t> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isUint32())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

template <typename T>
    requires(std::is_same_v<T, int64_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, double>)
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
                return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
            }
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            if (!f.isUint32())
                return {};
            if (f.asUint32() < bound) {
                return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
            }
        } else if constexpr (std::is_same_v<T, double>) {
            if (!f.isDouble())
                return {};
            if (f.asDouble() < bound) {
                return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
            }
        }
        return {};
    }
};

template <typename T>
Min(T) -> Min<T>;

template <typename T>
    requires(std::is_same_v<T, int64_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, double>)
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
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            if (!f.isUint32())
                return {};
            f.set(static_cast<uint32_t>(std::clamp(f.asUint32(), lo, hi)));
        } else if constexpr (std::is_same_v<T, double>) {
            if (!f.isDouble())
                return {};
            f.set(std::clamp(f.asDouble(), lo, hi));
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
        if (!f.isString())
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        if (!rpc::accountFromStringStrict(std::string{f.asString()}))
            return std::unexpected{
                rpc::Status{rpc::RippledError::rpcACT_MALFORMED, std::string{f.key()} + "Malformed"}
            };
        return {};
    }
};

class TimeFormatValidator final {
    std::string_view format_;

public:
    consteval explicit TimeFormatValidator(std::string_view format) noexcept : format_{format}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    verify(FA const& f) const
    {
        if (!f.present())
            return {};
        if (!f.isString())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        if (!util::systemTpFromUtcStr(std::string{f.asString()}, std::string{format_}))
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

}  // namespace rpc::spec
