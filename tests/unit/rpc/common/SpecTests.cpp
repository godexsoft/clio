#include "rpc/Errors.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldAccess.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/Section.hpp"
#include "rpc/common/spec/Types.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "rpc/common/spec/WarningsToJson.hpp"
#include "rpc/common/spec/WithCustomError.hpp"

#include <boost/json/parse.hpp>
#include <gtest/gtest.h>

#include <cstddef>
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
    EXPECT_EQ(warnings[0].code, rpc::WarningCode::WarnRpcDeprecated);
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
    EXPECT_EQ(warnings[0].code, rpc::WarningCode::WarnRpcDeprecated);
    EXPECT_EQ(warnings[1].field, "ledger");
    EXPECT_EQ(warnings[1].code, rpc::WarningCode::WarnRpcDeprecated);
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
    EXPECT_EQ(warnings[0].code, rpc::WarningCode::WarnRpcDeprecated);

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

    [[nodiscard]] static bool
    isObject() noexcept
    {
        return false;  // MockObject is flat; nested objects not supported
    }
    [[nodiscard]] static bool
    isArray() noexcept
    {
        return false;
    }
    [[nodiscard]] static std::size_t
    arraySize() noexcept
    {
        return 0;  // MockObject is flat; no arrays
    }

    template <typename T>
    [[nodiscard]] bool
    is() const noexcept
    {
        if constexpr (
            std::is_same_v<T, rpc::spec::JsonObject> || std::is_same_v<T, rpc::spec::JsonArray>
        ) {
            return false;  // MockObject is flat
        } else {
            return readValue_ != nullptr && std::holds_alternative<T>(*readValue_);
        }
    }

    [[nodiscard]] static MockFieldAccess
    child(std::string_view k) noexcept
    {
        return {static_cast<MockValue const*>(nullptr), k};
    }
    [[nodiscard]] static MockFieldAccess
    element(std::size_t) noexcept
    {
        return {static_cast<MockValue const*>(nullptr), {}};
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
    EXPECT_EQ(warnings[0].code, rpc::WarningCode::WarnRpcDeprecated);
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

// Section / IfObject / IfArray are SomeModifier
using SimpleSection = rpc::spec::Section<rpc::spec::FieldSpec<rpc::spec::Required>>;
static_assert(rpc::spec::SomeModifier<SimpleSection>);
using SimpleIfObject = rpc::spec::IfObject<SimpleSection>;
static_assert(rpc::spec::SomeModifier<SimpleIfObject>);
using SimpleIfArray = rpc::spec::IfArray<SimpleSection>;
static_assert(rpc::spec::SomeModifier<SimpleIfArray>);

// New validators
static_assert(rpc::spec::SomeRequirement<rpc::spec::Uint256HexStringValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Uint192HexStringValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Uint160HexStringValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::LedgerIndexValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::AccountBase58Validator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::CurrencyValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::IssuerValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::CredentialTypeValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::AuthorizeCredentialValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<rpc::spec::JsonObject>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<rpc::spec::JsonArray>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<std::string, int64_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Type<std::string, rpc::spec::JsonObject>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::NotSupported>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::OneOfValidator<2>>);
static_assert(rpc::spec::SomeModifier<rpc::spec::ToLowerModifier>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Between<uint32_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Between<int64_t>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Between<double>>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::Hex256ArrayValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::AccountMarkerValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::AccountTypeValidator>);
static_assert(rpc::spec::SomeRequirement<rpc::spec::LedgerEntryTypeValidator>);

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

// ============================================================================
// WarningsToJson — verifies that spec::toJsonArray mirrors the old wire-format
// aggregator: groups by code, appends each extra message with a leading space.
// ============================================================================

TEST(RpcSpecDSL_WarningsToJson, SingleDeprecatedFieldProducesGroupedWarning)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required, account),
        field("ident", account, deprecated),
    };

    auto request = boost::json::parse(R"JSON({
        "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
        "ident":   "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn"
    })JSON");

    auto const warnings = kSPEC.check(request);
    auto const arr = rpc::spec::toJsonArray(warnings);

    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0].as_object().at("id").as_int64(), 2004);

    auto const msg = std::string{arr[0].as_object().at("message").as_string()};
    // Standard text must be present as a prefix.
    EXPECT_NE(msg.find("deprecated"), std::string::npos);
    // The per-field extra must be appended with a leading space.
    EXPECT_NE(msg.find(" Field 'ident' is deprecated."), std::string::npos);
}

