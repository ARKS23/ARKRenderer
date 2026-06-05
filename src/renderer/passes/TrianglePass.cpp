#include "renderer/passes/TrianglePass.h"

#include "renderer/FrameContext.h"

namespace ark {
    bool TrianglePass::execute(FrameContext& frameContext) {
        (void)frameContext;

        // Phase 0.4 后续步骤会在这里绑定 pipeline / vertex buffer 并提交 draw(3, 0)。
        return true;
    }
} // namespace ark
