#pragma once

#include "rpc/common/Concepts.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/WarningsToJson.hpp>
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

        // Phase 1 — collect any warnings from the handler's spec (inherited from HandlerFor<Input>).
        boost::json::array warnings;
        if constexpr (SomeHandlerWithSpec<HandlerType>) {
            warnings = rpc::spec::toJsonArray(HandlerType::spec(ctx.apiVersion).check(value));
        }

        // Phase 2 — validate + parse into the strong Input, then dispatch.
        if constexpr (SomeHandlerWithTypedInput<HandlerType>) {
            auto parsed = HandlerType::parseInput(value, ctx.apiVersion);
            if (!parsed)
                return ReturnType{Error{std::move(parsed).error()}, std::move(warnings)};
            auto ret = handler.process(parsed.value(), ctx);
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
