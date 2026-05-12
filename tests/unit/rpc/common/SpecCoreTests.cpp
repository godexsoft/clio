#include "rpc/Errors.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/Concepts.hpp"
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

#include <cstdint>
#include <string>
#include <tuple>

using namespace rpc::spec;

// ============================================================================
// Core DSL — RpcSpec, FieldSpec, and RpcSpecView mechanics.
// ============================================================================

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
// Processing order — short-circuit on first failure, both within a field's items
// and across the field list. Ported from the parametric ordering tests in the
// pre-consteval SpecsTests (FirstError / SecondError patterns).
// ============================================================================

TEST(RpcSpecDSL_Ordering, StopsAtFirstFieldFailure)
{
    // Both fields would fail if reached; only the first error must surface.
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
        field("limit", required),
    };

    auto request = boost::json::parse(R"JSON({})JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message, "Required field 'account' missing");
}

TEST(RpcSpecDSL_Ordering, LaterFieldFailureReportedWhenEarlierPasses)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
        field("limit", required),
    };

    auto request =
        boost::json::parse(R"JSON({ "account": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn" })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message, "Required field 'limit' missing");
}

TEST(RpcSpecDSL_Ordering, StopsAtFirstItemFailureWithinAField)
{
    // Both Type<int64> and Min would fail (the value is the wrong type and below the bound),
    // but Type runs first and short-circuits — Min's error message must never surface.
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, min(int64_t{100})),
    };

    auto request = boost::json::parse(R"JSON({ "limit": "not-a-number" })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());
}

TEST(RpcSpecDSL_Ordering, LaterItemRunsWhenEarlierPasses)
{
    // Type<int64> passes (value is int), then Min sees the value and fails.
    static constexpr auto kSPEC = RpcSpec{
        field("limit", type<int64_t>, min(int64_t{100})),
    };

    auto request = boost::json::parse(R"JSON({ "limit": 5 })JSON");
    auto const result = kSPEC.process(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
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
// against a private archetype (not BoostJsonFieldView), so satisfaction
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
