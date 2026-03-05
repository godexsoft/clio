#include "cluster/ClusterCommunicationService.hpp"

#include "data/BackendInterface.hpp"
#include "etl/WriterState.hpp"

#include <chrono>
#include <ctime>
#include <memory>
#include <utility>

namespace cluster {

ClusterCommunicationService::ClusterCommunicationService(
    std::shared_ptr<data::BackendInterface> backend,
    std::unique_ptr<etl::WriterStateInterface> writerState,
    std::chrono::steady_clock::duration readInterval,
    std::chrono::steady_clock::duration writeInterval
)
    : backend_(ctx_, std::move(backend), writerState->clone(), readInterval, writeInterval)
    , writerDecider_(ctx_, std::move(writerState))
{
}

void
ClusterCommunicationService::run()
{
    backend_.subscribeToNewState([this](auto&&... args) {
        metrics_.onNewState(std::forward<decltype(args)>(args)...);
    });
    backend_.subscribeToNewState([this](auto&&... args) {
        writerDecider_.onNewState(std::forward<decltype(args)>(args)...);
    });
    backend_.run();
}

ClusterCommunicationService::~ClusterCommunicationService()
{
    stop();
}

void
ClusterCommunicationService::stop()
{
    backend_.stop();
}

}  // namespace cluster
