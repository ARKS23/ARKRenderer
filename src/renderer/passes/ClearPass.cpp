#include "renderer/passes/ClearPass.h"

#include "core/Log.h"
#include "renderer/core/FrameContext.h"
#include "rhi/DeviceContext.h"

namespace ark {
    bool ClearPass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !frameContext.backBufferView) {
            ARK_ERROR("ClearPass requires DeviceContext and backbuffer view");
            return false;
        }

        // 清屏已经由 FrameRenderer::beginRendering(loadOp=Clear) 表达，这里保留 pass 位置方便后续接入 RenderGraph。
        return true;
    }
} // namespace ark
