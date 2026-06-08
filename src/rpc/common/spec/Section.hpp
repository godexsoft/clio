/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <string_view>
#include <tuple>

namespace rpc::spec {

/**
 * @brief Validates a sub-object's fields using a list of FieldSpecs.
 *
 * Satisfies SomeModifier so it participates in the process() path and can run
 * both requirements and modifiers on nested fields. When the field is absent the
 * Section is a no-op; when the field is present but not an object it returns
 * rpcINVALID_PARAMS. Nested FieldSpecs are applied via processNested() which
 * navigates into the object using FA::child().
 *
 * Example:
 *   field("taker_pays", section(
 *       field("currency", required, type<std::string>),
 *       field("issuer",   account)
 *   ))
 */
template <typename... SubFields>
struct Section {
    static constexpr std::string_view kNAME = "section";

    std::tuple<SubFields...> subFields;

    consteval explicit Section(SubFields... sf) : subFields{sf...}
    {
    }

    template <SomeFieldView FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
    {
        if (!fa.present())
            return {};
        if (!fa.isObject())
            return std::unexpected{rpc::Status{rpc::RippledError::rpcINVALID_PARAMS}};

        MaybeError result{};
        std::apply(
            [&](auto const&... subSpec) {
                (void)((result = subSpec.processNested(fa), result.has_value()) && ...);
            },
            subFields
        );
        return result;
    }
};

template <typename... Fs>
Section(Fs...) -> Section<Fs...>;

}  // namespace rpc::spec
