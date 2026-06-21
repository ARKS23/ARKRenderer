#include "renderer/effects/ibl/EnvironmentSpecularPrefilterGenerator.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/EnvironmentCubeResource.h"
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
#include <cstddef>

namespace ark {
    namespace {
        constexpr u32 MinSampleCount = 16;
        constexpr u32 MaxSampleCount = 1024;

        struct alignas(16) SpecularPrefilterUniform {
            u32 faceIndex = 0;
            u32 mipLevel = 0;
            u32 sampleCount = 128;
            float roughness = 0.0f;
        };

        static_assert(sizeof(SpecularPrefilterUniform) == 16);

        u32 faceMipIndex(u32 faceIndex, u32 mipLevel) {
            return mipLevel * EnvironmentCubeResource::FaceCount + faceIndex;
        }

        u32 clampSampleCount(u32 sampleCount) {
            return std::clamp(sampleCount, MinSampleCount, MaxSampleCount);
        }

        float roughnessForMip(u32 mipLevel, u32 mipLevels) {
            if (mipLevels <= 1) {
                return 0.0f;
            }

            return static_cast<float>(mipLevel) / static_cast<float>(mipLevels - 1);
        }

        void setMipViewportAndScissor(rhi::DeviceContext& context, rhi::Extent2D extent) {
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

    EnvironmentSpecularPrefilterGenerator::~EnvironmentSpecularPrefilterGenerator() = default;

    void EnvironmentSpecularPrefilterGenerator::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorLayout();
        createShaderResources();
        createPipelineResources();
    }

    void EnvironmentSpecularPrefilterGenerator::resetImmediate() {
        m_Pipeline.reset();
        m_PipelineColorFormat = rhi::Format::Unknown;
        m_PipelineLayout.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_DescriptorSets.clear();
        m_UniformBuffers.clear();
        m_FaceMipResourceCount = 0;
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
    }

