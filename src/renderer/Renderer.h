#pragma once

#include "core/FileSystem.h"
#include "core/Memory.h"
#include "renderer/RendererQuality.h"
#include "renderer/SceneResource.h"
#include "rhi/RHICommon.h"

#include <glm/mat4x4.hpp>

#include <vector>

namespace ark {
    class RenderScene;
    class RenderView;

    // RendererDesc 只保存创建 renderer 所需的外部输入，不保存后端对象。
    struct RendererDesc {
        rhi::NativeWindowHandle nativeWindow;
        rhi::Extent2D extent{1280, 720};
        Path defaultModelPath;
        glm::mat4 defaultModelTransform{1.0f};
        std::vector<SceneAdditionalModelDesc> defaultAdditionalModels;
        Path defaultEnvironmentPath;
        float defaultEnvironmentIntensity = 1.0f;
        bool defaultOverrideLighting = false;
        SceneLighting defaultLighting;
        RendererQualityDesc quality;
        bool useDebugOrientationEnvironment = false;
        bool enableValidation = false;
    };

    // Renderer 是渲染系统门面；上层应用只通过它驱动一帧渲染和 resize。
    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void render(RenderScene& scene, const RenderView& view) = 0;
        virtual void resize(unsigned width, unsigned height) = 0;
    };

    Scope<Renderer> createRenderer(const RendererDesc& desc);
} // namespace ark
