#include "renderer/passes/ForwardPass.h"

#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
#include "asset/TextureLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "rhi/DeviceContext.h"
#include "rhi/SwapChain.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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
            float alphaCutoff = 0.5f;
            float alphaMode = 0.0f;
            float baseColorTexCoord = 0.0f;
            float normalTexCoord = 0.0f;
            float metallicRoughnessTexCoord = 0.0f;
            float occlusionTexCoord = 0.0f;
            float emissiveTexCoord = 0.0f;
            float padding = 0.0f;
            glm::vec4 baseColorUvTransform0;
            glm::vec4 baseColorUvTransform1;
            glm::vec4 normalUvTransform0;
            glm::vec4 normalUvTransform1;
            glm::vec4 metallicRoughnessUvTransform0;
            glm::vec4 metallicRoughnessUvTransform1;
            glm::vec4 occlusionUvTransform0;
            glm::vec4 occlusionUvTransform1;
            glm::vec4 emissiveUvTransform0;
            glm::vec4 emissiveUvTransform1;
        };

        static_assert(sizeof(MaterialUniform) == 240);

        struct alignas(16) LightingUniform {
            glm::vec4 lightDirection;
            glm::vec4 lightColor;
            glm::vec4 ambientColor;
            glm::vec4 cameraPosition;
            glm::vec4 environment;
        };

        static_assert(sizeof(LightingUniform) == 80);

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

        glm::vec3 normalizeLightDirection(const glm::vec3& direction) {
            constexpr float MinDirectionLengthSquared = 1.0e-6f;
            if (glm::dot(direction, direction) <= MinDirectionLengthSquared) {
                return glm::normalize(SceneLighting{}.mainLight.direction);
            }

            return glm::normalize(direction);
        }

        bool isSceneEnvironmentReady(const SceneEnvironment& environment) {
            return environment.isEnabled() && environment.environment && environment.environment->isReady();
        }

        LightingUniform makeLightingUniform(const FrameContext& frameContext) {
            const SceneLighting defaultLighting{};
            const SceneLighting& lighting = frameContext.scene ? frameContext.scene->lighting() : defaultLighting;
            const SceneEnvironment* environment = frameContext.scene ? &frameContext.scene->environment() : nullptr;

            LightingUniform uniform{};
            uniform.lightDirection = glm::vec4{normalizeLightDirection(lighting.mainLight.direction), 0.0f};
            uniform.lightColor = glm::vec4{lighting.mainLight.color, 1.0f};
            uniform.ambientColor = glm::vec4{lighting.ambientColor, 1.0f};
            uniform.cameraPosition = glm::vec4{0.0f, 0.0f, -4.0f, 1.0f};

            if (frameContext.view) {
                uniform.cameraPosition = glm::vec4{frameContext.view->cameraPosition(), 1.0f};
            }

            if (environment && isSceneEnvironmentReady(*environment)) {
                uniform.environment = glm::vec4{environment->intensity, 1.0f, 0.0f, 0.0f};
            }

            return uniform;
        }

        asset::ImageData makeFallbackEnvironmentImage() {
            constexpr u32 Width = 1;
            constexpr u32 Height = 1;
            constexpr u32 BytesPerPixel = 16;
            const std::array<float, 4> pixel{0.0f, 0.0f, 0.0f, 1.0f};

            asset::ImageData image{};
            image.width = Width;
            image.height = Height;
            image.format = asset::ImageFormat::Rgba32Float;
            image.bytesPerPixel = BytesPerPixel;
            image.pixels.resize(pixel.size() * sizeof(float));
            std::memcpy(image.pixels.data(), pixel.data(), image.pixels.size());
            image.debugName = "ForwardFallbackEnvironment";
            return image;
        }

        glm::vec4 makeOffsetScale(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.offset[0], transform.offset[1], transform.scale[0], transform.scale[1]};
        }

        glm::vec4 makeRotation(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.rotation, 0.0f, 0.0f, 0.0f};
        }

        MaterialUniform makeMaterialUniform(const MaterialResource& material) {
            const MaterialFactors& factors = material.factors();
            const MaterialRenderState& renderState = material.renderState();
            const MaterialTextureCoordinateSet& textureCoordinates = material.textureCoordinates();
            const MaterialTextureTransformSet& textureTransforms = material.textureTransforms();

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
            uniform.alphaCutoff = renderState.alphaCutoff;
            uniform.alphaMode = static_cast<float>(renderState.alphaMode);
            uniform.baseColorTexCoord = static_cast<float>(textureCoordinates.baseColor);
            uniform.normalTexCoord = static_cast<float>(textureCoordinates.normal);
            uniform.metallicRoughnessTexCoord = static_cast<float>(textureCoordinates.metallicRoughness);
            uniform.occlusionTexCoord = static_cast<float>(textureCoordinates.occlusion);
            uniform.emissiveTexCoord = static_cast<float>(textureCoordinates.emissive);
            uniform.baseColorUvTransform0 = makeOffsetScale(textureTransforms.baseColor);
            uniform.baseColorUvTransform1 = makeRotation(textureTransforms.baseColor);
            uniform.normalUvTransform0 = makeOffsetScale(textureTransforms.normal);
            uniform.normalUvTransform1 = makeRotation(textureTransforms.normal);
            uniform.metallicRoughnessUvTransform0 = makeOffsetScale(textureTransforms.metallicRoughness);
            uniform.metallicRoughnessUvTransform1 = makeRotation(textureTransforms.metallicRoughness);
            uniform.occlusionUvTransform0 = makeOffsetScale(textureTransforms.occlusion);
            uniform.occlusionUvTransform1 = makeRotation(textureTransforms.occlusion);
            uniform.emissiveUvTransform0 = makeOffsetScale(textureTransforms.emissive);
            uniform.emissiveUvTransform1 = makeRotation(textureTransforms.emissive);
            return uniform;
        }

        rhi::BlendStateDesc makeBlendState(asset::AlphaMode alphaMode) {
            rhi::BlendStateDesc blendState{};
            if (alphaMode != asset::AlphaMode::Blend) {
                return blendState;
            }

            blendState.colorAttachment.enableBlend = true;
            blendState.colorAttachment.srcColorBlendFactor = rhi::BlendFactor::SrcAlpha;
            blendState.colorAttachment.dstColorBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            blendState.colorAttachment.colorBlendOp = rhi::BlendOp::Add;
            blendState.colorAttachment.srcAlphaBlendFactor = rhi::BlendFactor::One;
            blendState.colorAttachment.dstAlphaBlendFactor = rhi::BlendFactor::OneMinusSrcAlpha;
            blendState.colorAttachment.alphaBlendOp = rhi::BlendOp::Add;
            return blendState;
        }

        rhi::CullMode makeCullMode(const MaterialRenderState& renderState) {
            return renderState.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        }

        rhi::FrontFace makeFrontFace() {
            return rhi::FrontFace::CounterClockwise;
        }

        rhi::Format resolveColorFormat(const FrameContext& frameContext) {
            if (frameContext.colorFormat != rhi::Format::Unknown) {
                return frameContext.colorFormat;
            }

            return frameContext.swapChain ? frameContext.swapChain->getDesc().colorFormat : rhi::Format::Unknown;
        }

        rhi::Format resolveDepthFormat(const FrameContext& frameContext) {
            if (frameContext.depthFormat != rhi::Format::Unknown) {
                return frameContext.depthFormat;
            }

            return frameContext.swapChain ? frameContext.swapChain->getDesc().depthFormat : rhi::Format::Unknown;
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

        if (!uploadEnvironmentResources(frameContext)) {
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

            if (!updateDrawDescriptorSet(frameContext, frameSlot, drawIndex, *item.material)) {
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

        if (!updateCameraUniform(frameContext, frameSlot) || !updateLightingUniform(frameContext, frameSlot)) {
            return false;
        }
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
        addFragmentTextureBindingPair(descriptorSetLayoutDesc, 14, 15);
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

    bool ForwardPass::ensureFallbackEnvironment() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires RenderDevice for fallback environment");
            return false;
        }

        if (m_FallbackEnvironment.textureView() && m_FallbackEnvironment.sampler()) {
            return true;
        }

        EnvironmentResourceDesc desc{};
        desc.debugName = "ForwardFallbackEnvironment";
        return m_FallbackEnvironment.create(*m_Device, makeFallbackEnvironmentImage(), desc);
    }

    bool ForwardPass::uploadEnvironmentResources(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("ForwardPass requires DeviceContext for environment upload");
            return false;
        }

        if (!ensureFallbackEnvironment() || !m_FallbackEnvironment.upload(*frameContext.context)) {
            return false;
        }

        const SceneEnvironment* sceneEnvironment =
            frameContext.scene ? &frameContext.scene->environment() : nullptr;
        if (!sceneEnvironment || !sceneEnvironment->isEnabled()) {
            return true;
        }

        if (!sceneEnvironment->environment) {
            ARK_ERROR("ForwardPass scene environment is enabled without a resource");
            return false;
        }

        return sceneEnvironment->environment->upload(*frameContext.context);
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

    rhi::PipelineState* ForwardPass::getOrCreatePipeline(FrameContext& frameContext, const MaterialResource& material) {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires RenderDevice");
            return nullptr;
        }

        const rhi::Format colorFormat = resolveColorFormat(frameContext);
        const rhi::Format depthFormat = resolveDepthFormat(frameContext);
        if (colorFormat == rhi::Format::Unknown) {
            ARK_ERROR("ForwardPass requires a valid color attachment format");
            return nullptr;
        }

        const MaterialRenderState& renderState = material.renderState();
        ForwardPipelineKey key{};
        key.colorFormat = colorFormat;
        key.depthFormat = depthFormat;
        key.alphaMode = renderState.alphaMode;
        key.doubleSided = renderState.doubleSided;

        auto existing = m_Pipelines.find(key);
        if (existing != m_Pipelines.end()) {
            return existing->second.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("ForwardPass requires shader modules and pipeline layout");
            return nullptr;
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
            .format = rhi::Format::R32G32Float,
            .offset = offsetof(asset::MeshVertex, uv1),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 4,
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
        pipelineDesc.rasterState.cullMode = makeCullMode(renderState);
        pipelineDesc.rasterState.frontFace = makeFrontFace();
        pipelineDesc.depthStencilState.enableDepthTest = true;
        pipelineDesc.depthStencilState.enableDepthWrite = renderState.alphaMode != asset::AlphaMode::Blend;
        pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::Less;
        pipelineDesc.blendState = makeBlendState(renderState.alphaMode);
        pipelineDesc.colorFormat = key.colorFormat;
        pipelineDesc.depthFormat = key.depthFormat;

        Scope<rhi::PipelineState> pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        if (!pipeline) {
            return nullptr;
        }

        rhi::PipelineState* result = pipeline.get();
        m_Pipelines.emplace(key, std::move(pipeline));
        return result;
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

    bool ForwardPass::updateDrawDescriptorSet(FrameContext& frameContext,
                                              u32 frameSlot,
                                              usize drawIndex,
                                              MaterialResource& material) {
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

        EnvironmentResource* environment = resolveEnvironmentResource(frameContext);
        if (!environment || !environment->textureView() || !environment->sampler()) {
            ARK_ERROR("ForwardPass requires a ready environment texture for descriptor update");
            return false;
        }

        rhi::SampledImageDescriptor environmentImageDescriptor{};
        environmentImageDescriptor.view = environment->textureView();
        descriptors.descriptorSet->updateSampledImage(14, environmentImageDescriptor);

        rhi::SamplerDescriptor environmentSamplerDescriptor{};
        environmentSamplerDescriptor.sampler = environment->sampler();
        descriptors.descriptorSet->updateSampler(15, environmentSamplerDescriptor);
        return true;
    }

    EnvironmentResource* ForwardPass::resolveEnvironmentResource(FrameContext& frameContext) {
        const SceneEnvironment* sceneEnvironment =
            frameContext.scene ? &frameContext.scene->environment() : nullptr;
        if (sceneEnvironment && isSceneEnvironmentReady(*sceneEnvironment)) {
            return sceneEnvironment->environment;
        }

        return &m_FallbackEnvironment;
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

        rhi::PipelineState* pipeline = getOrCreatePipeline(frameContext, material);
        if (!pipeline) {
            return false;
        }

        DrawDescriptorResources& descriptors = m_DrawDescriptors[frameSlot][drawIndex];
        frameContext.context->setPipeline(*pipeline);
        frameContext.context->bindDescriptorSet(0, *descriptors.descriptorSet);
        mesh.bind(*frameContext.context);
        frameContext.context->drawIndexed(mesh.makeDrawIndexedDesc());
        return true;
    }
} // namespace ark
