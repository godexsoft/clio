#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/Validators.hpp"

#include <boost/json/parse.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <string>

using namespace rpc::spec;

TEST(RpcSpecDSL_Clamp, Int64MutatesJsonValueInPlace)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, clamp(int64_t{10}, int64_t{400})),
    };

    auto tooLow = boost::json::parse(R"JSON({ "limit": 2 })JSON");
    ASSERT_TRUE(kSPEC.process(tooLow).has_value());
    EXPECT_EQ(tooLow.as_object().at("limit").as_int64(), 10);

    auto tooHigh = boost::json::parse(R"JSON({ "limit": 9999 })JSON");
    ASSERT_TRUE(kSPEC.process(tooHigh).has_value());
    EXPECT_EQ(tooHigh.as_object().at("limit").as_int64(), 400);

    auto inRange = boost::json::parse(R"JSON({ "limit": 50 })JSON");
    ASSERT_TRUE(kSPEC.process(inRange).has_value());
    EXPECT_EQ(inRange.as_object().at("limit").as_int64(), 50);
}

TEST(RpcSpecDSL_Clamp, DoubleClamp)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ratio", type<double>, clamp(0.0, 1.0)),
    };

    auto tooLow = boost::json::parse(R"JSON({ "ratio": -0.5 })JSON");
    ASSERT_TRUE(kSPEC.process(tooLow).has_value());
    EXPECT_DOUBLE_EQ(tooLow.as_object().at("ratio").as_double(), 0.0);

    auto tooHigh = boost::json::parse(R"JSON({ "ratio": 1.5 })JSON");
    ASSERT_TRUE(kSPEC.process(tooHigh).has_value());
    EXPECT_DOUBLE_EQ(tooHigh.as_object().at("ratio").as_double(), 1.0);
}

TEST(RpcSpecDSL_Clamp, Uint32Clamp)
{
    static constexpr auto kSPEC = RpcSpec{
        field("n", type<uint32_t>, clamp(uint32_t{10}, uint32_t{400})),
    };

    auto tooLow = boost::json::parse(R"JSON({ "n": 5 })JSON");
    ASSERT_TRUE(kSPEC.process(tooLow).has_value());
    EXPECT_EQ(tooLow.as_object().at("n").as_uint64(), 10u);

    auto tooHigh = boost::json::parse(R"JSON({ "n": 9999 })JSON");
    ASSERT_TRUE(kSPEC.process(tooHigh).has_value());
    EXPECT_EQ(tooHigh.as_object().at("n").as_uint64(), 400u);
}

TEST(RpcSpecDSL_IfType, SkipsSubValidatorsOnTypeMismatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    auto request = boost::json::parse(R"JSON({ "value": "hello" })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfType, RunsSubValidatorsOnTypeMatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    auto bad = boost::json::parse(R"JSON({ "value": 0 })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());

    auto good = boost::json::parse(R"JSON({ "value": 5 })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());
}

TEST(RpcSpecDSL_IfType, AbsentFieldIsSkipped)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfType, ModifierMutatesOnTypeMatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", ifType<int64_t>(clamp(int64_t{10}, int64_t{400}))),
    };

    auto request = boost::json::parse(R"JSON({ "limit": 3 })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("limit").as_int64(), 10);
}

TEST(RpcSpecDSL_IfType, ModifierSkipsOnTypeMismatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", ifType<int64_t>(clamp(int64_t{10}, int64_t{400}))),
    };

    auto request = boost::json::parse(R"JSON({ "limit": "default" })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("limit").as_string(), "default");
}

TEST(RpcSpecDSL_IfType, MultipleSubValidatorsAllRun)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", ifType<int64_t>(min(int64_t{1}), clamp(int64_t{10}, int64_t{400}))),
    };

    auto tooLow = boost::json::parse(R"JSON({ "limit": 0 })JSON");
    EXPECT_FALSE(kSPEC.process(tooLow).has_value());

    auto clamped = boost::json::parse(R"JSON({ "limit": 5 })JSON");
    ASSERT_TRUE(kSPEC.process(clamped).has_value());
    EXPECT_EQ(clamped.as_object().at("limit").as_int64(), 10);

    auto cappedHigh = boost::json::parse(R"JSON({ "limit": 9999 })JSON");
    ASSERT_TRUE(kSPEC.process(cappedHigh).has_value());
    EXPECT_EQ(cappedHigh.as_object().at("limit").as_int64(), 400);
}

