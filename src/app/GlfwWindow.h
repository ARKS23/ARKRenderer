#pragma once

#include "app/Window.h"

namespace ark {
    class GlfwWindow final : public Window {
    public:
        explicit GlfwWindow(const WindowDesc& desc);
        ~GlfwWindow() override;

        [[nodiscard]] bool shouldClose() const override;
        void pollEvents() override;

        [[nodiscard]] rhi::NativeWindowHandle getNativeWindowHandle() const override;
        [[nodiscard]] rhi::Extent2D getExtent() const override;

    private:
        void* m_Window = nullptr;
        rhi::Extent2D m_Extent{};
    };
} // namespace ark
