#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 * @brief The account_lines method returns information about an account's trust lines, which contain
 * balances in all non-XRP currencies and assets.
 *
 * For more details see: https://xrpl.org/account_lines.html
 */
class AccountLinesHandler {
    // dependencies
    std::shared_ptr<BackendInterface> const sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = 10;
    static constexpr auto kLimitMax = 400;
    static constexpr auto kLimitDefault = 200;

    /**
     * @brief A struct to hold data for one line response
     */
    struct LineResponse {
        std::string account;
        std::string balance;
        std::string currency;
        std::string limit;
        std::string limitPeer;
        uint32_t qualityIn{};
        uint32_t qualityOut{};
        std::optional<bool> noRipple;
        std::optional<bool> noRipplePeer;
        std::optional<bool> authorized;
        std::optional<bool> peerAuthorized;
        std::optional<bool> freeze;
        std::optional<bool> freezePeer;
        std::optional<bool> deepFreeze;
        std::optional<bool> deepFreezePeer;
    };

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string account;
        std::vector<LineResponse> lines;
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        bool validated = true;  // should be sent via framework
        std::optional<std::string> marker;
        uint32_t limit{};
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    struct Input {
        std::string account;
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
        std::optional<std::string> peer;
        bool ignoreDefault = false;  // TODO: document
                                     // https://github.com/XRPLF/xrpl-dev-portal/issues/1839
        uint32_t limit = kLimitDefault;
        std::optional<std::string> marker;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AccountLinesHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    AccountLinesHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
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
    spec([[maybe_unused]] uint32_t apiVersion);

    /**
     * @brief Process the AccountLines command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    [[nodiscard]] Result
    process(Input const& input, Context const& ctx) const;

private:
    static void
    addLine(
        std::vector<LineResponse>& lines,
        xrpl::SLE const& lineSle,
        xrpl::AccountID const& account,
        std::optional<xrpl::AccountID> const& peerAccount
    );

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
     * @brief Convert the LineResponse to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param line The line response to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, LineResponse const& line);
};

}  // namespace rpc
