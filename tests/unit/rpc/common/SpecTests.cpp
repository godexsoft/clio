#include "rpc/Errors.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/Types.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "rpc/common/spec/WithCustomError.hpp"

#include <boost/json/parse.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

using namespace rpc::spec;

TEST(RpcSpecDSL, ValidRequestPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ledger_index", type<std::string>),
        field("limit", type<int64_t>, min(int64_t{1})),
    };

    auto request = boost::json::parse(R"JSON({
        "account":      "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "ledger_index": "validated",
        "limit":        20
    })JSON");

    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL, MissingRequiredFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("limit", type<int64_t>),
    };

    auto request = boost::json::parse(R"JSON({ "limit": 10 })JSON");

    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'account' missing");
}

TEST(RpcSpecDSL, WrongTypeFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("limit", type<int64_t>),
    };

    auto request = boost::json::parse(R"JSON({
        "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "limit":   "not-a-number"
    })JSON");

    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL, WrongBoolTypeFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("signer_lists", type<bool>),
    };

    auto bad = boost::json::parse(R"JSON({ "signer_lists": "yes" })JSON");
    EXPECT_FALSE(kSPEC.process(bad).has_value());

    auto valid = boost::json::parse(R"JSON({ "signer_lists": true })JSON");
    EXPECT_TRUE(kSPEC.process(valid).has_value());
}

TEST(RpcSpecDSL, MinRejectsValueBelowBound)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, min(int64_t{1})),
    };

    auto request = boost::json::parse(R"JSON({ "limit": 0 })JSON");
    EXPECT_FALSE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL, ClampMutatesJsonValueInPlace)
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

TEST(RpcSpecDSL, DeprecatedFieldProducesWarning)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ident", account, deprecated),
    };

    auto request = boost::json::parse(R"JSON({
        "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "ident":   "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
    })JSON");

    EXPECT_TRUE(kSPEC.process(request).has_value());

    auto const warnings = kSPEC.check(request);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].field, "ident");
}

TEST(RpcSpecDSL, NoWarningWhenDeprecatedFieldAbsent)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ident", account, deprecated),
    };

    auto request =
        boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    EXPECT_TRUE(kSPEC.check(request).empty());
}

TEST(RpcSpecDSL, VersionedSpecViaRpcSpecView)
{
    static constexpr auto kSPEC_V1 = RpcSpec{
        field("account", required, account),
        field("ident", account, deprecated),
    };
    static constexpr auto kSPEC_V2 = kSPEC_V1 + field("signer_lists", type<bool>);

    auto const spec = [](uint32_t version) -> RpcSpecConstRef {
        return version == 1 ? RpcSpecView{kSPEC_V1} : RpcSpecView{kSPEC_V2};
    };

    auto request = boost::json::parse(R"JSON({
        "account":      "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "signer_lists": "not-a-bool"
    })JSON");
    EXPECT_TRUE(spec(1).process(request).has_value());
    EXPECT_FALSE(spec(2).process(request).has_value());

    auto valid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "signer_lists": true })JSON"
    );
    EXPECT_TRUE(spec(2).process(valid).has_value());
}

TEST(RpcSpecDSL, WarningsCollectedAcrossAllFields)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ident", deprecated),
        field("ledger", deprecated),
    };

    auto request = boost::json::parse(R"JSON({
        "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "ident":   "old",
        "ledger":  "validated"
    })JSON");

    auto const warnings = kSPEC.check(request);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0].field, "ident");
    EXPECT_EQ(warnings[1].field, "ledger");
}

TEST(RpcSpecDSL, FullRequestPipeline)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ledger_index", type<std::string>),
        field("limit", type<int64_t>, min(int64_t{1}), clamp(int64_t{10}, int64_t{400})),
        field("ident", account, deprecated),
    };

    auto request = boost::json::parse(R"JSON({
        "account":      "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "ledger_index": "validated",
        "limit":        5,
        "ident":        "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
    })JSON");

    auto const warnings = kSPEC.check(request);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].field, "ident");

    ASSERT_TRUE(kSPEC.process(request).has_value());
    EXPECT_EQ(request.as_object().at("limit").as_int64(), 10);
}

