#pragma once

// Include the split buffer headers for backward compatibility.
// New code should include the specific header:
//   <vortex/buffer/ref_counted_buffer.h> and <vortex/buffer/slab_allocator.h>

#include <vortex/buffer/ref_counted_buffer.h>
#include <vortex/buffer/slab_allocator.h>

namespace vortex::buffer {
// All types are defined in the split headers above.
}  // namespace vortex::buffer
