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

#include "util/requests/impl/SslContext.hpp"

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/stream.hpp>

namespace util::requests::impl {

template <typename StreamType>
struct PlainStreamData {
    static constexpr bool kSSL_ENABLED = false;

    explicit PlainStreamData(boost::asio::yield_context yield) : stream(boost::asio::get_associated_executor(yield))
    {
    }

    StreamType stream;
};

using TcpStreamData = PlainStreamData<boost::beast::tcp_stream>;
using WsStreamData = PlainStreamData<boost::beast::websocket::stream<boost::beast::tcp_stream>>;

template <typename StreamType>
struct SslStreamData {
    static constexpr bool kSSL_ENABLED = true;
    StreamType stream;

    static SslStreamData
    create(boost::asio::yield_context yield)
    {
        return SslStreamData{getClientSslContext(), yield};
    }

private:
    SslStreamData(boost::asio::ssl::context& sslContext, boost::asio::yield_context yield)
        : stream(boost::asio::get_associated_executor(yield), sslContext)
    {
    }
};

using SslTcpStreamData = SslStreamData<boost::asio::ssl::stream<boost::beast::tcp_stream>>;
using SslWsStreamData =
    SslStreamData<boost::beast::websocket::stream<boost::asio::ssl::stream<boost::beast::tcp_stream>>>;

}  // namespace util::requests::impl
