#pragma once

#include "rhi/RHICommon.h"

#include <memory>

namespace ark::rhi {
    class TextureView;

    struct SwapChainDesc {
        Extent2D extent;
        Format colorFormat = Format::Unknown;
        Format depthFormat = Format::D32Float;
        NativeWindowHandle nativeWindow;
        u32 imageCount = 2;
        bool enableVSync = true;
    };

    enum class SwapChainStatus {
        Ready,
        Suboptimal,
        OutOfDate,
        SurfaceLost,
    };

    class SwapChain {
    public:
        virtual ~SwapChain() = default;

        [[nodiscard]] virtual const SwapChainDesc& getDesc() const = 0;
        [[nodiscard]] virtual u32 getBackBufferCount() const = 0;
        [[nodiscard]] virtual TextureView* getCurrentBackBufferView() = 0;
        [[nodiscard]] virtual TextureView* getDepthBufferView() = 0;

        // 默认 depth buffer 跟随 swapchain 生命周期和 resize。
        virtual SwapChainStatus resize(Extent2D extent) = 0;
    };

    using SwapChainPtr = std::unique_ptr<SwapChain>;
} // namespace ark::rhi
