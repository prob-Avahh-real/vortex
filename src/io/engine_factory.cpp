#include <vortex/io/engine.h>

#ifdef VORTEX_USE_IO_URING
// io_uring backend will be implemented in engine_uring.cpp
#include <vortex/common.h>
#endif

namespace vortex::io {

auto create_engine(EngineType type) -> std::unique_ptr<IEngine> {
    // Phase 1: implement UringEngine and EpollEngine backends
    // For now, returns nullptr (scaffold)
    (void)type;
    return nullptr;
}

}  // namespace vortex::io
