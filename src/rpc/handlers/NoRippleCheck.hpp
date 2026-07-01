#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/handlers/noripple_check/Types.hpp>

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpc {

/**
 * @brief Handles the `noripple_check` command
 *
 * The noripple_check command provides a quick way to check the status of the Default Ripple field
 * for an account and the No Ripple flag of its trust lines, compared with the recommended settings.
 *
 * For more details see: https://xrpl.org/noripple_check.html
 */
class NoRippleCheckHandler : public spec::HandlerFor<spec::handlers::noripple_check::Input> {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    static constexpr auto kLimitMin = spec::handlers::noripple_check::kLimitMin;
    static constexpr auto kLimitMax = spec::handlers::noripple_check::kLimitMax;
    static constexpr auto kLimitDefault = spec::handlers::noripple_check::kLimitDefault;

    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string ledgerHash;
        uint32_t ledgerIndex{};
        std::vector<std::string> problems;
        // TODO: use better type than json
        std::optional<boost::json::array> transactions;
        bool validated = true;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new NoRippleCheckHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    NoRippleCheckHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Process the NoRippleCheck command
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
