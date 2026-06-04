#pragma once

#include "rhi/SwapChain.h"

namespace ark::rhi::vulkan
{
class VulkanSwapChain : public SwapChain
{
public:
    [[nodiscard]] const SwapChainDesc& getDesc() const override;
    [[nodiscard]] TextureView* getCurrentBackBufferView() override;

private:
    SwapChainDesc m_Desc;
};
} // namespace ark::rhi::vulkan
