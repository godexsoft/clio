#include "rpc/FakesAndMocks.hpp"

#include <rpcspec/HandlerForDefs.hpp>

// Emit the generic spec entry points for the test-fake Input types once in the test binary,
// mirroring how each real handler's .cpp instantiates them.
template struct rpc::spec::HandlerFor<tests::common::TestInput>;
template struct rpc::spec::HandlerFor<tests::common::InOutFake>;
