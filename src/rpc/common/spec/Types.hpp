/** @file */
#pragma once

#include "rpc/Errors.hpp"

#include <expected>
#include <string>
#include <vector>

namespace rpc::spec {

/**
 * @brief Result type returned by validators and modifiers.
 */
using MaybeError = std::expected<void, rpc::Status>;

/**
 * @brief A single non-blocking warning emitted by a checker (e.g. field deprecation).
 */
struct Warning {
    std::string field;
    std::string message;
};

using Warnings = std::vector<Warning>;

}  // namespace rpc::spec
