//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

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

#include "util/config/Config.hpp"

#include <cstddef>

namespace etl {

/**
 * @brief Settings for the cache loader
 */
struct CacheLoaderSettings {
    enum class LoadStyle { ASYNC, SYNC, NOT_AT_ALL };

    size_t numCacheDiffs = 32;       /**< number of diffs to use to generate cursors */
    size_t numCacheMarkers = 48;     /**< number of markers to use at one time to traverse the ledger */
    size_t cachePageFetchSize = 512; /**< number of ledger objects to fetch concurrently per marker */
    size_t numThreads = 2;           /**< number of threads to use for loading cache */

    LoadStyle loadStyle = LoadStyle::ASYNC; /**< how to load the cache */

    int
    operator<=>(CacheLoaderSettings const&) const = default;

    /** @returns True if the load style is SYNC; false otherwise */
    [[nodiscard]] bool
    isSync() const;

    /** @returns True if the load style is ASYNC; false otherwise */
    [[nodiscard]] bool
    isAsync() const;

    /** @returns True if the cache is disabled; false otherwise */
    [[nodiscard]] bool
    isDisabled() const;
};

[[nodiscard]] CacheLoaderSettings
make_CacheLoaderSettings(util::Config const& config);

}  // namespace etl
