#pragma once

#include "etlng/Models.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace etlng::impl {

struct ReverseOrderComparator {
    [[nodiscard]] bool
    operator()(model::LedgerData const& lhs, model::LedgerData const& rhs) const noexcept
    {
        return lhs.seq > rhs.seq;
    }
};

/**
 * @brief A wrapper for std::priority_queue that serialises operations using a mutex
 * @note This may be a candidate for future improvements if performance proves to be poor (e.g. use a lock free queue)
 */
class TaskQueue {
    std::size_t limit_;
    std::uint32_t expectedSequence_;
    std::uint32_t increment_;

    std::mutex mutex_;

    std::priority_queue<model::LedgerData, std::vector<model::LedgerData>, ReverseOrderComparator> forwardLoadQueue_;

public:
    struct Settings {
        std::uint32_t startSeq = 0u;   // sequence to start from (for dequeue)
        std::uint32_t increment = 1u;  // increment sequence by this value once dequeue was successful
        std::optional<std::size_t> limit = std::nullopt;
    };

    /**
     * @brief Construct a new priority queue
     * @param limit The limit of items allowed simultaneously in the queue
     */
    explicit TaskQueue(Settings settings)
        : limit_(settings.limit.value_or(0uz)), expectedSequence_(settings.startSeq), increment_(settings.increment)
    {
    }

    /**
     * @brief Enqueue a new item onto the queue if space is available
     * @note This function blocks until the item is attempted to be added to the queue
     *
     * @param item The item to add
     * @return true if item added to the queue; false otherwise
     */
    [[nodiscard]] bool
    enqueue(model::LedgerData item)
    {
        std::scoped_lock lk(mutex_);

        if (limit_ == 0uz or forwardLoadQueue_.size() < limit_) {
            forwardLoadQueue_.push(std::move(item));
            return true;
        }

        return false;
    }

    /**
     * @brief Dequeue the next available item out of the queue
     * @note This function blocks until the item is taken off the queue
     * @return An item if available; nullopt otherwise
     */
    [[nodiscard]] std::optional<model::LedgerData>
    dequeue()
    {
        std::scoped_lock lk(mutex_);
        std::optional<model::LedgerData> out;

        if (not forwardLoadQueue_.empty() && forwardLoadQueue_.top().seq == expectedSequence_) {
            out.emplace(forwardLoadQueue_.top());
            forwardLoadQueue_.pop();
            expectedSequence_ += increment_;
        }

        return out;
    }

    /**
     * @brief Check if the queue is empty
     * @note This function blocks until the queue is checked
     *
     * @return true if the queue is empty; false otherwise
     */
    [[nodiscard]] bool
    empty()
    {
        std::scoped_lock lk(mutex_);
        return forwardLoadQueue_.empty();
    }
};

}  // namespace etlng::impl
