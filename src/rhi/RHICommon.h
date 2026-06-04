#pragma once

#include "core/Types.h"

namespace ark::rhi {
    enum class Format {
        Unknown,
        BGRA8Unorm,
        RGBA8Unorm,
        RGBA16Float,
        D24UnormS8UInt,
        D32Float,
    };

    enum class NativeWindowType {
        GLFW,
        Win32,
        SDL,
    };

    struct NativeWindowHandle {
        NativeWindowType type = NativeWindowType::GLFW;
        void* handle = nullptr;
    };

    struct Extent2D {
        u32 width = 0;
        u32 height = 0;
    };
} // namespace ark::rhi
