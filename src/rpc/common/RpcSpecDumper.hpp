/** @file */
#pragma once

#include <cstdint>
#include <ostream>

namespace rpc {

/**
 * @brief Dump the input specs of every registered RPC handler to @p os.
 *
 * Reads only static metadata from the handler registry; no runtime
 * dependencies are constructed. Handlers without an input spec render as
 * @c (no inputs).
 *
 * @param os         Output stream.
 * @param apiVersion API version to dump.
 */
void
dumpAllRpcSpecs(std::ostream& os, uint32_t apiVersion);

}  // namespace rpc
