#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/Aliases.hpp>
#include <rpcspec/Converters.hpp>
#include <rpcspec/HandlerFor.hpp>
#include <rpcspec/Typed.hpp>
#include <rpcspec/Validators.hpp>
#include <rpcspec/VersionedSpec.hpp>

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <gmock/gmock.h>

#include <cstdint>
#include <optional>
#include <string>

namespace tests::common {

// input data for the test handlers below
struct TestInput {
    std::string hello;
    std::optional<uint32_t> limit;
};

// output data produced by the test handlers below
struct TestOutput {
    std::string computed;
};

// Typed spec for TestInput: `hello` is required and must equal "world"; `limit` is an
// optional 0..100 uint. Resolved by the framework via specFor(TestInput const*).
inline constexpr auto kTestInputSpec = rpc::spec::spec<TestInput>(
    rpc::spec::field(
        "hello",
        &TestInput::hello,
        rpc::spec::required,
        rpc::spec::type<std::string>,
        rpc::spec::oneOf("world"),
        rpc::spec::asString
    ),
    rpc::spec::field(
        "limit",
        &TestInput::limit,
        rpc::spec::type<uint32_t>,
        rpc::spec::between(uint32_t{0}, uint32_t{100}),
        rpc::spec::asUint32
    )
);
inline constexpr auto kTestInputVersioned = rpc::spec::versioned<TestInput>(kTestInputSpec);

[[nodiscard]] constexpr auto const&
specFor(TestInput const*) noexcept
{
    return kTestInputVersioned;
}

// must be implemented as per rpc/common/Concepts.h
inline void
tag_invoke(boost::json::value_from_tag, boost::json::value& jv, TestOutput const& output)
{
    jv = {{"computed", output.computed}};
}

// example handler
class HandlerFake : public rpc::spec::HandlerFor<TestInput> {
public:
    using Input = TestInput;
    using Output = TestOutput;
    using Result = rpc::HandlerReturnType<Output>;

    static Result
    process(Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{input.hello + '_' + std::to_string(input.limit.value_or(0))};
    }
};

class NoInputHandlerFake {
public:
    using Output = TestOutput;
    using Result = rpc::HandlerReturnType<Output>;

    static Result
    process([[maybe_unused]] rpc::Context const& ctx)
    {
        return Output{"test"};
    }
};

// example handler that returns custom error
class FailingHandlerFake : public rpc::spec::HandlerFor<TestInput> {
public:
    using Input = TestInput;
    using Output = TestOutput;
    using Result = rpc::HandlerReturnType<Output>;

    static Result
    process([[maybe_unused]] Input input, [[maybe_unused]] rpc::Context const& ctx)
    {
        // always fail
        return rpc::Error{rpc::Status{"Very custom error"}};
    }
};

struct InOutFake {
    std::string something;

    // Note: no spaceship comparison possible for std::string
    friend bool
    operator==(InOutFake const& lhs, InOutFake const& rhs) = default;
};

// Typed spec for InOutFake: `something` is a required string.
inline constexpr auto kInOutFakeSpec = rpc::spec::spec<InOutFake>(
    rpc::spec::field(
        "something", &InOutFake::something, rpc::spec::required, rpc::spec::type<std::string>, rpc::spec::asString
    )
);
inline constexpr auto kInOutFakeVersioned = rpc::spec::versioned<InOutFake>(kInOutFakeSpec);

[[nodiscard]] constexpr auto const&
specFor(InOutFake const*) noexcept
{
    return kInOutFakeVersioned;
}

// must be implemented as per rpc/common/Concepts.h
inline void
tag_invoke(boost::json::value_from_tag, boost::json::value& jv, InOutFake const& output)
{
    jv = {{"something", output.something}};
}

struct HandlerMock : rpc::spec::HandlerFor<InOutFake> {
    using Input = InOutFake;
    using Output = InOutFake;
    using Result = rpc::HandlerReturnType<Output>;

    MOCK_METHOD(Result, process, (Input, rpc::Context const&), (const));
};

struct HandlerWithoutInputMock {
    using Output = InOutFake;
    using Result = rpc::HandlerReturnType<Output>;

    MOCK_METHOD(Result, process, (rpc::Context const&), (const));
};

}  // namespace tests::common
