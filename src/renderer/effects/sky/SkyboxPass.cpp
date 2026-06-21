#include "renderer/effects/sky/SkyboxPass.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/TextureView.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstddef>

namespace ark {
    namespace {
        struct alignas(16) SkyboxUniform {
            glm::mat4 inverseProjection{1.0f};
            glm::mat4 inverseViewRotation{1.0f};
            glm::vec4 settings{1.0f, 0.0f, 0.0f, 0.0f};
        };

        static_assert(sizeof(SkyboxUniform) == 144);

        SkyboxUniform makeSkyboxUniform(const FrameContext& frameContext) {
            SkyboxUniform uniform{};
            if (frameContext.view) {
                const glm::mat4 projection = frameContext.view->projectionMatrix();
                glm::mat4 viewRotation = frameContext.view->viewMatrix();
                viewRotation[3] = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
                uniform.inverseProjection = glm::inverse(projection);
                uniform.inverseViewRotation = glm::inverse(viewRotation);
            }

            if (frameContext.scene && frameContext.scene->environment().isEnabled()) {
                uniform.settings.x = frameContext.scene->environment().intensity;
            }

            if (uniform.settings.x < 0.0f) {
                uniform.settings.x = 0.0f;
            }

            return uniform;
        }
    } // namespace

    SkyboxPass::~SkyboxPass() = default;

    void SkyboxPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool SkyboxPass::execute(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("SkyboxPass requires DeviceContext");
            return false;
        }

        EnvironmentCubeResource* environmentCube = frameContext.environmentCube;
        if (!environmentCube) {
            return true;
        }

        if (!environmentCube->isValid() || !environmentCube->textureView() || !environmentCube->sampler()) {
            return true;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!m_DescriptorSets[frameSlot] || !m_UniformBuffers[frameSlot]) {
            ARK_ERROR("SkyboxPass requires descriptor resources");
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreatePipeline(frameContext);
        if (!pipeline) {
            return false;
        }

        if (m_BoundCubeViews[frameSlot] != environmentCube->textureView() ||
            m_BoundSamplers[frameSlot] != environmentCube->sampler()) {
            rhi::SampledImageDescriptor cubeDescriptor{};
            cubeDescriptor.view = environmentCube->textureView();
            m_DescriptorSets[frameSlot]->updateSampledImage(1, cubeDescriptor);

            rhi::SamplerDescriptor samplerDescriptor{};
            samplerDescriptor.sampler = environmentCube->sampler();
            m_DescriptorSets[frameSlot]->updateSampler(2, samplerDescriptor);

            m_BoundCubeViews[frameSlot] = environmentCube->textureView();
            m_BoundSamplers[frameSlot] = environmentCube->sampler();
        }

        const SkyboxUniform uniform = makeSkyboxUniform(frameContext);
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

    bool SkyboxPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("SkyboxPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SkyboxDescriptorSetLayout";
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

        for (std::size_t frameSlot = 0; frameSlot < m_DescriptorSets.size(); ++frameSlot) {
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "SkyboxUniformBuffer";
            uniformBufferDesc.size = sizeof(SkyboxUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_UniformBuffers[frameSlot] = m_Device->createBuffer(uniformBufferDesc);

            m_DescriptorSets[frameSlot] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_UniformBuffers[frameSlot] || !m_DescriptorSets[frameSlot]) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = m_UniformBuffers[frameSlot].get();
            uniformDescriptor.range = sizeof(SkyboxUniform);
            m_DescriptorSets[frameSlot]->updateUniformBuffer(0, uniformDescriptor);
        }

        return true;
    }

    bool SkyboxPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("SkyboxPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "SkyboxVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("skybox.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "SkyboxFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("skybox.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool SkyboxPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("SkyboxPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SkyboxPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    rhi::PipelineState* SkyboxPass::getOrCreatePipeline(FrameContext& frameContext) {
        if (!m_Device) {
            ARK_ERROR("SkyboxPass requires RenderDevice");
            return nullptr;
        }

        const rhi::Format colorFormat = frameContext.colorFormat;
        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("SkyboxPass requires a valid color attachment format");
            return nullptr;
        }

        const rhi::Format depthFormat = frameContext.depthFormat;
        if (m_Pipeline && m_PipelineColorFormat == colorFormat && m_PipelineDepthFormat == depthFormat) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("SkyboxPass requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "SkyboxPipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = false;
        pipelineDesc.depthStencilState.enableDepthWrite = false;
        pipelineDesc.colorFormat = colorFormat;
        pipelineDesc.depthFormat = depthFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        m_PipelineDepthFormat = depthFormat;
        return m_Pipeline.get();
    }
} // namespace ark
