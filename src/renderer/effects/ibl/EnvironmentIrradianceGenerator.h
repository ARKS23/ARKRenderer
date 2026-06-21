#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"

#include <array>
#include <string>

namespace ark::rhi {
    class Buffer;
    class DescriptorSet;
    class DescriptorSetLayout;
    class DeviceContext;
    class PipelineLayout;
    class PipelineState;
    class RenderDevice;
    class Sampler;
    class Shader;
    class TextureView;
} // namespace ark::rhi

namespace ark {
    class EnvironmentCubeResource;

    struct EnvironmentIrradianceGenerationDesc {
        EnvironmentCubeResource* source = nullptr;
        EnvironmentCubeResource* target = nullptr;
        float sampleDelta = 0.025f;
        std::string debugName;
    };

    class EnvironmentIrradianceGenerator final {
    public:
        EnvironmentIrradianceGenerator() = default;
        ~EnvironmentIrradianceGenerator();

        void setup(rhi::RenderDevice& device);
        void resetImmediate();
        bool generate(rhi::DeviceContext& context, const EnvironmentIrradianceGenerationDesc& desc);

    private:
        static constexpr u32 FaceCount = 6;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::Buffer>, FaceCount> m_UniformBuffers;
        std::array<Scope<rhi::DescriptorSet>, FaceCount> m_DescriptorSets;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
