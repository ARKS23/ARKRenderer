#pragma once

#include "core/Memory.h"
#include "rhi/DeviceContext.h"
#include "rhi/RenderDevice.h"
#include "rhi/SwapChain.h"

namespace ark::rhi::detail {
    // 内部后端工厂只服务 RenderBackend 拼装流程，不作为 renderer 层公共 API。
    Scope<RenderDevice> createRenderDevice(const RenderDeviceCreateInfo& createInfo);
    Scope<DeviceContext> createDeviceContext(RenderDevice& device);
    Scope<SwapChain> createSwapChain(const SwapChainCreateInfo& createInfo);
} // namespace ark::rhi::detail
