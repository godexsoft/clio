#include "rpc/Errors.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/Types.hpp"
#include "rpc/common/spec/Validators.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace rpc::spec;

// Mock backend — proves the spec abstraction works with a non-boost::json type.
//
// MockObject is a plain std::map<string, variant>. MockFieldAccess satisfies
// SomeFieldAccess; MockRootAccess satisfies SomeRootAccess. The spec system itself
// is templated on those concepts, so swapping the backend changes nothing in the
// validators, modifiers, or RpcSpec/FieldSpec orchestration.

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

class MockRootAccess {
    MockObject const* readObj_;
    MockObject* writeObj_;

public:
    explicit MockRootAccess(MockObject& obj) noexcept : readObj_{&obj}, writeObj_{&obj}
    {
    }
    explicit MockRootAccess(MockObject const& obj) noexcept : readObj_{&obj}, writeObj_{nullptr}
    {
    }

    [[nodiscard]] static bool
    isObject() noexcept
    {
        return true;
    }
    [[nodiscard]] static bool
    isArray() noexcept
    {
        return false;
    }

    [[nodiscard]] MockFieldAccess
    child(std::string_view key) noexcept
    {
        if (writeObj_ != nullptr) {
            auto it = writeObj_->fields.find(std::string{key});
            return it != writeObj_->fields.end()
                ? MockFieldAccess{&it->second, key}
                : MockFieldAccess{static_cast<MockValue*>(nullptr), key};
        }
        auto it = readObj_->fields.find(std::string{key});
        return it != readObj_->fields.end()
            ? MockFieldAccess{&it->second, key}
            : MockFieldAccess{static_cast<MockValue const*>(nullptr), key};
    }

    [[nodiscard]] MockFieldAccess
    child(std::string_view key) const noexcept
    {
        auto it = readObj_->fields.find(std::string{key});
        return it != readObj_->fields.end()
            ? MockFieldAccess{&it->second, key}
            : MockFieldAccess{static_cast<MockValue const*>(nullptr), key};
    }
};

static_assert(SomeRootAccess<MockRootAccess>);

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
    MockRootAccess root{obj};
    EXPECT_TRUE(kSPEC.process(root).has_value());
}

TEST(RpcSpecDSL_MockBackend, MissingRequiredFieldFails)
{
    static constexpr auto kSPEC = RpcSpec{
        field("account", required),
    };

    MockObject obj{};
    MockRootAccess root{obj};
    auto const result = kSPEC.process(root);
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
    MockRootAccess root{obj};
    auto const result = kSPEC.process(root);
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
    MockRootAccess lowRoot{tooLow};
    ASSERT_TRUE(kSPEC.process(lowRoot).has_value());
    EXPECT_EQ(std::get<int64_t>(tooLow.fields.at("limit")), 10);

    MockObject tooHigh{.fields = {{"limit", int64_t{9999}}}};
    MockRootAccess highRoot{tooHigh};
    ASSERT_TRUE(kSPEC.process(highRoot).has_value());
    EXPECT_EQ(std::get<int64_t>(tooHigh.fields.at("limit")), 400);
}

TEST(RpcSpecDSL_MockBackend, IfTypeSkipsOnMismatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    // string value — int64 branch must not fire
    MockObject obj{.fields = {{"value", std::string{"hello"}}}};
    MockRootAccess root{obj};
    EXPECT_TRUE(kSPEC.process(root).has_value());
}

TEST(RpcSpecDSL_MockBackend, IfTypeRunsOnMatch)
{
    static constexpr auto kSPEC = RpcSpec{
        field("value", ifType<int64_t>(min(int64_t{1}))),
    };

    MockObject bad{.fields = {{"value", int64_t{0}}}};
    MockRootAccess badRoot{bad};
    auto const result = kSPEC.process(badRoot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), rpc::RippledError::rpcINVALID_PARAMS);
    EXPECT_TRUE(result.error().message.empty());

    MockObject good{.fields = {{"value", int64_t{5}}}};
    MockRootAccess goodRoot{good};
    EXPECT_TRUE(kSPEC.process(goodRoot).has_value());
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
    MockRootAccess const root{obj};
    auto const warnings = kSPEC.check(root);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].field, "ident");
    EXPECT_EQ(warnings[0].code, rpc::WarningCode::WarnRpcDeprecated);
}
