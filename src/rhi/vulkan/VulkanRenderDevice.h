#pragma once

#include "rhi/RenderDevice.h"

namespace ark::rhi::vulkan {
class VulkanRenderDevice : public RenderDevice {
public:
    void beginFrame() override;
    void endFrame() override;
    void waitIdle() override;
};
} // namespace ark::rhi::vulkan
