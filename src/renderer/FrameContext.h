#pragma once

#include "core/Types.h"

namespace ark::rhi {
class DeviceContext;
class RenderDevice;
class SwapChain;
} // namespace ark::rhi

namespace ark {
class RenderQueue;
class RenderScene;
class RenderView;

struct FrameContext {
    u64 frameIndex = 0;
    float deltaTime = 0.0f;

    RenderScene* scene = nullptr;
    RenderView* view = nullptr;
    RenderQueue* queue = nullptr;

    rhi::RenderDevice* device = nullptr;
    rhi::DeviceContext* context = nullptr;
    rhi::SwapChain* swapChain = nullptr;
};
} // namespace ark