TEST(RpcSpecDSL_IfType, StopsAtFirstSubValidatorError)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{5}), min(int64_t{10}))),
    };

    auto request = boost::json::parse(R"JSON({ "value": 3 })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL_IfType, UnionTypeLedgerIndex)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ledger_index", ifType<int64_t>(min(int64_t{0})), ifType<std::string>()),
    };

    auto intValid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": 42 })JSON"
    );
    EXPECT_TRUE(kSPEC.process(intValid).has_value());

    auto intNeg = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": -1 })JSON"
    );
    EXPECT_FALSE(kSPEC.process(intNeg).has_value());

    auto strValid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": "validated" })JSON"
    );
    EXPECT_TRUE(kSPEC.process(strValid).has_value());

    auto absent =
        boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());

    auto wrongType = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": true })JSON"
    );
    EXPECT_TRUE(kSPEC.process(wrongType).has_value());
}

TEST(RpcSpecDSL_IfType, PipeStyle)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account") | required | account,
        field("ledger_index") | ifType<int64_t>(min(int64_t{0})),
    };

    auto valid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": 100 })JSON"
    );
    EXPECT_TRUE(kSPEC.process(valid).has_value());

    auto invalid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "ledger_index": -5 })JSON"
    );
    EXPECT_FALSE(kSPEC.process(invalid).has_value());
}

TEST(RpcSpecDSL_IfType, CombinedWithOtherValidators)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field(
            "limit", required, ifType<int64_t>(min(int64_t{1}), clamp(int64_t{10}, int64_t{400}))
        ),
    };

    auto noLimit =
        boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    auto const result = kSPEC.process(noLimit);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'limit' missing");

    auto strLimit = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "limit": "max" })JSON"
    );
    EXPECT_TRUE(kSPEC.process(strLimit).has_value());

    auto good = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "limit": 50 })JSON"
    );
    ASSERT_TRUE(kSPEC.process(good).has_value());
    EXPECT_EQ(good.as_object().at("limit").as_int64(), 50);

    auto low = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "limit": 2 })JSON"
    );
    ASSERT_TRUE(kSPEC.process(low).has_value());
    EXPECT_EQ(low.as_object().at("limit").as_int64(), 10);
}

TEST(RpcSpecDSL_IfType, PipeStyleWithSubItems)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit") | ifType<int64_t>(min(int64_t{1}), clamp(int64_t{10}, int64_t{400})),
    };

    auto low = boost::json::parse(R"JSON({ "limit": 5 })JSON");
    ASSERT_TRUE(kSPEC.process(low).has_value());
    EXPECT_EQ(low.as_object().at("limit").as_int64(), 10);

    auto bad = boost::json::parse(R"JSON({ "limit": 0 })JSON");
    EXPECT_FALSE(kSPEC.process(bad).has_value());
}

TEST(RpcSpecDSL_Section, ValidSubObjectPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field(
            "taker_pays",
            section(
                field("currency", required, type<std::string>), field("value", type<std::string>)
            )
        ),
    };

    auto request =
        boost::json::parse(R"JSON({ "taker_pays": { "currency": "XRP", "value": "1" } })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_Section, MissingRequiredSubFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("taker_pays", section(field("currency", required))),
    };

    auto request = boost::json::parse(R"JSON({ "taker_pays": {} })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'currency' missing");
}

