# Examples
add_executable(vortex_echo_demo echo_demo.cpp)
target_link_libraries(vortex_echo_demo PRIVATE vortex)
target_compile_features(vortex_echo_demo PRIVATE cxx_std_23)
target_include_directories(vortex_echo_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(vortex_kv_store kv_store.cpp)
target_link_libraries(vortex_kv_store PRIVATE vortex)
target_compile_features(vortex_kv_store PRIVATE cxx_std_23)
target_include_directories(vortex_kv_store PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Legacy echo server placeholder
add_executable(vortex_echo_server echo_server.cpp)
target_link_libraries(vortex_echo_server PRIVATE vortex)
target_compile_features(vortex_echo_server PRIVATE cxx_std_23)
target_include_directories(vortex_echo_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
