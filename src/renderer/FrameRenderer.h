#pragma once

#include "core/Memory.h"
#include "rhi/RHICommon.h"

namespace ark {
    struct FrameContext;

    namespace rhi {
        class RenderDevice;
    } // namespace rhi

    // FrameRenderer 是 RenderGraph 落地前的轻量一帧调度器，负责按固定顺序执行 pass。
    class FrameRenderer {
    public:
        virtual ~FrameRenderer() = default;

        virtual void setup(rhi::RenderDevice& device) = 0;
        virtual bool render(FrameContext& frameContext) = 0;
        virtual void resize(rhi::Extent2D extent) = 0;
    };

    Scope<FrameRenderer> createFrameRenderer();
} // namespace ark
