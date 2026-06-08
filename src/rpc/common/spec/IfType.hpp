/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <string_view>
#include <tuple>

namespace rpc::spec {

/**
 * @brief Runs sub-processors only when the field's runtime type matches T.
 *
 * T may be any type accepted by FieldView::is<T>(), including the JsonObject and
 * JsonArray markers.
 *
 * Satisfies SomeModifier so it receives a mutable field view, enabling both
 * requirement and modifier sub-items. Checkers are excluded from sub-items;
 * hang them directly on the FieldSpec if conditional warning emission is needed.
 */
template <typename T, SomeProcessor... SubItems>
struct IfType {
    static constexpr std::string_view kNAME = "ifType";
    static constexpr std::string_view kBRANCH_TYPE = typeNameOf<T>();

    std::tuple<SubItems...> subItems;

    consteval explicit IfType(SubItems... s) : subItems{s...}
    {
    }

    template <typename Writer>
    void
    describeParams(Writer& w) const
    {
        w.param("type", kBRANCH_TYPE);
    }

    template <SomeFieldView FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
    {
        if (!fa.present() || !fa.template is<T>())
            return {};

        MaybeError result{};
        std::apply(
            [&](auto const&... item) {
                (void)((result = callIfProcessor(item, fa), result.has_value()) && ...);
            },
            subItems
        );
        return result;
    }
};

}  // namespace rpc::spec
