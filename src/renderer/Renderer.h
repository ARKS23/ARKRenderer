#pragma once

#include "core/Memory.h"
#include "renderer/settings/RendererQuality.h"
#include "renderer/scene/SceneResource.h"
#include "rhi/RHICommon.h"

namespace ark {
    class FrameOverlay;
    class RenderScene;
    class RenderView;

    // Public facade: renderer 的创建参数只描述外部输入。
    // RHI backend、swapchain、frame renderer 等内部对象由 Renderer 实现持有，应用层不直接管理。
    struct RendererDesc {
        rhi::NativeWindowHandle nativeWindow;
        rhi::Extent2D extent{1280, 720};
        SceneResourceLoadDesc defaultScene;
        RendererQualityDesc quality;
        bool enableValidation = false;
    };

    // Public facade: Renderer 是渲染系统的主入口。
    // 上层应用 / 未来引擎 adapter 只通过 render() 提交 RenderScene + RenderView，
    // 并通过 resize() 通知窗口尺寸变化；具体 pass / effect 调度保持 renderer internal。
    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void render(RenderScene& scene, const RenderView& view, FrameOverlay* overlay = nullptr) = 0;
        virtual void resize(unsigned width, unsigned height) = 0;
    };

    Scope<Renderer> createRenderer(const RendererDesc& desc);
} // namespace ark
