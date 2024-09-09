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

#include "util/async/AnyStrand.hpp"

#include <boost/asio/io_context.hpp>

#include <optional>
#include <queue>

namespace util {

/**
 * @brief A wrapper for std::priority_queue that serialises operations using a strand
 */
template <typename T>
class StrandedPriorityQueue {
    util::async::AnyStrand& strand_;
    std::priority_queue<T> queue_;

public:
    StrandedPriorityQueue(util::async::AnyStrand& strand) : strand_(strand)
    {
    }

    void
    add(T&& element)
    {
        strand_.execute([element = std::forward<T>(element), this] { queue_.push(std::move(element)); }).wait();
    }

    std::optional<T>
    next()
    {
        return strand_
            .execute([this] -> std::optional<T> {
                if (queue_.empty())
                    return std::nullopt;

                auto top = queue_.top();
                queue_.pop();

                return top;
            })
            .get()
            .value_or(std::nullopt);
    }
};

}  // namespace util
