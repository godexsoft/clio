/** @file */
#pragma once

#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace data {
class AmendmentCenterInterface;
class BackendInterface;
}  // namespace data
namespace etl {
struct ETLServiceInterface;
class LoadBalancerInterface;
}  // namespace etl
namespace feed {
class SubscriptionManagerInterface;
}  // namespace feed
namespace util::config {
class ClioConfigDefinition;
}  // namespace util::config

namespace rpc {
class Counters;
}  // namespace rpc

namespace rpc::impl {

/**
 * @brief Bundle of all runtime dependencies a handler factory may consume.
 */
struct HandlerDeps {
    util::config::ClioConfigDefinition const& config;
    std::shared_ptr<data::BackendInterface> const& backend;
    std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptionManager;
    std::shared_ptr<etl::LoadBalancerInterface> const& balancer;
    std::shared_ptr<etl::ETLServiceInterface const> const& etl;
    std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter;
    Counters const& counters;
};

/**
 * @brief One row of the handler registry: static metadata plus a runtime factory.
 *
 * @c specFn is @c nullptr for handlers that take no input; the dumper renders
 * those as @c (no inputs).
 */
struct HandlerEntry {
    std::string_view name;
    AnyHandler (*factory)(HandlerDeps const&);
    rpc::spec::RpcSpecView (*specFn)(uint32_t);
    bool isClioOnly = false;
};

/**
 * @brief The full set of registered RPC handlers.
 */
[[nodiscard]] std::span<HandlerEntry const>
handlerRegistry() noexcept;

}  // namespace rpc::impl
