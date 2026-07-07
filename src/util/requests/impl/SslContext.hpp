#pragma once

#include "util/requests/Types.hpp"

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/error.hpp>

#include <expected>
#include <optional>
#include <string>

namespace util::requests::impl {

/**
 * @brief Create a client SSL context from the given root certificate.
 *
 * @param rootCertificate The PEM-encoded root certificate bundle to trust.
 * @return The client SSL context, or a RequestError if @p rootCertificate is empty.
 */
std::expected<boost::asio::ssl::context, RequestError>
makeClientSslContext(std::optional<std::string> const& rootCertificate);

/**
 * @brief Create and cache the shared client SSL context.
 *
 * The context (including the potentially expensive parse of the system root certificate bundle) is
 * created once and cached for the lifetime of the process. Intended to be called at startup so a
 * client-side SSL misconfiguration is reported immediately instead of failing every request later.
 *
 * @return An error message if the context could not be created
 */
std::expected<void, std::string>
initClientSslContext();

/**
 * @brief Get the shared client SSL context.
 *
 * The returned context is safe to share across connections and threads: it is only mutated during
 * construction and each stream derives its own SSL object from it.
 *
 * @note initClientSslContext() must have been called successfully before this is used.
 *
 * @return A reference to the shared client SSL context.
 */
boost::asio::ssl::context&
getClientSslContext();

std::optional<std::string>
sslErrorToString(boost::beast::error_code const& error);

}  // namespace util::requests::impl
