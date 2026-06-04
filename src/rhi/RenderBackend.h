#pragma once

#include "core/Memory.h"
#include "rhi/RenderDevice.h"
#include "rhi/SwapChain.h"

namespace ark::rhi {
    struct RenderBackendDesc {
        RenderDeviceCreateInfo device;
        SwapChainDesc swapChain;
    };

    // RenderBackend 是一组已经创建好的 RHI 后端对象；成员顺序保证 swapchain 先于 device 销毁。
    class RenderBackend final {
    public:
        RenderBackend(Scope<RenderDevice> device, Scope<SwapChain> swapChain);
        ~RenderBackend();

        RenderDevice& device();
        const RenderDevice& device() const;

        SwapChain* swapChain();
        const SwapChain* swapChain() const;

        void setSwapChain(Scope<SwapChain> swapChain);

    private:
        Scope<RenderDevice> m_Device;
        Scope<SwapChain> m_SwapChain;
    };

    Scope<RenderDevice> createRenderDevice(const RenderDeviceCreateInfo& createInfo);
    Scope<SwapChain> createSwapChain(const SwapChainCreateInfo& createInfo);
    Scope<RenderBackend> createRenderBackend(const RenderBackendDesc& desc);
} // namespace ark::rhi
