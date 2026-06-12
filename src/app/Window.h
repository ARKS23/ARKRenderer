#pragma once

#include "app/Input.h"
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

        virtual bool shouldClose() const = 0;
        virtual void pollEvents() = 0;

        virtual rhi::NativeWindowHandle getNativeWindowHandle() const = 0;
        virtual rhi::Extent2D getExtent() const = 0;
        virtual InputSnapshot getInputSnapshot() const = 0;
    };
} // namespace ark
