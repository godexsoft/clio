#pragma once

#include "rpc/common/Types.hpp"
#include <rpcspec/RpcSpecView.hpp>

#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <concepts>
#include <cstdint>

namespace rpc {

/**
 * @brief A handler that exposes a consteval @ref rpc::spec::RpcSpecView via @c spec(version).
 *
 * Independent of whether the handler takes an @c Input. Handlers satisfying this
 * concept are validated through the consteval-spec path in
 * @ref rpc::impl::DefaultProcessor.
 */
template <typename T>
concept SomeHandlerWithSpec = requires(T a, uint32_t version) {
    { a.spec(version) } -> std::same_as<rpc::spec::RpcSpecView>;
};

/**
 * @brief A process function that expects both some Input and a Context.
 */
template <typename T>
concept SomeContextProcessWithInput =
    requires(T a, typename T::Input const& in, typename T::Output out, Context const& ctx) {
        { a.process(in, ctx) } -> std::same_as<HandlerReturnType<decltype(out)>>;
    };

/**
 * @brief A process function that expects no Input but does take a Context.
 */
template <typename T>
concept SomeContextProcessWithoutInput = requires(T a, typename T::Output out, Context const& ctx) {
    { a.process(ctx) } -> std::same_as<HandlerReturnType<decltype(out)>>;
};

/**
 * @brief A handler that consumes a typed @c Input deserialized from the request JSON.
 */
template <typename T>
concept SomeHandlerWithInput =
    SomeContextProcessWithInput<T> and boost::json::has_value_to<typename T::Input>::value;

/**
 * @brief A handler that takes no input (only the @ref Context).
 */
template <typename T>
concept SomeHandlerWithoutInput = SomeContextProcessWithoutInput<T>;

/**
 * @brief Specifies what a Handler type must provide.
 *
 * Handlers are decomposed into two orthogonal axes:
 *   - input shape: @ref SomeHandlerWithInput vs @ref SomeHandlerWithoutInput
 *   - spec presence: @ref SomeHandlerWithSpec or neither
 *
 * Every combination is valid (e.g. no-input + new-spec, input + no-spec).
 */
template <typename T>
concept SomeHandler = (SomeHandlerWithInput<T> or SomeHandlerWithoutInput<T>) and
    boost::json::has_value_from<typename T::Output>::value;

}  // namespace rpc
