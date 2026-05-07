/** @file */
#pragma once

#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/value.hpp>

#include <tuple>

namespace rpc::spec {

template <typename... Fields>
struct RpcSpec {
    std::tuple<Fields...> fields;

    consteval RpcSpec(Fields... f) : fields{f...}
    {
    }
    consteval explicit RpcSpec(std::tuple<Fields...> t) : fields{t}
    {
    }

    [[nodiscard]] MaybeError
    process(boost::json::value& val) const
    {
        MaybeError result{};
        std::apply(
            [&](auto const&... f) { ((result = f.process(val), result.has_value()) && ...); },
            fields
        );
        return result;
    }

    [[nodiscard]] Warnings
    check(boost::json::value const& val) const
    {
        Warnings out;
        std::apply(
            [&](auto const&... f) {
                auto collect = [&](auto const& fieldSpec) {
                    auto fw = fieldSpec.check(val);
                    out.insert(out.end(), fw.begin(), fw.end());
                };
                (collect(f), ...);
            },
            fields
        );
        return out;
    }
};

template <typename... Fs>
RpcSpec(Fs...) -> RpcSpec<Fs...>;

template <typename... Existing, typename... Extra>
[[nodiscard]] consteval auto
extend(RpcSpec<Existing...> const& base, Extra... extra)
{
    return std::apply(
        [&](auto const&... existing) { return RpcSpec{existing..., extra...}; }, base.fields
    );
}

template <typename... Existing, typename... NewItems>
[[nodiscard]] consteval auto
operator+(RpcSpec<Existing...> const& base, FieldSpec<NewItems...> extra)
{
    return extend(base, extra);
}

}  // namespace rpc::spec
