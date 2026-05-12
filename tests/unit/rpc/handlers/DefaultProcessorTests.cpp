#include "rpc/Errors.hpp"
#include "rpc/FakesAndMocks.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/impl/Processors.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "util/HandlerBaseTestFixture.hpp"

#include <boost/json/conversion.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>

using namespace testing;
using namespace std;

using namespace rpc;
using namespace tests::common;

namespace json = boost::json;

namespace newspec_fakes {

struct NewSpecInput {
    std::string token;

    friend NewSpecInput
    tag_invoke(boost::json::value_to_tag<NewSpecInput>, boost::json::value const& jv)
    {
        return {boost::json::value_to<std::string>(jv.as_object().at("token"))};
    }
};

struct NewSpecOutput {
    std::string token;

    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, NewSpecOutput const& out)
    {
        jv = {{"token", out.token}};
    }
};

struct NewSpecHandlerFake {
    using Input = NewSpecInput;
    using Output = NewSpecOutput;
    using Result = rpc::HandlerReturnType<Output>;

    [[nodiscard]] static rpc::spec::RpcSpecView
    spec([[maybe_unused]] uint32_t apiVersion)
    {
        static constexpr auto kSPEC = rpc::spec::RpcSpec{
            rpc::spec::field("token", rpc::spec::Required{}),
        };
        return kSPEC;
    }

    [[nodiscard]] static Result
    process(Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{input.token};
    }
};

struct DeprecatedFieldHandlerFake {
    using Input = NewSpecInput;
    using Output = NewSpecOutput;
    using Result = rpc::HandlerReturnType<Output>;

    [[nodiscard]] static rpc::spec::RpcSpecView
    spec([[maybe_unused]] uint32_t apiVersion)
    {
        static constexpr auto kSPEC = rpc::spec::RpcSpec{
            rpc::spec::field("token", rpc::spec::Required{}),
            rpc::spec::field("ident", rpc::spec::Deprecated{}),
        };
        return kSPEC;
    }

    [[nodiscard]] static Result
    process(Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{input.token};
    }
};

struct NoInputNewSpecHandlerFake {
    using Output = NewSpecOutput;
    using Result = rpc::HandlerReturnType<Output>;

    [[nodiscard]] static rpc::spec::RpcSpecView
    spec([[maybe_unused]] uint32_t apiVersion)
    {
        static constexpr auto kSPEC = rpc::spec::RpcSpec{
            rpc::spec::field("token", rpc::spec::Required{}),
        };
        return kSPEC;
    }

