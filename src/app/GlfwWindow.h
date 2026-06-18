#pragma once

#include "app/Window.h"

namespace ark {
    class GlfwWindow final : public Window {
    public:
        explicit GlfwWindow(const WindowDesc& desc);
        ~GlfwWindow() override;

        bool shouldClose() const override;
        void pollEvents() override;

        rhi::NativeWindowHandle getNativeWindowHandle() const override;
        rhi::Extent2D getExtent() const override;
        InputSnapshot getInputSnapshot() const override;

        void onScroll(double xOffset, double yOffset);

    private:
        void updateInputSnapshot();

        void* m_Window = nullptr;
        rhi::Extent2D m_Extent{};
        InputSnapshot m_Input{};
        glm::vec2 m_PreviousCursorPosition{0.0f};
        glm::vec2 m_PendingScrollDelta{0.0f};
        bool m_HasPreviousCursorPosition = false;
        bool m_PreviousResetDown = false;
        bool m_PreviousDebugUiToggleDown = false;
    };
} // namespace ark
