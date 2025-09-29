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

#include "rpc/openapi/AccountChannels.hpp"

#include "data/BackendInterface.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include "util/Assert.hpp"

#include <api/DefaultApi.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <fmt/format.h>
#include <model/AccountChannelsErrorResponse.hpp>
#include <model/AccountChannelsErrorResponseCodes.hpp>
#include <model/AccountChannelsRequest.hpp>
#include <model/AccountChannelsResponse.hpp>
#include <model/AccountChannelsSuccessResponse.hpp>
#include <model/CacheInfo.hpp>
#include <model/Channel.hpp>
#include <model/Counters.hpp>
#include <model/ETLInfo.hpp>
#include <model/ETLInfo_etl_sources_inner.hpp>
#include <model/Info.hpp>
#include <model/RPC.hpp>
#include <model/RPCMetrics.hpp>
#include <model/Subscriptions.hpp>
#include <model/UniversalErrorResponseCodes.hpp>
#include <model/ValidatedLedger.hpp>
#include <model/WorkQueue.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace openapi_clio::model;

namespace {

std::expected<ripple::LedgerHeader, AccountChannelsErrorResponseCodes>
ledgerHeaderFromHashOrSeq(
    BackendInterface const& backend,
    boost::asio::yield_context yield,
    std::optional<std::string> ledgerHash,
    std::optional<std::variant<std::string, uint32_t>> ledgerIndex,
    uint32_t maxSeq
)
{
    std::optional<ripple::LedgerHeader> lgrInfo;
    auto const err = std::unexpected{AccountChannelsErrorResponseCodes::LGRNOTFOUND};  // TODO: move to universal codes

    if (ledgerHash) {
        ripple::uint256 const ledgerHash256{std::string_view(*ledgerHash)};
        lgrInfo = backend.fetchLedgerByHash(ledgerHash256, yield);
        if (!lgrInfo || lgrInfo->seq > maxSeq)
            return err;

        return *lgrInfo;
    }

    uint32_t ledgerSequence = maxSeq;

    if (ledgerIndex.has_value()) {
        if (std::holds_alternative<std::string>(ledgerIndex.value())) {
            // current, validated etc.
            // assume validated for now - maxSeq already set
        } else {
            ledgerSequence = std::get<uint32_t>(ledgerIndex.value());
        }

        // return without check db
        if (ledgerSequence > maxSeq)
            return err;

        lgrInfo = backend.fetchLedgerBySequence(ledgerSequence, yield);
        if (!lgrInfo)
            return err;

        return *lgrInfo;
    }

    return std::unexpected{AccountChannelsErrorResponseCodes::INVALIDPARAMS};  // TODO: move to universal codes
}

void
addChannel(std::vector<Channel>& channels, ripple::SLE const& channelSle)
{
    Channel channel;
    channel.setChannelId(ripple::to_string(channelSle.key()));
    channel.setAccount(ripple::to_string(channelSle.getAccountID(ripple::sfAccount)));
    channel.setDestinationAccount(ripple::to_string(channelSle.getAccountID(ripple::sfDestination)));
    channel.setAmount(channelSle[ripple::sfAmount].getText());
    channel.setBalance(channelSle[ripple::sfBalance].getText());
    channel.setSettleDelay(channelSle[ripple::sfSettleDelay]);

    if (publicKeyType(channelSle[ripple::sfPublicKey])) {
        ripple::PublicKey const pk(channelSle[ripple::sfPublicKey]);
        channel.setPublicKey(toBase58(ripple::TokenType::AccountPublic, pk));
        channel.setPublicKeyHex(strHex(pk));
    }

    if (auto const& v = channelSle[~ripple::sfExpiration])
        channel.setExpiration(v);

    if (auto const& v = channelSle[~ripple::sfCancelAfter])
        channel.setCancelAfter(v);

    if (auto const& v = channelSle[~ripple::sfSourceTag])
        channel.setSourceTag(v);

    if (auto const& v = channelSle[~ripple::sfDestinationTag])
        channel.setDestinationTag(v);

    channels.push_back(channel);
}

}  // namespace

namespace rpc::openapi {

AccountChannelsHandlerImpl::AccountChannelsHandlerImpl(std::shared_ptr<BackendInterface> const& backend)
    : backend_(backend)
{
}

std::expected<AccountChannelsSuccessResponse, AccountChannelsHandlerImpl::ErrorCodes>
AccountChannelsHandlerImpl::process(AccountChannelsRequestBase const& req, rpc::Context const& ctx)
{
    auto const range = backend_->fetchLedgerRange();
    ASSERT(range.has_value(), "AccountChannel's ledger range must be available");
    auto const expectedLgrInfo =
        ledgerHeaderFromHashOrSeq(*backend_, ctx.yield, req.getLedgerHash(), req.getLedgerIndex(), range->maxSequence);

    if (not expectedLgrInfo.has_value())
        return std::unexpected{expectedLgrInfo.error()};

    auto const& lgrInfo = expectedLgrInfo.value();
    auto const accountID = accountFromStringStrict(req.getAccount());
    auto const accountLedgerObject =
        backend_->fetchLedgerObject(ripple::keylet::account(*accountID).key, lgrInfo.seq, ctx.yield);

    if (not accountLedgerObject.has_value())
        return std::unexpected(AccountChannelsErrorResponseCodes::ACTNOTFOUND);

    auto const destAccountID = req.getDestinationAccount()
        ? accountFromStringStrict(req.getDestinationAccount().value())
        : std::optional<ripple::AccountID>{};
    auto resp = AccountChannelsSuccessResponse{};

    std::vector<Channel> channels;
    auto const addToResponse = [&](ripple::SLE const sle) {
        if (sle.getType() == ripple::ltPAYCHAN && sle.getAccountID(ripple::sfAccount) == accountID &&
            (!destAccountID || *destAccountID == sle.getAccountID(ripple::sfDestination))) {
            addChannel(channels, sle);
        }

        return true;
    };

    auto const expectedNext = traverseOwnedNodes(
        *backend_,
        *accountID,
        lgrInfo.seq,
        req.getLimit().value_or(kLIMIT_DEFAULT),  // wrong type here, double vs uint32_t
        req.getMarker(),
        ctx.yield,
        addToResponse
    );

    if (!expectedNext.has_value())
        return std::unexpected(AccountChannelsErrorResponseCodes::INVALIDPARAMS);

    resp.setChannels(std::move(channels));
    resp.setAccount(req.getAccount());
    resp.setLimit(req.getLimit().value_or(kLIMIT_DEFAULT));  // limit is not sanitized
    resp.setLedgerHash(ripple::strHex(lgrInfo.hash));

    resp.setLedgerIndex(lgrInfo.seq);

    auto const nextMarker = expectedNext.value();
    if (nextMarker.isNonZero())
        resp.setMarker(nextMarker.toString());

    resp.setStatus(AccountChannelsSuccessResponseBase::StatusEnum::SUCCESS);
    resp.setValidated(true);

    return resp;
}

}  // namespace rpc::openapi
