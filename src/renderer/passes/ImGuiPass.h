#pragma once

#include "renderer/core/RenderPass.h"

namespace ark {
    class ImGuiPass : public RenderPass {
    public:
        bool execute(FrameContext& frameContext) override;
    };
} // namespace ark
