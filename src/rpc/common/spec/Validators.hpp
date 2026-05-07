/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"
#include "util/AccountUtils.hpp"
#include "util/TimeUtils.hpp"

#include <fmt/format.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/UintTypes.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace rpc::spec {

struct Required {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS,
                "Required field '" + std::string{f.key()} + "' missing"
            }};
        }
        return {};
    }
};

template <typename... Ts>
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

template <>
struct Type<JsonObject> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isObject())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

template <>
struct Type<JsonArray> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isArray())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        return {};
    }
};

// OR-semantics: accepts any of the listed types. Returns rpcINVALID_PARAMS if none match.
template <typename T1, typename T2, typename... Rest>
struct Type<T1, T2, Rest...> {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (f.template is<T1>() || f.template is<T2>() || (f.template is<Rest>() || ...))
            return {};
        return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
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
        if (f.present()) {
            return Warning{
                .code = rpc::WarningCode::WarnRpcDeprecated,
                .field = std::string{f.key()},
                .message = fmt::format("Field '{}' is deprecated.", f.key())
            };
        }
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
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        }
        if (!rpc::accountFromStringStrict(std::string{f.asString()})) {
            return std::unexpected{
                rpc::Status{rpc::RippledError::rpcACT_MALFORMED, std::string{f.key()} + "Malformed"}
            };
        }
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

// Helper: true if sv parses as a uint32 integer via from_chars.
[[nodiscard]] inline bool
checkIsU32Numeric(std::string_view sv)
{
    uint32_t unused = 0;
    auto [_, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), unused);
    return ec == std::errc();
}

// Validates a hex-encoded integer of the given ripple type (uint160/uint192/uint256).
// rpcINVALID_PARAMS + "<key>NotString" if not a string.
// rpcINVALID_PARAMS + "<key>Malformed" if not valid hex.
template <typename HexType>
    requires(
        std::is_same_v<HexType, ripple::uint160> || std::is_same_v<HexType, ripple::uint192> ||
        std::is_same_v<HexType, ripple::uint256>
    )
struct HexStringValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        }
        HexType parsed;
        if (!parsed.parseHex(std::string{f.asString()}.c_str())) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "Malformed"
            }};
        }
        return {};
    }
};

using Uint256HexStringValidator = HexStringValidator<ripple::uint256>;
using Uint192HexStringValidator = HexStringValidator<ripple::uint192>;
using Uint160HexStringValidator = HexStringValidator<ripple::uint160>;

// Accepts a ledger index: any integer, or a string that is "validated" / uint32-numeric.
// rpcINVALID_PARAMS + "ledgerIndexMalformed" on failure.
struct LedgerIndexValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (f.isInt64() || f.isUint32())
            return {};
        if (!f.isString()) {
            return std::unexpected{
                rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "ledgerIndexMalformed"}
            };
        }
        auto const sv = f.asString();
        if (sv == "validated" || checkIsU32Numeric(sv))
            return {};
        return std::unexpected{
            rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "ledgerIndexMalformed"}
        };
    }
};

// Validates a strictly base58-encoded AccountID (not hex pubkeys).
// rpcINVALID_PARAMS + "<key>NotString" if not string.
// ClioError::RpcMalformedAddress if not a valid base58 account or zero account.
struct AccountBase58Validator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        }
        auto const account = util::parseBase58Wrapper<ripple::AccountID>(std::string{f.asString()});
        if (!account || account->isZero()) {
            return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedAddress}};
        }
        return {};
    }
};

// Validates a currency string (XRP, 3-char ISO, or 40-char hex).
// rpcINVALID_PARAMS + "<key>NotString" if not string.
// rpcINVALID_PARAMS + "<key>IsEmpty" if empty string.
// ClioError::RpcMalformedCurrency + "malformedCurrency" if ripple::to_currency fails.
struct CurrencyValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        }
        auto const str = std::string{f.asString()};
        if (str.empty()) {
            return std::unexpected{
                rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "IsEmpty"}
            };
        }
        ripple::Currency currency;
        if (!ripple::to_currency(currency, str)) {
            return std::unexpected{
                rpc::Status{rpc::ClioError::RpcMalformedCurrency, "malformedCurrency"}
            };
        }
        return {};
    }
};

// Validates an issuer account string (hex or base58).
// rpcINVALID_PARAMS + "<key>NotString" if not string.
// rpcINVALID_PARAMS + "Invalid field '<key>', bad issuer." if ripple::to_issuer fails.
// rpcINVALID_PARAMS + "Invalid field '<key>', bad issuer account one." if noAccount().
struct IssuerValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS, std::string{f.key()} + "NotString"
            }};
        }
        ripple::AccountID issuer;
        if (!ripple::to_issuer(issuer, std::string{f.asString()})) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS,
                fmt::format("Invalid field '{}', bad issuer.", f.key())
            }};
        }
        if (issuer == ripple::noAccount()) {
            return std::unexpected{rpc::Status{
                rpc::RippledError::rpcINVALID_PARAMS,
                fmt::format("Invalid field '{}', bad issuer account one.", f.key())
            }};
        }
        return {};
    }
};

