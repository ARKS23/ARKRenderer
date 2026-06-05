#pragma once

#include "core/Memory.h"
#include "rhi/FrameResource.h"
#include "rhi/RHICommon.h"

namespace ark::rhi {
    class RenderDevice;
    class TextureView;

    // SwapChainDesc 是运行期可查询的持久描述，不保存创建时借用的外部依赖。
    struct SwapChainDesc {
        Extent2D extent;
        Format colorFormat = Format::Unknown;
        Format depthFormat = Format::D32Float;
        u32 imageCount = 2;
        bool enableVSync = true;
    };

    // SwapChain 创建依赖已初始化的 RenderDevice；surface 由后端设备内部提供。
    struct SwapChainCreateInfo {
        SwapChainDesc desc;
        RenderDevice* device = nullptr;
    };

    // resize/acquire/present 通过状态通知上层是否需要重建或暂停渲染。
    enum class SwapChainStatus {
        Ready,
        Suboptimal,
        OutOfDate,
        SurfaceLost,
        DeviceLost,
        Error,
    };

    constexpr u32 InvalidBackBufferIndex = static_cast<u32>(-1);

    struct AcquireResult {
        SwapChainStatus status = SwapChainStatus::Ready;
        u32 imageIndex = InvalidBackBufferIndex;
    };

    class SwapChain {
    public:
        virtual ~SwapChain() = default;

        virtual const SwapChainDesc& getDesc() const = 0;
        virtual u32 getBackBufferCount() const = 0;
        virtual u32 getCurrentBackBufferIndex() const = 0;
        virtual TextureView* getCurrentBackBufferView() = 0;
        virtual TextureView* getDepthBufferView() = 0;

        // acquire/present 属于窗口 backbuffer 生命周期，具体同步对象由 FrameResource 后端实现提供。
        virtual AcquireResult acquireNextImage(FrameResource& frameResource) = 0;
        virtual SwapChainStatus present(FrameResource& frameResource) = 0;

        // 默认 depth buffer 跟随 swapchain 生命周期和 resize。
        virtual SwapChainStatus resize(Extent2D extent) = 0;
    };
} // namespace ark::rhi
