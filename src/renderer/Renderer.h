#pragma once

#include "core/Memory.h"
#include "rhi/RHICommon.h"

namespace ark {
    class RenderScene;
    class RenderView;

    struct RendererDesc {
        rhi::NativeWindowHandle nativeWindow;
        rhi::Extent2D extent{1280, 720};
        bool enableValidation = false;
    };

    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void render(RenderScene& scene, const RenderView& view) = 0;
        virtual void resize(unsigned width, unsigned height) = 0;
    };

    Scope<Renderer> createRenderer(const RendererDesc& desc);
} // namespace ark
