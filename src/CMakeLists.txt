# Vortex src library
add_library(vortex)

target_sources(vortex PRIVATE
    # I/O engine
    io/engine_factory.cpp
    io/uring_engine.cpp
    io/epoll_engine.cpp
    io/scheduler.cpp
    # Buffer
    buffer/ref_counted_buffer.cpp
    buffer/slab_allocator.cpp
    # Transport
    transport/frame.cpp
    transport/flow_control.cpp
    transport/multiplexer.cpp
    transport/connection.cpp
    transport/connection_migration.cpp
    # Codec
    codec/codec.cpp
    # RPC
    rpc/channel.cpp
    rpc/load_balancer.cpp
    rpc/circuit_breaker.cpp
    rpc/adaptive_limiter.cpp
    rpc/name_resolver.cpp
    rpc/client_server.cpp
    # Metrics
    metrics/metrics.cpp
)

target_include_directories(vortex PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(vortex PUBLIC cxx_std_23)

# Link dependencies
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(vortex PUBLIC fmt::fmt spdlog::spdlog)

# Platform-specific
if(VORTEX_USE_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(vortex PUBLIC VORTEX_USE_IO_URING=1)
    # io_uring uses raw syscall interface — no external liburing dependency
endif()