TEST(RpcSpecDSL_WarningsToJson, MultipleDeprecatedFieldsGroupIntoOneEntry)
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

    auto const arr = rpc::spec::toJsonArray(warnings);

    // Both fields share WarnRpcDeprecated — they must collapse into a single object.
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0].as_object().at("id").as_int64(), 2004);

    auto const msg = std::string{arr[0].as_object().at("message").as_string()};
    EXPECT_NE(msg.find(" Field 'ident' is deprecated."), std::string::npos);
    EXPECT_NE(msg.find(" Field 'ledger' is deprecated."), std::string::npos);
}

TEST(RpcSpecDSL_WarningsToJson, EmptyWarningsProducesEmptyArray)
{
    Warnings const empty{};
    auto const arr = rpc::spec::toJsonArray(empty);
    EXPECT_TRUE(arr.empty());
}

// ============================================================================
// Section — validates named sub-fields within an object field.
// ============================================================================

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

// ============================================================================
// IfObject — runs sub-processors only when the field is a JSON object.
// ============================================================================

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

// ============================================================================
// IfArray — runs sub-processors only when the field is a JSON array.
// ============================================================================

TEST(RpcSpecDSL_IfArray, SkipsWhenFieldIsNotArray)
{
    // A no-op sub-processor just to exercise the type check.
    static constexpr auto kSPEC = RpcSpec{
        field("ids", ifArray(ifType<int64_t>())),  // noop, just guards the type
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

// ============================================================================
// FieldAccess navigation — child() and element() on BoostJsonFieldAccess.
// ============================================================================

TEST(RpcSpecDSL_FieldAccess, ChildReturnsAbsentFaWhenParentAbsent)
{
    auto request = boost::json::parse(R"JSON({})JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "foo");
    EXPECT_FALSE(fa.present());
    auto child = fa.child("bar");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldAccess, ChildReturnsAbsentFaWhenParentNotObject)
{
    auto request = boost::json::parse(R"JSON({ "foo": 42 })JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "foo");
    EXPECT_TRUE(fa.present());
    EXPECT_FALSE(fa.isObject());
    auto child = fa.child("bar");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldAccess, ChildNavigatesIntoSubObject)
{
    auto request = boost::json::parse(R"JSON({ "foo": { "bar": "hello" } })JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "foo");
    ASSERT_TRUE(fa.present());
    ASSERT_TRUE(fa.isObject());

    auto child = fa.child("bar");
    ASSERT_TRUE(child.present());
    EXPECT_TRUE(child.isString());
    EXPECT_EQ(child.asString(), "hello");
}

TEST(RpcSpecDSL_FieldAccess, ChildMissingKeyReturnsAbsent)
{
    auto request = boost::json::parse(R"JSON({ "foo": { "a": 1 } })JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "foo");
    auto child = fa.child("missing");
    EXPECT_FALSE(child.present());
}

TEST(RpcSpecDSL_FieldAccess, ElementNavigatesIntoArray)
{
    auto request = boost::json::parse(R"JSON({ "ids": [10, 20, 30] })JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "ids");
    ASSERT_TRUE(fa.isArray());

    auto elem0 = fa.element(0);
    ASSERT_TRUE(elem0.present());
    EXPECT_TRUE(elem0.isInt64());
    EXPECT_EQ(elem0.asInt64(), 10);

    auto elem2 = fa.element(2);
    ASSERT_TRUE(elem2.present());
    EXPECT_EQ(elem2.asInt64(), 30);
}

TEST(RpcSpecDSL_FieldAccess, ElementOutOfBoundsReturnsAbsent)
{
    auto request = boost::json::parse(R"JSON({ "ids": [1, 2] })JSON");
    auto fa = rpc::spec::makeFieldAccess(request, "ids");
    EXPECT_FALSE(fa.element(5).present());
}

// ============================================================================
// Type<JsonObject> and Type<JsonArray>
// ============================================================================

TEST(RpcSpecDSL_TypeObject, AcceptsObjectRejectsOthers)
{
    static constexpr auto kSPEC = RpcSpec{
        field("entry", type<JsonObject>),
    };

    auto obj = boost::json::parse(R"JSON({ "entry": {} })JSON");
    EXPECT_TRUE(kSPEC.process(obj).has_value());

    auto str = boost::json::parse(R"JSON({ "entry": "hello" })JSON");
    auto const r = kSPEC.process(str);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(r.error().message.empty());

    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

TEST(RpcSpecDSL_TypeArray, AcceptsArrayRejectsOthers)
{
    static constexpr auto kSPEC = RpcSpec{
        field("ids", type<JsonArray>),
    };

    auto arr = boost::json::parse(R"JSON({ "ids": [1, 2] })JSON");
    EXPECT_TRUE(kSPEC.process(arr).has_value());

    auto str = boost::json::parse(R"JSON({ "ids": "hello" })JSON");
    EXPECT_FALSE(kSPEC.process(str).has_value());

    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// Multi-type Type<T1, T2, ...> — OR semantics
// ============================================================================

TEST(RpcSpecDSL_MultiType, AcceptsFirstType)
{
    static constexpr auto kSPEC = RpcSpec{
        field("v", type<int64_t, std::string>),
    };
    auto goodInt = boost::json::parse(R"JSON({ "v": 42 })JSON");
    EXPECT_TRUE(kSPEC.process(goodInt).has_value());
}

TEST(RpcSpecDSL_MultiType, AcceptsSecondType)
{
    static constexpr auto kSPEC = RpcSpec{
        field("v", type<int64_t, std::string>),
    };
    auto goodStr = boost::json::parse(R"JSON({ "v": "hello" })JSON");
    EXPECT_TRUE(kSPEC.process(goodStr).has_value());
}

TEST(RpcSpecDSL_MultiType, RejectsNeitherType)
{
    static constexpr auto kSPEC = RpcSpec{
        field("v", type<int64_t, std::string>),
    };
    auto bad = boost::json::parse(R"JSON({ "v": true })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_MultiType, AcceptsObjectWhenIncluded)
{
    static constexpr auto kSPEC = RpcSpec{
        field("entry", type<std::string, JsonObject>),
    };
    auto str = boost::json::parse(R"JSON({ "entry": "abc" })JSON");
    EXPECT_TRUE(kSPEC.process(str).has_value());

    auto obj = boost::json::parse(R"JSON({ "entry": {} })JSON");
    EXPECT_TRUE(kSPEC.process(obj).has_value());

    auto num = boost::json::parse(R"JSON({ "entry": 42 })JSON");
    EXPECT_FALSE(kSPEC.process(num).has_value());
}

TEST(RpcSpecDSL_MultiType, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{
        field("v", type<int64_t, std::string>),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// HexStringValidator — uint256, uint192, uint160
// ============================================================================

TEST(RpcSpecDSL_HexString, Uint256AcceptsValidHex)
{
    static constexpr auto kSPEC = RpcSpec{
        field("hash", uint256Hex),
    };
    auto good = boost::json::parse(
        R"JSON({ "hash": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" })JSON"
    );
    EXPECT_TRUE(kSPEC.process(good).has_value());
}

TEST(RpcSpecDSL_HexString, Uint256RejectsMalformedHex)
{
    static constexpr auto kSPEC = RpcSpec{
        field("hash", uint256Hex),
    };
    auto bad = boost::json::parse(R"JSON({ "hash": "NOTAHEX" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "hashMalformed");
}

TEST(RpcSpecDSL_HexString, Uint256RejectsNonString)
{
    static constexpr auto kSPEC = RpcSpec{
        field("hash", uint256Hex),
    };
    auto bad = boost::json::parse(R"JSON({ "hash": 42 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "hashNotString");
}

TEST(RpcSpecDSL_HexString, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{
        field("hash", uint256Hex),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// LedgerIndexValidator
// ============================================================================

TEST(RpcSpecDSL_LedgerIndex, AcceptsPositiveInt)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": 42 })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_LedgerIndex, AcceptsZero)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": 0 })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_LedgerIndex, AcceptsValidatedString)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": "validated" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_LedgerIndex, AcceptsNumericString)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": "12345" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_LedgerIndex, RejectsArbitraryString)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": "closed" })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "ledgerIndexMalformed");
}

TEST(RpcSpecDSL_LedgerIndex, RejectsBool)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto req = boost::json::parse(R"JSON({ "ledger_index": true })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "ledgerIndexMalformed");
}

TEST(RpcSpecDSL_LedgerIndex, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{field("ledger_index", ledgerIndex)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// AccountBase58Validator
// ============================================================================

TEST(RpcSpecDSL_AccountBase58, AcceptsValidBase58Account)
{
    static constexpr auto kSPEC = RpcSpec{field("account", accountBase58)};
    auto req = boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_AccountBase58, RejectsNonString)
{
    static constexpr auto kSPEC = RpcSpec{field("account", accountBase58)};
    auto req = boost::json::parse(R"JSON({ "account": 42 })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "accountNotString");
}

TEST(RpcSpecDSL_AccountBase58, RejectsInvalidAccount)
{
    static constexpr auto kSPEC = RpcSpec{field("account", accountBase58)};
    auto req = boost::json::parse(R"JSON({ "account": "rNotValid" })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::ClioError::RpcMalformedAddress);
}

TEST(RpcSpecDSL_AccountBase58, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{field("account", accountBase58)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// CurrencyValidator
// ============================================================================

TEST(RpcSpecDSL_Currency, AcceptsXRP)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto req = boost::json::parse(R"JSON({ "currency": "XRP" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_Currency, AcceptsThreeCharCode)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto req = boost::json::parse(R"JSON({ "currency": "USD" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_Currency, RejectsNonString)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto req = boost::json::parse(R"JSON({ "currency": 42 })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "currencyNotString");
}

TEST(RpcSpecDSL_Currency, RejectsEmpty)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto req = boost::json::parse(R"JSON({ "currency": "" })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "currencyIsEmpty");
}

TEST(RpcSpecDSL_Currency, RejectsMalformed)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto req =
        boost::json::parse(R"JSON({ "currency": "NOT_VALID_CURRENCY_STRING_TOO_LONG" })JSON");
    auto const r = kSPEC.process(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::ClioError::RpcMalformedCurrency);
}

TEST(RpcSpecDSL_Currency, AbsentFieldSkipped)
{
    static constexpr auto kSPEC = RpcSpec{field("currency", currency)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// Integration: Section + new validators — mimics ripple_state / AMM patterns
// ============================================================================

TEST(RpcSpecDSL_Integration, RippleStatePattern)
{
    static constexpr auto kSPEC = RpcSpec{
        field(
            "ripple_state",
            type<JsonObject>,
            section(
                field("currency", required, currency), field("account", required, accountBase58)
            )
        ),
    };

    auto good = boost::json::parse(
        R"JSON({ "ripple_state": { "currency": "USD", "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" } })JSON"
    );
    EXPECT_TRUE(kSPEC.process(good).has_value());

    auto missingCurrency = boost::json::parse(
        R"JSON({ "ripple_state": { "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" } })JSON"
    );
    auto const r = kSPEC.process(missingCurrency);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "Required field 'currency' missing");
}

TEST(RpcSpecDSL_Integration, StringOrObjectPattern)
{
    // Mimics fields like "offer": string hex OR object {account, seq}
    static constexpr auto kSPEC = RpcSpec{
        field(
            "offer",
            type<std::string, JsonObject>,
            ifType<std::string>(uint256Hex),
            ifObject(section(
                field("account", required, accountBase58), field("seq", required, type<uint32_t>)
            ))
        ),
    };

    auto hex = boost::json::parse(
        R"JSON({ "offer": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" })JSON"
    );
    EXPECT_TRUE(kSPEC.process(hex).has_value());

    auto obj = boost::json::parse(
        R"JSON({ "offer": { "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn", "seq": 1 } })JSON"
    );
    EXPECT_TRUE(kSPEC.process(obj).has_value());

    auto badType = boost::json::parse(R"JSON({ "offer": 42 })JSON");
    EXPECT_FALSE(kSPEC.process(badType).has_value());
}

// ============================================================================
// NotSupported — rejects any present value with rpcNOT_SUPPORTED.
// ============================================================================

TEST(RpcSpecDSL_NotSupported, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("full", notSupported),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

TEST(RpcSpecDSL_NotSupported, PresentFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("full", notSupported),
    };
    auto present = boost::json::parse(R"JSON({ "full": true })JSON");
    auto const r = kSPEC.process(present);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcNOT_SUPPORTED);
}

// ============================================================================
// OneOfValidator — string field must equal one of a fixed set.
// ============================================================================

TEST(RpcSpecDSL_OneOf, AcceptsValidValue)
{
    static constexpr auto kSPEC = RpcSpec{
        field("role", oneOf<std::string>("gateway", "user")),
    };
    auto valid = boost::json::parse(R"JSON({ "role": "gateway" })JSON");
    EXPECT_TRUE(kSPEC.process(valid).has_value());

    auto valid2 = boost::json::parse(R"JSON({ "role": "user" })JSON");
    EXPECT_TRUE(kSPEC.process(valid2).has_value());
}

TEST(RpcSpecDSL_OneOf, RejectsUnknownValue)
{
    static constexpr auto kSPEC = RpcSpec{
        field("role", oneOf<std::string>("gateway", "user")),
    };
    auto bad = boost::json::parse(R"JSON({ "role": "admin" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_OneOf, RejectsNonString)
{
    static constexpr auto kSPEC = RpcSpec{
        field("role", oneOf<std::string>("gateway", "user")),
    };
    auto bad = boost::json::parse(R"JSON({ "role": 42 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_OneOf, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("role", oneOf<std::string>("gateway", "user")),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// ToLowerModifier — converts string field to lowercase in-place.
// ============================================================================

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

// ============================================================================
// Between — inclusive range check for numeric fields.
// ============================================================================

TEST(RpcSpecDSL_Between, Uint32InRangePasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("trim", type<uint32_t>, between(uint32_t{1}, uint32_t{25})),
    };
    auto request = boost::json::parse(R"JSON({ "trim": 10 })JSON");
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_Between, Uint32AtBoundariesPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("trim", type<uint32_t>, between(uint32_t{1}, uint32_t{25})),
    };
    auto lo = boost::json::parse(R"JSON({ "trim": 1 })JSON");
    EXPECT_TRUE(kSPEC.process(lo).has_value());

    auto hi = boost::json::parse(R"JSON({ "trim": 25 })JSON");
    EXPECT_TRUE(kSPEC.process(hi).has_value());
}

TEST(RpcSpecDSL_Between, Uint32BelowLoFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("trim", type<uint32_t>, between(uint32_t{1}, uint32_t{25})),
    };
    auto bad = boost::json::parse(R"JSON({ "trim": 0 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Between, Uint32AboveHiFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("trim", type<uint32_t>, between(uint32_t{1}, uint32_t{25})),
    };
    auto bad = boost::json::parse(R"JSON({ "trim": 26 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Between, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("trim", between(uint32_t{1}, uint32_t{25})),
    };
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// Hex256ArrayValidator — each element of an array must be a valid uint256 hex.
// ============================================================================

TEST(RpcSpecDSL_Hex256Array, ValidArrayPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("credentials", hex256Array),
    };
    auto request = boost::json::parse(
        R"JSON({ "credentials": [
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
            "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        ] })JSON"
    );
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_Hex256Array, EmptyArrayPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("credentials", hex256Array)};
    auto empty = boost::json::parse(R"JSON({ "credentials": [] })JSON");
    EXPECT_TRUE(kSPEC.process(empty).has_value());
}

TEST(RpcSpecDSL_Hex256Array, InvalidElementFails)
{
    static constexpr auto kSPEC = RpcSpec{field("credentials", hex256Array)};
    auto bad = boost::json::parse(R"JSON({ "credentials": ["NOTAHEX"] })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Hex256Array, NotAnArrayFails)
{
    static constexpr auto kSPEC = RpcSpec{field("credentials", hex256Array)};
    auto bad = boost::json::parse(R"JSON({ "credentials": "abc" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_Hex256Array, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("credentials", hex256Array)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// AccountMarkerValidator — validates "<hex256>,<uint64>" cursor format.
// ============================================================================

TEST(RpcSpecDSL_AccountMarker, ValidMarkerPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto request = boost::json::parse(
        R"JSON({ "marker": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA,0" })JSON"
    );
    EXPECT_TRUE(kSPEC.process(request).has_value());
}

TEST(RpcSpecDSL_AccountMarker, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

TEST(RpcSpecDSL_AccountMarker, NotStringFails)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto bad = boost::json::parse(R"JSON({ "marker": 42 })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_EQ(r.error().message, "Malformed cursor.");
}

TEST(RpcSpecDSL_AccountMarker, NoCommaFails)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto bad = boost::json::parse(R"JSON({ "marker": "AABB" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "Malformed cursor.");
}

TEST(RpcSpecDSL_AccountMarker, BadHexPartFails)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto bad = boost::json::parse(R"JSON({ "marker": "NOTVALIDHEX,0" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "Malformed cursor.");
}

TEST(RpcSpecDSL_AccountMarker, BadHintPartFails)
{
    static constexpr auto kSPEC = RpcSpec{field("marker", accountMarker)};
    auto bad = boost::json::parse(
        R"JSON({ "marker": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA,notanumber" })JSON"
    );
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "Malformed cursor.");
}

// ============================================================================
// AccountTypeValidator — string must be a valid account-owned ledger entry type.
// ============================================================================

TEST(RpcSpecDSL_AccountType, ValidTypeStringPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("type", accountType)};
    auto req = boost::json::parse(R"JSON({ "type": "offer" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_AccountType, UnknownTypeStringFails)
{
    static constexpr auto kSPEC = RpcSpec{field("type", accountType)};
    auto bad = boost::json::parse(R"JSON({ "type": "not_a_type" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_AccountType, NonStringFails)
{
    static constexpr auto kSPEC = RpcSpec{field("type", accountType)};
    auto bad = boost::json::parse(R"JSON({ "type": 42 })JSON");
    EXPECT_FALSE(kSPEC.process(bad).has_value());
}

TEST(RpcSpecDSL_AccountType, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("type", accountType)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// LedgerEntryTypeValidator — string must be any valid ledger entry type.
// ============================================================================

TEST(RpcSpecDSL_LedgerEntryType, ValidTypeStringPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("type", ledgerType)};
    auto req = boost::json::parse(R"JSON({ "type": "state" })JSON");
    EXPECT_TRUE(kSPEC.process(req).has_value());
}

TEST(RpcSpecDSL_LedgerEntryType, UnknownTypeStringFails)
{
    static constexpr auto kSPEC = RpcSpec{field("type", ledgerType)};
    auto bad = boost::json::parse(R"JSON({ "type": "not_a_type" })JSON");
    auto const r = kSPEC.process(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), rpc::RippledError::rpcINVALID_PARAMS);
}

TEST(RpcSpecDSL_LedgerEntryType, AbsentFieldPasses)
{
    static constexpr auto kSPEC = RpcSpec{field("type", ledgerType)};
    auto absent = boost::json::parse(R"JSON({})JSON");
    EXPECT_TRUE(kSPEC.process(absent).has_value());
}

// ============================================================================
// CustomModifier — wraps a captureless lambda as a modifier.
// ============================================================================

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
