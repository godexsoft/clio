#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/nft_offers_common/Types.hpp>

#include <boost/asio/spawn.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 * @brief Contains common functionality for handling the `nft_offers` command
 */
class NFTOffersHandlerBase {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::nft_offers_common::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::nft_offers_common::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::nft_offers_common::kLimitDefault;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string nftID;
        std::vector<xrpl::SLE> offers;

        // validated should be sent via framework
        bool validated = true;
        std::optional<uint32_t> limit;
        std::optional<std::string> marker;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::nft_offers_common::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new NFTOffersHandlerBase object
     *
     * @param sharedPtrBackend The backend to use
     */
    NFTOffersHandlerBase(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static rpc::spec::RpcSpecView
    spec(uint32_t apiVersion);

protected:
    /**
     * @brief Iterate the NFT offer directory
     *
     * @param input The input data for the command
     * @param tokenID The tokenID of the NFT
     * @param directory The directory to iterate
     * @param yield The coroutine context
     * @return The result of the iteration
     */
    [[nodiscard]] Result
    iterateOfferDirectory(
        Input input,
        xrpl::uint256 const& tokenID,
        xrpl::Keylet const& directory,
        boost::asio::yield_context yield
    ) const;

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
namespace spec::handlers::nft_offers_common {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

}  // namespace spec::handlers::nft_offers_common

}  // namespace rpc
