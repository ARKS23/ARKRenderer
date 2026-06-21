#include "renderer/effects/ibl/EnvironmentCubeConverter.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <array>
#include <cstddef>

namespace ark {
    namespace {
        struct alignas(16) EquirectToCubeUniform {
            u32 faceIndex = 0;
            float outputResolution = 0.0f;
            float padding0 = 0.0f;
            float padding1 = 0.0f;
        };

        static_assert(sizeof(EquirectToCubeUniform) == 16);

        EquirectToCubeUniform makeConversionUniform(u32 faceIndex, rhi::Extent2D faceExtent) {
            EquirectToCubeUniform uniform{};
            uniform.faceIndex = faceIndex;
            uniform.outputResolution = static_cast<float>(faceExtent.width);
            return uniform;
        }

        void setFaceViewportAndScissor(rhi::DeviceContext& context, rhi::Extent2D extent) {
            rhi::Viewport viewport{};
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            context.setViewport(viewport);

            rhi::ScissorRect scissor{};
            scissor.width = extent.width;
            scissor.height = extent.height;
            context.setScissorRect(scissor);
        }
    } // namespace

    EnvironmentCubeConverter::~EnvironmentCubeConverter() = default;

    void EnvironmentCubeConverter::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    void EnvironmentCubeConverter::resetImmediate() {
        m_Pipeline.reset();
        m_PipelineColorFormat = rhi::Format::Unknown;
        m_PipelineLayout.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        for (Scope<rhi::DescriptorSet>& descriptorSet : m_DescriptorSets) {
            descriptorSet.reset();
        }
        for (Scope<rhi::Buffer>& uniformBuffer : m_UniformBuffers) {
            uniformBuffer.reset();
        }
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
    }

    bool EnvironmentCubeConverter::convert(rhi::DeviceContext& context, const EnvironmentCubeConversionDesc& desc) {
        const std::string debugName = desc.debugName.empty() ? "EnvironmentCubeConversion" : desc.debugName;

        if (!desc.source || !desc.target) {
            ARK_ERROR("{} requires source EnvironmentResource and target EnvironmentCubeResource", debugName);
            return false;
        }

        if (!desc.source->isReady() || !desc.source->textureView() || !desc.source->sampler()) {
            ARK_ERROR("{} requires uploaded source environment", debugName);
            return false;
        }

        if (!desc.target->isValid() || !desc.target->texture()) {
            ARK_ERROR("{} requires a valid target cubemap", debugName);
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreatePipeline(desc.target->format());
        if (!pipeline) {
            return false;
        }

        const rhi::Extent2D faceExtent = desc.target->faceExtent();
        if (!rhi::isValidExtent(faceExtent)) {
            ARK_ERROR("{} requires a valid target face extent", debugName);
            return false;
        }

        const std::array<rhi::ResourceBarrier, 1> toRenderTarget{{
            rhi::ResourceBarrier{
                .texture = desc.target->texture(),
                .before = desc.target->texture()->getState(),
                .after = rhi::ResourceState::RenderTarget,
            },
        }};
        context.pipelineBarrier(toRenderTarget);

        for (u32 faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
            rhi::TextureView* faceView = desc.target->faceRenderTargetView(faceIndex);
            if (!faceView) {
                ARK_ERROR("{} target face view {} is missing", debugName, faceIndex);
                return false;
            }

            if (!m_DescriptorSets[faceIndex] || !m_UniformBuffers[faceIndex]) {
                ARK_ERROR("{} requires descriptor resources for face {}", debugName, faceIndex);
                return false;
            }

            rhi::SampledImageDescriptor sourceImageDescriptor{};
            sourceImageDescriptor.view = desc.source->textureView();
            m_DescriptorSets[faceIndex]->updateSampledImage(1, sourceImageDescriptor);

            rhi::SamplerDescriptor sourceSamplerDescriptor{};
            sourceSamplerDescriptor.sampler = desc.source->sampler();
            m_DescriptorSets[faceIndex]->updateSampler(2, sourceSamplerDescriptor);

            const EquirectToCubeUniform uniform = makeConversionUniform(faceIndex, faceExtent);
            if (!context.updateBuffer(*m_UniformBuffers[faceIndex], &uniform, sizeof(uniform))) {
                return false;
            }

            rhi::RenderingDesc renderingDesc{};
            renderingDesc.extent = faceExtent;
            renderingDesc.colorAttachment.view = faceView;
            renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
            renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
            renderingDesc.colorAttachment.clearColor = rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

            if (!context.beginRendering(renderingDesc)) {
                return false;
            }

            setFaceViewportAndScissor(context, faceExtent);
            context.setPipeline(*pipeline);
            context.bindDescriptorSet(0, *m_DescriptorSets[faceIndex]);

            rhi::DrawDesc drawDesc{};
            drawDesc.vertexCount = 3;
            context.draw(drawDesc);

            context.endRendering();
        }

        const std::array<rhi::ResourceBarrier, 1> toShaderResource{{
            rhi::ResourceBarrier{
                .texture = desc.target->texture(),
                .before = desc.target->texture()->getState(),
                .after = rhi::ResourceState::ShaderResource,
            },
        }};
        context.pipelineBarrier(toShaderResource);

        return true;
    }

    bool EnvironmentCubeConverter::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentCubeConverter requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "EquirectToCubeDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 1,
            .type = rhi::DescriptorType::SampledImage,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 2,
            .type = rhi::DescriptorType::Sampler,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });

        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        for (std::size_t faceIndex = 0; faceIndex < m_DescriptorSets.size(); ++faceIndex) {
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "EquirectToCubeUniformBuffer";
            uniformBufferDesc.size = sizeof(EquirectToCubeUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_UniformBuffers[faceIndex] = m_Device->createBuffer(uniformBufferDesc);

            m_DescriptorSets[faceIndex] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_UniformBuffers[faceIndex] || !m_DescriptorSets[faceIndex]) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = m_UniformBuffers[faceIndex].get();
            uniformDescriptor.range = sizeof(EquirectToCubeUniform);
            m_DescriptorSets[faceIndex]->updateUniformBuffer(0, uniformDescriptor);
        }

        return true;
    }

    bool EnvironmentCubeConverter::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentCubeConverter requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "EquirectToCubeVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("equirect_to_cube.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "EquirectToCubeFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("equirect_to_cube.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool EnvironmentCubeConverter::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("EnvironmentCubeConverter requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "EquirectToCubePipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    rhi::PipelineState* EnvironmentCubeConverter::getOrCreatePipeline(rhi::Format colorFormat) {
        if (!m_Device) {
            ARK_ERROR("EnvironmentCubeConverter requires RenderDevice");
            return nullptr;
        }

        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("EnvironmentCubeConverter requires a valid color attachment format");
            return nullptr;
        }

        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("EnvironmentCubeConverter requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "EquirectToCubePipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = false;
        pipelineDesc.depthStencilState.enableDepthWrite = false;
        pipelineDesc.colorFormat = colorFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        return m_Pipeline.get();
    }
} // namespace ark
