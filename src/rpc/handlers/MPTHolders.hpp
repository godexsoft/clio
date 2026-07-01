#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/mpt_holders/Types.hpp>

#include <boost/json/array.hpp>
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
 * @brief The mpt_holders command asks the Clio server for all holders of a particular
 * MPTokenIssuance.
 */
class MPTHoldersHandler : public spec::HandlerFor<spec::handlers::mpt_holders::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::mpt_holders::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::mpt_holders::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::mpt_holders::kLimitDefault;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        boost::json::array mpts;
        uint32_t ledgerIndex;
        std::string mptID;
        bool validated = true;
        uint32_t limit;
        std::optional<std::string> marker;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    using Input = spec::handlers::mpt_holders::Input;

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new MPTHoldersHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    MPTHoldersHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the MPTHolders command
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
