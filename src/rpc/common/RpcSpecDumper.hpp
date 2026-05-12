/** @file */
#pragma once

#include <cstdint>
#include <ostream>

namespace rpc {

/**
 * @brief Dump the input specs of every registered RPC handler to @p os.
 *
 * Iterates @ref rpc::impl::handlerRegistry directly, reading each handler's
 * static `spec(apiVersion)` function pointer. No runtime dependencies (backend,
 * ETL, counters, etc.) are constructed — only the static metadata in the
 * registry is touched.
 *
 * Handlers with no input (ping, random, ledger_range, version) render as
 * `(no inputs)`.
 *
 * @param os         Output stream (e.g. `std::cout`).
 * @param apiVersion API version to dump (typically 1 or 2).
 */
void
dumpAllRpcSpecs(std::ostream& os, uint32_t apiVersion);

}  // namespace rpc
