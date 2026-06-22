#pragma once

#include "renderer/core/RenderPass.h"

namespace ark {
    class ClearPass : public RenderPass {
    public:
        bool execute(FrameContext& frameContext) override;
    };
} // namespace ark
