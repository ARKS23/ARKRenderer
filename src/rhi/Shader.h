#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::rhi {
    enum class ShaderStage {
        Vertex,
        Fragment,
        Compute,
    };

    struct ShaderDesc {
        std::string debugName;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint = "main";

        // SPIR-V 以 u32 word 为单位，和 Vulkan shader module 创建输入保持一致。
        std::vector<u32> bytecode;
    };

    class Shader {
    public:
        virtual ~Shader() = default;

        virtual const ShaderDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
