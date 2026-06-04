#pragma once

#include "renderer/RenderPass.h"

namespace ark
{
class ForwardPass : public RenderPass
{
public:
    void execute(FrameContext& frameContext) override;
};
} // namespace ark
