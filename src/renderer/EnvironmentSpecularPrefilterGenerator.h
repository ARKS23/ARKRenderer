#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"

#include <string>
#include <vector>

namespace ark::rhi {
    class Buffer;
    class DescriptorSet;
    class DescriptorSetLayout;
    class DeviceContext;
    class PipelineLayout;
    class PipelineState;
    class RenderDevice;
    class Shader;
} // namespace ark::rhi

namespace ark {
    class EnvironmentCubeResource;

    struct EnvironmentSpecularPrefilterDesc {
        EnvironmentCubeResource* source = nullptr;
        EnvironmentCubeResource* target = nullptr;
        u32 sampleCount = 128;
        std::string debugName;
    };

    class EnvironmentSpecularPrefilterGenerator final {
    public:
        EnvironmentSpecularPrefilterGenerator() = default;
        ~EnvironmentSpecularPrefilterGenerator();

        void setup(rhi::RenderDevice& device);
        void resetImmediate();
        bool generate(rhi::DeviceContext& context, const EnvironmentSpecularPrefilterDesc& desc);

    private:
        static constexpr u32 FaceCount = 6;

        bool createDescriptorLayout();
        bool ensureFaceMipResources(u32 faceMipCount);
        bool createShaderResources();
        bool createPipelineResources();
        rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::vector<Scope<rhi::Buffer>> m_UniformBuffers;
        std::vector<Scope<rhi::DescriptorSet>> m_DescriptorSets;
        u32 m_FaceMipResourceCount = 0;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
