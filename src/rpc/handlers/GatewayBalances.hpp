#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 * @brief Handles the `gateway_balances` command
 *
 * The gateway_balances command calculates the total balances issued by a given account, optionally
 * excluding amounts held by operational addresses.
 *
 * For more details see: https://xrpl.org/gateway_balances.html#gateway_balances
 */
class GatewayBalancesHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string ledgerHash;
        uint32_t ledgerIndex;
        std::string accountID;
        bool overflow = false;
        std::map<xrpl::Currency, xrpl::STAmount> sums;
        std::map<xrpl::AccountID, std::vector<xrpl::STAmount>> hotBalances;
        std::map<xrpl::AccountID, std::vector<xrpl::STAmount>> assets;
        std::map<xrpl::AccountID, std::vector<xrpl::STAmount>> frozenBalances;
        std::map<xrpl::Currency, xrpl::STAmount> locked;
        // validated should be sent via framework
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    struct Input {
        std::string account;
        std::set<xrpl::AccountID> hotWallets;
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new GatewayBalancesHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    GatewayBalancesHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
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
     * @brief Process the GatewayBalances command
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

    /**
     * @brief Convert a JSON object to Input type
     *
     * @param jv The JSON object to convert
     * @return Input parsed from the JSON object
     */
    friend Input
    tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);
};

}  // namespace rpc