TEST(RpcSpecDSL_Section, WrongSubFieldTypeFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("taker_pays", section(field("currency", required, type<std::string>))),
    };

    auto request = boost::json::parse(R"JSON({ "taker_pays": { "currency": 42 } })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Section, AbsentParentFieldSkipsSection)
{
    static constexpr auto kSPEC = RpcSpec{
        field("taker_pays", section(field("currency", required))),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_Section, NonObjectParentFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("taker_pays", section(field("currency", required))),
    };

    auto request = boost::json::parse(R"JSON({ "taker_pays": "XRP" })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Section, ModifierMutatesSubField)
{
    static constexpr auto kSPEC = RpcSpec{
        field("options", section(field("limit", type<int64_t>, clamp(int64_t{10}, int64_t{400})))),
    };

    auto request = boost::json::parse(R"JSON({ "options": { "limit": 3 } })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("options").as_object().at("limit").as_int64(), 10);
}

TEST(RpcSpecDSL_Section, PipeStyle)
{
    static constexpr auto kSPEC = RpcSpec{
        field("payload") |
            section(
                field("type", required, type<std::string>), field("value", required, type<int64_t>)
            ),
    };

    auto good = boost::json::parse(R"JSON({ "payload": { "type": "foo", "value": 1 } })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());

    auto bad = boost::json::parse(R"JSON({ "payload": { "type": "foo" } })JSON");
    EXPECT_FALSE(kSPEC.process(bad).has_value());
}

TEST(RpcSpecDSL_IfObject, SkipsWhenFieldIsNotObject)
{
    static constexpr auto kSPEC = RpcSpec{
        field("entry", ifObject(section(field("a", required)))),
    };

    // string value — object branch must not fire
    auto request = boost::json::parse(R"JSON({ "entry": "validated" })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfObject, RunsSectionWhenFieldIsObject)
{
    static constexpr auto kSPEC = RpcSpec{
        field("entry", ifObject(section(field("a", required, type<std::string>)))),
    };

    auto good = boost::json::parse(R"JSON({ "entry": { "a": "hello" } })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());

    auto bad = boost::json::parse(R"JSON({ "entry": {} })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'a' missing");
}

TEST(RpcSpecDSL_IfObject, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{
        field("entry", ifObject(section(field("a", required)))),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfArray, SkipsWhenFieldIsNotArray)
{
    // A no-op sub-processor just to exercise the type check.
    static constexpr auto kSPEC = RpcSpec{
        field("ids", ifArray(ifType<int64_t>())),
    };

    // object — not an array, should be skipped
    auto request = boost::json::parse(R"JSON({ "ids": {} })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfArray, RunsSubProcessorsWhenFieldIsArray)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ids", ifArray(ifType<int64_t>())),
    };

    auto request = boost::json::parse(R"JSON({ "ids": [1, 2, 3] })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_IfArray, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ids", ifArray(ifType<int64_t>())),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_WithCustomError, OverridesCodeOnRequirementFailure)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", withCustomError(required, rpc::RippledError::rpcACT_MALFORMED)),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcACT_MALFORMED);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL_WithCustomError, PassesThroughWhenWrappedSucceeds)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", withCustomError(required, rpc::RippledError::rpcACT_MALFORMED)),
    };

    auto request =
        boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_WithCustomError, AppendsCustomMessageOnFailure)
{
    static constexpr auto kSPEC = RpcSpec{
        field(
            "marker",
            withCustomError(required, rpc::RippledError::rpcINVALID_PARAMS, "invalidMarker")
        ),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "invalidMarker");
}

TEST(RpcSpecDSL_WithCustomError, ModifierPathOverridesCode)
{
    // Wrapping IfType (a SomeModifier) — sub-validator failure must surface as the custom code.
    static constexpr auto kSPEC = RpcSpec{
        field(
            "limit",
            withCustomError(
                ifType<int64_t>(min(int64_t{1})), rpc::RippledError::rpcINVALID_PARAMS, "tooLow"
            )
        ),
    };

    auto bad = boost::json::parse(R"JSON({ "limit": 0 })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "tooLow");

    // Type mismatch: IfType skips, no error fires.
    auto skipped = boost::json::parse(R"JSON({ "limit": "default" })JSON");
    EXPECT_TRUE(kSPEC.process(skipped).has_value());
}

TEST(RpcSpecDSL_CustomModifier, LambdaInvokedWhenPresent)
{
    static constexpr auto kSPEC = RpcSpec{
        field("val", customModifier([](auto& f) -> rpc::MaybeError {
                  f.set(int64_t{99});
                  return {};
              })),
    };
    auto request = boost::json::parse(R"JSON({ "val": 1 })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("val").as_int64(), 99);
}

TEST(RpcSpecDSL_CustomModifier, LambdaNotInvokedWhenAbsent)
{
    static constexpr auto kSPEC = RpcSpec{
        field("val", customModifier([](auto& f) -> rpc::MaybeError {
                  f.set(int64_t{99});
                  return {};
              })),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

TEST(RpcSpecDSL_CustomModifier, LambdaCanReturnError)
{
    static constexpr auto kSPEC = RpcSpec{
        field("val", customModifier([](auto& /*f*/) -> rpc::MaybeError {
                  return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};
              })),
    };
    auto request = boost::json::parse(R"JSON({ "val": 1 })JSON");
    auto const r = kSPEC.process(request);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_ToLower, ConvertsToLowercase)
{
    static constexpr auto kSPEC = RpcSpec{
        field("tx_type", toLower),
    };
    auto request = boost::json::parse(R"JSON({ "tx_type": "Payment" })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("tx_type").as_string(), "payment");
}

TEST(RpcSpecDSL_ToLower, AlreadyLowercaseUnchanged)
{
    static constexpr auto kSPEC = RpcSpec{
        field("tx_type", toLower),
    };
    auto request = boost::json::parse(R"JSON({ "tx_type": "payment" })JSON");
    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("tx_type").as_string(), "payment");
}

TEST(RpcSpecDSL_ToLower, AbsentFieldNoOp)
{
    static constexpr auto kSPEC = RpcSpec{
        field("tx_type", toLower),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

TEST(RpcSpecDSL_ToLower, NonStringNoOp)
{
    static constexpr auto kSPEC = RpcSpec{
        field("tx_type", toLower),
    };
    auto num = boost::json::parse(R"JSON({ "tx_type": 42 })JSON");
    ASSERT_TRUE(kSPEC.process(num).has_value());
    EXPECT_TRUE(num.as_object().at("tx_type").is_int64());
}

TEST(RpcSpecDSL_ClampAs, Int32OverflowClampedToMax)
{
    static constexpr auto kSPEC = RpcSpec{field("v", type<int64_t>, clampAs<int32_t>)};
    auto j = boost::json::parse(R"JSON({ "v": 4294967296 })JSON");
    EXPECT_TRUE(kSPEC.process(j).has_value());
    EXPECT_EQ(j.as_object().at("v").as_int64(), std::numeric_limits<int32_t>::max());
}

TEST(RpcSpecDSL_ClampAs, Int32UnderflowClampedToMin)
{
    static constexpr auto kSPEC = RpcSpec{field("v", type<int64_t>, clampAs<int32_t>)};
    auto j = boost::json::parse(R"JSON({ "v": -4294967296 })JSON");
    EXPECT_TRUE(kSPEC.process(j).has_value());
    EXPECT_EQ(j.as_object().at("v").as_int64(), std::numeric_limits<int32_t>::min());
}

TEST(RpcSpecDSL_ClampAs, Int32InRangeUnchanged)
{
    static constexpr auto kSPEC = RpcSpec{field("v", type<int64_t>, clampAs<int32_t>)};
    auto j = boost::json::parse(R"JSON({ "v": 12345 })JSON");
    EXPECT_TRUE(kSPEC.process(j).has_value());
    EXPECT_EQ(j.as_object().at("v").as_int64(), 12345);
}

TEST(RpcSpecDSL_ClampAs, Uint32OverflowClampedToMax)
{
    static constexpr auto kSPEC = RpcSpec{field("v", type<int64_t>, clampAs<uint32_t>)};
    auto j = boost::json::parse(R"JSON({ "v": 8589934592 })JSON");
    EXPECT_TRUE(kSPEC.process(j).has_value());
    EXPECT_EQ(
        j.as_object().at("v").as_uint64(),
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
    );
}

TEST(RpcSpecDSL_ClampAs, Uint32NegativeClampedToZero)
{
    static constexpr auto kSPEC = RpcSpec{field("v", type<int64_t>, clampAs<uint32_t>)};
    auto j = boost::json::parse(R"JSON({ "v": -5 })JSON");
    EXPECT_TRUE(kSPEC.process(j).has_value());
    EXPECT_EQ(j.as_object().at("v").as_uint64(), 0u);
}

TEST(RpcSpecDSL_ClampAs, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("v", clampAs<int32_t>)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}
