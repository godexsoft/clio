//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#pragma once

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "feed/SubscriptionManager.hpp"
#include "rpc/common/AnyHandler.hpp"
#include "rpc/common/HandlerProvider.hpp"
#include "rpc/common/Types.hpp"
#include "util/config/Config.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace etl {
class ETLService;
class LoadBalancer;
}  // namespace etl
namespace rpc {
class Counters;
}  // namespace rpc
namespace feed {
class SubscriptionManager;
}  // namespace feed
namespace data {
class AmendmentCenter;
}  // namespace data

namespace rpc::impl {

class ProductionHandlerProvider final : public HandlerProvider {
    struct Handler {
        AnyHandler handler;
        bool isClioOnly = false;
    };

    std::unordered_map<std::string, Handler> handlerMap_;

public:
    ProductionHandlerProvider(
        util::Config const& config,
        std::shared_ptr<BackendInterface> const& backend,
        std::shared_ptr<feed::SubscriptionManager> const& subscriptionManager,
        std::shared_ptr<etl::LoadBalancer> const& balancer,
        std::shared_ptr<etl::ETLService const> const& etl,
        std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter,
        Counters const& counters
    );

    bool
    contains(std::string const& command) const override;

    std::optional<AnyHandler>
    getHandler(std::string const& command) const override;

    bool
    isClioOnly(std::string const& command) const override;
};

}  // namespace rpc::impl
