/** @file */
#pragma once

#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

namespace rpc::spec {

/**
 * @brief Non-owning, type-erased view over any @ref RpcSpec instantiation.
 *
 * Stores a raw pointer to the underlying spec together with two stateless
 * function pointers; no heap allocation takes place and the constructor is
 * genuinely @c noexcept.
 *
 * **Lifetime contract**: this class does *not* own the spec it refers to.
 * The caller is responsible for ensuring that the @ref RpcSpec object
 * outlives every @c RpcSpecView constructed from it.  The recommended
 * pattern is to declare the spec as @c static @c constexpr (or any other
 * object with static storage duration) and construct an @c RpcSpecView on
 * demand:
 *
 * @code
 * static constexpr auto kMySpec = RpcSpec{ ... };
 * RpcSpecView view{kMySpec};  // safe: kMySpec lives forever
 * @endcode
 *
 * Enables uniform return type for versioned specs.
 */
class RpcSpecView {
    void const* self_;
    MaybeError (*processImpl_)(void const*, boost::json::value&);
    Warnings (*checkImpl_)(void const*, boost::json::value const&);

public:
    template <typename... Fields>
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr RpcSpecView(RpcSpec<Fields...> const& spec) noexcept
        : self_{&spec}
        , processImpl_{[](void const* s, boost::json::value& v) {
            return static_cast<RpcSpec<Fields...> const*>(s)->process(v);
        }}
        , checkImpl_{[](void const* s, boost::json::value const& v) {
            return static_cast<RpcSpec<Fields...> const*>(s)->check(v);
        }}
    {
    }

    [[nodiscard]] MaybeError
    process(boost::json::value& v) const
    {
        return processImpl_(self_, v);
    }

    [[nodiscard]] Warnings
    check(boost::json::value const& v) const
    {
        return checkImpl_(self_, v);
    }
};

using RpcSpecConstRef = RpcSpecView;

}  // namespace rpc::spec
