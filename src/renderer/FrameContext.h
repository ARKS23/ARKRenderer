#pragma once

#include "core/Types.h"
#include "renderer/ShadowCascade.h"
#include "rhi/RHICommon.h"

#include <glm/mat4x4.hpp>

namespace ark::rhi {
    class DeviceContext;
    struct FrameResource;
    class RenderDevice;
    class Sampler;
    class SwapChain;
    class TextureView;
} // namespace ark::rhi

namespace ark {
    class EnvironmentBrdfLutResource;
    class EnvironmentCubeResource;
    class RenderQueue;
    class RenderScene;
    class RenderView;

    // FrameContext 是 renderer 层的一帧逻辑上下文，负责把 pass 执行所需的公共 RHI 对象集中传递。
    // 它不拥有资源，也不暴露 Vulkan 类型；底层同步对象仍由 rhi::FrameResource 后端实现承担。
    struct FrameContext {
        u64 frameIndex = 0;
        float deltaTime = 0.0f;

        RenderScene* scene = nullptr;
        const RenderView* view = nullptr;
        RenderQueue* queue = nullptr;
        RenderQueue* forwardQueue = nullptr;

        rhi::RenderDevice* device = nullptr;
        rhi::DeviceContext* context = nullptr;
        rhi::SwapChain* swapChain = nullptr;
        rhi::FrameResource* frameResource = nullptr;
        rhi::TextureView* backBufferView = nullptr;
        rhi::TextureView* sceneColorView = nullptr;
        EnvironmentCubeResource* environmentCube = nullptr;
        EnvironmentCubeResource* irradianceCube = nullptr;
        EnvironmentCubeResource* prefilteredSpecularCube = nullptr;
        EnvironmentBrdfLutResource* brdfLut = nullptr;
        rhi::TextureView* shadowMapView = nullptr;
        rhi::Sampler* shadowSampler = nullptr;

        rhi::Extent2D extent{};
        rhi::Format colorFormat = rhi::Format::Unknown;
        rhi::Format depthFormat = rhi::Format::Unknown;
        rhi::ClearColor clearColor{};
        glm::mat4 lightViewProjection{1.0f};
        float shadowStrength = 0.0f;
        float shadowBias = 0.0015f;
        float shadowFilterMode = 0.0f;
        float shadowFilterRadiusTexels = 0.0f;
        // CSM 帧数据由阴影阶段生成，前向光照阶段消费；关闭阴影或重建资源时必须清空。
        CascadeShadowFrameData cascadeShadows;
    };
} // namespace ark
