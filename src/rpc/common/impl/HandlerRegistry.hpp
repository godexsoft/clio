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
 *
 * Each factory in @ref kHANDLERS picks the fields it needs and ignores the rest.
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
 * @brief One row of the handler registry: static metadata + a runtime factory.
 *
 * `factory` and `specFn` are captureless lambdas that decay to function pointers,
 * keeping the surrounding table `constexpr`. `specFn` is `nullptr` for handlers
 * that take no input (ping, random, ledger_range, version) — the dumper renders
 * those as `(no inputs)`.
 */
struct HandlerEntry {
    std::string_view name;
    AnyHandler (*factory)(HandlerDeps const&);
    rpc::spec::RpcSpecView (*specFn)(uint32_t);
    bool isClioOnly = false;
};

/**
 * @brief The full set of registered RPC handlers.
 *
 * Single source of truth. Both @ref ProductionHandlerProvider (which builds
 * runtime instances) and @ref rpc::dumpAllRpcSpecs (which only needs the static
 * metadata) iterate this span.
 */
[[nodiscard]] std::span<HandlerEntry const>
handlerRegistry() noexcept;

}  // namespace rpc::impl
