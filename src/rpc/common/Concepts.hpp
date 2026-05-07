#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/Checkers.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"

#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace rpc {

struct RpcSpec;

/**
 * @brief Specifies what a requirement used with @ref rpc::FieldSpec must provide.
 */
template <typename T>
concept SomeRequirement = requires(T a, boost::json::value lval) {
    { a.verify(lval, std::string{}) } -> std::same_as<MaybeError>;
};

/**
 * @brief Specifies what a modifier used with @ref rpc::FieldSpec must provide.
 */
template <typename T>
concept SomeModifier = requires(T a, boost::json::value lval) {
    { a.modify(lval, std::string{}) } -> std::same_as<MaybeError>;
};

/**
 * @brief Specifies what a check used with @ref rpc::FieldSpec must provide.
 */
template <typename T>
concept SomeCheck = requires(T a, boost::json::value lval) {
    { a.check(lval, std::string{}) } -> std::same_as<std::optional<check::Warning>>;
};

/**
 * @brief The requirements of a processor to be used with @ref rpc::FieldSpec.
 */
template <typename T>
concept SomeProcessor = (SomeRequirement<T> or SomeModifier<T>);

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
 * @brief A handler that exposes the OLD runtime @ref rpc::RpcSpec via @c spec(version).
 *
 * Independent of whether the handler takes an @c Input — orthogonal to the
 * input axis. The dispatcher runs validation when this concept is satisfied,
 * regardless of input shape.
 */
template <typename T>
concept SomeHandlerWithOldSpec = requires(T a, uint32_t version) {
    { a.spec(version) } -> std::same_as<RpcSpec const&>;
};

/**
 * @brief A handler that exposes the NEW consteval @ref rpc::spec::RpcSpecView via @c spec(version).
 *
 * Independent of whether the handler takes an @c Input. Handlers satisfying this
 * concept are validated through the new consteval-spec path in
 * @ref rpc::impl::DefaultProcessor.
 */
template <typename T>
concept SomeHandlerWithNewSpec = requires(T a, uint32_t version) {
    { a.spec(version) } -> std::same_as<rpc::spec::RpcSpecView>;
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
 *   - spec presence: @ref SomeHandlerWithOldSpec, @ref SomeHandlerWithNewSpec, or neither
 *
 * Every combination is valid (e.g. no-input + new-spec, input + no-spec).
 */
template <typename T>
concept SomeHandler = (SomeHandlerWithInput<T> or SomeHandlerWithoutInput<T>) and
    boost::json::has_value_from<typename T::Output>::value;

}  // namespace rpc
