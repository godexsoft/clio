find_package(xrpl-rpc-spec REQUIRED CONFIG)

if(NOT DEFINED RPCSPEC_IS_CLIO)
  add_compile_definitions(RPCSPEC_IS_CLIO=1)
endif()