    bool EnvironmentSpecularPrefilterGenerator::generate(rhi::DeviceContext& context,
                                                         const EnvironmentSpecularPrefilterDesc& desc) {
        const std::string debugName =
            desc.debugName.empty() ? "EnvironmentSpecularPrefilter" : desc.debugName;

        if (!desc.source || !desc.target) {
            ARK_ERROR("{} requires source and target EnvironmentCubeResource", debugName);
            return false;
        }

        if (desc.source == desc.target) {
            ARK_ERROR("{} source and target cubemaps must be different resources", debugName);
            return false;
        }

        if (!desc.source->isValid() || !desc.source->textureView() || !desc.source->sampler()) {
            ARK_ERROR("{} requires a valid source cubemap", debugName);
            return false;
        }

        if (!desc.source->texture() || desc.source->texture()->getState() != rhi::ResourceState::ShaderResource) {
            ARK_ERROR("{} requires source cubemap in ShaderResource state", debugName);
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

        const u32 mipLevels = desc.target->mipLevels();
        if (mipLevels == 0) {
            ARK_ERROR("{} requires at least one target mip", debugName);
            return false;
        }

        const u32 faceMipCount = FaceCount * mipLevels;
        if (!ensureFaceMipResources(faceMipCount)) {
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

        const u32 sampleCount = clampSampleCount(desc.sampleCount);
        for (u32 mipLevel = 0; mipLevel < mipLevels; ++mipLevel) {
            const rhi::Extent2D mipExtent = desc.target->mipExtent(mipLevel);
            if (!rhi::isValidExtent(mipExtent)) {
                ARK_ERROR("{} target mip {} extent is invalid", debugName, mipLevel);
                return false;
            }

            const float roughness = roughnessForMip(mipLevel, mipLevels);
            for (u32 faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
                rhi::TextureView* faceMipView = desc.target->faceMipRenderTargetView(faceIndex, mipLevel);
                if (!faceMipView) {
                    ARK_ERROR("{} target face={} mip={} view is missing", debugName, faceIndex, mipLevel);
                    return false;
                }

                const u32 resourceIndex = faceMipIndex(faceIndex, mipLevel);
                if (resourceIndex >= m_FaceMipResourceCount ||
                    !m_DescriptorSets[resourceIndex] ||
                    !m_UniformBuffers[resourceIndex]) {
                    ARK_ERROR("{} requires descriptor resources for face={} mip={}",
                              debugName,
                              faceIndex,
                              mipLevel);
                    return false;
                }

                rhi::SampledImageDescriptor sourceImageDescriptor{};
                sourceImageDescriptor.view = desc.source->textureView();
                m_DescriptorSets[resourceIndex]->updateSampledImage(1, sourceImageDescriptor);

                rhi::SamplerDescriptor sourceSamplerDescriptor{};
                sourceSamplerDescriptor.sampler = desc.source->sampler();
                m_DescriptorSets[resourceIndex]->updateSampler(2, sourceSamplerDescriptor);

                const SpecularPrefilterUniform uniform{
                    .faceIndex = faceIndex,
                    .mipLevel = mipLevel,
                    .sampleCount = sampleCount,
                    .roughness = roughness,
                };
                if (!context.updateBuffer(*m_UniformBuffers[resourceIndex], &uniform, sizeof(uniform))) {
                    return false;
                }

                rhi::RenderingDesc renderingDesc{};
                renderingDesc.extent = mipExtent;
                renderingDesc.colorAttachment.view = faceMipView;
                renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
                renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
                renderingDesc.colorAttachment.clearColor = rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

                if (!context.beginRendering(renderingDesc)) {
                    return false;
                }

                setMipViewportAndScissor(context, mipExtent);
                context.setPipeline(*pipeline);
                context.bindDescriptorSet(0, *m_DescriptorSets[resourceIndex]);

                rhi::DrawDesc drawDesc{};
                drawDesc.vertexCount = 3;
                context.draw(drawDesc);

                context.endRendering();
            }
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

    bool EnvironmentSpecularPrefilterGenerator::createDescriptorLayout() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires device for descriptor layout");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SpecularPrefilterDescriptorSetLayout";
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
        return m_DescriptorSetLayout != nullptr;
    }

    bool EnvironmentSpecularPrefilterGenerator::ensureFaceMipResources(u32 faceMipCount) {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires device and descriptor layout");
            return false;
        }

        if (faceMipCount == 0) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires at least one face mip resource");
            return false;
        }

        if (m_FaceMipResourceCount == faceMipCount &&
            m_UniformBuffers.size() == faceMipCount &&
            m_DescriptorSets.size() == faceMipCount) {
            return true;
        }

        m_UniformBuffers.clear();
        m_DescriptorSets.clear();
        m_FaceMipResourceCount = 0;
        m_UniformBuffers.resize(faceMipCount);
        m_DescriptorSets.resize(faceMipCount);

        for (u32 resourceIndex = 0; resourceIndex < faceMipCount; ++resourceIndex) {
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "SpecularPrefilterUniformBuffer";
            uniformBufferDesc.size = sizeof(SpecularPrefilterUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_UniformBuffers[resourceIndex] = m_Device->createBuffer(uniformBufferDesc);

            m_DescriptorSets[resourceIndex] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_UniformBuffers[resourceIndex] || !m_DescriptorSets[resourceIndex]) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = m_UniformBuffers[resourceIndex].get();
            uniformDescriptor.range = sizeof(SpecularPrefilterUniform);
            m_DescriptorSets[resourceIndex]->updateUniformBuffer(0, uniformDescriptor);
        }

        m_FaceMipResourceCount = faceMipCount;
        return true;
    }

    bool EnvironmentSpecularPrefilterGenerator::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "SpecularPrefilterVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("specular_prefilter.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "SpecularPrefilterFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("specular_prefilter.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool EnvironmentSpecularPrefilterGenerator::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SpecularPrefilterPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    rhi::PipelineState* EnvironmentSpecularPrefilterGenerator::getOrCreatePipeline(rhi::Format colorFormat) {
        if (!m_Device) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires RenderDevice");
            return nullptr;
        }

        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires a valid color attachment format");
            return nullptr;
        }

        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("EnvironmentSpecularPrefilterGenerator requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "SpecularPrefilterPipeline";
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
