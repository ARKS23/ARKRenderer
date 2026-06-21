#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"

#include <string>

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
    class EnvironmentBrdfLutResource;

    struct EnvironmentBrdfLutGenerationDesc {
        EnvironmentBrdfLutResource* target = nullptr;
        u32 sampleCount = 1024;
        std::string debugName;
    };

    class EnvironmentBrdfLutGenerator final {
    public:
        EnvironmentBrdfLutGenerator() = default;
        ~EnvironmentBrdfLutGenerator();

        void setup(rhi::RenderDevice& device);
        void resetImmediate();
        bool generate(rhi::DeviceContext& context, const EnvironmentBrdfLutGenerationDesc& desc);

    private:
        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        Scope<rhi::Buffer> m_UniformBuffer;
        Scope<rhi::DescriptorSet> m_DescriptorSet;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
