/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Types.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/string.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rpc::spec {

/**
 * @brief Convert a flat list of spec warnings into the wire-format JSON array.
 *
 * Warnings are grouped by their @ref rpc::WarningCode. For each code, the
 * standard JSON object produced by @ref rpc::makeWarning is used as the base,
 * and each warning's extra message is appended to the `"message"` field with a
 * leading space — mirroring the behaviour of the old @c RpcSpec::check
 * aggregator in @c Specs.cpp.
 *
 * Using @c std::map gives deterministic insertion-order iteration (sorted by
 * code value), which makes the output reproducible in tests without imposing
 * any overhead in production paths.
 *
 * @param warnings The flat vector of @ref Warning objects to convert.
 * @return A @c boost::json::array of grouped warning objects.
 */
[[nodiscard]] inline boost::json::array
toJsonArray(Warnings const& warnings)
{
    std::map<rpc::WarningCode, std::vector<std::string>> grouped;
    for (auto const& w : warnings)
        grouped[w.code].push_back(w.message);

    boost::json::array out;
    for (auto const& [code, messages] : grouped) {
        auto obj = rpc::makeWarning(code);
        auto& msg = obj["message"].as_string();
        for (auto const& extra : messages) {
            msg.append(" ").append(extra);
        }
        out.push_back(std::move(obj));
    }
    return out;
}

}  // namespace rpc::spec
