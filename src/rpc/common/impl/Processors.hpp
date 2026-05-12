#pragma once

#include "rpc/common/Concepts.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/WarningsToJson.hpp"
#include "util/UnsupportedType.hpp"

#include <boost/json/array.hpp>
#include <boost/json/value.hpp>

#include <utility>

namespace rpc::impl {

template <SomeHandler HandlerType>
struct DefaultProcessor final {
    [[nodiscard]] ReturnType
    operator()(
        HandlerType const& handler,
        boost::json::value const& value,
        Context const& ctx
    ) const
    {
        using boost::json::value_from;
        using boost::json::value_to;

        // Phase 1 — run the spec (if the handler exposes one) and collect any warnings.
        // The spec axis is independent of the input axis: a handler with no Input may still
        // declare a spec, and a handler with an Input may opt out of having one.
        boost::json::array warnings;
        auto input = value;  // mutable copy; spec.process may modify it before deserialization

        if constexpr (SomeHandlerWithSpec<HandlerType>) {
            auto const spec = handler.spec(ctx.apiVersion);
            warnings = rpc::spec::toJsonArray(spec.check(value));
            if (auto const ret = spec.process(input); not ret)
                return ReturnType{Error{ret.error()}, std::move(warnings)};
        }

        // Phase 2 — dispatch to the handler.
        if constexpr (SomeHandlerWithInput<HandlerType>) {
            auto const inData = value_to<typename HandlerType::Input>(input);
            auto ret = handler.process(inData, ctx);
            if (!ret)
                return ReturnType{Error{std::move(ret).error()}, std::move(warnings)};
            return ReturnType{value_from(std::move(ret).value()), std::move(warnings)};
        } else if constexpr (SomeHandlerWithoutInput<HandlerType>) {
            auto ret = handler.process(ctx);
            if (!ret)
                return ReturnType{Error{std::move(ret).error()}, std::move(warnings)};
            return ReturnType{value_from(std::move(ret).value()), std::move(warnings)};
        } else {
            static_assert(util::Unsupported<HandlerType>);
        }
    }
};

}  // namespace rpc::impl
