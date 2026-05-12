#pragma once

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "etl/ETLServiceInterface.hpp"
#include "etl/LoadBalancerInterface.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/HandlerProvider.hpp"
#include "rpc/common/Types.hpp"
#include "util/log/Logger.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rpc {
class Counters;
}  // namespace rpc

namespace rpc::impl {

class ProductionHandlerProvider final : public HandlerProvider {
    struct Handler {
        AnyHandler handler;
        bool isClioOnly = false;
    };

    std::unordered_map<std::string, Handler> handlerMap_;

public:
    ProductionHandlerProvider(
        util::config::ClioConfigDefinition const& config,
        std::shared_ptr<BackendInterface> const& backend,
        std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptionManager,
        std::shared_ptr<etl::LoadBalancerInterface> const& balancer,
        std::shared_ptr<etl::ETLServiceInterface const> const& etl,
        std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter,
        Counters const& counters
    );

    [[nodiscard]] bool
    contains(std::string const& command) const override;

    [[nodiscard]] std::optional<AnyHandler>
    getHandler(std::string const& command) const override;

    [[nodiscard]] bool
    isClioOnly(std::string const& command) const override;

    [[nodiscard]] std::unordered_set<std::string>
    handlerNames() const;
};

/**
 * @brief Dump every handler's input spec to @p os in YAML-ish indented form.
 *
 * Standalone helper that does not require building a @ref ProductionHandlerProvider —
 * handler `spec()` methods are static, so no backend / ETL / counters are needed.
 * Used by the `--dump-spec` CLI mode. The handler list mirrored here must stay in
 * sync with @ref ProductionHandlerProvider's constructor in HandlerProvider.cpp.
 *
 * @param os         Output stream.
 * @param apiVersion API version to dump (typically 1 or 2).
 */
void
dumpAllRpcSpecs(std::ostream& os, uint32_t apiVersion);

}  // namespace rpc::impl
