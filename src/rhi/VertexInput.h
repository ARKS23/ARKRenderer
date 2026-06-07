#pragma once

#include "rhi/RHICommon.h"

#include <vector>

/*
    binding = 数据流 / buffer slot
    location = shader 输入变量编号
    attribute = location 和 binding 之间的映射规则
    vertex input 可以有多个binding，一个binding内可以有多个location
 */

namespace ark::rhi {
    enum class VertexInputRate {
        PerVertex,
        PerInstance,
    };

    struct VertexAttributeDesc {
        u32 location = 0;
        Format format = Format::Unknown;
        u32 offset = 0;
    };

    struct VertexBufferLayoutDesc {
        u32 binding = 0;
        u32 stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
        std::vector<VertexAttributeDesc> attributes;
    };

    // 顶点输入布局属于 pipeline 状态；实际绑定的 Buffer 由 Mesh/Pass 在录制命令时提供。
    struct VertexInputLayoutDesc {
        std::vector<VertexBufferLayoutDesc> buffers;
    };
} // namespace ark::rhi
