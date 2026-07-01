#pragma once

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/ledger/Types.hpp>

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
 * @brief Retrieve information about the public ledger.
 *
 * For more details see: https://xrpl.org/ledger.html
 */
class LedgerHandler : public spec::HandlerFor<spec::handlers::ledger::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;
    std::shared_ptr<data::AmendmentCenterInterface const> amendmentCenter_;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        uint32_t ledgerIndex{};
        std::string ledgerHash;
        // TODO: use better type
        boost::json::object header;
        bool validated = true;
    };

    /**
     * @brief Input data for the command — aliased from the shared xrpl-rpc-spec framework type.
     *
     * Clio does not support:
     * - queue
     *
     * And the following are deprecated altogether:
     * - full
     * - accounts
     * - ledger
     * - type
     *
     * Clio will throw an error when `queue`, `full` or `accounts` is set to `true`.
     * @see https://github.com/XRPLF/clio/issues/603 and https://github.com/XRPLF/clio/issues/1537
     */
    using Input = spec::handlers::ledger::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new LedgerHandler object
     *
     * @param sharedPtrBackend The backend to use
     * @param amendmentCenter The amendmentCenter to use
     */
    LedgerHandler(
        std::shared_ptr<BackendInterface> sharedPtrBackend,
        std::shared_ptr<data::AmendmentCenterInterface const> amendmentCenter
    )
        : sharedPtrBackend_(std::move(sharedPtrBackend))
        , amendmentCenter_(std::move(amendmentCenter))
    {
    }

    /**
     * @brief Process the Ledger command
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
