#pragma once

#include "core/Types.h"

#include <string>

namespace ark::rhi {
    enum class BufferUsage : u32 {
        None = 0,
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
        Storage = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
    };

    constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) {
        return static_cast<BufferUsage>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
    }

    constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) {
        return static_cast<BufferUsage>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
    }

    constexpr bool hasBufferUsage(BufferUsage value, BufferUsage flag) {
        return static_cast<u32>(value & flag) != 0;
    }

    enum class MemoryUsage {
        CpuToGpu,
        GpuOnly,
        GpuToCpu,
    };

    struct BufferDesc {
        std::string debugName;
        u64 size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryUsage memoryUsage = MemoryUsage::GpuOnly;

        // initialData 只在创建期间有效，后端不能长期保存该指针。
        const void* initialData = nullptr;
    };

    class Buffer {
    public:
        virtual ~Buffer() = default;

        virtual const BufferDesc& getDesc() const = 0;
        virtual bool readData(void*, u64, u64 = 0) const {
            return false;
        }
    };
} // namespace ark::rhi
