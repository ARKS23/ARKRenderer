#include "renderer/RenderPass.h"

namespace ark {
    void RenderPass::setup(rhi::RenderDevice& device) {
        (void)device;
    }

    bool RenderPass::prepare(FrameContext& frameContext) {
        (void)frameContext;
        return true;
    }
} // namespace ark
