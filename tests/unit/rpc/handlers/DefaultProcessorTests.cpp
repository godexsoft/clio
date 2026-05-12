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

// ---------------------------------------------------------------------------
// Minimal in-test handler satisfying SomeHandlerWithNewSpec.
// Input: a JSON object with a required "token" string field.
// Output: the same "token" value echoed back.
// ---------------------------------------------------------------------------
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

// Handler whose spec marks "ident" as deprecated — used for warning round-trip tests.
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

// No-input + new-spec handler. Validates request via the new spec but
// process() takes only the Context.  Pins the orthogonal-dispatch behavior
// — the spec must run even though the handler has no Input.
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
        ASSERT_TRUE(ret);  // no error
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
        ASSERT_TRUE(ret);  // no error
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
        ASSERT_FALSE(ret);  // returns error
        EXPECT_TRUE(ret.warnings.empty());
    });
}

// ---------------------------------------------------------------------------
// Tests for the new SomeHandlerWithNewSpec dispatch branch
// ---------------------------------------------------------------------------

// Happy path: a new-style handler with a required field receives the field —
// the dispatcher decodes Input, calls process(), and returns the JSON output.
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

// Missing required field: the new-spec path returns an error whose Status has
// rpcINVALID_PARAMS and a message containing the field name.
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

// Deprecated field present: the new-spec path forwards warnings produced by
// spec.check() through toJsonArray().  The returned warnings array must
// contain exactly one object whose "id" equals WarnRpcDeprecated (2004) and
// whose "message" contains the field name reported by the Deprecated checker.
TEST_F(RPCDefaultProcessorTest, NewSpecHandler_DeprecatedField_WarningsForwarded)
{
    runSpawn([](auto yield) {
        DeprecatedFieldHandlerFake const handler;
        rpc::impl::DefaultProcessor<DeprecatedFieldHandlerFake> const processor;

        // "ident" is present — triggers the Deprecated checker
        auto const input = json::parse(R"JSON({ "token": "abc", "ident": "old" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_TRUE(ret);  // still succeeds — deprecation is a warning, not an error
        ASSERT_EQ(ret.warnings.size(), 1u);

        auto const& w = ret.warnings.at(0).as_object();
        EXPECT_EQ(w.at("id").as_int64(), static_cast<int>(rpc::WarningCode::WarnRpcDeprecated));
        EXPECT_THAT(std::string{w.at("message").as_string()}, HasSubstr("ident"));
    });
}

// Deprecated field absent: no warnings emitted, output is clean.
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

// Pins the orthogonal-dispatch contract: a handler with no Input but with
// a new-style spec must still have the spec executed against the request.
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
