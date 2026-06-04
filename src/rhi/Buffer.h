#pragma once

#include "core/Types.h"

namespace ark::rhi {
enum class BufferUsage : u32 {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Constant = 1 << 2,
    Storage = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
};

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::None;
};

class Buffer {
public:
    virtual ~Buffer() = default;
};
} // namespace ark::rhi
