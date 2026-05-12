/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/FieldView.hpp"
#include "rpc/common/spec/Types.hpp"

#include <concepts>
#include <tuple>

namespace rpc::spec {

template <typename... Fields>
struct RpcSpec {
    std::tuple<Fields...> fields;

    consteval RpcSpec(Fields... f) : fields{f...}
    {
    }

    template <SomeObjectView Root>
    [[nodiscard]] MaybeError
    process(Root& root) const
    {
        MaybeError result{};
        std::apply(
            [&](auto const&... f) {
                (void)((result = f.process(root), result.has_value()) && ...);
            },
            fields
        );
        return result;
    }

    template <SomeObjectView Root>
    [[nodiscard]] Warnings
    check(Root const& root) const
    {
        Warnings out;
        std::apply(
            [&](auto const&... f) {
                auto collect = [&](auto const& fieldSpec) {
                    auto fw = fieldSpec.check(root);
                    out.insert(out.end(), fw.begin(), fw.end());
                };
                (collect(f), ...);
            },
            fields
        );
        return out;
    }

    // Convenience overloads accepting any value the configured backend's ObjectView
    // alias can wrap. The primary SomeObjectView overloads above are the contract;
    // these forwarders just save callers from constructing the wrapper by hand.
    template <typename V>
        requires(!SomeObjectView<V>) && std::constructible_from<ObjectView, V&>
    [[nodiscard]] MaybeError
    process(V& v) const
    {
        ObjectView root{v};
        return process(root);
    }

    template <typename V>
        requires(!SomeObjectView<V>) && std::constructible_from<ObjectView, V const&>
    [[nodiscard]] Warnings
    check(V const& v) const
    {
        ObjectView const root{v};
        return check(root);
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
