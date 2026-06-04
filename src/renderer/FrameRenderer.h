#pragma once

namespace ark
{
struct FrameContext;

class FrameRenderer
{
public:
    virtual ~FrameRenderer() = default;

    virtual void render(FrameContext& frameContext) = 0;
    virtual void resize(unsigned width, unsigned height) = 0;
};
} // namespace ark
