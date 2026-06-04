#pragma once

#include "rhi/RHICommon.h"

#include <string>

namespace ark {
    struct WindowDesc {
        std::string title = "ARKRenderer";
        rhi::Extent2D extent{1280, 720};
    };

    class Window {
    public:
        virtual ~Window() = default;

        [[nodiscard]] virtual bool shouldClose() const = 0;
        virtual void pollEvents() = 0;

        [[nodiscard]] virtual rhi::NativeWindowHandle getNativeWindowHandle() const = 0;
        [[nodiscard]] virtual rhi::Extent2D getExtent() const = 0;
    };
} // namespace ark