TEST(RpcSpecDSL, PipeStyleFieldDefinition)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account") | required | account,
        field("limit") | type<int64_t> | min(int64_t{1}) | clamp(int64_t{10}, int64_t{400}),
    };

    auto valid = boost::json::parse(
        R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "limit": 50 })JSON"
    );
    EXPECT_TRUE(kSPEC.process(valid).has_value());

    auto missingAccount = boost::json::parse(R"JSON({ "limit": 50 })JSON");
    EXPECT_FALSE(kSPEC.process(missingAccount).has_value());
}

// ============================================================================
// IfType tests
// ============================================================================

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

// ============================================================================
// Mock backend — proves the abstraction works with a non-boost::json type.
//
// MockObject is a plain std::map<string, variant>. MockFieldAccess satisfies
// SomeFieldAccess. makeFieldAccess is found via ADL on MockObject (same namespace).
// ============================================================================

namespace rpc::spec {

using MockValue = std::variant<int64_t, uint32_t, bool, std::string, double>;

struct MockObject {
    std::map<std::string, MockValue> fields;
};

class MockFieldAccess {
    MockValue const* readValue_;
    MockValue* writeValue_;
    std::string_view key_;

public:
    MockFieldAccess(MockValue* v, std::string_view k) noexcept
        : readValue_{v}, writeValue_{v}, key_{k}
    {
    }

    MockFieldAccess(MockValue const* v, std::string_view k) noexcept
        : readValue_{v}, writeValue_{nullptr}, key_{k}
    {
    }

    [[nodiscard]] std::string_view
    key() const noexcept
    {
        return key_;
    }
    [[nodiscard]] bool
    present() const noexcept
    {
        return readValue_ != nullptr;
    }

    [[nodiscard]] bool
    isInt64() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<int64_t>(*readValue_);
    }
    [[nodiscard]] int64_t
    asInt64() const
    {
        return std::get<int64_t>(*readValue_);
    }

    [[nodiscard]] bool
    isUint32() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<uint32_t>(*readValue_);
    }
    [[nodiscard]] uint32_t
    asUint32() const
    {
        return std::get<uint32_t>(*readValue_);
    }

    [[nodiscard]] bool
    isBool() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<bool>(*readValue_);
    }
    [[nodiscard]] bool
    asBool() const
    {
        return std::get<bool>(*readValue_);
    }

    [[nodiscard]] bool
    isString() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<std::string>(*readValue_);
    }
    [[nodiscard]] std::string_view
    asString() const
    {
        return std::get<std::string>(*readValue_);
    }

    [[nodiscard]] bool
    isDouble() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<double>(*readValue_);
    }
    [[nodiscard]] double
    asDouble() const
    {
        return std::get<double>(*readValue_);
    }

    template <typename T>
    [[nodiscard]] bool
    is() const noexcept
    {
        return readValue_ != nullptr && std::holds_alternative<T>(*readValue_);
    }

    void
    set(int64_t v)
    {
        *writeValue_ = v;
    }
    void
    set(uint32_t v)
    {
        *writeValue_ = v;
    }
    void
    set(std::string_view v)
    {
        *writeValue_ = std::string{v};
    }
    void
    set(bool v)
    {
        *writeValue_ = v;
    }
    void
    set(double v)
    {
        *writeValue_ = v;
    }
};

static_assert(SomeFieldAccess<MockFieldAccess>);

[[nodiscard]] inline MockFieldAccess
makeFieldAccess(MockObject& obj, std::string_view key)
{
    auto it = obj.fields.find(std::string{key});
    return it != obj.fields.end() ? MockFieldAccess{&it->second, key}
                                  : MockFieldAccess{static_cast<MockValue*>(nullptr), key};
}

