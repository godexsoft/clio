#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/account_objects/Types.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 * @brief The account_objects command returns the raw ledger format for all objects owned by an
 * account. The results can be filtered by the type. The valid types are: check, deposit_preauth,
 * escrow, nft_offer, offer, payment_channel, signer_list, state (trust line), did and ticket.
 *
 * For more details see: https://xrpl.org/account_objects.html
 */
class AccountObjectsHandler : public spec::HandlerFor<spec::handlers::account_objects::Input> {
    // dependencies
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::account_objects::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::account_objects::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::account_objects::kLimitDefault;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string account;
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::optional<std::string> marker;
        uint32_t limit{};
        std::vector<xrpl::SLE> accountObjects;
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::account_objects::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AccountObjectsHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    AccountObjectsHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the AccountObjects command
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
