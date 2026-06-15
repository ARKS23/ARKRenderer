#pragma once

#include "core/Memory.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderPass.h"
#include "renderer/material/MaterialResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <map>
#include <vector>

namespace ark {
    // ForwardPass 只消费 RenderQueue；空 queue 表示本帧没有 forward draw。
    class ForwardPass final : public RenderPass {
    public:
        ~ForwardPass() override;

        void setup(rhi::RenderDevice& device) override;
        bool prepare(FrameContext& frameContext) override;
        bool execute(FrameContext& frameContext) override;

    private:
        static constexpr u32 FramesInFlight = 2;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        bool ensureFallbackEnvironment();
        bool ensureFallbackIrradianceCube();
        bool ensureFallbackSpecularCube();
        bool ensureFallbackBrdfLut();
        bool uploadEnvironmentResources(FrameContext& frameContext);
        usize drawItemCount(const FrameContext& frameContext) const;
        bool ensureDrawDescriptorResources(u32 frameSlot, usize drawCount);
        rhi::PipelineState* getOrCreatePipeline(FrameContext& frameContext, const MaterialResource& material);
        bool updateCameraUniform(FrameContext& frameContext, u32 frameSlot);
        bool updateLightingUniform(FrameContext& frameContext, u32 frameSlot);
        bool updateObjectUniform(FrameContext& frameContext,
                                 u32 frameSlot,
                                 usize drawIndex,
                                 const glm::mat4& modelMatrix);
        bool updateMaterialUniform(FrameContext& frameContext,
                                   u32 frameSlot,
                                   usize drawIndex,
                                   const MaterialResource& material);
        bool updateDrawDescriptorSet(FrameContext& frameContext,
                                     u32 frameSlot,
                                     usize drawIndex,
                                     MaterialResource& material);
        EnvironmentResource* resolveEnvironmentResource(FrameContext& frameContext);
        EnvironmentCubeResource* resolveIrradianceResource(FrameContext& frameContext);
        EnvironmentCubeResource* resolvePrefilteredSpecularResource(FrameContext& frameContext);
        EnvironmentBrdfLutResource* resolveBrdfLutResource(FrameContext& frameContext);
        bool drawMeshItem(FrameContext& frameContext,
                          u32 frameSlot,
                          usize drawIndex,
                          MeshResource& mesh,
                          MaterialResource& material,
                          const glm::mat4& modelMatrix);

        struct DrawDescriptorResources {
            Scope<rhi::Buffer> objectBuffer;
            Scope<rhi::Buffer> materialBuffer;
            Scope<rhi::DescriptorSet> descriptorSet;
        };

        struct ForwardPipelineKey {
            rhi::Format colorFormat = rhi::Format::Unknown;
            rhi::Format depthFormat = rhi::Format::Unknown;
            asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
            bool doubleSided = false;

            bool operator<(const ForwardPipelineKey& other) const {
                if (colorFormat != other.colorFormat) {
                    return static_cast<int>(colorFormat) < static_cast<int>(other.colorFormat);
                }
                if (depthFormat != other.depthFormat) {
                    return static_cast<int>(depthFormat) < static_cast<int>(other.depthFormat);
                }
                if (alphaMode != other.alphaMode) {
                    return static_cast<int>(alphaMode) < static_cast<int>(other.alphaMode);
                }
                return doubleSided < other.doubleSided;
            }
        };

        rhi::RenderDevice* m_Device = nullptr;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_CameraBuffers;
        std::array<Scope<rhi::Buffer>, FramesInFlight> m_LightingBuffers;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<std::vector<DrawDescriptorResources>, FramesInFlight> m_DrawDescriptors;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        std::map<ForwardPipelineKey, Scope<rhi::PipelineState>> m_Pipelines;
        EnvironmentResource m_FallbackEnvironment;
        EnvironmentCubeResource m_FallbackIrradianceCube;
        EnvironmentCubeResource m_FallbackSpecularCube;
        EnvironmentBrdfLutResource m_FallbackBrdfLut;
    };
} // namespace ark
