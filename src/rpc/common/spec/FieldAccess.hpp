/** @file */
#pragma once

#include <boost/json/string.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace rpc::spec {

/**
 * @brief Non-owning view of a single resolved field, usable by validators and modifiers.
 *
 * Backends provide a concrete type satisfying SomeFieldAccess (defined in Concepts.hpp)
 * and a corresponding makeFieldAccess(JsonObject[const]&, std::string_view) factory pair
 * found via ADL. FieldSpec constructs the access object once per field; validators never
 * see the raw JSON type.
 *
 * Two constructors carry const-correctness through:
 *   - mutable ctor (from JsonObject&):       both readValue_ and writeValue_ are set
 *   - const ctor   (from JsonObject const&): only readValue_ is set; set() is unreachable
 *     during check() since callIfChecker passes FA const& which prevents non-const calls.
 */
class BoostJsonFieldAccess {
    boost::json::value const* readValue_;  // nullptr when field is absent
    boost::json::value* writeValue_;       // nullptr when constructed from const

    std::string_view key_;

public:
    BoostJsonFieldAccess(boost::json::value* v, std::string_view k) noexcept
        : readValue_{v}, writeValue_{v}, key_{k}
    {
    }

    BoostJsonFieldAccess(boost::json::value const* v, std::string_view k) noexcept
        : readValue_{v}, writeValue_{nullptr}, key_{k}
    {
    }

    [[nodiscard]] std::string_view
    key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] bool
    present() const noexcept
    {
        return readValue_ != nullptr;
    }

    [[nodiscard]] bool
    isInt64() const noexcept
    {
        return readValue_ != nullptr && (readValue_->is_int64() || readValue_->is_uint64());
    }

    [[nodiscard]] int64_t
    asInt64() const
    {
        return readValue_->is_int64() ? readValue_->as_int64()
                                      : static_cast<int64_t>(readValue_->as_uint64());
    }

    [[nodiscard]] bool
    isBool() const noexcept
    {
        return readValue_ != nullptr && readValue_->is_bool();
    }

    [[nodiscard]] bool
    asBool() const
    {
        return readValue_->as_bool();
    }

    [[nodiscard]] bool
    isString() const noexcept
    {
        return readValue_ != nullptr && readValue_->is_string();
    }

    [[nodiscard]] std::string_view
    asString() const
    {
        return readValue_->as_string();
    }

    [[nodiscard]] bool
    isDouble() const noexcept
    {
        return readValue_ != nullptr && readValue_->is_double();
    }

    [[nodiscard]] double
    asDouble() const
    {
        return readValue_->as_double();
    }

    template <typename T>
    [[nodiscard]] bool
    is() const noexcept
    {
        if constexpr (std::is_same_v<T, int64_t>) {
            return isInt64();
        } else if constexpr (std::is_same_v<T, bool>) {
            return isBool();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return isString();
        } else if constexpr (std::is_same_v<T, double>) {
            return isDouble();
        }
        return false;
    }

    void
    set(int64_t v)
    {
        *writeValue_ = v;
    }

    void
    set(std::string_view v)
    {
        *writeValue_ = boost::json::string{v};
    }

    void
    set(bool v)
    {
        *writeValue_ = v;
    }

    void
    set(double v)
    {
        *writeValue_ = v;
    }
};

[[nodiscard]] inline BoostJsonFieldAccess
makeFieldAccess(boost::json::value& obj, std::string_view key)
{
    if (obj.is_object()) {
        if (auto it = obj.as_object().find(key); it != obj.as_object().end())
            return BoostJsonFieldAccess{&it->value(), key};
    }
    return BoostJsonFieldAccess{static_cast<boost::json::value*>(nullptr), key};
}

[[nodiscard]] inline BoostJsonFieldAccess
makeFieldAccess(boost::json::value const& obj, std::string_view key)
{
    if (obj.is_object()) {
        if (auto it = obj.as_object().find(key); it != obj.as_object().end())
            return BoostJsonFieldAccess{&it->value(), key};
    }
    return BoostJsonFieldAccess{static_cast<boost::json::value const*>(nullptr), key};
}

}  // namespace rpc::spec
