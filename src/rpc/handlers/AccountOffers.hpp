#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/account_offers/Types.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <xrpl/protocol/STAmount.h>
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
 * @brief The account_offers method retrieves a list of offers made by a given account.
 *
 * For more details see: https://xrpl.org/account_offers.html
 */
class AccountOffersHandler : public spec::HandlerFor<spec::handlers::account_offers::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::account_offers::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::account_offers::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::account_offers::kLimitDefault;

    /**
     * @brief A struct to hold data for one offer response
     */
    struct Offer {
        uint32_t flags{};
        uint32_t seq{};
        xrpl::STAmount takerGets;
        xrpl::STAmount takerPays;
        std::string quality;
        std::optional<uint32_t> expiration;
    };

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string account;
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::vector<Offer> offers;
        std::optional<std::string> marker;
        // validated should be sent via framework
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::account_offers::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new AccountOffersHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    AccountOffersHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the AccountOffers command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    [[nodiscard]] Result
    process(Input const& input, Context const& ctx) const;

private:
    static void
    addOffer(std::vector<Offer>& offers, xrpl::SLE const& offerSle);

    /**
     * @brief Convert the Output to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param output The output to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Output const& output);

    /**
     * @brief Convert the Offer to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param offer The offer to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Offer const& offer);
};

}  // namespace rpc
