#include "renderer/passes/ForwardPass.h"

#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderView.h"
#include "rhi/DeviceContext.h"
#include "rhi/SwapChain.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace ark {
    namespace {
        struct alignas(16) CameraUniform {
            glm::mat4 view;
            glm::mat4 projection;
        };

        struct alignas(16) ObjectUniform {
            glm::mat4 model;
            glm::mat4 normalMatrix;
        };

        static_assert(sizeof(ObjectUniform) == 128);

        struct alignas(16) MaterialUniform {
            glm::vec4 baseColorFactor;
            glm::vec4 emissiveFactor;
            float metallicFactor = 1.0f;
            float roughnessFactor = 1.0f;
            float normalScale = 1.0f;
            float occlusionStrength = 1.0f;
        };

        static_assert(sizeof(MaterialUniform) == 48);

        struct alignas(16) LightingUniform {
            glm::vec4 lightDirection;
            glm::vec4 lightColor;
            glm::vec4 ambientColor;
            glm::vec4 cameraPosition;
        };

        static_assert(sizeof(LightingUniform) == 64);

        void addFragmentTextureBindingPair(rhi::DescriptorSetLayoutDesc& desc, u32 imageBinding, u32 samplerBinding) {
            desc.bindings.push_back(rhi::DescriptorBindingDesc{
                .binding = imageBinding,
                .type = rhi::DescriptorType::SampledImage,
                .count = 1,
                .stages = rhi::ShaderStageFlags::Fragment,
            });
            desc.bindings.push_back(rhi::DescriptorBindingDesc{
                .binding = samplerBinding,
                .type = rhi::DescriptorType::Sampler,
                .count = 1,
                .stages = rhi::ShaderStageFlags::Fragment,
            });
        }

        CameraUniform makeCameraUniform(const FrameContext& frameContext) {
            CameraUniform uniform{};
            if (frameContext.view) {
                uniform.view = frameContext.view->viewMatrix();
                uniform.projection = frameContext.view->projectionMatrix();
                return uniform;
            }

            uniform.view = glm::mat4{1.0f};
            uniform.projection = glm::mat4{1.0f};
            return uniform;
        }

        LightingUniform makeLightingUniform(const FrameContext& frameContext) {
            LightingUniform uniform{};
            uniform.lightDirection = glm::vec4{glm::normalize(glm::vec3{-0.35f, -0.8f, -0.45f}), 0.0f};
            uniform.lightColor = glm::vec4{1.0f, 0.96f, 0.88f, 1.0f};
            uniform.ambientColor = glm::vec4{0.08f, 0.09f, 0.11f, 1.0f};
            uniform.cameraPosition = glm::vec4{0.0f, 0.0f, -4.0f, 1.0f};

            if (frameContext.view) {
                const glm::mat4 inverseView = glm::affineInverse(frameContext.view->viewMatrix());
                uniform.cameraPosition = inverseView[3];
            }

            return uniform;
        }

        MaterialUniform makeMaterialUniform(const MaterialResource& material) {
            const MaterialFactors& factors = material.factors();

            MaterialUniform uniform{};
            uniform.baseColorFactor = glm::vec4{
                factors.baseColorFactor[0],
                factors.baseColorFactor[1],
                factors.baseColorFactor[2],
                factors.baseColorFactor[3],
            };
            uniform.emissiveFactor = glm::vec4{
                factors.emissiveFactor[0],
                factors.emissiveFactor[1],
                factors.emissiveFactor[2],
                0.0f,
            };
            uniform.metallicFactor = factors.metallicFactor;
            uniform.roughnessFactor = factors.roughnessFactor;
            uniform.normalScale = factors.normalScale;
            uniform.occlusionStrength = factors.occlusionStrength;
            return uniform;
        }
    } // namespace

    ForwardPass::~ForwardPass() = default;

    void ForwardPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool ForwardPass::prepare(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("ForwardPass requires DeviceContext");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!ensureDrawDescriptorResources(frameSlot, drawItemCount(frameContext))) {
            return false;
        }

        if (!frameContext.queue || frameContext.queue->empty()) {
            return true;
        }

        usize drawIndex = 0;
        for (const DrawItem& item : frameContext.queue->drawItems()) {
            if (!item.isDrawable()) {
                ARK_ERROR("ForwardPass queue contains an invalid draw item");
                return false;
            }

            if (!item.mesh->upload(*frameContext.context) || !item.material->upload(*frameContext.context)) {
                return false;
            }

            if (!updateDrawDescriptorSet(frameSlot, drawIndex, *item.material)) {
                return false;
            }

            ++drawIndex;
        }

        return true;
    }

    bool ForwardPass::execute(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("ForwardPass requires DeviceContext");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        const usize drawCount = drawItemCount(frameContext);
        if (frameSlot >= m_DrawDescriptors.size() || m_DrawDescriptors[frameSlot].size() < drawCount) {
            ARK_ERROR("ForwardPass draw descriptor resources were not prepared");
            return false;
        }

        if (drawCount == 0) {
            return true;
        }

        if (!updateCameraUniform(frameContext, frameSlot) || !updateLightingUniform(frameContext, frameSlot) ||
            !ensurePipeline(frameContext)) {
            return false;
        }

        frameContext.context->setPipeline(*m_Pipeline);
        if (!frameContext.queue || frameContext.queue->empty()) {
            return true;
        }

        usize drawIndex = 0;
        for (const DrawItem& item : frameContext.queue->drawItems()) {
            if (!item.isDrawable() ||
                !drawMeshItem(frameContext, frameSlot, drawIndex, *item.mesh, *item.material, item.modelMatrix)) {
                return false;
            }

            ++drawIndex;
        }

        return true;
    }

    bool ForwardPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        descriptorSetLayoutDesc.debugName = "ForwardDescriptorSetLayout";
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 1,
            .type = rhi::DescriptorType::SampledImage,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 2,
            .type = rhi::DescriptorType::Sampler,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 3,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 4,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        addFragmentTextureBindingPair(descriptorSetLayoutDesc, 5, 6);
        addFragmentTextureBindingPair(descriptorSetLayoutDesc, 7, 8);
        addFragmentTextureBindingPair(descriptorSetLayoutDesc, 9, 10);
        addFragmentTextureBindingPair(descriptorSetLayoutDesc, 11, 12);
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 13,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(descriptorSetLayoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        for (u32 frameSlot = 0; frameSlot < FramesInFlight; ++frameSlot) {
            rhi::BufferDesc cameraBufferDesc{};
            cameraBufferDesc.debugName = "ForwardCameraUniformBuffer";
            cameraBufferDesc.size = sizeof(CameraUniform);
            cameraBufferDesc.usage = rhi::BufferUsage::Uniform;
            cameraBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_CameraBuffers[frameSlot] = m_Device->createBuffer(cameraBufferDesc);

            rhi::BufferDesc lightingBufferDesc{};
            lightingBufferDesc.debugName = "ForwardLightingUniformBuffer";
            lightingBufferDesc.size = sizeof(LightingUniform);
            lightingBufferDesc.usage = rhi::BufferUsage::Uniform;
            lightingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_LightingBuffers[frameSlot] = m_Device->createBuffer(lightingBufferDesc);

            if (!m_CameraBuffers[frameSlot] || !m_LightingBuffers[frameSlot]) {
                return false;
            }
        }

        return true;
    }

    bool ForwardPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "ForwardMeshVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("mesh.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "ForwardMeshFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("mesh.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool ForwardPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("ForwardPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ForwardPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    usize ForwardPass::drawItemCount(const FrameContext& frameContext) const {
        return frameContext.queue ? frameContext.queue->size() : 0;
    }

    bool ForwardPass::ensureDrawDescriptorResources(u32 frameSlot, usize drawCount) {
        if (!m_Device || !m_DescriptorSetLayout || frameSlot >= m_DrawDescriptors.size()) {
            ARK_ERROR("ForwardPass requires descriptor layout before draw descriptors");
            return false;
        }

        std::vector<DrawDescriptorResources>& descriptors = m_DrawDescriptors[frameSlot];
        while (descriptors.size() < drawCount) {
            const usize drawIndex = descriptors.size();

            DrawDescriptorResources resources{};
            rhi::BufferDesc objectBufferDesc{};
            objectBufferDesc.debugName = "ForwardObjectUniformBuffer." + std::to_string(drawIndex);
            objectBufferDesc.size = sizeof(ObjectUniform);
            objectBufferDesc.usage = rhi::BufferUsage::Uniform;
            objectBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            resources.objectBuffer = m_Device->createBuffer(objectBufferDesc);

            rhi::BufferDesc materialBufferDesc{};
            materialBufferDesc.debugName = "ForwardMaterialUniformBuffer." + std::to_string(drawIndex);
            materialBufferDesc.size = sizeof(MaterialUniform);
            materialBufferDesc.usage = rhi::BufferUsage::Uniform;
            materialBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            resources.materialBuffer = m_Device->createBuffer(materialBufferDesc);
            resources.descriptorSet = m_Device->createDescriptorSet(*m_DescriptorSetLayout);

            if (!resources.objectBuffer || !resources.materialBuffer || !resources.descriptorSet) {
                ARK_ERROR("ForwardPass failed to create draw descriptor resources");
                return false;
            }

            descriptors.push_back(std::move(resources));
        }

        return true;
    }

    bool ForwardPass::ensurePipeline(FrameContext& frameContext) {
        if (!m_Device || !frameContext.swapChain) {
            ARK_ERROR("ForwardPass requires RenderDevice and SwapChain");
            return false;
        }

        const rhi::SwapChainDesc& swapChainDesc = frameContext.swapChain->getDesc();
        const rhi::Format colorFormat = swapChainDesc.colorFormat;
        const rhi::Format depthFormat = swapChainDesc.depthFormat;
        if (m_Pipeline && m_PipelineColorFormat == colorFormat && m_PipelineDepthFormat == depthFormat) {
            return true;
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("ForwardPass requires shader modules and pipeline layout");
            return false;
        }

        rhi::VertexBufferLayoutDesc vertexLayout{};
        vertexLayout.binding = 0;
        vertexLayout.stride = sizeof(asset::MeshVertex);
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 0,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(asset::MeshVertex, position),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 1,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(asset::MeshVertex, normal),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 2,
            .format = rhi::Format::R32G32Float,
            .offset = offsetof(asset::MeshVertex, uv0),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 3,
            .format = rhi::Format::R32G32B32A32Float,
            .offset = offsetof(asset::MeshVertex, tangent),
        });

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "ForwardMeshPipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = true;
        pipelineDesc.depthStencilState.enableDepthWrite = true;
        pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::Less;
        pipelineDesc.colorFormat = colorFormat;
        pipelineDesc.depthFormat = depthFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        m_PipelineDepthFormat = depthFormat;
        return m_Pipeline != nullptr;
    }

    bool ForwardPass::updateCameraUniform(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context || frameSlot >= m_CameraBuffers.size() || !m_CameraBuffers[frameSlot]) {
            ARK_ERROR("ForwardPass requires per-frame camera buffer");
            return false;
        }

        const CameraUniform cameraUniform = makeCameraUniform(frameContext);
        return frameContext.context->updateBuffer(*m_CameraBuffers[frameSlot], &cameraUniform, sizeof(cameraUniform));
    }

    bool ForwardPass::updateLightingUniform(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context || frameSlot >= m_LightingBuffers.size() || !m_LightingBuffers[frameSlot]) {
            ARK_ERROR("ForwardPass requires per-frame lighting buffer");
            return false;
        }

        const LightingUniform lightingUniform = makeLightingUniform(frameContext);
        return frameContext.context->updateBuffer(*m_LightingBuffers[frameSlot],
                                                  &lightingUniform,
                                                  sizeof(lightingUniform));
    }

    bool ForwardPass::updateObjectUniform(FrameContext& frameContext,
                                          u32 frameSlot,
                                          usize drawIndex,
                                          const glm::mat4& modelMatrix) {
        if (!frameContext.context || frameSlot >= m_DrawDescriptors.size() ||
            drawIndex >= m_DrawDescriptors[frameSlot].size() || !m_DrawDescriptors[frameSlot][drawIndex].objectBuffer) {
            ARK_ERROR("ForwardPass requires per-draw object buffer");
            return false;
        }

        ObjectUniform objectUniform{};
        objectUniform.model = modelMatrix;
        // normalMatrix 用于非等比缩放下的法线/切线方向变换。
        objectUniform.normalMatrix = glm::inverseTranspose(modelMatrix);
        return frameContext.context->updateBuffer(*m_DrawDescriptors[frameSlot][drawIndex].objectBuffer,
                                                  &objectUniform, sizeof(objectUniform));
    }

    bool ForwardPass::updateMaterialUniform(FrameContext& frameContext,
                                            u32 frameSlot,
                                            usize drawIndex,
                                            const MaterialResource& material) {
        if (!frameContext.context || frameSlot >= m_DrawDescriptors.size() ||
            drawIndex >= m_DrawDescriptors[frameSlot].size() || !m_DrawDescriptors[frameSlot][drawIndex].materialBuffer) {
            ARK_ERROR("ForwardPass requires per-draw material buffer");
            return false;
        }

        const MaterialUniform materialUniform = makeMaterialUniform(material);
        return frameContext.context->updateBuffer(*m_DrawDescriptors[frameSlot][drawIndex].materialBuffer,
                                                  &materialUniform, sizeof(materialUniform));
    }

    bool ForwardPass::updateDrawDescriptorSet(u32 frameSlot, usize drawIndex, MaterialResource& material) {
        if (frameSlot >= m_DrawDescriptors.size() || drawIndex >= m_DrawDescriptors[frameSlot].size() ||
            !m_CameraBuffers[frameSlot] || !m_LightingBuffers[frameSlot] ||
            !m_DrawDescriptors[frameSlot][drawIndex].objectBuffer ||
            !m_DrawDescriptors[frameSlot][drawIndex].materialBuffer ||
            !m_DrawDescriptors[frameSlot][drawIndex].descriptorSet) {
            ARK_ERROR("ForwardPass requires descriptor resources before descriptor update");
            return false;
        }

        DrawDescriptorResources& descriptors = m_DrawDescriptors[frameSlot][drawIndex];

        rhi::BufferDescriptor cameraDescriptor{};
        cameraDescriptor.buffer = m_CameraBuffers[frameSlot].get();
        cameraDescriptor.range = sizeof(CameraUniform);
        descriptors.descriptorSet->updateUniformBuffer(0, cameraDescriptor);

        MaterialTextureBindingSet textureBindings{};
        if (!material.updateDescriptorSet(*descriptors.descriptorSet, textureBindings)) {
            return false;
        }

        rhi::BufferDescriptor objectDescriptor{};
        objectDescriptor.buffer = descriptors.objectBuffer.get();
        objectDescriptor.range = sizeof(ObjectUniform);
        descriptors.descriptorSet->updateUniformBuffer(3, objectDescriptor);

        rhi::BufferDescriptor materialDescriptor{};
        materialDescriptor.buffer = descriptors.materialBuffer.get();
        materialDescriptor.range = sizeof(MaterialUniform);
        descriptors.descriptorSet->updateUniformBuffer(4, materialDescriptor);

        rhi::BufferDescriptor lightingDescriptor{};
        lightingDescriptor.buffer = m_LightingBuffers[frameSlot].get();
        lightingDescriptor.range = sizeof(LightingUniform);
        descriptors.descriptorSet->updateUniformBuffer(13, lightingDescriptor);
        return true;
    }

    bool ForwardPass::drawMeshItem(FrameContext& frameContext,
                                   u32 frameSlot,
                                   usize drawIndex,
                                   MeshResource& mesh,
                                   MaterialResource& material,
                                   const glm::mat4& modelMatrix) {
        if (!frameContext.context || frameSlot >= m_DrawDescriptors.size() ||
            drawIndex >= m_DrawDescriptors[frameSlot].size()) {
            ARK_ERROR("ForwardPass requires draw descriptor resources");
            return false;
        }

        if (!mesh.isReady() || !material.isReady()) {
            ARK_ERROR("ForwardPass draw item requires ready mesh and material");
            return false;
        }

        if (!updateObjectUniform(frameContext, frameSlot, drawIndex, modelMatrix) ||
            !updateMaterialUniform(frameContext, frameSlot, drawIndex, material)) {
            return false;
        }

        DrawDescriptorResources& descriptors = m_DrawDescriptors[frameSlot][drawIndex];
        frameContext.context->bindDescriptorSet(0, *descriptors.descriptorSet);
        mesh.bind(*frameContext.context);
        frameContext.context->drawIndexed(mesh.makeDrawIndexedDesc());
        return true;
    }
} // namespace ark
