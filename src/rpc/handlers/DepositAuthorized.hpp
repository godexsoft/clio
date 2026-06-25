#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/deposit_authorized/Types.hpp>

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/STArray.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief Handles the `deposit_authorized` command
 *
 * The deposit_authorized command indicates whether one account is authorized to send payments
 * directly to another. See Deposit Authorization for information on how to require authorization to
 * deliver money to your account.
 *
 * For more details see: https://xrpl.org/deposit_authorized.html
 */
class DepositAuthorizedHandler {
    // dependencies
    std::shared_ptr<BackendInterface> const sharedPtrBackend_;

public:
    // Note: `ledger_current_index` is omitted because it only makes sense for rippled
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        bool depositAuthorized = true;
        std::string sourceAccount;
        std::string destinationAccount;
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::optional<boost::json::array> credentials;

        // validated should be sent via framework
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::deposit_authorized::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new DepositAuthorizedHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    DepositAuthorizedHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
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

    /**
     * @brief Process the DepositAuthorized command
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
namespace spec::handlers::deposit_authorized {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

}  // namespace spec::handlers::deposit_authorized

}  // namespace rpc
