#pragma once

#include "util/requests/Types.hpp"

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/error.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>

namespace util::requests::impl {

/**
 * @brief Get the shared client SSL context.
 *
 * The context (including the potentially expensive parse of the system root certificate bundle) is
 * created once on first use and cached for the lifetime of the process. The returned context is
 * safe to share across connections and threads: it is only mutated during construction and each
 * stream derives its own SSL object from it.
 *
 * @return A reference to the shared client SSL context or a RequestError if it could not be
 * created.
 */
std::expected<std::reference_wrapper<boost::asio::ssl::context>, RequestError>
getClientSslContext();

std::optional<std::string>
sslErrorToString(boost::beast::error_code const& error);

}  // namespace util::requests::impl
