#pragma once

#include "data/BackendInterface.hpp"
#include "etl/ETLServiceInterface.hpp"
#include "rpc/common/JsonBool.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "util/log/Logger.hpp"

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
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
class AccountTxHandler {
    util::Logger log_{"RPC"};
    std::shared_ptr<BackendInterface> sharedPtrBackend_;
    std::shared_ptr<etl::ETLServiceInterface const> etl_;

public:
    static constexpr auto kLIMIT_MIN = 1;
    static constexpr auto kLIMIT_MAX = 1000;
    static constexpr auto kLIMIT_DEFAULT = 200;

    /**
     * @brief A struct to hold the marker data
     */
    struct Marker {
        uint32_t ledger;
        uint32_t seq;
    };

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
    struct Input {
        std::string account;
        // You must use at least one of the following fields in your request:
        // ledger_index, ledger_hash, ledger_index_min, or ledger_index_max.
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
        std::optional<int32_t> ledgerIndexMin;
        std::optional<int32_t> ledgerIndexMax;
        bool usingValidatedLedger = false;
        JsonBool binary{false};
        JsonBool forward{false};
        std::optional<uint32_t> limit;
        std::optional<Marker> marker;
        std::optional<std::string> transactionTypeInLowercase;
    };

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
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static rpc::spec::RpcSpecView
    spec(uint32_t apiVersion);

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

    /**
     * @brief Convert a JSON object to Input type
     *
     * @param jv The JSON object to convert
     * @return Input parsed from the JSON object
     */
    friend Input
    tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);

    /**
     * @brief Convert the Marker to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param marker The marker to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Marker const& marker);
};
}  // namespace rpc
