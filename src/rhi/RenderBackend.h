#pragma once

#include "core/Memory.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"
#include "rhi/SwapChain.h"

namespace ark::rhi {
    struct RenderBackendDesc {
        RenderDeviceCreateInfo device;
        SwapChainDesc swapChain;
    };

    // RenderBackend 是一组已经创建好的 RHI 后端对象；成员顺序保证后端对象先于 device 销毁。
    class RenderBackend final {
    public:
        RenderBackend(Scope<RenderDevice> device, Scope<SwapChain> swapChain, Scope<DeviceContext> context);
        ~RenderBackend();

        RenderDevice& device();
        const RenderDevice& device() const;

        DeviceContext& context();
        const DeviceContext& context() const;

        SwapChain* swapChain();
        const SwapChain* swapChain() const;

        // Renderer 只表达“需要一个新的 swapchain”，具体后端对象创建仍由 RenderBackend 内部完成。
        SwapChain& recreateSwapChain(const SwapChainDesc& desc);

    private:
        Scope<RenderDevice> m_Device;
        Scope<SwapChain> m_SwapChain;
        Scope<DeviceContext> m_Context;
    };

    // 公开给 renderer/app 的唯一后端创建入口；底层 device/context/swapchain 工厂隐藏在 rhi/detail。
    Scope<RenderBackend> createRenderBackend(const RenderBackendDesc& desc);
} // namespace ark::rhi
