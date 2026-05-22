/** @file */
#pragma once

#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/FieldView.hpp"
#include "rpc/common/spec/Types.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>

namespace rpc::spec {

namespace impl {

template <std::size_t N>
struct OverridePlan {
    std::array<bool, N> shouldRun;
    std::array<std::size_t, N> effectiveIdx;
};

// For each position i, decides whether to run a FieldSpec there (only when it's the
// first occurrence of its key) and which FieldSpec to use (the last one declared with
// that key — last-override-wins). Position is preserved at the first occurrence so
// error ordering across versions is stable.
template <std::size_t N>
constexpr OverridePlan<N>
buildOverridePlan(std::array<std::string_view, N> const& keys)
{
    OverridePlan<N> plan{};
    for (std::size_t i = 0; i < N; ++i) {
        bool isFirst = true;
        for (std::size_t j = 0; j < i; ++j) {
            if (keys[j] == keys[i]) {
                isFirst = false;
                break;
            }
        }
        plan.shouldRun[i] = isFirst;
        if (isFirst) {
            std::size_t last = i;
            for (std::size_t j = i + 1; j < N; ++j) {
                if (keys[j] == keys[i])
                    last = j;
            }
            plan.effectiveIdx[i] = last;
        }
    }
    return plan;
}

template <typename FieldsTuple, SomeObjectView Root, std::size_t... Is>
[[nodiscard]] MaybeError
process(FieldsTuple const& fields, Root& root, std::index_sequence<Is...>)
{
    if constexpr (sizeof...(Is) == 0) {
        return {};
    } else {
        constexpr auto kN = sizeof...(Is);
        std::array<std::string_view, kN> const keys{std::get<Is>(fields).key...};
        auto const plan = buildOverridePlan(keys);

        using DispatchFn = MaybeError (*)(FieldsTuple const&, Root&);
        static constexpr std::array<DispatchFn, kN> kDISPATCH{
            +[](FieldsTuple const& t, Root& r) -> MaybeError { return std::get<Is>(t).process(r); }...
        };

        MaybeError result{};
        for (std::size_t i = 0; i < kN; ++i) {
            if (!plan.shouldRun[i])
                continue;
            result = kDISPATCH[plan.effectiveIdx[i]](fields, root);
            if (!result.has_value())
                return result;
        }
        return result;
    }
}

template <typename FieldsTuple, SomeObjectView Root, std::size_t... Is>
[[nodiscard]] Warnings
check(FieldsTuple const& fields, Root const& root, std::index_sequence<Is...>)
{
    Warnings out;
    if constexpr (sizeof...(Is) > 0) {
        constexpr auto kN = sizeof...(Is);
        std::array<std::string_view, kN> const keys{std::get<Is>(fields).key...};
        auto const plan = buildOverridePlan(keys);

        using DispatchFn = Warnings (*)(FieldsTuple const&, Root const&);
        static constexpr std::array<DispatchFn, kN> kDISPATCH{
            +[](FieldsTuple const& t, Root const& r) -> Warnings { return std::get<Is>(t).check(r); }...
        };

        for (std::size_t i = 0; i < kN; ++i) {
            if (!plan.shouldRun[i])
                continue;
            auto w = kDISPATCH[plan.effectiveIdx[i]](fields, root);
            out.insert(out.end(), w.begin(), w.end());
        }
    }
    return out;
}

}  // namespace impl

template <typename... Fields>
struct RpcSpec {
    using FieldsTuple = std::tuple<Fields...>;
    FieldsTuple fields;

    consteval RpcSpec(Fields... f) : fields{f...}
    {
    }

    template <SomeObjectView Root>
    [[nodiscard]] MaybeError
    process(Root& root) const
    {
        return impl::process(fields, root, std::index_sequence_for<Fields...>{});
    }

    template <SomeObjectView Root>
    [[nodiscard]] Warnings
    check(Root const& root) const
    {
        return impl::check(fields, root, std::index_sequence_for<Fields...>{});
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

// extend() concatenates fields onto an existing spec. Duplicate keys are allowed —
// at process/check/dump time the last FieldSpec with a given key wins (its items
// replace any earlier definitions of that key) while keeping the original position
// of the first occurrence for stable ordering of errors and warnings.
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
