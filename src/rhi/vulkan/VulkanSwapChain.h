#pragma once

#include "rhi/SwapChain.h"

namespace ark::rhi::vulkan {
    class VulkanSwapChain : public SwapChain {
    public:
        [[nodiscard]] const SwapChainDesc& getDesc() const override;
        [[nodiscard]] u32 getBackBufferCount() const override;
        [[nodiscard]] TextureView* getCurrentBackBufferView() override;
        [[nodiscard]] TextureView* getDepthBufferView() override;

        SwapChainStatus resize(Extent2D extent) override;

    private:
        SwapChainDesc m_Desc;
        u32 m_BackBufferCount = 0;
    };
} // namespace ark::rhi::vulkan