[[nodiscard]] inline MockFieldAccess
makeFieldAccess(MockObject const& obj, std::string_view key)
{
    auto it = obj.fields.find(std::string{key});
    return it != obj.fields.end() ? MockFieldAccess{&it->second, key}
                                  : MockFieldAccess{static_cast<MockValue const*>(nullptr), key};
}

}  // namespace rpc::spec

TEST(RpcSpecDSL_MockBackend, ValidRequestPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("limit", type<int64_t>, min(int64_t{1})),
    };

    MockObject obj{
        .fields = {
            {"account", std::string{"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"}}, {"limit", int64_t{10}}
        }
    };
    EXPECT_TRUE(kSPEC.process(obj).has_value());
}

TEST(RpcSpecDSL_MockBackend, MissingRequiredFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
    };

    MockObject obj{};
    auto const result = kSPEC.process(obj);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'account' missing");
}

TEST(RpcSpecDSL_MockBackend, WrongTypeFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>),
    };

    MockObject obj{.fields = {{"limit", std::string{"not-a-number"}}}};
    auto const result = kSPEC.process(obj);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL_MockBackend, ClampMutatesValueInPlace)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, clamp(int64_t{10}, int64_t{400})),
    };

    MockObject tooLow{.fields = {{"limit", int64_t{2}}}};
    ASSERT_TRUE(kSPEC.process(tooLow).has_value());
    EXPECT_EQ(std::get<int64_t>(tooLow.fields.at("limit")), 10);

    MockObject tooHigh{.fields = {{"limit", int64_t{9999}}}};
    ASSERT_TRUE(kSPEC.process(tooHigh).has_value());
    EXPECT_EQ(std::get<int64_t>(tooHigh.fields.at("limit")), 400);
}

TEST(RpcSpecDSL_MockBackend, IfTypeSkipsOnMismatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    // string value — int64 branch must not fire
    MockObject obj{.fields = {{"value", std::string{"hello"}}}};
    EXPECT_TRUE(kSPEC.process(obj).has_value());
}

TEST(RpcSpecDSL_MockBackend, IfTypeRunsOnMatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    MockObject bad{.fields = {{"value", int64_t{0}}}};
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());

    MockObject good{.fields = {{"value", int64_t{5}}}};
    EXPECT_TRUE(kSPEC.process(good).has_value());
}

TEST(RpcSpecDSL_MockBackend, DeprecatedFieldProducesWarning)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
        field("ident", deprecated),
    };

    MockObject const obj{
        .fields = {
            {"account", std::string{"rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"}},
            {"ident", std::string{"old"}}
        }
    };
    auto const warnings = kSPEC.check(obj);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].field, "ident");
}

// ============================================================================
// check() must never mutate — even when the spec contains modifiers.
// ============================================================================

TEST(RpcSpecDSL, CheckDoesNotInvokeModifiers)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, clamp(int64_t{10}, int64_t{400})),
    };

    auto request = boost::json::parse(R"JSON({ "limit": 2 })JSON");
    auto const warnings = kSPEC.check(request);
    EXPECT_TRUE(warnings.empty());
    EXPECT_EQ(request.as_object().at("limit").as_int64(), 2);  // unchanged
}

// ============================================================================
// Non-object root: every field appears absent; required fields fire.
// ============================================================================

TEST(RpcSpecDSL, NonObjectRootTreatsAllFieldsAsAbsent)
{
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>),  // optional — absent is fine
    };

    auto arr = boost::json::parse(R"JSON([1, 2, 3])JSON");
    EXPECT_TRUE(kSPEC.process(arr).has_value());

    auto scalar = boost::json::parse(R"JSON(42)JSON");
    EXPECT_TRUE(kSPEC.process(scalar).has_value());
}

TEST(RpcSpecDSL, NonObjectRootWithRequiredFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
    };

    auto arr = boost::json::parse(R"JSON([])JSON");
    auto const result = kSPEC.process(arr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(result.error().message, "Required field 'account' missing");
}

// ============================================================================
// Boundary cases: empty spec, empty field.
// ============================================================================

