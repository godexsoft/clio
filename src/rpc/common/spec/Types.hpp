/** @file */
#pragma once

#include <expected>
#include <string>
#include <vector>

namespace rpc::spec {

/**
 * @brief Error type for the new spec system.
 *
 * @note Intentionally a plain string for Phase 1 independence.
 * Will be replaced with rpc::Status during integration (Phase 2).
 */
using Error = std::string;

/**
 * @brief Result type returned by validators and modifiers.
 *
 * @note Will align with rpc::MaybeError during Phase 2 integration.
 */
using MaybeError = std::expected<void, Error>;

/**
 * @brief A single non-blocking warning emitted by a checker (e.g. field deprecation).
 */
struct Warning {
    std::string field;
    std::string message;
};

using Warnings = std::vector<Warning>;

}  // namespace rpc::spec
