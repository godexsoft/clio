/** @file */
#pragma once

#include "rpc/common/spec/FieldAccess.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/SpecDump.hpp"
#include "rpc/common/spec/SpecDumpWriter.hpp"
#include "rpc/common/spec/Types.hpp"

#include <concepts>

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
    MaybeError (*processImpl_)(void const*, RootAccess&);
    Warnings (*checkImpl_)(void const*, RootAccess const&);
    void (*dumpImpl_)(void const*, SpecDumpWriter&);

public:
    template <typename... Fields>
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr RpcSpecView(RpcSpec<Fields...> const& spec) noexcept
        : self_{&spec}
        , processImpl_{[](void const* s, RootAccess& r) {
            return static_cast<RpcSpec<Fields...> const*>(s)->process(r);
        }}
        , checkImpl_{[](void const* s, RootAccess const& r) {
            return static_cast<RpcSpec<Fields...> const*>(s)->check(r);
        }}
        , dumpImpl_{[](void const* s, SpecDumpWriter& w) {
            dumpRpcSpec(w, *static_cast<RpcSpec<Fields...> const*>(s));
        }}
    {
    }

    [[nodiscard]] MaybeError
    process(RootAccess& root) const
    {
        return processImpl_(self_, root);
    }

    [[nodiscard]] Warnings
    check(RootAccess const& root) const
    {
        return checkImpl_(self_, root);
    }

    /// Walk the spec tree and emit a human-readable description via @p w.
    void
    dump(SpecDumpWriter& w) const
    {
        dumpImpl_(self_, w);
    }

    // Convenience overloads: accept any value the backend's RootAccess can wrap.
    template <typename V>
        requires(!std::same_as<V, RootAccess>) && std::constructible_from<RootAccess, V&>
    [[nodiscard]] MaybeError
    process(V& v) const
    {
        RootAccess root{v};
        return processImpl_(self_, root);
    }

    template <typename V>
        requires(!std::same_as<V, RootAccess>) && std::constructible_from<RootAccess, V const&>
    [[nodiscard]] Warnings
    check(V const& v) const
    {
        RootAccess const root{v};
        return checkImpl_(self_, root);
    }
};

using RpcSpecConstRef = RpcSpecView;

}  // namespace rpc::spec
