#pragma once

namespace ark
{
struct FrameContext;

class RenderPass
{
public:
    virtual ~RenderPass() = default;

    virtual void execute(FrameContext& frameContext) = 0;
};
} // namespace ark
