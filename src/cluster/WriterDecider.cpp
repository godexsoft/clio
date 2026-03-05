#include "cluster/WriterDecider.hpp"

#include "cluster/Backend.hpp"
#include "cluster/ClioNode.hpp"
#include "etl/WriterState.hpp"
#include "util/Assert.hpp"
#include "util/Spawn.hpp"

#include <boost/asio/thread_pool.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace cluster {

WriterDecider::WriterDecider(
    boost::asio::thread_pool& ctx,
    std::unique_ptr<etl::WriterStateInterface> writerState
)
    : ctx_(ctx), writerState_(std::move(writerState))
{
}

void
WriterDecider::onNewState(
    ClioNode::CUuid selfId,
    std::shared_ptr<Backend::ClusterData const> clusterData
)
{
    if (not clusterData->has_value())
        return;

    util::spawn(
        ctx_,
        [writerState = writerState_->clone(),
         selfId = std::move(selfId),
         clusterData = clusterData->value()](auto&&) mutable {
            auto const selfData = std::ranges::find_if(
                clusterData, [&selfId](ClioNode const& node) { return node.uuid == selfId; }
            );
            ASSERT(selfData != clusterData.end(), "Self data should always be in the cluster data");

            if (selfData->dbRole == ClioNode::DbRole::Fallback) {
                return;
            }

            if (selfData->dbRole == ClioNode::DbRole::ReadOnly) {
                writerState->giveUpWriting();
                return;
            }

            // If any node in the cluster is in Fallback mode, the entire cluster must switch
            // to the fallback writer decision mechanism for consistency
            if (std::ranges::any_of(clusterData, [](ClioNode const& node) {
                    return node.dbRole == ClioNode::DbRole::Fallback;
                })) {
                writerState->setWriterDecidingFallback();
                return;
            }

            // We are not ReadOnly and there is no Fallback in the cluster
            std::ranges::sort(clusterData, [](ClioNode const& lhs, ClioNode const& rhs) {
                return *lhs.uuid < *rhs.uuid;
            });

            auto const it = std::ranges::find_if(clusterData, [](ClioNode const& node) {
                return node.dbRole == ClioNode::DbRole::NotWriter or
                    node.dbRole == ClioNode::DbRole::Writer;
            });

            if (it == clusterData.end()) {
                // No writer nodes in the cluster yet
                return;
            }

            if (*it->uuid == *selfId) {
                writerState->startWriting();
            } else {
                writerState->giveUpWriting();
            }
        }
    );
}

}  // namespace cluster
