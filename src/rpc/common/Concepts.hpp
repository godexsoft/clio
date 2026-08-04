#pragma once

#include "rpc/common/Types.hpp"

#include <boost/json/value_from.hpp>

#include <concepts>

namespace rpc {

/**
 * @brief A process function that expects both some Input and a Context.
 */
template <typename T>
concept SomeContextProcessWithInput =
    requires(T a, T::Input const& in, T::Output out, Context const& ctx) {
        { a.process(in, ctx) } -> std::same_as<HandlerReturnType<decltype(out)>>;
    };

/**
 * @brief A process function that expects no Input but does take a Context.
 */
template <typename T>
concept SomeContextProcessWithoutInput = requires(T a, T::Output out, Context const& ctx) {
    { a.process(ctx) } -> std::same_as<HandlerReturnType<decltype(out)>>;
};

/**
 * @brief A handler backed by a consteval spec, resolved from its @c Input type.
 *
 * A handler that exposes an @c Input has a versioned spec associated with that Input in the spec
 * library (via `specFor(Input const*)`). The framework uses it for validation, warning collection
 * and schema dumping through the spec associated with the Input (see @ref rpc::spec::HandlerFor) — the handler
 * declares nothing spec-related beyond the @c Input typedef.
 */
template <typename T>
concept SomeHandlerWithSpec = requires { typename T::Input; };

/**
 * @brief A handler that validates and parses its request into a strong-typed @c Input in one pass.
 *
 * The only per-handler requirement is an @c Input typedef plus @c process(Input, Context). The
 * framework resolves the request-to-Input parsing (and warnings/dump) generically from the spec
 * associated with @c Input (see @ref rpc::spec::HandlerFor and @ref rpc::impl::DefaultProcessor).
 */
template <typename T>
concept SomeHandlerWithTypedInput = SomeContextProcessWithInput<T>;

/**
 * @brief A handler that takes no input (only the @ref Context).
 */
template <typename T>
concept SomeHandlerWithoutInput = SomeContextProcessWithoutInput<T>;

/**
 * @brief Specifies what a Handler type must provide.
 *
 * A handler either consumes a typed @c Input (@ref SomeHandlerWithTypedInput) or takes only a
 * @ref Context (@ref SomeHandlerWithoutInput), and must expose an @c Output serialisable via
 * @c value_from.
 */
template <typename T>
concept SomeHandler = (SomeHandlerWithTypedInput<T> or SomeHandlerWithoutInput<T>) and
    boost::json::has_value_from<typename T::Output>::value;

}  // namespace rpc
