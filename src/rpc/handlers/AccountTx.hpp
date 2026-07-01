#pragma once

#include "data/BackendInterface.hpp"
#include "etl/ETLServiceInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/JsonBool.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/account_tx/Types.hpp>
#include "util/log/Logger.hpp"

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief The account_tx method retrieves a list of transactions that involved the specified
 * account.
 *
 * For more details see: https://xrpl.org/account_tx.html
 */
class AccountTxHandler : public spec::HandlerFor<spec::handlers::account_tx::Input> {
    util::Logger log_{"RPC"};
    std::shared_ptr<BackendInterface> sharedPtrBackend_;
    std::shared_ptr<etl::ETLServiceInterface const> etl_;

public:
    static constexpr auto kLimitMin = spec::handlers::account_tx::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::account_tx::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::account_tx::kLimitDefault;

    using Marker = spec::handlers::account_tx::Marker;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string account;
        uint32_t ledgerIndexMin{0};
        uint32_t ledgerIndexMax{0};
        std::optional<uint32_t> limit;
        std::optional<Marker> marker;
        // TODO: use a better type than json
        boost::json::array transactions;
        // validated should be sent via framework
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::account_tx::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AccountTxHandler object
     *
     * @param sharedPtrBackend The backend to use
     * @param etl The ETL service to use
     */
    AccountTxHandler(
        std::shared_ptr<BackendInterface> sharedPtrBackend,
        std::shared_ptr<etl::ETLServiceInterface const> const& etl
    )
        : sharedPtrBackend_(std::move(sharedPtrBackend)), etl_{etl}
    {
    }

    /**
     * @brief Process the AccountTx command
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

// Declared in the shared-spec namespace so ADL resolves the Marker output
// conversion to it (the types live in rpcspec); the logic stays Clio-side.
namespace spec::handlers::account_tx {

void
tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Marker const& marker);

}  // namespace spec::handlers::account_tx

}  // namespace rpc