// Validates a {currency, issuer} object as a ripple::Issue.
// All failures return ClioError::RpcMalformedRequest.
// Rules: currency is required; XRP must have no issuer; non-XRP must have a valid issuer.
struct CurrencyIssueValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isObject()) {
            return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
        }
        auto const currFa = f.child("currency");
        if (!currFa.present() || !currFa.isString()) {
            return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
        }
        ripple::Currency currency{};
        if (!ripple::to_currency(currency, std::string{currFa.asString()})) {
            return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
        }
        auto const issuerFa = f.child("issuer");
        if (ripple::isXRP(currency)) {
            if (issuerFa.present()) {
                return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
            }
        } else {
            if (!issuerFa.present() || !issuerFa.isString()) {
                return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
            }
            ripple::AccountID issuer;
            if (!ripple::to_issuer(issuer, std::string{issuerFa.asString()})) {
                return std::unexpected{rpc::Status{rpc::ClioError::RpcMalformedRequest}};
            }
        }
        return {};
    }
};

// Converts a string field to an integer in-place.
// No-op when field is absent or already an integer.
// Returns rpcINVALID_PARAMS if the string looks like a float or is not numeric.
struct ToNumberModifier {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    modify(FA& f)
    {
        if (!f.present() || !f.isString())
            return {};
        auto const sv = f.asString();
        if (sv.find('.') != std::string_view::npos) {
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        }
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        if (ec != std::errc() || ptr != sv.data() + sv.size()) {
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
        }
        f.set(val);
        return {};
    }
};

// Validates a credential_type hex string: must be non-empty and <= maxCredentialTypeLength.
// All errors use ClioError::RpcMalformedAuthorizedCredentials.
struct CredentialTypeValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isString()) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                std::string{f.key()} + " NotString"
            }};
        }
        auto const decoded = ripple::strViewUnHex(f.asString());
        if (!decoded) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                std::string{f.key()} + " NotHexString"
            }};
        }
        if (decoded->empty()) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                std::string{f.key()} + " is empty"
            }};
        }
        if (decoded->size() > ripple::maxCredentialTypeLength) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                std::string{f.key()} + " greater than max length"
            }};
        }
        return {};
    }
};

// Validates an authorized_credentials array:
// - Must be an array: ClioError::RpcMalformedRequest + "<key> not array"
// - Must be non-empty: ClioError::RpcMalformedAuthorizedCredentials + message
// - Must be <= maxCredentialsArraySize: ClioError::RpcMalformedAuthorizedCredentials + message
// - Each element must be an object with "issuer" (required, IssuerValidator) and
//   "credential_type" (required, CredentialTypeValidator).
struct AuthorizeCredentialValidator {
    template <SomeFieldAccess FA>
    [[nodiscard]] static MaybeError
    verify(FA const& f)
    {
        if (!f.present())
            return {};
        if (!f.isArray()) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedRequest, std::string{f.key()} + " not array"
            }};
        }
        auto const sz = f.arraySize();
        if (sz == 0) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                "Requires at least one element in authorized_credentials array."
            }};
        }
        if (sz > ripple::maxCredentialsArraySize) {
            return std::unexpected{rpc::Status{
                rpc::ClioError::RpcMalformedAuthorizedCredentials,
                fmt::format(
                    "Max {} number of credentials in authorized_credentials array",
                    ripple::maxCredentialsArraySize
                )
            }};
        }
        for (std::size_t i = 0; i < sz; ++i) {
            auto const elem = f.element(i);
            if (!elem.isObject()) {
                return std::unexpected{rpc::Status{
                    rpc::ClioError::RpcMalformedAuthorizedCredentials,
                    "authorized_credentials elements in array are not objects."
                }};
            }
            auto const issuerFa = elem.child("issuer");
            if (!issuerFa.present()) {
                return std::unexpected{rpc::Status{
                    rpc::ClioError::RpcMalformedAuthorizedCredentials,
                    "Field 'Issuer' is required but missing."
                }};
            }
            if (auto err = IssuerValidator::verify(issuerFa); !err) {
                return std::unexpected{rpc::Status{
                    rpc::ClioError::RpcMalformedAuthorizedCredentials, "issuer NotString"
                }};
            }
            auto const credFa = elem.child("credential_type");
            if (!credFa.present()) {
                return std::unexpected{rpc::Status{
                    rpc::ClioError::RpcMalformedAuthorizedCredentials,
                    "Field 'CredentialType' is required but missing."
                }};
            }
            if (auto err = CredentialTypeValidator::verify(credFa); !err) {
                return err;
            }
        }
        return {};
    }
};

// Wraps a callable Fn that takes FA const& and returns MaybeError.
// The callable is invoked only when the field is present.
// Fn must be default-constructible and callable with FA const&.
template <typename Fn>
struct CustomValidator {
    Fn fn;

    consteval explicit CustomValidator(Fn f) : fn{f}
    {
    }

    template <SomeFieldAccess FA>
    [[nodiscard]] MaybeError
    verify(FA const& f) const
    {
        if (!f.present())
            return {};
        return fn(f);
    }
};

template <typename Fn>
CustomValidator(Fn) -> CustomValidator<Fn>;

}  // namespace rpc::spec
