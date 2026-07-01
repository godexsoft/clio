#include "rpc/handlers/NFTBuyOffers.hpp"

#include "rpc/common/Types.hpp"

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Indexes.h>

using namespace xrpl;

namespace rpc {

NFTBuyOffersHandler::Result
NFTBuyOffersHandler::process(NFTBuyOffersHandler::Input const& input, Context const& ctx) const
{
    // input.nftID is an already-validated strong xrpl::uint256 — no re-parse.
    auto const tokenID = input.nftID;
    auto const directory = keylet::nftBuys(tokenID);

    return iterateOfferDirectory(input, tokenID, directory, ctx.yield);
}
}  // namespace rpc
