# Benchmarks
find_package(benchmark CONFIG REQUIRED)

add_executable(vortex_benchmarks
    codec_bench.cpp
    flow_control_bench.cpp
    protocol_bench.cpp
    rpc_bench.cpp
)

target_link_libraries(vortex_benchmarks PRIVATE vortex benchmark::benchmark)
target_compile_features(vortex_benchmarks PRIVATE cxx_std_23)
target_include_directories(vortex_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
