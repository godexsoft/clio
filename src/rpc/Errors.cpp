#include "rpc/Errors.hpp"

#include "rpc/JS.hpp"
#include "util/OverloadSet.hpp"

#include <boost/json/object.hpp>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace std;

namespace rpc {

std::ostream&
operator<<(std::ostream& stream, Status const& status)
{
    std::visit(
        util::OverloadSet{
            [&stream, &status](RippledError err) {
                stream << "Code: " << static_cast<std::underlying_type_t<RippledError>>(err);
                if (!status.error.empty())
                    stream << ", Error: " << status.error;
                if (!status.message.empty()) {
                    stream << ", Message: " << status.message;
                } else {
                    stream << ", Message: " << xrpl::RPC::getErrorInfo(err).message;
                }
            },
            [&stream, &status](ClioError err) {
                stream << "Code: " << static_cast<std::underlying_type_t<ClioError>>(err);
                if (!status.error.empty())
                    stream << ", Error: " << status.error;
                if (!status.message.empty()) {
                    stream << ", Message: " << status.message;
                } else {
                    stream << ", Message: " << getErrorInfo(err).message;
                }
            },
            [&stream, &status](EtlError err) {
                stream << "Code: " << static_cast<std::underlying_type_t<EtlError>>(err);
                if (!status.error.empty())
                    stream << ", Error: " << status.error;
                if (!status.message.empty()) {
                    stream << ", Message: " << status.message;
                } else {
                    stream << ", Message: " << getEtlErrorInfo(err).message;
                }
            }
        },
        status.code
    );

    if (status.extraInfo.has_value())
        stream << ", Extra Info: " << *status.extraInfo;

    return stream;
}

EtlErrorInfo const&
getEtlErrorInfo(EtlError code)
{
    // clang-format off
    static constexpr EtlErrorInfo kINFOS[]{
        {.code = EtlError::ConnectionError, .error = "connectionError",  .message = "Couldn't connect to rippled."},
        {.code = EtlError::RequestError,    .error = "requestError",     .message = "Error sending request to rippled."},
        {.code = EtlError::RequestTimeout,  .error = "timeout",          .message = "Request to rippled timed out."},
        {.code = EtlError::InvalidResponse, .error = "invalidResponse",  .message = "Rippled returned an invalid response."},
    };
    // clang-format on

    switch (code) {
        case EtlError::ConnectionError: return kINFOS[0];
        case EtlError::RequestError:    return kINFOS[1];
        case EtlError::RequestTimeout:  return kINFOS[2];
        case EtlError::InvalidResponse: return kINFOS[3];
    }
    throw(out_of_range("Invalid EtlError code"));
}

ClioErrorInfo const&
getErrorInfo(ClioError code)
{
    static constexpr ClioErrorInfo kInfos[]{
        {.code = ClioError::RpcMalformedCurrency,
         .error = "malformedCurrency",
         .message = "Malformed currency."},
        {.code = ClioError::RpcMalformedRequest,
         .error = "malformedRequest",
         .message = "Malformed request."},
        {.code = ClioError::RpcMalformedOwner,
         .error = "malformedOwner",
         .message = "Malformed owner."},
        {.code = ClioError::RpcMalformedAddress,
         .error = "malformedAddress",
         .message = "Malformed address."},
        {.code = ClioError::RpcUnknownOption,
         .error = "unknownOption",
         .message = "Unknown option."},
        {.code = ClioError::RpcFieldNotFoundTransaction,
         .error = "fieldNotFoundTransaction",
         .message = "Missing field."},
        {.code = ClioError::RpcMalformedOracleDocumentId,
         .error = "malformedDocumentID",
         .message = "Malformed oracle_document_id."},
        {.code = ClioError::RpcMalformedAuthorizedCredentials,
         .error = "malformedAuthorizedCredentials",
         .message = "Malformed authorized credentials."},
        // special system errors
        {.code = ClioError::RpcInvalidApiVersion,
         .error = JS(invalid_API_version),
         .message = "Invalid API version."},
        {.code = ClioError::RpcCommandIsMissing,
         .error = JS(missingCommand),
         .message = "Method is not specified or is not a string."},
        {.code = ClioError::RpcCommandNotString,
         .error = "commandNotString",
         .message = "Method is not a string."},
        {.code = ClioError::RpcCommandIsEmpty,
         .error = "emptyCommand",
         .message = "Method is an empty string."},
        {.code = ClioError::RpcParamsUnparsable,
         .error = "paramsUnparsable",
         .message = "Params must be an array holding exactly one object."},
    };

    auto matchByCode = [code](auto const& info) { return info.code == code; };
    if (auto it = ranges::find_if(kInfos, matchByCode); it != end(kInfos))
        return *it;

    throw(out_of_range("Invalid error code"));
}

boost::json::object
makeError(
    RippledError err,
    std::optional<std::string_view> customError,
    std::optional<std::string_view> customMessage
)
{
    boost::json::object json;
    auto const& info = xrpl::RPC::getErrorInfo(err);

    json["error"] = customError.value_or(info.token.cStr()).data();
    json["error_code"] = static_cast<uint32_t>(err);
    json["error_message"] = customMessage.value_or(info.message.cStr()).data();
    json["status"] = "error";
    json["type"] = "response";

    return json;
}

boost::json::object
makeError(
    ClioError err,
    std::optional<std::string_view> customError,
    std::optional<std::string_view> customMessage
)
{
    boost::json::object json;
    auto const& info = getErrorInfo(err);

    json["error"] = customError.value_or(info.error);
    json["error_code"] = static_cast<uint32_t>(info.code);
    json["error_message"] = customMessage.value_or(info.message);
    json["status"] = "error";
    json["type"] = "response";

    return json;
}

boost::json::object
makeError(Status const& status)
{
    auto wrapOptional = [](string_view const& str) {
        return str.empty() ? nullopt : make_optional(str);
    };

    auto res = visit(
        util::OverloadSet{
            [&status, &wrapOptional](RippledError err) {
                if (err == xrpl::RpcUnknown) {
                    return boost::json::object{
                        {"error", status.message}, {"type", "response"}, {"status", "error"}
                    };
                }

                return makeError(err, wrapOptional(status.error), wrapOptional(status.message));
            },
            [&status, &wrapOptional](ClioError err) {
                return makeError(err, wrapOptional(status.error), wrapOptional(status.message));
            },
            [](EtlError err) {
                auto const& info = getEtlErrorInfo(err);
                return boost::json::object{
                    {"error", info.error},
                    {"error_code", static_cast<uint32_t>(err)},
                    {"error_message", info.message},
                    {"status", "error"},
                    {"type", "response"}
                };
            },
        },
        status.code
    );

    if (status.extraInfo) {
        for (auto& [key, value] : *status.extraInfo)
            res[key] = value;
    }

    return res;
}

}  // namespace rpc
