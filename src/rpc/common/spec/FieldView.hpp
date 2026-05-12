/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/string.hpp>
#include <boost/json/value.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace rpc::spec {

/**
 * @brief Non-owning view of a single resolved field, usable by validators and modifiers.
 *
 * Backends provide a concrete type satisfying SomeFieldView (defined in Concepts.hpp)
 * and a SomeObjectView type whose child() method returns it. FieldSpec obtains the FA via
 * the root's child(key); validators never see the raw JSON type.
 *
 * Two constructors carry const-correctness through:
 *   - mutable ctor (from value&):       both readValue_ and writeValue_ are set
 *   - const ctor   (from value const&): only readValue_ is set; set() is unreachable
 *     during check() since callIfChecker passes FA const& which prevents non-const calls.
 */
class BoostJsonFieldView {
    boost::json::value const* readValue_;  // nullptr when field is absent
    boost::json::value* writeValue_;       // nullptr when constructed from const

    std::string_view key_;

public:
    BoostJsonFieldView(boost::json::value* v, std::string_view k) noexcept
        : readValue_{v}, writeValue_{v}, key_{k}
    {
    }

    BoostJsonFieldView(boost::json::value const* v, std::string_view k) noexcept
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
        return readValue_ != nullptr &&
            (readValue_->is_int64() ||
             (readValue_->is_uint64() &&
              readValue_->as_uint64() <=
                  static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
    }

    [[nodiscard]] int64_t
    asInt64() const
    {
        return readValue_->is_int64() ? readValue_->as_int64()
                                      : static_cast<int64_t>(readValue_->as_uint64());
    }

    [[nodiscard]] bool
    isUint32() const noexcept
    {
        if (readValue_ == nullptr)
            return false;
        if (readValue_->is_uint64())
            return readValue_->as_uint64() <= std::numeric_limits<uint32_t>::max();
        if (readValue_->is_int64()) {
            auto const v = readValue_->as_int64();
            return v >= 0 && v <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
        }
        return false;
    }

    [[nodiscard]] uint32_t
    asUint32() const
    {
        return readValue_->is_uint64() ? static_cast<uint32_t>(readValue_->as_uint64())
                                       : static_cast<uint32_t>(readValue_->as_int64());
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

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return readValue_ != nullptr && readValue_->is_object();
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return readValue_ != nullptr && readValue_->is_array();
    }

    [[nodiscard]] std::size_t
    arraySize() const noexcept
    {
        if (readValue_ == nullptr || !readValue_->is_array())
            return 0;
        return readValue_->as_array().size();
    }

    // Returns a FieldView for a named sub-field within this field (must be an object).
    // If this field is absent, not an object, or the child key is not found, returns an absent FA.
    [[nodiscard]] BoostJsonFieldView
    child(std::string_view childKey) const noexcept
    {
        if (writeValue_ != nullptr && writeValue_->is_object()) {
            auto& obj = writeValue_->as_object();
            auto it = obj.find(childKey);
            if (it == obj.end())
                return {static_cast<boost::json::value*>(nullptr), childKey};
            return {&it->value(), childKey};
        }
        if (readValue_ == nullptr || !readValue_->is_object())
            return {static_cast<boost::json::value const*>(nullptr), childKey};
        auto const& obj = readValue_->as_object();
        auto it = obj.find(childKey);
        if (it == obj.end())
            return {static_cast<boost::json::value const*>(nullptr), childKey};
        return {&it->value(), childKey};
    }

    // Returns a FieldView for an element within this field (must be an array).
    // If this field is absent, not an array, or idx is out of bounds, returns an absent FA.
    // The child FA inherits the parent key for error message context.
    [[nodiscard]] BoostJsonFieldView
    element(std::size_t idx) const noexcept
    {
        if (writeValue_ != nullptr && writeValue_->is_array()) {
            auto& arr = writeValue_->as_array();
            if (idx >= arr.size())
                return {static_cast<boost::json::value*>(nullptr), key_};
            return {&arr[idx], key_};
        }
        if (readValue_ == nullptr || !readValue_->is_array())
            return {static_cast<boost::json::value const*>(nullptr), key_};
        auto const& arr = readValue_->as_array();
        if (idx >= arr.size())
            return {static_cast<boost::json::value const*>(nullptr), key_};
        return {&arr[idx], key_};
    }

    template <typename T>
    [[nodiscard]] bool
    is() const noexcept
    {
        if constexpr (std::is_same_v<T, int64_t>) {
            return isInt64();
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return isUint32();
        } else if constexpr (std::is_same_v<T, bool>) {
            return isBool();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return isString();
        } else if constexpr (std::is_same_v<T, double>) {
            return isDouble();
        } else if constexpr (std::is_same_v<T, JsonObject>) {
            return isObject();
        } else if constexpr (std::is_same_v<T, JsonArray>) {
            return isArray();
        } else {
            static_assert(false, "unsupported type for is<T>()");
        }
    }

    void
    set(int64_t v)
    {
        *writeValue_ = v;
    }

    void
    set(uint32_t v)
    {
        *writeValue_ = static_cast<uint64_t>(v);  // boost::json stores unsigned as uint64
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

static_assert(SomeFieldView<BoostJsonFieldView>);

/**
 * @brief Non-owning access object for the document root.
 *
 * Distinct from BoostJsonFieldView: the root has no name, is always present, and is
 * only used by RpcSpec/FieldSpec to navigate into named fields via child(). Keeping the
 * type separate prevents passing a keyless FA into validators.
 */
class BoostJsonObjectView {
    boost::json::value const* readValue_;
    boost::json::value* writeValue_;

public:
    explicit BoostJsonObjectView(boost::json::value& v) noexcept : readValue_{&v}, writeValue_{&v}
    {
    }

    explicit BoostJsonObjectView(boost::json::value const& v) noexcept
        : readValue_{&v}, writeValue_{nullptr}
    {
    }

    [[nodiscard]] bool
    isObject() const noexcept
    {
        return readValue_->is_object();
    }

    [[nodiscard]] bool
    isArray() const noexcept
    {
        return readValue_->is_array();
    }

    [[nodiscard]] BoostJsonFieldView
    child(std::string_view key) noexcept
    {
        if (writeValue_ != nullptr && writeValue_->is_object()) {
            auto& obj = writeValue_->as_object();
            if (auto it = obj.find(key); it != obj.end())
                return BoostJsonFieldView{&it->value(), key};
        }
        return BoostJsonFieldView{static_cast<boost::json::value*>(nullptr), key};
    }

    [[nodiscard]] BoostJsonFieldView
    child(std::string_view key) const noexcept
    {
        if (readValue_->is_object()) {
            auto const& obj = readValue_->as_object();
            if (auto it = obj.find(key); it != obj.end())
                return BoostJsonFieldView{&it->value(), key};
        }
        return BoostJsonFieldView{static_cast<boost::json::value const*>(nullptr), key};
    }
};

static_assert(SomeObjectView<BoostJsonObjectView>);

// Backend-selection aliases. Change these to swap JSON libraries — the spec system
// (RpcSpec, FieldSpec, Validators, Section) is templated on the concepts above and
// is otherwise independent of any concrete JSON type.
using FieldView = BoostJsonFieldView;
using ObjectView = BoostJsonObjectView;

}  // namespace rpc::spec
