//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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
