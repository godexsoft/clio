#pragma once

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/amm_info/Types.hpp>

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief AMMInfoHandler returns information about AMM pools.
 *
 * For more info see: https://xrpl.org/amm_info.html
 */
class AMMInfoHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;
    std::shared_ptr<data::AmendmentCenterInterface const> amendmentCenter_;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        // todo: use better type than json types
        boost::json::value amount1;
        boost::json::value amount2;
        boost::json::value lpToken;
        boost::json::array voteSlots;
        boost::json::value auctionSlot;
        std::uint16_t tradingFee = 0;
        std::string ammAccount;
        std::optional<bool> asset1Frozen;
        std::optional<bool> asset2Frozen;

        std::string ledgerHash;
        uint32_t ledgerIndex{};
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::amm_info::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AMMInfoHandler object
     *
     * @param sharedPtrBackend The backend to use
     * @param amendmentCenter The amendmentCenter to use
     */
    AMMInfoHandler(
        std::shared_ptr<BackendInterface> sharedPtrBackend,
        std::shared_ptr<data::AmendmentCenterInterface const> const& amendmentCenter
    )
        : sharedPtrBackend_(std::move(sharedPtrBackend)), amendmentCenter_{amendmentCenter}
    {
    }

    /**
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static rpc::spec::RpcSpecView
    spec([[maybe_unused]] uint32_t apiVersion);

    /**
     * @brief Process the AMMInfo command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    [[nodiscard]] Result
    process(Input const& input, Context const& ctx) const;

private:
    /**
     * @brief Convert the Output to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param output The output to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Output const& output);
};

// Declared in the shared-spec namespace so ADL resolves these conversions to it
// (the types now live in rpcspec); the conversion logic itself stays Clio-side.
namespace spec::handlers::amm_info {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

}  // namespace spec::handlers::amm_info

}  // namespace rpc
