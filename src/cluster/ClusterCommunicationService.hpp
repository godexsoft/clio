#pragma once

#include "cluster/Backend.hpp"
#include "cluster/Concepts.hpp"
#include "cluster/Metrics.hpp"
#include "cluster/WriterDecider.hpp"
#include "data/BackendInterface.hpp"
#include "etl/WriterState.hpp"

#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/uuid/uuid.hpp>

#include <chrono>
#include <memory>

namespace cluster {

/**
 * @brief Service to post and read messages to/from the cluster. It uses a backend to communicate
 * with the cluster.
 */
class ClusterCommunicationService : public ClusterCommunicationServiceTag {
    // TODO: Use util::async::CoroExecutionContext after https://github.com/XRPLF/clio/issues/1973
    // is implemented
    boost::asio::thread_pool ctx_{1};
    Backend backend_;
    Metrics metrics_;
    WriterDecider writerDecider_;

public:
    static constexpr std::chrono::milliseconds kDEFAULT_READ_INTERVAL{1000};
    static constexpr std::chrono::milliseconds kDEFAULT_WRITE_INTERVAL{1000};

    /**
     * @brief Construct a new Cluster Communication Service object.
     *
     * @param backend The backend to use for communication.
     * @param writerState The state showing whether clio is writing to the database.
     * @param readInterval The interval to read messages from the cluster.
     * @param writeInterval The interval to write messages to the cluster.
     */
    ClusterCommunicationService(
        std::shared_ptr<data::BackendInterface> backend,
        std::unique_ptr<etl::WriterStateInterface> writerState,
        std::chrono::steady_clock::duration readInterval = kDEFAULT_READ_INTERVAL,
        std::chrono::steady_clock::duration writeInterval = kDEFAULT_WRITE_INTERVAL
    );

    ~ClusterCommunicationService() override;

    ClusterCommunicationService(ClusterCommunicationService&&) = delete;
    ClusterCommunicationService(ClusterCommunicationService const&) = delete;
    ClusterCommunicationService&
    operator=(ClusterCommunicationService&&) = delete;
    ClusterCommunicationService&
    operator=(ClusterCommunicationService const&) = delete;

    /**
     * @brief Start the service.
     */
    void
    run();

    /**
     * @brief Stop the service.
     */
    void
    stop();
};

}  // namespace cluster
