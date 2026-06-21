#include "renderer/effects/tone_mapping/ToneMappingPass.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderView.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"
#include "rhi/TextureView.h"

#include <cstddef>

namespace ark {
    namespace {
        constexpr float DefaultOutputGamma = 2.2f;

        struct alignas(16) ToneMappingUniform {
            float exposure = 1.0f;
            float inverseOutputGamma = 1.0f / DefaultOutputGamma;
            float operatorType = 0.0f;
            float padding1 = 0.0f;
        };

        static_assert(sizeof(ToneMappingUniform) == 16);

        ToneMappingUniform makeToneMappingUniform(const FrameContext& frameContext) {
            const ToneMappingSettings defaultSettings{};
            const ToneMappingSettings& settings =
                frameContext.view ? frameContext.view->toneMappingSettings() : defaultSettings;

            ToneMappingUniform uniform{};
            uniform.exposure = settings.exposure < 0.0f ? 0.0f : settings.exposure;
            uniform.inverseOutputGamma =
                settings.outputGamma > 0.0f ? 1.0f / settings.outputGamma : 1.0f / DefaultOutputGamma;
            uniform.operatorType = static_cast<float>(settings.operatorType);
            return uniform;
        }
    } // namespace

    ToneMappingPass::~ToneMappingPass() = default;

    void ToneMappingPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool ToneMappingPass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !frameContext.sceneColorView) {
            ARK_ERROR("ToneMappingPass requires DeviceContext and scene color view");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!m_DescriptorSets[frameSlot] || !m_UniformBuffers[frameSlot] || !m_Sampler) {
            ARK_ERROR("ToneMappingPass requires descriptor resources");
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreatePipeline(frameContext);
        if (!pipeline) {
            return false;
        }

        if (m_BoundSceneColorViews[frameSlot] != frameContext.sceneColorView) {
            rhi::SampledImageDescriptor sceneColorDescriptor{};
            sceneColorDescriptor.view = frameContext.sceneColorView;
            m_DescriptorSets[frameSlot]->updateSampledImage(0, sceneColorDescriptor);

            rhi::SamplerDescriptor samplerDescriptor{};
            samplerDescriptor.sampler = m_Sampler.get();
            m_DescriptorSets[frameSlot]->updateSampler(1, samplerDescriptor);
            m_BoundSceneColorViews[frameSlot] = frameContext.sceneColorView;
        }

        const ToneMappingUniform uniform = makeToneMappingUniform(frameContext);
        if (!frameContext.context->updateBuffer(*m_UniformBuffers[frameSlot], &uniform, sizeof(uniform))) {
            return false;
        }

        frameContext.context->setPipeline(*pipeline);
        frameContext.context->bindDescriptorSet(0, *m_DescriptorSets[frameSlot]);

        rhi::DrawDesc drawDesc{};
        drawDesc.vertexCount = 3;
        frameContext.context->draw(drawDesc);
        return true;
    }

    bool ToneMappingPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("ToneMappingPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ToneMappingDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::SampledImage,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 1,
            .type = rhi::DescriptorType::Sampler,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 2,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });

        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        for (std::size_t frameSlot = 0; frameSlot < m_DescriptorSets.size(); ++frameSlot) {
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "ToneMappingUniformBuffer";
            uniformBufferDesc.size = sizeof(ToneMappingUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_UniformBuffers[frameSlot] = m_Device->createBuffer(uniformBufferDesc);

            m_DescriptorSets[frameSlot] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_UniformBuffers[frameSlot] || !m_DescriptorSets[frameSlot]) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = m_UniformBuffers[frameSlot].get();
            uniformDescriptor.range = sizeof(ToneMappingUniform);
            m_DescriptorSets[frameSlot]->updateUniformBuffer(2, uniformDescriptor);
        }

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = "ToneMappingSceneColorSampler";
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.mipFilter = rhi::FilterMode::Linear;
        samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
        m_Sampler = m_Device->createSampler(samplerDesc);
        return m_Sampler != nullptr;
    }

    bool ToneMappingPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("ToneMappingPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "ToneMappingVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("tonemap.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "ToneMappingFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("tonemap.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool ToneMappingPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("ToneMappingPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ToneMappingPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    rhi::PipelineState* ToneMappingPass::getOrCreatePipeline(FrameContext& frameContext) {
        if (!m_Device) {
            ARK_ERROR("ToneMappingPass requires RenderDevice");
            return nullptr;
        }

        const rhi::Format colorFormat = frameContext.colorFormat != rhi::Format::Unknown
                                            ? frameContext.colorFormat
                                            : frameContext.swapChain ? frameContext.swapChain->getDesc().colorFormat
                                                                     : rhi::Format::Unknown;
        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("ToneMappingPass requires a valid color attachment format");
            return nullptr;
        }

        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("ToneMappingPass requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "ToneMappingPipeline";
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
