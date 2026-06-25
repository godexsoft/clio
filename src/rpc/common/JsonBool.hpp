#pragma once

#include <rpcspec/JsonBool.hpp>

namespace rpc {

/**
 * @brief A wrapper around bool that allows conversion from any JSON value.
 *
 * Moved to the shared rpcspec framework as @ref rpc::spec::JsonBool (the
 * lenient V1-API bool coercion is identical for Clio and rippled). Kept as an
 * alias here so existing `rpc::JsonBool` usages — and ADL-based `value_to`,
 * which resolves the `tag_invoke` from `rpc::spec` — continue to work unchanged.
 */
using JsonBool = spec::JsonBool;

}  // namespace rpc
