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

#include "util/AsioContextTestFixture.hpp"
#include "util/AssignRandomPort.hpp"
#include "util/LoggerFixtures.hpp"
#include "util/NameGenerator.hpp"
#include "util/Taggable.hpp"
#include "util/TestHttpClient.hpp"
#include "util/TestWebSocketClient.hpp"
#include "util/config/Config.hpp"
#include "web/SubscriptionContextInterface.hpp"
#include "web/ng/Connection.hpp"
#include "web/ng/ProcessingPolicy.hpp"
#include "web/ng/Request.hpp"
#include "web/ng/Response.hpp"
#include "web/ng/Server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>

using namespace web::ng;

namespace http = boost::beast::http;

struct MakeServerTestBundle {
    std::string testName;
    std::string configJson;
    bool expectSuccess;
};

struct MakeServerTest : NoLoggerFixture, testing::WithParamInterface<MakeServerTestBundle> {
    boost::asio::io_context ioContext;
};

TEST_P(MakeServerTest, Make)
{
    util::Config const config{boost::json::parse(GetParam().configJson)};
    auto const expectedServer = makeServer(config, ioContext);
    EXPECT_EQ(expectedServer.has_value(), GetParam().expectSuccess);
}

INSTANTIATE_TEST_CASE_P(
    MakeServerTests,
    MakeServerTest,
    testing::Values(
        MakeServerTestBundle{
            "NoIp",
            R"json(
                {
                    "server": {"port": 12345}
                }
            )json",
            false
        },
        MakeServerTestBundle{
            "BadEndpoint",
            R"json(
                {
                    "server": {"ip": "wrong", "port": 12345}
                }
            )json",
            false
        },
        MakeServerTestBundle{
            "PortMissing",
            R"json(
        {
            "server": {"ip": "127.0.0.1"}
        }
            )json",
            false
        },
        MakeServerTestBundle{
            "BadSslConfig",
            R"json(
        {
            "server": {"ip": "127.0.0.1", "port": 12345},
            "ssl_cert_file": "somг_file"
        }
            )json",
            false
        },
        MakeServerTestBundle{
            "BadProcessingPolicy",
            R"json(
        {
            "server": {"ip": "127.0.0.1", "port": 12345, "processing_policy": "wrong"}
        }
            )json",
            false
        },
        MakeServerTestBundle{
            "CorrectConfig_ParallelPolicy",
            R"json(
        {
            "server": {"ip": "127.0.0.1", "port": 12345, "processing_policy": "parallel"}
        }
            )json",
            true
        },
        MakeServerTestBundle{
            "CorrectConfig_SequentPolicy",
            R"json(
        {
            "server": {"ip": "127.0.0.1", "port": 12345, "processing_policy": "sequent"}
        }
            )json",
            true
        }
    ),
    tests::util::kNAME_GENERATOR
);

struct ServerTest : SyncAsioContextTest {
    ServerTest()
    {
        [&]() { ASSERT_TRUE(server.has_value()); }();
        server->onGet("/", getHandler.AsStdFunction());
        server->onPost("/", postHandler.AsStdFunction());
        server->onWs(wsHandler.AsStdFunction());
    }

    uint32_t const serverPort = tests::util::generateFreePort();

    util::Config const config{
        boost::json::object{{"server", boost::json::object{{"ip", "127.0.0.1"}, {"port", serverPort}}}}
    };

    std::expected<Server, std::string> server = makeServer(config, ctx_);

    std::string requestMessage = "some request";
    std::string const headerName = "Some-header";
    std::string const headerValue = "some value";

    testing::StrictMock<testing::MockFunction<
        Response(Request const&, ConnectionMetadata const&, web::SubscriptionContextPtr, boost::asio::yield_context)>>
        getHandler;
    testing::StrictMock<testing::MockFunction<
        Response(Request const&, ConnectionMetadata const&, web::SubscriptionContextPtr, boost::asio::yield_context)>>
        postHandler;
    testing::StrictMock<testing::MockFunction<
        Response(Request const&, ConnectionMetadata const&, web::SubscriptionContextPtr, boost::asio::yield_context)>>
        wsHandler;
};

TEST_F(ServerTest, BadEndpoint)
{
    boost::asio::ip::tcp::endpoint const endpoint{boost::asio::ip::address_v4::from_string("1.2.3.4"), 0};
    util::TagDecoratorFactory const tagDecoratorFactory{util::Config{boost::json::value{}}};
    Server server{
        ctx_, endpoint, std::nullopt, ProcessingPolicy::Sequential, std::nullopt, tagDecoratorFactory, std::nullopt
    };
    auto maybeError = server.run();
    ASSERT_TRUE(maybeError.has_value());
    EXPECT_THAT(*maybeError, testing::HasSubstr("Error creating TCP acceptor"));
}

struct ServerHttpTestBundle {
    std::string testName;
    http::verb method;

    Request::Method
    expectedMethod() const
    {
        switch (method) {
            case http::verb::get:
                return Request::Method::Get;
            case http::verb::post:
                return Request::Method::Post;
            default:
                return Request::Method::Unsupported;
        }
    }
};

