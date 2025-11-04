add_library(clio_app)
target_sources(clio_app PRIVATE CliArgs.cpp ClioApplication.cpp Stopper.cpp WebHandlers.cpp)

target_link_libraries(
  clio_app
  PUBLIC clio_cluster
         clio_web
         clio_etl
         clio_feed
         clio_rpc
         clio_migration
  PRIVATE Boost::program_options
)
