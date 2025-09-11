//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#pragma once

#include <boost/json.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value_to.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

/**
 * @brief This namespace contains various utilities.
 */
namespace util {

/**
 * @brief Convert a string to lowercase
 *
 * @param str The string to convert
 * @return The string in lowercase
 */
inline std::string
toLower(std::string str)
{
    std::ranges::transform(str, std::begin(str), [](unsigned char c) { return std::tolower(c); });
    return str;
}

/**
 * @brief Convert a string to uppercase
 *
 * @param str The string to convert
 * @return The string in uppercase
 */
inline std::string
toUpper(std::string str)
{
    std::ranges::transform(str, std::begin(str), [](unsigned char c) { return std::toupper(c); });
    return str;
}

/**
 * @brief Removes any detected secret information from a response JSON object.
 *
 * @param object The JSON object to remove secrets from
 * @return A secret-free copy of the passed object
 */
inline boost::json::object
removeSecret(boost::json::object const& object)
{
    auto newObject = object;
    auto const secretFields = {"secret", "seed", "seed_hex", "passphrase"};

    if (newObject.contains("params") and newObject.at("params").is_array() and
        not newObject.at("params").as_array().empty() and newObject.at("params").as_array()[0].is_object()) {
        for (auto const& secretField : secretFields) {
            if (newObject.at("params").as_array()[0].as_object().contains(secretField))
                newObject.at("params").as_array()[0].as_object()[secretField] = "*";
        }
    }

    // for websocket requests
    for (auto const& secretField : secretFields) {
        if (newObject.contains(secretField))
            newObject[secretField] = "*";
    }

    return newObject;
}

/**
 * @brief Converts a value using `value_to` and casts it back to the requested Type.
 * @note This conversion can possibly cause wrapping around or UB. Use with caution.
 *
 * @tparam Type The type to cast to
 * @param value The JSON value to cast
 * @return Value casted to the requested type
 */
template <typename Type>
Type
castValueTo(boost::json::value const& value)
{
    using boost::json::value_to;

    if constexpr (std::is_integral_v<Type>) {
        // This helps to mitigate the "not exact" exception from Boost.Json for large numbers when the number does not
        // exactly fit in `Type`. In practice we don't need huge numbers that don't fit uint32_t but users can still
        // send them through RPC and Clio needs to be able to process them without producing an internal error.
        return static_cast<Type>(value_to<int64_t>(value));
    } else {
        return value_to<Type>(value);
    }
}

}  // namespace util
