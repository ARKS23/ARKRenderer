#pragma once

#include "renderer/RenderPass.h"

namespace ark {
    // TrianglePass 是 Phase 0.4 的几何绘制落点；当前先建立 pass 位置，后续接入 Buffer/Shader/Pipeline 后执行 draw。
    class TrianglePass final : public RenderPass {
    public:
        bool execute(FrameContext& frameContext) override;
    };
} // namespace ark
