#include "rpc/handlers/ServerInfo.hpp"

#include <rpcspec/HandlerForDefs.hpp>
#include <rpcspec/handlers/server_info/Spec.hpp>

// server_info is a header-only (templated) handler; emit its spec entry points here so the
// registry and DefaultProcessor link against a single instantiation for its Input.
template struct rpc::spec::HandlerFor<rpc::spec::handlers::server_info::Input>;
