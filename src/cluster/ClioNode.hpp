#pragma once

#include "etl/WriterState.hpp"

#include <boost/json/conversion.hpp>
#include <boost/json/value.hpp>
#include <boost/uuid/uuid.hpp>

#include <chrono>
#include <memory>

namespace cluster {

/**
 * @brief Represents a node in the cluster.
 */
struct ClioNode {
    /**
     * @brief The format of the time to store in the database.
     */
    static constexpr char const* kTIME_FORMAT = "%Y-%m-%dT%H:%M:%SZ";

    /**
     * @brief Database role of a node in the cluster.
     *
     * Roles are used to coordinate which node writes to the database:
     * - ReadOnly: Node is configured to never write (strict read-only mode)
     * - NotWriter: Node can write but is currently not the designated writer
     * - Writer: Node is actively writing to the database
     * - Fallback: Node is using the fallback writer decision mechanism
     *
     * When any node in the cluster is in Fallback mode, the entire cluster switches
     * from the cluster communication mechanism to the slower but more reliable
     * database-based conflict detection mechanism.
     */
    enum class DbRole {
        ReadOnly = 0,
        LoadingCache = 1,
        NotWriter = 2,
        Writer = 3,
        Fallback = 4,
        MAX = 4
    };

    using Uuid = std::shared_ptr<boost::uuids::uuid>;
    using CUuid = std::shared_ptr<boost::uuids::uuid const>;

    Uuid uuid;  ///< The UUID of the node.
    std::chrono::system_clock::time_point
        updateTime;  ///< The time the data about the node was last updated.
    DbRole dbRole;   ///< The database role of the node

    /**
     * @brief Create a ClioNode from writer state.
     *
     * @param uuid The UUID of the node
     * @param writerState The writer state to determine the node's database role
     * @return A ClioNode with the current time and role derived from writerState
     */
    static ClioNode
    from(Uuid uuid, etl::WriterStateInterface const& writerState);
};

void
tag_invoke(boost::json::value_from_tag, boost::json::value& jv, ClioNode const& node);

ClioNode
tag_invoke(boost::json::value_to_tag<ClioNode>, boost::json::value const& jv);

}  // namespace cluster
