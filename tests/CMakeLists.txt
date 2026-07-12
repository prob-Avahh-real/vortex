# Tests
find_package(GTest CONFIG REQUIRED)

add_executable(vortex_tests
    buffer/buffer_test.cpp
    buffer/ref_counted_buffer_test.cpp
    buffer/slab_allocator_test.cpp
    io/mpmc_queue_test.cpp
    transport/frame_test.cpp
    transport/flow_control_test.cpp
    transport/multiplexer_test.cpp
    transport/settings_migration_test.cpp
    codec/codec_test.cpp
    rpc/circuit_breaker_test.cpp
    rpc/rpc_integration_test.cpp
)

target_link_libraries(vortex_tests PRIVATE vortex GTest::gtest GTest::gtest_main)
target_compile_features(vortex_tests PRIVATE cxx_std_23)
target_include_directories(vortex_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Threads library for tests using std::thread
find_package(Threads REQUIRED)
target_link_libraries(vortex_tests PRIVATE Threads::Threads)

include(GoogleTest)
gtest_discover_tests(vortex_tests)
