#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/ledger_index/Types.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rpc {

/**
 * @brief The ledger_index method fetches the latest closed ledger before the given date.
 *
 */
class LedgerIndexHandler : public spec::HandlerFor<spec::handlers::ledger_index::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;
    static constexpr auto kDateFormat = spec::handlers::ledger_index::kDateFormat;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        uint32_t ledgerIndex{};
        std::string ledgerHash;
        std::string closeTimeIso;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::ledger_index::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new LedgerIndexHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    LedgerIndexHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the LedgerIndex command
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
