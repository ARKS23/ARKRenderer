#pragma once

namespace ark {
    struct FrameContext;

    // FrameOverlay 是 renderer 和 sandbox UI 之间的最小缝合点，不暴露任何具体 UI 后端类型。
    class FrameOverlay {
    public:
        virtual ~FrameOverlay() = default;

        virtual bool isEnabled() const = 0;
        virtual bool render(FrameContext& frameContext) = 0;
    };
} // namespace ark
