#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/vault_info/Types.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace rpc {

/**
 * @brief The vault_info command retrieves information about a vault, currency, shares etc.
 */
class VaultInfoHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    /**
     * @brief Construct a new VaultInfo object
     *
     * @param sharedPtrBackend The backend to use
     */
    VaultInfoHandler(std::shared_ptr<BackendInterface> sharedPtrBackend);

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::vault_info::Input;

    /**
     * @brief A struct to hold the output data for the command
     */
    struct Output {
        boost::json::value vault;
        uint32_t ledgerIndex{};
        bool validated = true;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static rpc::spec::RpcSpecView
    spec(uint32_t apiVersion);

    /**
     * @brief Process the VaultInfo command
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
     * @param jv The JSON object to convert to
     * @param output The output to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Output const& output);
};

// Declared in the shared-spec namespace so ADL resolves these conversions to it
// (the types now live in rpcspec); the conversion logic itself stays Clio-side.
namespace spec::handlers::vault_info {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

}  // namespace spec::handlers::vault_info

}  // namespace rpc
