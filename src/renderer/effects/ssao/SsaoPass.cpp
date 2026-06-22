#include "renderer/effects/ssao/SsaoPass.h"

#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderView.h"
#include "renderer/TextureResource.h"
#include "renderer/material/MaterialResource.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace ark {
    namespace {
        constexpr rhi::Format SsaoFormat = rhi::Format::RGBA16Float;
        constexpr usize FullscreenPassCount = 3;

        struct alignas(16) SsaoGeometryUniform {
            glm::mat4 view{1.0f};
            glm::mat4 projection{1.0f};
            glm::mat4 model{1.0f};
            glm::mat4 normalMatrix{1.0f};
        };

        static_assert(sizeof(SsaoGeometryUniform) == 256);

        struct alignas(16) SsaoMaterialUniform {
            glm::vec4 baseColorFactor{1.0f};
            glm::vec4 baseColorUvTransform0{0.0f, 0.0f, 1.0f, 1.0f};
            glm::vec4 baseColorUvTransform1{0.0f};
            float alphaCutoff = 0.5f;
            float alphaMode = 0.0f;
            float baseColorTexCoord = 0.0f;
            float padding = 0.0f;
        };

        static_assert(sizeof(SsaoMaterialUniform) == 64);

        struct alignas(16) SsaoFullscreenUniform {
            glm::mat4 projection{1.0f};
            glm::mat4 inverseProjection{1.0f};
            glm::mat4 inverseView{1.0f};
            glm::vec4 parameters0{0.0f};
            glm::vec4 parameters1{0.0f};
            glm::vec4 texelSize{1.0f};
        };

        static_assert(sizeof(SsaoFullscreenUniform) == 240);

        const SsaoSettings& resolveSettings(const FrameContext& frameContext) {
            static const PostProcessingSettings DefaultPostProcessing{};
            const PostProcessingSettings& postProcessing =
                frameContext.view ? frameContext.view->postProcessingSettings() : DefaultPostProcessing;
            return postProcessing.ssao;
        }

        const RenderQueue* resolveSsaoQueue(const FrameContext& frameContext) {
            return frameContext.forwardQueue ? frameContext.forwardQueue : frameContext.queue;
        }

        bool isBlendSkipped(const DrawItem& item) {
            return item.material &&
                   item.material->renderState().alphaMode == asset::AlphaMode::Blend;
        }

        bool sameExtent(rhi::Extent2D lhs, rhi::Extent2D rhs) {
            return lhs.width == rhs.width && lhs.height == rhs.height;
        }

        rhi::Extent2D scaledExtent(rhi::Extent2D extent, float resolutionScale) {
            if (!rhi::isValidExtent(extent)) {
                return {};
            }

            const float scale = std::clamp(resolutionScale, 0.25f, 1.0f);
            return rhi::Extent2D{
                .width = std::max(1u, static_cast<u32>(std::round(static_cast<float>(extent.width) * scale))),
                .height = std::max(1u, static_cast<u32>(std::round(static_cast<float>(extent.height) * scale))),
            };
        }

        rhi::Extent2D textureViewExtent(const rhi::TextureView& view) {
            const rhi::Texture* texture = view.getTexture();
            return texture ? texture->getDesc().extent : rhi::Extent2D{};
        }

        void setTargetViewportAndScissor(rhi::DeviceContext& context, rhi::Extent2D extent) {
            rhi::Viewport viewport{};
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            context.setViewport(viewport);

            rhi::ScissorRect scissor{};
            scissor.width = extent.width;
            scissor.height = extent.height;
            context.setScissorRect(scissor);
        }

        glm::vec4 makeOffsetScale(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.offset[0], transform.offset[1], transform.scale[0], transform.scale[1]};
        }

        glm::vec4 makeRotation(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.rotation, 0.0f, 0.0f, 0.0f};
        }

        SsaoMaterialUniform makeSsaoMaterialUniform(const MaterialResource& material) {
            const MaterialFactors& factors = material.factors();
            const MaterialRenderState& renderState = material.renderState();
            const MaterialTextureCoordinateSet& textureCoordinates = material.textureCoordinates();
            const MaterialTextureTransformSet& textureTransforms = material.textureTransforms();

            SsaoMaterialUniform uniform{};
            uniform.baseColorFactor = glm::vec4{
                factors.baseColorFactor[0],
                factors.baseColorFactor[1],
                factors.baseColorFactor[2],
                factors.baseColorFactor[3],
            };
            uniform.baseColorUvTransform0 = makeOffsetScale(textureTransforms.baseColor);
            uniform.baseColorUvTransform1 = makeRotation(textureTransforms.baseColor);
            uniform.alphaCutoff = renderState.alphaCutoff;
            uniform.alphaMode = static_cast<float>(renderState.alphaMode);
            uniform.baseColorTexCoord = static_cast<float>(textureCoordinates.baseColor);
            return uniform;
        }

        SsaoFullscreenUniform makeSsaoFullscreenUniform(u32 mode,
                                                        const SsaoSettings& settings,
                                                        const FrameContext& frameContext,
                                                        rhi::Extent2D sourceExtent,
                                                        rhi::Extent2D sceneExtent) {
            SsaoFullscreenUniform uniform{};
            if (frameContext.view) {
                uniform.projection = frameContext.view->projectionMatrix();
                uniform.inverseProjection = glm::inverse(frameContext.view->projectionMatrix());
                uniform.inverseView = glm::affineInverse(frameContext.view->viewMatrix());
            }
            uniform.parameters0 = glm::vec4{
                settings.radius,
                settings.intensity,
                settings.bias,
                settings.power,
            };
            uniform.parameters1 = glm::vec4{
                static_cast<float>(settings.sampleCount),
                static_cast<float>(settings.blurRadius),
                static_cast<float>(settings.debugMode),
                static_cast<float>(mode),
            };
            uniform.texelSize = glm::vec4{
                sourceExtent.width > 0 ? 1.0f / static_cast<float>(sourceExtent.width) : 1.0f,
                sourceExtent.height > 0 ? 1.0f / static_cast<float>(sourceExtent.height) : 1.0f,
                sceneExtent.width > 0 ? 1.0f / static_cast<float>(sceneExtent.width) : 1.0f,
                sceneExtent.height > 0 ? 1.0f / static_cast<float>(sceneExtent.height) : 1.0f,
            };
            return uniform;
        }

        rhi::CullMode makeCullMode(const MaterialRenderState& renderState) {
            return renderState.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        }
    } // namespace

    SsaoPass::~SsaoPass() = default;

    void SsaoPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createGeometryDescriptorResources();
        createFullscreenDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool SsaoPass::prepare(FrameContext& frameContext) {
        const SsaoSettings& settings = resolveSettings(frameContext);
        if (!settings.enabled) {
            clearFrameBindings(frameContext);
            return true;
        }

        if (!frameContext.context || !frameContext.sceneColorView) {
            ARK_ERROR("SsaoPass requires DeviceContext and scene color view");
            return false;
        }

        if (!ensureTargets(frameContext, settings)) {
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        const RenderQueue* queue = resolveSsaoQueue(frameContext);
        const usize drawCount = queue ? queue->size() : 0;
        if (!ensureGeometryDrawResources(frameSlot, drawCount) ||
            !ensureFullscreenDrawResources(frameSlot, FullscreenPassCount)) {
            return false;
        }

        if (!queue || queue->empty()) {
            return true;
        }

        for (const DrawItem& item : queue->drawItems()) {
            if (!item.isDrawable()) {
                ARK_ERROR("SsaoPass queue contains an invalid draw item");
                return false;
            }

            if (isBlendSkipped(item)) {
                continue;
            }

            if (!item.mesh->upload(*frameContext.context) ||
                !item.material->upload(*frameContext.context)) {
                return false;
            }
        }

        return true;
    }

    bool SsaoPass::execute(FrameContext& frameContext) {
        const SsaoSettings& settings = resolveSettings(frameContext);
        if (!settings.enabled) {
            clearFrameBindings(frameContext);
            return true;
        }

        if (!frameContext.context || !frameContext.sceneColorView) {
            ARK_ERROR("SsaoPass requires prepared frame inputs");
            return false;
        }

        if (!ensureTargets(frameContext, settings)) {
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        const RenderQueue* queue = resolveSsaoQueue(frameContext);
        const usize drawCount = queue ? queue->size() : 0;
        if (!ensureGeometryDrawResources(frameSlot, drawCount) ||
            !ensureFullscreenDrawResources(frameSlot, FullscreenPassCount)) {
            return false;
        }

        if (!recordNormalDepthPass(frameContext, frameSlot)) {
            return false;
        }

        if (!m_NormalDepthTarget.view || !m_OcclusionTarget.view || !m_BlurTarget.view || !m_CompositeTarget.view) {
            ARK_ERROR("SsaoPass targets were not prepared");
            return false;
        }

        rhi::TextureView* originalSceneColorView = frameContext.sceneColorView;
        if (!recordFullscreenPass(frameContext,
                                  frameSlot,
                                  0,
                                  FullscreenMode::Evaluate,
                                  *m_NormalDepthTarget.view,
                                  *m_NormalDepthTarget.view,
                                  m_OcclusionTarget,
                                  settings) ||
            !recordFullscreenPass(frameContext,
                                  frameSlot,
                                  1,
                                  FullscreenMode::Blur,
                                  *m_OcclusionTarget.view,
                                  *m_NormalDepthTarget.view,
                                  m_BlurTarget,
                                  settings)) {
            return false;
        }

        rhi::TextureView& compositeAuxSource =
            settings.debugMode == SsaoDebugMode::NormalDepth ? *m_NormalDepthTarget.view : *m_BlurTarget.view;
        if (!recordFullscreenPass(frameContext,
                                  frameSlot,
                                  2,
                                  FullscreenMode::Composite,
                                  *originalSceneColorView,
                                  compositeAuxSource,
                                  m_CompositeTarget,
                                  settings)) {
            return false;
        }

        frameContext.sceneColorView = m_CompositeTarget.view.get();
        publishFrameBindings(frameContext);
        return frameContext.sceneColorView != nullptr;
    }

    bool SsaoPass::createGeometryDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("SsaoPass requires device for geometry descriptors");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SsaoGeometryDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
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
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 3,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        m_GeometryDescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        return m_GeometryDescriptorSetLayout != nullptr;
    }

    bool SsaoPass::createFullscreenDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("SsaoPass requires device for fullscreen descriptors");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "SsaoFullscreenDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::SampledImage,
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
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 3,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        m_FullscreenDescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_FullscreenDescriptorSetLayout) {
            return false;
        }

        rhi::SamplerDesc linearSamplerDesc{};
        linearSamplerDesc.debugName = "SsaoLinearClampSampler";
        linearSamplerDesc.minFilter = rhi::FilterMode::Linear;
        linearSamplerDesc.magFilter = rhi::FilterMode::Linear;
        linearSamplerDesc.mipFilter = rhi::FilterMode::Nearest;
        linearSamplerDesc.addressU = rhi::AddressMode::ClampToEdge;
        linearSamplerDesc.addressV = rhi::AddressMode::ClampToEdge;
        linearSamplerDesc.addressW = rhi::AddressMode::ClampToEdge;
        m_LinearSampler = m_Device->createSampler(linearSamplerDesc);

        rhi::SamplerDesc pointSamplerDesc = linearSamplerDesc;
        pointSamplerDesc.debugName = "SsaoPointClampSampler";
        pointSamplerDesc.minFilter = rhi::FilterMode::Nearest;
        pointSamplerDesc.magFilter = rhi::FilterMode::Nearest;
        pointSamplerDesc.mipFilter = rhi::FilterMode::Nearest;
        m_PointSampler = m_Device->createSampler(pointSamplerDesc);
        return m_LinearSampler && m_PointSampler;
    }

    bool SsaoPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("SsaoPass requires device for shaders");
            return false;
        }

        rhi::ShaderDesc geometryVertexDesc{};
        geometryVertexDesc.debugName = "SsaoNormalDepthVertexShader";
        geometryVertexDesc.stage = rhi::ShaderStage::Vertex;
        geometryVertexDesc.bytecode = asset::loadCompiledShader("ssao_normal_depth.vert.spv");
        if (!geometryVertexDesc.bytecode.empty()) {
            m_GeometryVertexShader = m_Device->createShader(geometryVertexDesc);
        }

        rhi::ShaderDesc geometryFragmentDesc{};
        geometryFragmentDesc.debugName = "SsaoNormalDepthFragmentShader";
        geometryFragmentDesc.stage = rhi::ShaderStage::Fragment;
        geometryFragmentDesc.bytecode = asset::loadCompiledShader("ssao_normal_depth.frag.spv");
        if (!geometryFragmentDesc.bytecode.empty()) {
            m_GeometryFragmentShader = m_Device->createShader(geometryFragmentDesc);
        }

        rhi::ShaderDesc fullscreenVertexDesc{};
        fullscreenVertexDesc.debugName = "SsaoFullscreenVertexShader";
        fullscreenVertexDesc.stage = rhi::ShaderStage::Vertex;
        fullscreenVertexDesc.bytecode = asset::loadCompiledShader("tonemap.vert.spv");
        if (!fullscreenVertexDesc.bytecode.empty()) {
            m_FullscreenVertexShader = m_Device->createShader(fullscreenVertexDesc);
        }

        rhi::ShaderDesc fullscreenFragmentDesc{};
        fullscreenFragmentDesc.debugName = "SsaoFragmentShader";
        fullscreenFragmentDesc.stage = rhi::ShaderStage::Fragment;
        fullscreenFragmentDesc.bytecode = asset::loadCompiledShader("ssao.frag.spv");
        if (!fullscreenFragmentDesc.bytecode.empty()) {
            m_FullscreenFragmentShader = m_Device->createShader(fullscreenFragmentDesc);
        }

        return m_GeometryVertexShader && m_GeometryFragmentShader &&
               m_FullscreenVertexShader && m_FullscreenFragmentShader;
    }

    bool SsaoPass::createPipelineResources() {
        if (!m_Device || !m_GeometryDescriptorSetLayout || !m_FullscreenDescriptorSetLayout) {
            ARK_ERROR("SsaoPass requires descriptor layouts before pipeline layouts");
            return false;
        }

        rhi::PipelineLayoutDesc geometryLayoutDesc{};
        geometryLayoutDesc.debugName = "SsaoGeometryPipelineLayout";
        geometryLayoutDesc.descriptorSetLayouts.push_back(m_GeometryDescriptorSetLayout.get());
        m_GeometryPipelineLayout = m_Device->createPipelineLayout(geometryLayoutDesc);
        if (!m_GeometryPipelineLayout) {
            return false;
        }

        rhi::PipelineLayoutDesc fullscreenLayoutDesc{};
        fullscreenLayoutDesc.debugName = "SsaoFullscreenPipelineLayout";
        fullscreenLayoutDesc.descriptorSetLayouts.push_back(m_FullscreenDescriptorSetLayout.get());
        m_FullscreenPipelineLayout = m_Device->createPipelineLayout(fullscreenLayoutDesc);
        if (!m_FullscreenPipelineLayout || !m_GeometryVertexShader || !m_GeometryFragmentShader) {
            return false;
        }

        auto makeGeometryPipelineDesc = [&](const char* debugName, const MaterialRenderState& renderState) {
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

            rhi::GraphicsPipelineDesc pipelineDesc{};
            pipelineDesc.debugName = debugName;
            pipelineDesc.vertexShader = m_GeometryVertexShader.get();
            pipelineDesc.fragmentShader = m_GeometryFragmentShader.get();
            pipelineDesc.layout = m_GeometryPipelineLayout.get();
            pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
            pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
            pipelineDesc.rasterState.cullMode = makeCullMode(renderState);
            pipelineDesc.rasterState.frontFace = rhi::FrontFace::CounterClockwise;
            pipelineDesc.depthStencilState.enableDepthTest = true;
            pipelineDesc.depthStencilState.enableDepthWrite = true;
            pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::LessOrEqual;
            pipelineDesc.colorFormat = SsaoFormat;
            pipelineDesc.depthFormat = rhi::Format::D32Float;
            return pipelineDesc;
        };

        MaterialRenderState singleSided{};
        singleSided.doubleSided = false;
        MaterialRenderState doubleSided{};
        doubleSided.doubleSided = true;
        m_GeometrySingleSidedPipeline =
            m_Device->createGraphicsPipeline(makeGeometryPipelineDesc("SsaoNormalDepthPipeline.SingleSided", singleSided));
        m_GeometryDoubleSidedPipeline =
            m_Device->createGraphicsPipeline(makeGeometryPipelineDesc("SsaoNormalDepthPipeline.DoubleSided", doubleSided));
        return m_GeometrySingleSidedPipeline && m_GeometryDoubleSidedPipeline;
    }

    bool SsaoPass::ensureTargets(FrameContext& frameContext, const SsaoSettings& settings) {
        if (!m_Device) {
            ARK_ERROR("SsaoPass requires RenderDevice before creating targets");
            return false;
        }

        if (!frameContext.context) {
            ARK_ERROR("SsaoPass requires DeviceContext before creating targets");
            return false;
        }

        const rhi::Extent2D frameExtent = frameContext.extent;
        const rhi::Extent2D ssaoExtent = scaledExtent(frameExtent, settings.resolutionScale);
        if (!rhi::isValidExtent(frameExtent) || !rhi::isValidExtent(ssaoExtent)) {
            ARK_ERROR("SsaoPass requires valid frame and SSAO extents");
            return false;
        }

        if (sameExtent(m_FrameExtent, frameExtent) &&
            sameExtent(m_SsaoExtent, ssaoExtent) &&
            m_NormalDepthTarget.view &&
            m_NormalDepthDepthTarget.view &&
            m_OcclusionTarget.view &&
            m_BlurTarget.view &&
            m_CompositeTarget.view) {
            return true;
        }

        if (!releaseTargetsDeferred(frameContext)) {
            return false;
        }

        if (!createTarget(m_NormalDepthTarget, ssaoExtent, SsaoFormat, "SsaoNormalDepthTarget") ||
            !createDepthTarget(m_NormalDepthDepthTarget, ssaoExtent, "SsaoNormalDepthDepthTarget") ||
            !createTarget(m_OcclusionTarget, ssaoExtent, SsaoFormat, "SsaoOcclusionTarget") ||
            !createTarget(m_BlurTarget, ssaoExtent, SsaoFormat, "SsaoBlurTarget") ||
            !createTarget(m_CompositeTarget, frameExtent, SsaoFormat, "SsaoCompositeTarget")) {
            return false;
        }

        m_FrameExtent = frameExtent;
        m_SsaoExtent = ssaoExtent;
        return true;
    }

    bool SsaoPass::ensureGeometryDrawResources(u32 frameSlot, usize drawCount) {
        if (!m_Device || !m_GeometryDescriptorSetLayout || frameSlot >= m_GeometryDrawResources.size()) {
            ARK_ERROR("SsaoPass requires geometry descriptor layout before draw resources");
            return false;
        }

        std::vector<GeometryDrawResources>& resources = m_GeometryDrawResources[frameSlot];
        while (resources.size() < drawCount) {
            const usize resourceIndex = resources.size();

            GeometryDrawResources drawResources{};
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "SsaoGeometryUniformBuffer." + std::to_string(resourceIndex);
            uniformBufferDesc.size = sizeof(SsaoGeometryUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.uniformBuffer = m_Device->createBuffer(uniformBufferDesc);

            rhi::BufferDesc materialBufferDesc{};
            materialBufferDesc.debugName = "SsaoMaterialUniformBuffer." + std::to_string(resourceIndex);
            materialBufferDesc.size = sizeof(SsaoMaterialUniform);
            materialBufferDesc.usage = rhi::BufferUsage::Uniform;
            materialBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.materialBuffer = m_Device->createBuffer(materialBufferDesc);

            drawResources.descriptorSet = m_Device->createDescriptorSet(*m_GeometryDescriptorSetLayout);
            if (!drawResources.uniformBuffer || !drawResources.materialBuffer || !drawResources.descriptorSet) {
                return false;
            }

            rhi::BufferDescriptor geometryDescriptor{};
            geometryDescriptor.buffer = drawResources.uniformBuffer.get();
            geometryDescriptor.range = sizeof(SsaoGeometryUniform);
            drawResources.descriptorSet->updateUniformBuffer(0, geometryDescriptor);

            rhi::BufferDescriptor materialDescriptor{};
            materialDescriptor.buffer = drawResources.materialBuffer.get();
            materialDescriptor.range = sizeof(SsaoMaterialUniform);
            drawResources.descriptorSet->updateUniformBuffer(3, materialDescriptor);

            resources.push_back(std::move(drawResources));
        }

        return true;
    }

    bool SsaoPass::ensureFullscreenDrawResources(u32 frameSlot, usize drawCount) {
        if (!m_Device || !m_FullscreenDescriptorSetLayout || frameSlot >= m_FullscreenDrawResources.size()) {
            ARK_ERROR("SsaoPass requires fullscreen descriptor layout before draw resources");
            return false;
        }

        std::vector<FullscreenDrawResources>& resources = m_FullscreenDrawResources[frameSlot];
        while (resources.size() < drawCount) {
            const usize resourceIndex = resources.size();

            FullscreenDrawResources drawResources{};
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "SsaoFullscreenUniformBuffer." + std::to_string(resourceIndex);
            uniformBufferDesc.size = sizeof(SsaoFullscreenUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.uniformBuffer = m_Device->createBuffer(uniformBufferDesc);
            drawResources.descriptorSet = m_Device->createDescriptorSet(*m_FullscreenDescriptorSetLayout);
            if (!drawResources.uniformBuffer || !drawResources.descriptorSet) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = drawResources.uniformBuffer.get();
            uniformDescriptor.range = sizeof(SsaoFullscreenUniform);
            drawResources.descriptorSet->updateUniformBuffer(3, uniformDescriptor);
            resources.push_back(std::move(drawResources));
        }

        return true;
    }

    bool SsaoPass::createTarget(Target& target,
                                rhi::Extent2D extent,
                                rhi::Format format,
                                const char* debugName) {
        if (!m_Device || !rhi::isValidExtent(extent)) {
            return false;
        }

        target = {};
        target.extent = extent;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = extent;
        textureDesc.format = format;
        textureDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        target.texture = m_Device->createTexture(textureDesc);
        if (!target.texture) {
            ARK_ERROR("SsaoPass failed to create {}", debugName);
            return false;
        }

        rhi::TextureViewDesc viewDesc{};
        viewDesc.format = format;
        target.view = m_Device->createTextureView(*target.texture, viewDesc);
        if (!target.view) {
            ARK_ERROR("SsaoPass failed to create {} view", debugName);
            return false;
        }

        return true;
    }

    bool SsaoPass::createDepthTarget(Target& target,
                                     rhi::Extent2D extent,
                                     const char* debugName) {
        if (!m_Device || !rhi::isValidExtent(extent)) {
            return false;
        }

        target = {};
        target.extent = extent;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = extent;
        textureDesc.format = rhi::Format::D32Float;
        textureDesc.usage = rhi::TextureUsage::DepthStencil;
        target.texture = m_Device->createTexture(textureDesc);
        if (!target.texture) {
            ARK_ERROR("SsaoPass failed to create {}", debugName);
            return false;
        }

        rhi::TextureViewDesc viewDesc{};
        viewDesc.format = textureDesc.format;
        target.view = m_Device->createTextureView(*target.texture, viewDesc);
        if (!target.view) {
            ARK_ERROR("SsaoPass failed to create {} view", debugName);
            return false;
        }

        return true;
    }

    bool SsaoPass::releaseTargetDeferred(rhi::DeviceContext& context, Target& target) {
        if (target.view && !context.deferReleaseTextureView(target.view)) {
            return false;
        }
        if (target.texture && !context.deferReleaseTexture(target.texture)) {
            return false;
        }

        target.extent = {};
        return true;
    }

    bool SsaoPass::releaseTargetsDeferred(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("SsaoPass requires DeviceContext for deferred target release");
            return false;
        }

        if (!releaseTargetDeferred(*frameContext.context, m_NormalDepthTarget) ||
            !releaseTargetDeferred(*frameContext.context, m_NormalDepthDepthTarget) ||
            !releaseTargetDeferred(*frameContext.context, m_OcclusionTarget) ||
            !releaseTargetDeferred(*frameContext.context, m_BlurTarget) ||
            !releaseTargetDeferred(*frameContext.context, m_CompositeTarget)) {
            return false;
        }

        m_FrameExtent = {};
        m_SsaoExtent = {};
        return true;
    }

    bool SsaoPass::updateGeometryDrawResources(FrameContext& frameContext,
                                               GeometryDrawResources& resources,
                                               const MaterialResource& material,
                                               const glm::mat4& modelMatrix) {
        if (!frameContext.context || !frameContext.view || !resources.uniformBuffer ||
            !resources.materialBuffer || !resources.descriptorSet) {
            return false;
        }

        const MaterialTextureSet& textures = material.textures();
        if (!textures.baseColor || !textures.baseColor->textureView() || !textures.baseColor->sampler()) {
            ARK_ERROR("SsaoPass requires a ready baseColor texture for alpha mask normal-depth");
            return false;
        }

        SsaoGeometryUniform uniform{};
        uniform.view = frameContext.view->viewMatrix();
        uniform.projection = frameContext.view->projectionMatrix();
        uniform.model = modelMatrix;
        uniform.normalMatrix = glm::inverseTranspose(modelMatrix);
        if (!frameContext.context->updateBuffer(*resources.uniformBuffer, &uniform, sizeof(uniform))) {
            return false;
        }

        const SsaoMaterialUniform materialUniform = makeSsaoMaterialUniform(material);
        if (!frameContext.context->updateBuffer(*resources.materialBuffer,
                                                &materialUniform,
                                                sizeof(materialUniform))) {
            return false;
        }

        rhi::SampledImageDescriptor baseColorImageDescriptor{};
        baseColorImageDescriptor.view = textures.baseColor->textureView();
        resources.descriptorSet->updateSampledImage(1, baseColorImageDescriptor);

        rhi::SamplerDescriptor baseColorSamplerDescriptor{};
        baseColorSamplerDescriptor.sampler = textures.baseColor->sampler();
        resources.descriptorSet->updateSampler(2, baseColorSamplerDescriptor);
        return true;
    }

    rhi::PipelineState* SsaoPass::selectGeometryPipeline(const MaterialRenderState& renderState) const {
        return renderState.doubleSided ? m_GeometryDoubleSidedPipeline.get()
                                       : m_GeometrySingleSidedPipeline.get();
    }

    rhi::PipelineState* SsaoPass::getOrCreateFullscreenPipeline() {
        if (!m_Device) {
            ARK_ERROR("SsaoPass requires RenderDevice");
            return nullptr;
        }

        if (m_FullscreenPipeline) {
            return m_FullscreenPipeline.get();
        }

        if (!m_FullscreenVertexShader || !m_FullscreenFragmentShader || !m_FullscreenPipelineLayout) {
            ARK_ERROR("SsaoPass requires fullscreen shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "SsaoFullscreenPipeline";
        pipelineDesc.vertexShader = m_FullscreenVertexShader.get();
        pipelineDesc.fragmentShader = m_FullscreenFragmentShader.get();
        pipelineDesc.layout = m_FullscreenPipelineLayout.get();
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = false;
        pipelineDesc.depthStencilState.enableDepthWrite = false;
        pipelineDesc.colorFormat = SsaoFormat;

        m_FullscreenPipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        return m_FullscreenPipeline.get();
    }

    bool SsaoPass::recordNormalDepthPass(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context ||
            !m_NormalDepthTarget.texture || !m_NormalDepthTarget.view ||
            !m_NormalDepthDepthTarget.texture || !m_NormalDepthDepthTarget.view) {
            return false;
        }

        const std::array<rhi::ResourceBarrier, 2> toRenderTarget{{
            rhi::ResourceBarrier{
                .texture = m_NormalDepthTarget.texture.get(),
                .before = m_NormalDepthTarget.texture->getState(),
                .after = rhi::ResourceState::RenderTarget,
            },
            rhi::ResourceBarrier{
                .texture = m_NormalDepthDepthTarget.texture.get(),
                .before = m_NormalDepthDepthTarget.texture->getState(),
                .after = rhi::ResourceState::DepthStencilWrite,
            },
        }};
        frameContext.context->pipelineBarrier(toRenderTarget);

        rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = m_SsaoExtent;
        renderingDesc.colorAttachment.view = m_NormalDepthTarget.view.get();
        renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = rhi::ClearColor{0.5f, 0.5f, 1.0f, 0.0f};
        // Normal-depth 是 SSAO 私有预通道，必须自己清深度并写入同分辨率 depth target；
        // 不能依赖 Forward pass 的 depth，因为主场景 depth attachment 的 storeOp 可能是 DontCare。
        renderingDesc.depthStencilAttachment.view = m_NormalDepthDepthTarget.view.get();
        renderingDesc.depthStencilAttachment.loadOp = rhi::LoadOp::Clear;
        renderingDesc.depthStencilAttachment.storeOp = rhi::StoreOp::DontCare;
        renderingDesc.depthStencilAttachment.clearDepth = 1.0f;
        if (!frameContext.context->beginRendering(renderingDesc)) {
            return false;
        }

        setTargetViewportAndScissor(*frameContext.context, m_SsaoExtent);

        const RenderQueue* queue = resolveSsaoQueue(frameContext);
        usize drawIndex = 0;
        if (queue) {
            for (const DrawItem& item : queue->drawItems()) {
                if (isBlendSkipped(item)) {
                    ++drawIndex;
                    continue;
                }

                if (!item.isDrawable() || frameSlot >= m_GeometryDrawResources.size() ||
                    drawIndex >= m_GeometryDrawResources[frameSlot].size()) {
                    frameContext.context->endRendering();
                    return false;
                }

                GeometryDrawResources& resources = m_GeometryDrawResources[frameSlot][drawIndex];
                rhi::PipelineState* pipeline = selectGeometryPipeline(item.material->renderState());
                if (!pipeline ||
                    !updateGeometryDrawResources(frameContext, resources, *item.material, item.modelMatrix)) {
                    frameContext.context->endRendering();
                    return false;
                }

                frameContext.context->setPipeline(*pipeline);
                frameContext.context->bindDescriptorSet(0, *resources.descriptorSet);
                item.mesh->bind(*frameContext.context);
                frameContext.context->drawIndexed(item.mesh->makeDrawIndexedDesc());
                ++drawIndex;
            }
        }

        frameContext.context->endRendering();

        const std::array<rhi::ResourceBarrier, 1> toShaderResource{{
            rhi::ResourceBarrier{
                .texture = m_NormalDepthTarget.texture.get(),
                .before = m_NormalDepthTarget.texture->getState(),
                .after = rhi::ResourceState::ShaderResource,
            },
        }};
        frameContext.context->pipelineBarrier(toShaderResource);
        return true;
    }

    bool SsaoPass::recordFullscreenPass(FrameContext& frameContext,
                                        u32 frameSlot,
                                        usize drawIndex,
                                        FullscreenMode mode,
                                        rhi::TextureView& source0,
                                        rhi::TextureView& source1,
                                        Target& target,
                                        const SsaoSettings& settings) {
        if (!frameContext.context || !target.texture || !target.view || !m_LinearSampler || !m_PointSampler) {
            ARK_ERROR("SsaoPass requires context, target and sampler for fullscreen pass");
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreateFullscreenPipeline();
        if (!pipeline) {
            return false;
        }

        if (frameSlot >= m_FullscreenDrawResources.size() ||
            drawIndex >= m_FullscreenDrawResources[frameSlot].size()) {
            ARK_ERROR("SsaoPass fullscreen draw resources were not prepared");
            return false;
        }

        FullscreenDrawResources& resources = m_FullscreenDrawResources[frameSlot][drawIndex];
        if (!resources.uniformBuffer || !resources.descriptorSet) {
            return false;
        }

        rhi::SampledImageDescriptor source0Descriptor{};
        source0Descriptor.view = &source0;
        resources.descriptorSet->updateSampledImage(0, source0Descriptor);

        rhi::SampledImageDescriptor source1Descriptor{};
        source1Descriptor.view = &source1;
        resources.descriptorSet->updateSampledImage(1, source1Descriptor);

        rhi::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.sampler =
            mode == FullscreenMode::Composite ? m_LinearSampler.get() : m_PointSampler.get();
        resources.descriptorSet->updateSampler(2, samplerDescriptor);

        const SsaoFullscreenUniform uniform = makeSsaoFullscreenUniform(
            static_cast<u32>(mode),
            settings,
            frameContext,
            textureViewExtent(source0),
            frameContext.extent);
        if (!frameContext.context->updateBuffer(*resources.uniformBuffer, &uniform, sizeof(uniform))) {
            return false;
        }

        const std::array<rhi::ResourceBarrier, 1> toRenderTarget{{
            rhi::ResourceBarrier{
                .texture = target.texture.get(),
                .before = target.texture->getState(),
                .after = rhi::ResourceState::RenderTarget,
            },
        }};
        frameContext.context->pipelineBarrier(toRenderTarget);

        rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = target.extent;
        renderingDesc.colorAttachment.view = target.view.get();
        renderingDesc.colorAttachment.loadOp = rhi::LoadOp::Clear;
        renderingDesc.colorAttachment.storeOp = rhi::StoreOp::Store;
        renderingDesc.colorAttachment.clearColor = rhi::ClearColor{1.0f, 1.0f, 1.0f, 1.0f};
        if (!frameContext.context->beginRendering(renderingDesc)) {
            return false;
        }

        setTargetViewportAndScissor(*frameContext.context, target.extent);
        frameContext.context->setPipeline(*pipeline);
        frameContext.context->bindDescriptorSet(0, *resources.descriptorSet);

        rhi::DrawDesc drawDesc{};
        drawDesc.vertexCount = 3;
        frameContext.context->draw(drawDesc);
        frameContext.context->endRendering();

        const std::array<rhi::ResourceBarrier, 1> toShaderResource{{
            rhi::ResourceBarrier{
                .texture = target.texture.get(),
                .before = target.texture->getState(),
                .after = rhi::ResourceState::ShaderResource,
            },
        }};
        frameContext.context->pipelineBarrier(toShaderResource);
        return true;
    }

    void SsaoPass::publishFrameBindings(FrameContext& frameContext) {
        frameContext.ssaoNormalDepthView = m_NormalDepthTarget.view.get();
        frameContext.ssaoOcclusionView = m_BlurTarget.view.get();
        frameContext.ssaoCompositeView = m_CompositeTarget.view.get();
        frameContext.ssaoExtent = m_SsaoExtent;
    }

    void SsaoPass::clearFrameBindings(FrameContext& frameContext) {
        frameContext.ssaoNormalDepthView = nullptr;
        frameContext.ssaoOcclusionView = nullptr;
        frameContext.ssaoCompositeView = nullptr;
        frameContext.ssaoExtent = {};
    }
} // namespace ark
