#pragma once

#include "renderer/RenderPass.h"

namespace ark {
    class SkyboxPass : public RenderPass {
    public:
        bool execute(FrameContext& frameContext) override;
    };
} // namespace ark
