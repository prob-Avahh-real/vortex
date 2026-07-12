cmake_minimum_required(VERSION 3.28)
project(vortex VERSION 0.1.0 LANGUAGES CXX)

# C++23 required
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(VORTEX_BUILD_TESTS "Build unit tests" ON)
option(VORTEX_BUILD_BENCHMARKS "Build benchmarks" ON)
option(VORTEX_BUILD_EXAMPLES "Build examples" ON)
option(VORTEX_USE_IO_URING "Enable io_uring backend (Linux 5.19+)" ON)
option(VORTEX_USE_ASAN "Enable AddressSanitizer" OFF)
option(VORTEX_USE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(VORTEX_USE_TSAN "Enable ThreadSanitizer" OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror
                        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                        -Wcast-align -Wunused -Woverloaded-virtual
                        -Wconversion -Wsign-conversion -Wnull-dereference
                        -Wdouble-promotion -Wformat=2)
endif()

# Sanitizers
if(VORTEX_USE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(VORTEX_USE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()
if(VORTEX_USE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# Subdirectories
add_subdirectory(src)
if(VORTEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(VORTEX_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
if(VORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
