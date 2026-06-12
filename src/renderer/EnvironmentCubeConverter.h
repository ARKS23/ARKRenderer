#pragma once

#include "core/Memory.h"
#include "core/Types.h"
#include "rhi/RHICommon.h"

#include <array>
#include <string>

namespace ark {
    class EnvironmentCubeResource;
    class EnvironmentResource;
} // namespace ark

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
    struct EnvironmentCubeConversionDesc {
        EnvironmentResource* source = nullptr;
        EnvironmentCubeResource* target = nullptr;
        std::string debugName;
    };

    class EnvironmentCubeConverter final {
    public:
        EnvironmentCubeConverter() = default;
        ~EnvironmentCubeConverter();

        void setup(rhi::RenderDevice& device);
        void resetImmediate();
        bool convert(rhi::DeviceContext& context, const EnvironmentCubeConversionDesc& desc);

    private:
        static constexpr u32 FaceCount = 6;

        bool createDescriptorResources();
        bool createShaderResources();
        bool createPipelineResources();
        rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);

        rhi::RenderDevice* m_Device = nullptr;
        Scope<rhi::DescriptorSetLayout> m_DescriptorSetLayout;
        std::array<Scope<rhi::DescriptorSet>, FaceCount> m_DescriptorSets;
        std::array<Scope<rhi::Buffer>, FaceCount> m_UniformBuffers;
        Scope<rhi::Shader> m_VertexShader;
        Scope<rhi::Shader> m_FragmentShader;
        Scope<rhi::PipelineLayout> m_PipelineLayout;
        Scope<rhi::PipelineState> m_Pipeline;
        rhi::Format m_PipelineColorFormat = rhi::Format::Unknown;
    };
} // namespace ark
