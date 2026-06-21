#include "renderer/effects/ibl/EnvironmentBrdfLutGenerator.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/EnvironmentBrdfLutResource.h"
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

#include <algorithm>
#include <array>

namespace ark {
    namespace {
        constexpr u32 MinSampleCount = 16;
        constexpr u32 MaxSampleCount = 4096;

        struct alignas(16) BrdfLutUniform {
            u32 sampleCount = 1024;
            u32 padding0 = 0;
            u32 padding1 = 0;
            u32 padding2 = 0;
        };

        static_assert(sizeof(BrdfLutUniform) == 16);

        u32 clampSampleCount(u32 sampleCount) {
            return std::clamp(sampleCount, MinSampleCount, MaxSampleCount);
        }

        void setViewportAndScissor(rhi::DeviceContext& context, rhi::Extent2D extent) {
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

    EnvironmentBrdfLutGenerator::~EnvironmentBrdfLutGenerator() = default;

    void EnvironmentBrdfLutGenerator::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    void EnvironmentBrdfLutGenerator::resetImmediate() {
        m_Pipeline.reset();
        m_PipelineColorFormat = rhi::Format::Unknown;
        m_PipelineLayout.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_DescriptorSet.reset();
        m_UniformBuffer.reset();
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
    }

    bool EnvironmentBrdfLutGenerator::generate(rhi::DeviceContext& context,
                                               const EnvironmentBrdfLutGenerationDesc& desc) {
        const std::string debugName = desc.debugName.empty() ? "EnvironmentBrdfLutGeneration" : desc.debugName;

        if (!desc.target) {
            ARK_ERROR("{} requires target EnvironmentBrdfLutResource", debugName);
            return false;
        }

        if (!desc.target->isValid() || !desc.target->texture() || !desc.target->renderTargetView()) {
            ARK_ERROR("{} requires a valid target BRDF LUT resource", debugName);
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreatePipeline(desc.target->format());
        if (!pipeline) {
            return false;
        }

        const rhi::Extent2D extent = desc.target->extent();
        if (!rhi::isValidExtent(extent)) {
            ARK_ERROR("{} requires a valid target extent", debugName);
            return false;
        }

        if (!m_DescriptorSet || !m_UniformBuffer) {
            ARK_ERROR("{} requires descriptor resources", debugName);
            return false;
        }

        const BrdfLutUniform uniform{
            .sampleCount = clampSampleCount(desc.sampleCount),
        };
        if (!context.updateBuffer(*m_UniformBuffer, &uniform, sizeof(uniform))) {
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

        rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = extent;
        renderingDesc.colorAttachment.view = desc.target->renderTargetView();
        renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

        if (!context.beginRendering(renderingDesc)) {
            return false;
        }

        setViewportAndScissor(context, extent);
        context.setPipeline(*pipeline);
        context.bindDescriptorSet(0, *m_DescriptorSet);

        rhi::DrawDesc drawDesc{};
        drawDesc.vertexCount = 3;
        context.draw(drawDesc);

        context.endRendering();

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

    bool EnvironmentBrdfLutGenerator::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "BrdfLutDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        rhi::BufferDesc uniformBufferDesc{};
        uniformBufferDesc.debugName = "BrdfLutUniformBuffer";
        uniformBufferDesc.size = sizeof(BrdfLutUniform);
        uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
        uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        m_UniformBuffer = m_Device->createBuffer(uniformBufferDesc);

        m_DescriptorSet = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
        if (!m_UniformBuffer || !m_DescriptorSet) {
            return false;
        }

        rhi::BufferDescriptor uniformDescriptor{};
        uniformDescriptor.buffer = m_UniformBuffer.get();
        uniformDescriptor.range = sizeof(BrdfLutUniform);
        m_DescriptorSet->updateUniformBuffer(0, uniformDescriptor);
        return true;
    }

    bool EnvironmentBrdfLutGenerator::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "BrdfLutVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("brdf_lut.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "BrdfLutFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("brdf_lut.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool EnvironmentBrdfLutGenerator::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "BrdfLutPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    rhi::PipelineState* EnvironmentBrdfLutGenerator::getOrCreatePipeline(rhi::Format colorFormat) {
        if (!m_Device) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires RenderDevice");
            return nullptr;
        }

        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires a valid color attachment format");
            return nullptr;
        }

        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("EnvironmentBrdfLutGenerator requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "BrdfLutPipeline";
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
