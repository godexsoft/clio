#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 *@brief The get_aggregate_price method.
 */
class GetAggregatePriceHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    /**
     * @brief A struct to hold the statistics
     */
    struct Stats {
        ripple::STAmount avg{};  // NOLINT(readability-redundant-member-init)
        // standard deviation
        ripple::Number sd{};  // NOLINT(readability-redundant-member-init)
        uint32_t size{0};
    };

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        uint32_t time;
        Stats extireStats{};
        std::optional<Stats> trimStats;
        std::string ledgerHash;
        uint32_t ledgerIndex;
        std::string median;
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input oracle data
     */
    struct Oracle {
        std::uint32_t documentId{0};
        ripple::AccountID account;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    struct Input {
        std::optional<std::string> ledgerHash;
        std::optional<std::uint32_t> ledgerIndex;
        std::vector<Oracle> oracles;  // valid range is 1-200
        std::string baseAsset;
        std::string quoteAsset;
        std::optional<std::uint32_t> timeThreshold;
        std::optional<std::uint8_t> trim;  // valid range is 1-25
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new GetAggregatePrice handler object
     *
     * @param sharedPtrBackend The backend to use
     */
    GetAggregatePriceHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
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
     * @brief Process the GetAggregatePrice command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    [[nodiscard]] Result
    process(Input const& input, Context const& ctx) const;

private:
    /**
     * @brief Calls callback on the oracle ledger entry
     If the oracle entry does not contains the price pair, search up to three previous metadata
     objects. Stops early if the callback returns true.
     */
    void
    tracebackOracleObject(
        boost::asio::yield_context yield,
        ripple::STObject const& oracleObject,
        std::function<bool(ripple::STObject const&)> const& callback
    ) const;

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