struct ServerHttpTest : ServerTest, testing::WithParamInterface<ServerHttpTestBundle> {};

TEST_F(ServerHttpTest, ClientDisconnects)
{
    HttpAsyncClient client{ctx_};
    boost::asio::spawn(ctx_, [&](boost::asio::yield_context yield) {
        auto maybeError =
            client.connect("127.0.0.1", std::to_string(serverPort), yield, std::chrono::milliseconds{100});
        [&]() { ASSERT_FALSE(maybeError.has_value()) << maybeError->message(); }();

        client.disconnect();
        ctx_.stop();
    });

    server->run();
    runContext();
}

TEST_P(ServerHttpTest, RequestResponse)
{
    HttpAsyncClient client{ctx_};

    http::request<http::string_body> request{GetParam().method, "/", 11, requestMessage};
    request.set(headerName, headerValue);

    Response const response{http::status::ok, "some response", Request{request}};

    boost::asio::spawn(ctx_, [&](boost::asio::yield_context yield) {
        auto maybeError =
            client.connect("127.0.0.1", std::to_string(serverPort), yield, std::chrono::milliseconds{100});
        [&]() { ASSERT_FALSE(maybeError.has_value()) << maybeError->message(); }();

        for ([[maybe_unused]] auto i : std::ranges::iota_view{0, 3}) {
            maybeError = client.send(request, yield, std::chrono::milliseconds{100});
            EXPECT_FALSE(maybeError.has_value()) << maybeError->message();

            auto const expectedResponse = client.receive(yield, std::chrono::milliseconds{100});
            [&]() { ASSERT_TRUE(expectedResponse.has_value()) << expectedResponse.error().message(); }();
            EXPECT_EQ(expectedResponse->result(), http::status::ok);
            EXPECT_EQ(expectedResponse->body(), response.message());
        }

        client.gracefulShutdown();
        ctx_.stop();
    });

    auto& handler = GetParam().method == http::verb::get ? getHandler : postHandler;

    EXPECT_CALL(handler, Call)
        .Times(3)
        .WillRepeatedly([&, response = response](Request const& receivedRequest, auto&&, auto&&, auto&&) {
            EXPECT_TRUE(receivedRequest.isHttp());
            EXPECT_EQ(receivedRequest.method(), GetParam().expectedMethod());
            EXPECT_EQ(receivedRequest.message(), request.body());
            EXPECT_EQ(receivedRequest.target(), request.target());
            EXPECT_EQ(receivedRequest.headerValue(headerName), request.at(headerName));

            return response;
        });

    server->run();

    runContext();
}

INSTANTIATE_TEST_SUITE_P(
    ServerHttpTests,
    ServerHttpTest,
    testing::Values(ServerHttpTestBundle{"GET", http::verb::get}, ServerHttpTestBundle{"POST", http::verb::post}),
    tests::util::kNAME_GENERATOR
);

TEST_F(ServerTest, WsClientDisconnects)
{
    WebSocketAsyncClient client{ctx_};

    boost::asio::spawn(ctx_, [&](boost::asio::yield_context yield) {
        auto maybeError =
            client.connect("127.0.0.1", std::to_string(serverPort), yield, std::chrono::milliseconds{100});
        [&]() { ASSERT_FALSE(maybeError.has_value()) << maybeError->message(); }();

        client.close();
        ctx_.stop();
    });

    server->run();

    runContext();
}

TEST_F(ServerTest, WsRequestResponse)
{
    WebSocketAsyncClient client{ctx_};

    Response const response{http::status::ok, "some response", Request{requestMessage, Request::HttpHeaders{}}};

    boost::asio::spawn(ctx_, [&](boost::asio::yield_context yield) {
        auto maybeError =
            client.connect("127.0.0.1", std::to_string(serverPort), yield, std::chrono::milliseconds{100});
        [&]() { ASSERT_FALSE(maybeError.has_value()) << maybeError->message(); }();

        for ([[maybe_unused]] auto i : std::ranges::iota_view{0, 3}) {
            maybeError = client.send(yield, requestMessage, std::chrono::milliseconds{100});
            EXPECT_FALSE(maybeError.has_value()) << maybeError->message();

            auto const expectedResponse = client.receive(yield, std::chrono::milliseconds{100});
            [&]() { ASSERT_TRUE(expectedResponse.has_value()) << expectedResponse.error().message(); }();
            EXPECT_EQ(expectedResponse.value(), response.message());
        }

        client.gracefulClose(yield, std::chrono::milliseconds{100});
        ctx_.stop();
    });

    EXPECT_CALL(wsHandler, Call)
        .Times(3)
        .WillRepeatedly([&, response = response](Request const& receivedRequest, auto&&, auto&&, auto&&) {
            EXPECT_FALSE(receivedRequest.isHttp());
            EXPECT_EQ(receivedRequest.method(), Request::Method::Websocket);
            EXPECT_EQ(receivedRequest.message(), requestMessage);
            EXPECT_EQ(receivedRequest.target(), std::nullopt);

            return response;
        });

    server->run();

    runContext();
}
