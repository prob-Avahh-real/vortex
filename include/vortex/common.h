#pragma once

#define VORTEX_VERSION_MAJOR 0
#define VORTEX_VERSION_MINOR 1
#define VORTEX_VERSION_PATCH 0
#define VORTEX_VERSION_STRING "0.1.0"

namespace vortex {

// Common error type used throughout the framework
struct Error {
    enum class Code {
        kOk = 0,
        kUnknown = 1,
        kInvalidArgument = 2,
        kUnavailable = 3,
        kDeadlineExceeded = 4,
        kResourceExhausted = 5,
        kInternal = 6,
    };

    Code code = Code::kOk;
    const char* message = nullptr;

    explicit operator bool() const { return code != Code::kOk; }
};

}  // namespace vortex
