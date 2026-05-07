/** @file */
#pragma once

#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <functional>

namespace rpc::spec {

/**
 * @brief Type-erased view over any RpcSpec instantiation.
 * Enables uniform return type for versioned specs.
 */
class RpcSpecView {
    std::function<MaybeError(boost::json::value&)> processImpl_;
    std::function<Warnings(boost::json::value const&)> checkImpl_;

public:
    template <typename... Fields>
    // NOLINTNEXTLINE(google-explicit-constructor)
    RpcSpecView(RpcSpec<Fields...> const& spec) noexcept
        : processImpl_{[&spec](boost::json::value& v) { return spec.process(v); }}
        , checkImpl_{[&spec](boost::json::value const& v) { return spec.check(v); }}
    {
    }

    [[nodiscard]] MaybeError
    process(boost::json::value& v) const
    {
        return processImpl_(v);
    }

    [[nodiscard]] Warnings
    check(boost::json::value const& v) const
    {
        return checkImpl_(v);
    }
};

using RpcSpecConstRef = RpcSpecView;

}  // namespace rpc::spec
