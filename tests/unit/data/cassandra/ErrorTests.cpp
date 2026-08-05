#include "data/cassandra/Error.hpp"

#include <cassandra.h>
#include <gtest/gtest.h>

#include <cstdint>

using namespace data::cassandra;

namespace {

CassandraError
makeError(uint32_t const code)
{
    return CassandraError{"some error", code};
}

}  // namespace

// isInvalidQuery is load bearing: DefaultExecutionStrategy::throwErrorIfNeeded treats it as the
// only permanent failure and retries everything else, so anything wrongly reported here would be
// retried forever (if false) or surfaced as a fatal std::runtime_error (if true).
TEST(BackendCassandraErrorTest, IsInvalidQueryOnlyForInvalidQuery)
{
    EXPECT_TRUE(makeError(CASS_ERROR_SERVER_INVALID_QUERY).isInvalidQuery());

    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_READ_FAILURE).isInvalidQuery());
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_WRITE_FAILURE).isInvalidQuery());
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_READ_TIMEOUT).isInvalidQuery());
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_UNAVAILABLE).isInvalidQuery());
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_SYNTAX_ERROR).isInvalidQuery());
    EXPECT_FALSE(makeError(CASS_OK).isInvalidQuery());
}

TEST(BackendCassandraErrorTest, IsTimeoutForTimeoutLikeCodes)
{
    EXPECT_TRUE(makeError(CASS_ERROR_LIB_NO_HOSTS_AVAILABLE).isTimeout());
    EXPECT_TRUE(makeError(CASS_ERROR_LIB_REQUEST_TIMED_OUT).isTimeout());
    EXPECT_TRUE(makeError(CASS_ERROR_SERVER_UNAVAILABLE).isTimeout());
    EXPECT_TRUE(makeError(CASS_ERROR_SERVER_OVERLOADED).isTimeout());
    EXPECT_TRUE(makeError(CASS_ERROR_SERVER_READ_TIMEOUT).isTimeout());

    // a quorum failure is not a timeout: replicas did answer, they answered with failures. It is
    // still retried, because throwErrorIfNeeded retries everything that is not an invalid query.
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_READ_FAILURE).isTimeout());
    EXPECT_FALSE(makeError(CASS_ERROR_SERVER_WRITE_FAILURE).isTimeout());
    EXPECT_FALSE(makeError(CASS_OK).isTimeout());
}

TEST(BackendCassandraErrorTest, MessageAndCodeArePreserved)
{
    // throwErrorIfNeeded puts message() into the DatabaseTimeout it throws, so that treating an
    // unclassified error as a timeout still reports what actually failed
    auto const err = CassandraError{"received 1 responses and 1 failures", 0x1300};

    EXPECT_EQ(err.message(), "received 1 responses and 1 failures");
    EXPECT_EQ(err.code(), 0x1300u);
}