TEST(RpcSpecDSL, EmptySpecAcceptsEverything)
{
    static constexpr auto kSPEC = RpcSpec{};

    auto request = boost::json::parse(R"JSON({ "anything": 42 })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
    EXPECT_TRUE(kSPEC.check(request).empty());
}

TEST(RpcSpecDSL, FieldWithNoItemsIsNoOp)
{
    static constexpr auto kSPEC = RpcSpec{
        field("anything") | required,  // pipe-style starts from zero-item field
    };

    auto request = boost::json::parse(R"JSON({ "anything": 42 })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

// ============================================================================
// Type<T> direct coverage for every supported T.
// ============================================================================

TEST(RpcSpecDSL, TypeStringDirect)
{
    static constexpr auto kSPEC = RpcSpec{
        field("name", type<std::string>),
    };

    auto good = boost::json::parse(R"JSON({ "name": "alice" })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());

    auto bad = boost::json::parse(R"JSON({ "name": 42 })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL, TypeDoubleAcceptsDoubleAndRejectsOthers)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ratio", type<double>),
    };

    auto good = boost::json::parse(R"JSON({ "ratio": 1.5 })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());

    auto bad = boost::json::parse(R"JSON({ "ratio": "high" })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());

    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// Double-typed Min and Clamp — proves the new requires-clause supports double.
// ============================================================================

TEST(RpcSpecDSL, MinDouble)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ratio", type<double>, min(0.5)),
    };

    auto bad = boost::json::parse(R"JSON({ "ratio": 0.1 })JSON");
    auto const result = kSPEC.process(bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());

    auto good = boost::json::parse(R"JSON({ "ratio": 1.0 })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());
}

TEST(RpcSpecDSL, ClampDouble)
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

// ============================================================================
// uint32_t support — Type<uint32_t>, Min<uint32_t>, Clamp<uint32_t>.
// ============================================================================

TEST(RpcSpecDSL, TypeUint32AcceptsInRangeRejectsOthers)
{
    static constexpr auto kSPEC = RpcSpec{
        field("n", type<uint32_t>),
    };

    auto positive = boost::json::parse(R"JSON({ "n": 42 })JSON");
    EXPECT_TRUE(kSPEC.process(positive).has_value());

    auto maxU32 = boost::json::parse(R"JSON({ "n": 4294967295 })JSON");
    EXPECT_TRUE(kSPEC.process(maxU32).has_value());

    auto overflow = boost::json::parse(R"JSON({ "n": 4294967296 })JSON");
    auto const r1 = kSPEC.process(overflow);
    ASSERT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error(), rpc::RippledError::rpcINVALID_PARAMS);

    auto negative = boost::json::parse(R"JSON({ "n": -1 })JSON");
    auto const r2 = kSPEC.process(negative);
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL, MinUint32)
{
    static constexpr auto kSPEC = RpcSpec{
        field("n", type<uint32_t>, min(uint32_t{10})),
    };

    auto bad = boost::json::parse(R"JSON({ "n": 5 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);

    auto good = boost::json::parse(R"JSON({ "n": 100 })JSON");
    EXPECT_TRUE(kSPEC.process(good).has_value());
}

TEST(RpcSpecDSL, ClampUint32)
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

// ============================================================================
// AccountFormat — real validator from rpc::accountFromStringStrict.
// ============================================================================

TEST(RpcSpecDSL, AccountFormatRejectsInvalidString)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", account),
    };

    auto bad = boost::json::parse(R"JSON({ "account": "rNotAValidAccount" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcACT_MALFORMED);
    EXPECT_EQ(r.error().message, "accountMalformed");
}

TEST(RpcSpecDSL, AccountFormatRejectsNonString)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", account),
    };

    auto bad = boost::json::parse(R"JSON({ "account": 12345 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "accountNotString");
}

TEST(RpcSpecDSL, AccountFormatAbsentFieldAccepted)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", account),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

// ============================================================================
// uint64 boundary: BoostJsonFieldAccess::isInt64() guards against overflow.
// Values > INT64_MAX must not satisfy Type<int64_t>.
// ============================================================================

TEST(RpcSpecDSL, Uint64AboveInt64MaxFailsTypeInt64)
{
    static constexpr auto kSPEC = RpcSpec{
        field("n", type<int64_t>),
    };

    // 2^63 — one above INT64_MAX, parsed as uint64 by boost::json.
    auto request = boost::json::parse(R"JSON({ "n": 9223372036854775808 })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL, Uint64WithinInt64RangePassesTypeInt64)
{
    static constexpr auto kSPEC = RpcSpec{
        field("n", type<int64_t>, min(int64_t{0})),
    };

    // INT64_MAX exactly — boost::json may parse as uint64; must still be accepted.
    auto request = boost::json::parse(R"JSON({ "n": 9223372036854775807 })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

// ============================================================================
// Pipe-style IfType with sub-items.
// ============================================================================

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

// ============================================================================
// Consteval smoke test — proves the spec is genuinely constant-evaluable.
// ============================================================================

TEST(RpcSpecDSL, SpecIsConstantEvaluable)
{
    static constexpr auto kSPEC = RpcSpec{
        field("a", required),
        field("b", type<int64_t>, min(int64_t{1})),
        field("c", deprecated),
    };
    static constexpr auto kFIELD_COUNT = std::tuple_size_v<decltype(kSPEC.fields)>;
    static_assert(kFIELD_COUNT == 3);
    EXPECT_EQ(kFIELD_COUNT, 3u);
}

// ============================================================================
// Concept satisfaction — every shipped validator is recognised by the
// SomeRequirement / SomeModifier / SomeCheck concepts. The concepts witness
// against a private archetype (not BoostJsonFieldAccess), so satisfaction
// proves the validators are genuinely backend-agnostic.
// ============================================================================

static_assert(rpc::spec::SomeRequirement<rpc::spec::Required>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<int64_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<bool>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<std::string>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<double>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<uint32_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Min<int64_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Min<uint32_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Min<double>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::AccountFormat>);
static_assert(rpc::spec::SomeModifier<rpc::spec::Clamp<int64_t>>);
static_assert(rpc::spec::SomeModifier<rpc::spec::Clamp<uint32_t>>);
static_assert(rpc::spec::SomeModifier<rpc::spec::Clamp<double>>);
static_assert(rpc::spec::SomeCheck<rpc::spec::Deprecated>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::TimeFormatValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::WithCustomError<rpc::spec::Required>>);
static_assert(rpc::spec::SomeModifier<rpc::spec::WithCustomError<rpc::spec::Clamp<int64_t>>>);

// ============================================================================
// WithCustomError — code-only override.
// ============================================================================

TEST(RpcSpecDSL_WithCustomError, OverridesCodeOnRequirementFailure)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", withCustomError(required, rpc::RippledError::rpcACT_MALFORMED)),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcACT_MALFORMED);
    EXPECT_TRUE(result.error().message.empty());  // no message override given
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

// ============================================================================
// TimeFormatValidator — port of validation::TimeFormatValidator.
// ============================================================================

TEST(RpcSpecDSL_TimeFormat, ValidIsoStringAccepted)
{
    static constexpr auto kSPEC = RpcSpec{
        field("date", type<std::string>, timeFormat("%Y-%m-%dT%TZ")),
    };

    auto request = boost::json::parse(R"JSON({ "date": "2025-05-07T12:34:56Z" })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_TimeFormat, MalformedStringRejected)
{
    static constexpr auto kSPEC = RpcSpec{
        field("date", timeFormat("%Y-%m-%dT%TZ")),
    };

    auto request = boost::json::parse(R"JSON({ "date": "not-a-date" })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_TimeFormat, NonStringRejected)
{
    static constexpr auto kSPEC = RpcSpec{
        field("date", timeFormat("%Y-%m-%dT%TZ")),
    };

    auto request = boost::json::parse(R"JSON({ "date": 12345 })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_TimeFormat, AbsentFieldAccepted)
{
    static constexpr auto kSPEC = RpcSpec{
        field("date", timeFormat("%Y-%m-%dT%TZ")),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}
