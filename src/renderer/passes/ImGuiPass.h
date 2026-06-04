#pragma once

#include "renderer/RenderPass.h"

namespace ark {
    class ImGuiPass : public RenderPass {
    public:
        void execute(FrameContext& frameContext) override;
    };
} // namespace ark
