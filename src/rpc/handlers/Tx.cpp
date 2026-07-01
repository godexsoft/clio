#include "rpc/handlers/Tx.hpp"

#include <rpcspec/HandlerForDefs.hpp>
#include <rpcspec/handlers/tx/Spec.hpp>

// tx is a header-only handler; emit its spec entry points here so the registry and
// DefaultProcessor link against a single instantiation for its Input.
template struct rpc::spec::HandlerFor<rpc::spec::handlers::tx::Input>;
