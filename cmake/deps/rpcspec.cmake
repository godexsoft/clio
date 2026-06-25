find_package(xrpl-rpc-spec REQUIRED CONFIG)
# RPCSPEC_IS_CLIO=1 is set here for direct cmake builds; Conan-based builds
# additionally get it from the generated conan_toolchain.cmake.
if(NOT DEFINED RPCSPEC_IS_CLIO)
    add_compile_definitions(RPCSPEC_IS_CLIO=1)
endif()
