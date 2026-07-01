#include "rpc/Errors.hpp"
#include "rpc/FakesAndMocks.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerForDefs.hpp>
#include "rpc/common/impl/Processors.hpp"
#include <rpcspec/Aliases.hpp>
#include <rpcspec/Converters.hpp>
#include <rpcspec/Typed.hpp>
#include <rpcspec/Validators.hpp>
#include <rpcspec/VersionedSpec.hpp>
#include "util/HandlerBaseTestFixture.hpp"

#include <boost/json/conversion.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>

using namespace testing;
using namespace std;

using namespace rpc;
using namespace tests::common;

namespace newspec_fakes {

struct NewSpecOutput {
    std::string token;

    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, NewSpecOutput const& out)
    {
        jv = {{"token", out.token}};
    }
};

// A handler Input with a single required `token` field.
struct NewSpecInput {
    std::string token;
};

inline constexpr auto kNewSpec = rpc::spec::spec<NewSpecInput>(
    rpc::spec::field("token", &NewSpecInput::token, rpc::spec::required, rpc::spec::asString)
);
inline constexpr auto kNewSpecVersioned = rpc::spec::versioned<NewSpecInput>(kNewSpec);

[[nodiscard]] constexpr auto const&
specFor(NewSpecInput const*) noexcept
{
    return kNewSpecVersioned;
}

// A distinct Input that additionally declares a deprecated `ident` field (validate-only).
struct DeprecatedSpecInput {
    std::string token;
};

inline constexpr auto kDeprecatedSpec = rpc::spec::spec<DeprecatedSpecInput>(
    rpc::spec::field("token", &DeprecatedSpecInput::token, rpc::spec::required, rpc::spec::asString),
    rpc::spec::field("ident", rpc::spec::deprecated)
);
inline constexpr auto kDeprecatedSpecVersioned = rpc::spec::versioned<DeprecatedSpecInput>(kDeprecatedSpec);

[[nodiscard]] constexpr auto const&
specFor(DeprecatedSpecInput const*) noexcept
{
    return kDeprecatedSpecVersioned;
}

struct NewSpecHandlerFake : rpc::spec::HandlerFor<NewSpecInput> {
    using Input = NewSpecInput;
    using Output = NewSpecOutput;
    using Result = rpc::HandlerReturnType<Output>;

    [[nodiscard]] static Result
    process(Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{input.token};
    }
};

struct DeprecatedFieldHandlerFake : rpc::spec::HandlerFor<DeprecatedSpecInput> {
    using Input = DeprecatedSpecInput;
    using Output = NewSpecOutput;
    using Result = rpc::HandlerReturnType<Output>;

    [[nodiscard]] static Result
    process(Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{input.token};
    }
};

}  // namespace newspec_fakes

// Emit the generic spec entry points for the local fake Inputs.
template struct rpc::spec::HandlerFor<newspec_fakes::NewSpecInput>;
template struct rpc::spec::HandlerFor<newspec_fakes::DeprecatedSpecInput>;

using newspec_fakes::DeprecatedFieldHandlerFake;
using newspec_fakes::NewSpecHandlerFake;

class RPCDefaultProcessorTest : public HandlerBaseTest {};

TEST_F(RPCDefaultProcessorTest, ValidInput)
{
    runSpawn([](auto yield) {
        HandlerMock const handler;
        rpc::impl::DefaultProcessor<HandlerMock> const processor;

        auto const input = boost::json::parse(R"JSON({ "something": "works" })JSON");
        auto const data = InOutFake{"works"};
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
        auto const input = boost::json::parse(R"JSON({})JSON");
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

        // "something" is required by InOutFake's spec but absent
        auto const input = boost::json::parse(R"JSON({ "other": "nope" })JSON");

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

        auto const input = boost::json::parse(R"JSON({ "token": "abc123" })JSON");
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
        auto const input = boost::json::parse(R"JSON({ "other": "value" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_FALSE(ret);
        EXPECT_TRUE(ret.warnings.empty());
        ASSERT_FALSE(ret.result.has_value());
        auto const& status = ret.result.error();
        ASSERT_TRUE(std::holds_alternative<rpc::RippledError>(status.code));
        EXPECT_EQ(std::get<rpc::RippledError>(status.code), rpc::RippledError::RpcInvalidParams);
        EXPECT_THAT(status.message, HasSubstr("token"));
    });
}

TEST_F(RPCDefaultProcessorTest, NewSpecHandler_DeprecatedField_WarningsForwarded)
{
    runSpawn([](auto yield) {
        DeprecatedFieldHandlerFake const handler;
        rpc::impl::DefaultProcessor<DeprecatedFieldHandlerFake> const processor;

        // "ident" is present — triggers the Deprecated checker
        auto const input = boost::json::parse(R"JSON({ "token": "abc", "ident": "old" })JSON");
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

        auto const input = boost::json::parse(R"JSON({ "token": "abc" })JSON");
        auto const ret = processor(handler, input, Context{yield});

        ASSERT_TRUE(ret);
        EXPECT_TRUE(ret.warnings.empty());
    });
}
