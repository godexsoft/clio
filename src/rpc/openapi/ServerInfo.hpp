//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

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

#include "data/BackendInterface.hpp"
#include "etlng/ETLServiceInterface.hpp"
#include "etlng/LoadBalancerInterface.hpp"
#include "feed/SubscriptionManagerInterface.hpp"
#include "rpc/Counters.hpp"
#include "rpc/common/Types.hpp"

#include <api/DefaultApi.hpp>
#include <model/ServerInfoRequest.hpp>
#include <model/ServerInfoResponse.hpp>
#include <model/ServerInfoSuccessResponse.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace rpc::openapi {

class ServerInfoHandlerImpl : public openapi_clio::ServerInfoHandlerBase<rpc::Context> {
    static constexpr auto kBACKEND_COUNTERS_KEY = "backend_counters";

    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<feed::SubscriptionManagerInterface> subscriptions_;
    std::shared_ptr<etlng::LoadBalancerInterface> balancer_;
    std::shared_ptr<etlng::ETLServiceInterface const> etl_;
    std::reference_wrapper<rpc::Counters const> counters_;

public:
    ServerInfoHandlerImpl(
        std::shared_ptr<BackendInterface> const& backend,
        std::shared_ptr<feed::SubscriptionManagerInterface> const& subscriptions,
        std::shared_ptr<etlng::LoadBalancerInterface> const& balancer,
        std::shared_ptr<etlng::ETLServiceInterface const> const& etl,
        rpc::Counters const& counters
    );

    std::expected<openapi_clio::model::ServerInfoSuccessResponse, ErrorCodes>
    process(openapi_clio::model::ServerInfoRequestBase const&, rpc::Context& ctx) override;
};

}  // namespace rpc::openapi
