#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/handlers/account_currencies/Types.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief The account_currencies command retrieves a list of currencies that an account can send or
 * receive, based on its trust lines.
 *
 * For more details see: https://xrpl.org/account_currencies.html
 */
class AccountCurrenciesHandler {
    // dependencies
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::set<std::string> receiveCurrencies;
        std::set<std::string> sendCurrencies;
        // validated should be sent via framework
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::account_currencies::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AccountCurrenciesHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    AccountCurrenciesHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
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
     * @brief Process the AccountCurrencies command
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
namespace spec::handlers::account_currencies {

Input
tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

}  // namespace spec::handlers::account_currencies

}  // namespace rpc
