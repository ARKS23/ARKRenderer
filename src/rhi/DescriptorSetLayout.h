#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace ark::rhi {
    // DescriptorType 描述 shader 可见资源的绑定类型；Phase 0.5 先只接入 uniform buffer。
    enum class DescriptorType {
        UniformBuffer,
    };

    // ShaderStageFlags 表达一个 binding 对哪些 shader stage 可见，后端会映射到对应 API 的 stage flags。
    enum class ShaderStageFlags : u32 {
        None = 0,
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Compute = 1 << 2,
    };

    constexpr ShaderStageFlags operator|(ShaderStageFlags lhs, ShaderStageFlags rhs) {
        return static_cast<ShaderStageFlags>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
    }

    constexpr ShaderStageFlags operator&(ShaderStageFlags lhs, ShaderStageFlags rhs) {
        return static_cast<ShaderStageFlags>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
    }

    constexpr bool hasShaderStage(ShaderStageFlags value, ShaderStageFlags flag) {
        return static_cast<u32>(value & flag) != 0;
    }

    struct DescriptorBindingDesc {
        u32 binding = 0;
        DescriptorType type = DescriptorType::UniformBuffer;
        u32 count = 1;
        ShaderStageFlags stages = ShaderStageFlags::None;
    };

    struct DescriptorSetLayoutDesc {
        std::string debugName;
        std::vector<DescriptorBindingDesc> bindings;
    };

    // DescriptorSetLayout 是 pipeline layout 的组成部分，只描述资源绑定布局，不持有具体资源。
    class DescriptorSetLayout {
    public:
        virtual ~DescriptorSetLayout() = default;

        virtual const DescriptorSetLayoutDesc& getDesc() const = 0;
    };
} // namespace ark::rhi
