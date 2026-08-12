/** @file */
#pragma once

#include <boost/json/object.hpp>
#include <rpcspec/Errors.hpp>

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rpc {

/**
 * @brief Holds info about a particular @ref ClioError.
 */
struct ClioErrorInfo {
    ClioError const code;
    std::string_view const error;
    std::string_view const message;
};

/**
 * @brief Holds info about a particular @ref rpc::EtlError.
 */
struct EtlErrorInfo {
    EtlError const code;
    std::string_view const error;
    std::string_view const message;
};

/**
 * @brief Invalid parameters error.
 */
class InvalidParamsError : public std::exception {
    std::string msg_;

public:
    /**
     * @brief Construct a new Invalid Params Error object
     *
     * @param msg The error message
     */
    explicit InvalidParamsError(std::string msg) : msg_(std::move(msg))
    {
    }

    /**
     * @brief Get the error message as a C string
     *
     * @return The error message
     */
    [[nodiscard]] char const*
    what() const noexcept override
    {
        return msg_.c_str();
    }
};

/**
 * @brief Account not found error.
 */
class AccountNotFoundError : public std::exception {
    std::string account_;

public:
    /**
     * @brief Construct a new Account Not Found Error object
     *
     * @param acct The account
     */
    explicit AccountNotFoundError(std::string acct) : account_(std::move(acct))
    {
    }

    /**
     * @brief Get the error message as a C string
     *
     * @return The error message
     */
    [[nodiscard]] char const*
    what() const noexcept override
    {
        return account_.c_str();
    }
};

/**
 * @brief A globally available @ref rpc::Status that represents a successful state.
 */
static Status gOk;

/**
 * @brief Get the error info object from an clio-specific error code.
 *
 * @param code The error code
 * @return A reference to the static error info
 */
ClioErrorInfo const&
getErrorInfo(ClioError code);

/**
 * @brief Get the ETL error info object from an ETL error code.
 *
 * @param code The error code
 * @return A reference to the static error info
 */
EtlErrorInfo const&
getEtlErrorInfo(EtlError code);

/**
 * @brief Generate JSON from a @ref rpc::Status.
 *
 * @param status The status object
 * @return The JSON output
 */
boost::json::object
makeError(Status const& status);

/**
 * @brief Generate JSON from a @ref rpc::RippledError.
 *
 * @param err The rippled error
 * @param customError A custom error
 * @param customMessage A custom message
 * @return The JSON output
 */
boost::json::object
makeError(
    RippledError err,
    std::optional<std::string_view> customError = std::nullopt,
    std::optional<std::string_view> customMessage = std::nullopt
);

/**
 * @brief Generate JSON from a @ref rpc::ClioError.
 *
 * @param err The clio's custom error
 * @param customError A custom error
 * @param customMessage A custom message
 * @return The JSON output
 */
boost::json::object
makeError(
    ClioError err,
    std::optional<std::string_view> customError = std::nullopt,
    std::optional<std::string_view> customMessage = std::nullopt
);

}  // namespace rpc
