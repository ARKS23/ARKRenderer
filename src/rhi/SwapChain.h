#pragma once

#include "rhi/RHICommon.h"

namespace ark::rhi {
    class TextureView;

    struct SwapChainDesc {
        Extent2D extent;
        Format colorFormat = Format::Unknown;
        NativeWindowHandle nativeWindow;
    };

    class SwapChain {
    public:
        virtual ~SwapChain() = default;

        [[nodiscard]] virtual const SwapChainDesc& getDesc() const = 0;
        [[nodiscard]] virtual TextureView* getCurrentBackBufferView() = 0;
    };
} // namespace ark::rhi
