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
    rpc::WarningCode code;  // grouping key for the wire-format converter
    std::string field;      // identifier for the field that triggered the warning
    std::string message;    // extra context appended to the standard message for `code`
};

using Warnings = std::vector<Warning>;

}  // namespace rpc::spec
