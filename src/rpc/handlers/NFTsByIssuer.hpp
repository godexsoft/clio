#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/nfts_by_issuer/Types.hpp>

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief Handler for the `nfts_by_issuer` command
 */
class NFTsByIssuerHandler : public spec::HandlerFor<spec::handlers::nfts_by_issuer::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::nfts_by_issuer::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::nfts_by_issuer::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::nfts_by_issuer::kLimitDefault;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        boost::json::array nfts;
        uint32_t ledgerIndex;
        std::string issuer;
        bool validated = true;
        std::optional<uint32_t> nftTaxon;
        uint32_t limit;
        std::optional<std::string> marker;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::nfts_by_issuer::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new NFTsByIssuerHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    NFTsByIssuerHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the NFTsByIssuer command
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

}  // namespace rpc
