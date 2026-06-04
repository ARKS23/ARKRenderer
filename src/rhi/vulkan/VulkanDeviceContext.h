#pragma once

#include "rhi/DeviceContext.h"

namespace ark::rhi::vulkan
{
class VulkanDeviceContext : public DeviceContext
{
public:
    void begin() override;
    void end() override;
    void submit(const SubmitDesc& desc) override;
};
} // namespace ark::rhi::vulkan
