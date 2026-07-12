# Build Environment

ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE}

LABEL org.opencontainers.image.title="Vortex RPC Build Environment"
LABEL org.opencontainers.image.description="C++23 build environment for Vortex RPC framework"

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Compilers
    g++-14 gcc-14 \
    clang-18 clang-tidy-18 clang-format-18 \
    # Build system
    cmake ninja-build make \
    # Version control
    git \
    # Debug / profiling
    gdb valgrind linux-tools-generic \
    # Networking tools (for tests)
    netcat-openbsd \
    # Cleanup
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set GCC 14 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 && \
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100

# Install vcpkg
RUN git clone --depth=1 https://github.com/Microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh

# Install Vortex dependencies via vcpkg
RUN /opt/vcpkg/vcpkg install \
    gtest \
    benchmark \
    fmt \
    spdlog

ENV VCPKG_ROOT=/opt/vcpkg
ENV CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

# Create workspace
WORKDIR /workspace/vortex

# Default command: build and test
CMD ["sh", "-c", "\
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DVORTEX_BUILD_TESTS=ON \
        -DVORTEX_BUILD_BENCHMARKS=ON \
        -DVORTEX_BUILD_EXAMPLES=ON \
        -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build -j $(nproc) \
    && cd build && ctest --output-on-failure"]
