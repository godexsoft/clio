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
#include "rpc/common/Types.hpp"

#include <api/DefaultApi.hpp>
#include <model/AccountChannelsRequest.hpp>
#include <model/AccountChannelsResponse.hpp>
#include <model/AccountChannelsSuccessResponse.hpp>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <memory>

namespace rpc::openapi {

class AccountChannelsHandlerImpl : public openapi_clio::AccountChannelsHandlerBase<rpc::Context> {
    std::shared_ptr<BackendInterface> backend_;

public:
    static constexpr auto kLIMIT_MIN = 10;
    static constexpr auto kLIMIT_MAX = 400;
    static constexpr auto kLIMIT_DEFAULT = 200;

    AccountChannelsHandlerImpl(std::shared_ptr<BackendInterface> const& backend);

    std::expected<openapi_clio::model::AccountChannelsSuccessResponse, ErrorCodes>
    process(openapi_clio::model::AccountChannelsRequestBase const&, rpc::Context const& ctx) override;
};

}  // namespace rpc::openapi