    [[nodiscard]] static Result
    process([[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{"no-input"};
    }
};

}  // namespace newspec_fakes

using newspec_fakes::DeprecatedFieldHandlerFake;
using newspec_fakes::NewSpecHandlerFake;
using newspec_fakes::NoInputNewSpecHandlerFake;

class RPCDefaultProcessorTest : public HandlerBaseTest {};

TEST_F(RPCDefaultProcessorTest, ValidInput)
{
    runSpawn([](auto yield) {
        HandlerMock const handler;
        rpc::impl::DefaultProcessor<HandlerMock> const processor;

        auto const input = json::parse(R"JSON({ "something": "works" })JSON");
        static constexpr auto kSPEC =
            rpc::spec::RpcSpec{rpc::spec::field("something", rpc::spec::required)};
        auto const data = InOutFake{"works"};
        EXPECT_CALL(handler, spec(_)).WillOnce(Return(rpc::spec::RpcSpecView{kSPEC}));
        EXPECT_CALL(handler, process(Eq(data), _)).WillOnce(Return(data));

        auto const ret = processor(handler, input, Context{yield});
        ASSERT_TRUE(ret);
        EXPECT_TRUE(ret.warnings.empty());
    });
}

TEST_F(RPCDefaultProcessorTest, NoInputValidCall)
{
    runSpawn([](auto yield) {
        HandlerWithoutInputMock const handler;
        rpc::impl::DefaultProcessor<HandlerWithoutInputMock> const processor;

        auto const data = InOutFake{"works"};
        auto const input = json::parse(R"JSON({})JSON");
        EXPECT_CALL(handler, process(_)).WillOnce(Return(data));

        auto const ret = processor(handler, input, Context{yield});
        ASSERT_TRUE(ret);
        EXPECT_TRUE(ret.warnings.empty());
    });
}

TEST_F(RPCDefaultProcessorTest, InvalidInput)
{
    runSpawn([](auto yield) {
        HandlerMock const handler;
        rpc::impl::DefaultProcessor<HandlerMock> const processor;

        auto const input = json::parse(R"JSON({ "other": "nope" })JSON");
        static constexpr auto kSPEC =
            rpc::spec::RpcSpec{rpc::spec::field("something", rpc::spec::required)};
        EXPECT_CALL(handler, spec(_)).WillOnce(Return(rpc::spec::RpcSpecView{kSPEC}));

        auto const ret = processor(handler, input, Context{yield});
        ASSERT_FALSE(ret);
        EXPECT_TRUE(ret.warnings.empty());
    });
}

TEST_F(RPCDefaultProcessorTest, NewSpecHandler_HappyPath)
{
    runSpawn([](auto yield) {
        NewSpecHandlerFake const handler;
        rpc::impl::DefaultProcessor<NewSpecHandlerFake> const processor;

        auto const input = json::parse(R"JSON({ "token": "abc123" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_TRUE(ret);
        EXPECT_TRUE(ret.warnings.empty());
        ASSERT_TRUE(ret.result.has_value());
        EXPECT_EQ(ret.result.value().as_object().at("token").as_string(), "abc123");
    });
}

TEST_F(RPCDefaultProcessorTest, NewSpecHandler_MissingRequiredField_ReturnsError)
{
    runSpawn([](auto yield) {
        NewSpecHandlerFake const handler;
        rpc::impl::DefaultProcessor<NewSpecHandlerFake> const processor;

        // "token" is required but absent
        auto const input = json::parse(R"JSON({ "other": "value" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_FALSE(ret);
        EXPECT_TRUE(ret.warnings.empty());
        ASSERT_FALSE(ret.result.has_value());
        auto const& status = ret.result.error();
        ASSERT_TRUE(std::holds_alternative<rpc::RippledError>(status.code));
        EXPECT_EQ(std::get<rpc::RippledError>(status.code), rpc::RippledError::rpcINVALID_PARAMS);
        EXPECT_THAT(status.message, HasSubstr("token"));
    });
}

TEST_F(RPCDefaultProcessorTest, NewSpecHandler_DeprecatedField_WarningsForwarded)
{
    runSpawn([](auto yield) {
        DeprecatedFieldHandlerFake const handler;
        rpc::impl::DefaultProcessor<DeprecatedFieldHandlerFake> const processor;

        // "ident" is present — triggers the Deprecated checker
        auto const input = json::parse(R"JSON({ "token": "abc", "ident": "old" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_TRUE(ret);
        ASSERT_EQ(ret.warnings.size(), 1u);

        auto const& w = ret.warnings.at(0).as_object();
        EXPECT_EQ(w.at("id").as_int64(), static_cast<int>(rpc::WarningCode::WarnRpcDeprecated));
        EXPECT_THAT(std::string{w.at("message").as_string()}, HasSubstr("ident"));
    });
}

TEST_F(RPCDefaultProcessorTest, NewSpecHandler_DeprecatedFieldAbsent_NoWarnings)
{
    runSpawn([](auto yield) {
        DeprecatedFieldHandlerFake const handler;
        rpc::impl::DefaultProcessor<DeprecatedFieldHandlerFake> const processor;

        auto const input = json::parse(R"JSON({ "token": "abc" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_TRUE(ret);
        EXPECT_TRUE(ret.warnings.empty());
    });
}

TEST_F(RPCDefaultProcessorTest, NoInputNewSpecHandler_SpecRunsEvenWithoutInput)
{
    runSpawn([](auto yield) {
        NoInputNewSpecHandlerFake const handler;
        rpc::impl::DefaultProcessor<NoInputNewSpecHandlerFake> const processor;

        // Missing required field → spec must reject before process() is called.
        auto const bad = json::parse(R"JSON({})JSON");
        auto const failed = processor(handler, bad, Context{yield});
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.result.error(), rpc::RippledError::rpcINVALID_PARAMS);
        EXPECT_EQ(failed.result.error().message, "Required field 'token' missing");

        // Valid request → process() runs and returns success despite no Input.
        auto const good = json::parse(R"JSON({ "token": "abc" })JSON");
        auto const ok = processor(handler, good, Context{yield});
        ASSERT_TRUE(ok);
        EXPECT_TRUE(ok.warnings.empty());
    });
}
