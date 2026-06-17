#include "renderer/passes/ShadowPass.h"

#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/Bounds.h"
#include "renderer/FrameContext.h"
#include "renderer/MeshResource.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/material/MaterialResource.h"
#include "rhi/DeviceContext.h"
#include "rhi/FrameResource.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ark {
    namespace {
        struct alignas(16) ShadowUniform {
            glm::mat4 lightViewProjection{1.0f};
            glm::mat4 model{1.0f};
        };

        static_assert(sizeof(ShadowUniform) == 128);

        bool isShadowEnabled(const FrameContext& frameContext) {
            return frameContext.view && frameContext.view->shadowSettings().enabled &&
                   frameContext.view->shadowSettings().strength > 0.0f;
        }

        glm::vec3 normalizeLightDirection(const glm::vec3& direction) {
            constexpr float MinDirectionLengthSquared = 1.0e-6f;
            if (glm::dot(direction, direction) <= MinDirectionLengthSquared) {
                return glm::normalize(SceneLighting{}.mainLight.direction);
            }

            return glm::normalize(direction);
        }

        glm::vec3 chooseLightUp(const glm::vec3& lightDirection) {
            const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(worldUp, lightDirection)) < 0.95f) {
                return worldUp;
            }

            return glm::vec3{0.0f, 0.0f, 1.0f};
        }

        void expandRangeToMinHalfExtent(float& rangeMin, float& rangeMax, float minHalfExtent) {
            const float center = (rangeMin + rangeMax) * 0.5f;
            const float halfExtent = std::max((rangeMax - rangeMin) * 0.5f, minHalfExtent);
            rangeMin = center - halfExtent;
            rangeMax = center + halfExtent;
        }

        bool isValidRange(float rangeMin, float rangeMax) {
            return std::isfinite(rangeMin) && std::isfinite(rangeMax) && rangeMin <= rangeMax;
        }

        glm::mat4 makeFixedLightViewProjection(const SceneLighting& lighting, const ShadowSettings& settings) {
            const glm::vec3 lightDirection = normalizeLightDirection(lighting.mainLight.direction);
            const glm::vec3 lightTarget{0.0f, 0.0f, 0.0f};
            const glm::vec3 lightPosition = lightTarget - lightDirection * settings.lightDistance;
            const glm::mat4 lightView = glm::lookAt(lightPosition,
                                                    lightTarget,
                                                    chooseLightUp(lightDirection));
            glm::mat4 lightProjection = glm::orthoRH_ZO(-settings.orthographicHalfExtent,
                                                        settings.orthographicHalfExtent,
                                                        -settings.orthographicHalfExtent,
                                                        settings.orthographicHalfExtent,
                                                        settings.nearPlane,
                                                        settings.farPlane);
            lightProjection[1][1] *= -1.0f;
            return lightProjection * lightView;
        }

        glm::mat4 makeSceneFitLightViewProjection(const RenderScene& scene,
                                                  const SceneLighting& lighting,
                                                  const ShadowSettings& settings) {
            const Bounds3& sceneBounds = scene.bounds();
            const glm::vec3 lightDirection = normalizeLightDirection(lighting.mainLight.direction);
            const glm::vec3 lightTarget = sceneBounds.center();
            const glm::vec3 sceneHalfExtent = sceneBounds.halfExtent();
            const float sceneRadius = glm::length(sceneHalfExtent);
            const float padding = std::max(0.5f, sceneRadius * 0.1f);
            const float lightDistance =
                std::max(settings.lightDistance, sceneRadius + padding + settings.nearPlane);
            const glm::vec3 lightPosition = lightTarget - lightDirection * lightDistance;
            const glm::mat4 lightView = glm::lookAt(lightPosition,
                                                    lightTarget,
                                                    chooseLightUp(lightDirection));

            // 第一版只用 scene world AABB 拟合单张 directional shadow map；CSM 和 texel snapping 后续再接。
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            float bottom = std::numeric_limits<float>::max();
            float top = std::numeric_limits<float>::lowest();
            float minDepth = std::numeric_limits<float>::max();
            float maxDepth = std::numeric_limits<float>::lowest();

            for (const glm::vec3& corner : boundsCorners(sceneBounds)) {
                const glm::vec4 lightCorner = lightView * glm::vec4{corner, 1.0f};
                left = std::min(left, lightCorner.x);
                right = std::max(right, lightCorner.x);
                bottom = std::min(bottom, lightCorner.y);
                top = std::max(top, lightCorner.y);

                const float depth = -lightCorner.z;
                minDepth = std::min(minDepth, depth);
                maxDepth = std::max(maxDepth, depth);
            }

            if (!isValidRange(left, right) || !isValidRange(bottom, top) || !isValidRange(minDepth, maxDepth)) {
                return makeFixedLightViewProjection(lighting, settings);
            }

            left -= padding;
            right += padding;
            bottom -= padding;
            top += padding;
            expandRangeToMinHalfExtent(left, right, settings.orthographicHalfExtent);
            expandRangeToMinHalfExtent(bottom, top, settings.orthographicHalfExtent);

            const float nearPlane = std::max(settings.nearPlane, minDepth - padding);
            const float farPlane = std::max({settings.farPlane, maxDepth + padding, nearPlane + 0.01f});

            glm::mat4 lightProjection = glm::orthoRH_ZO(left, right, bottom, top, nearPlane, farPlane);
            lightProjection[1][1] *= -1.0f;
            return lightProjection * lightView;
        }

        glm::mat4 makeLightViewProjection(const FrameContext& frameContext) {
            const SceneLighting defaultLighting{};
            const SceneLighting& lighting = frameContext.scene ? frameContext.scene->lighting() : defaultLighting;
            const ShadowSettings& settings = frameContext.view->shadowSettings();
            if (settings.fitSceneBounds && frameContext.scene && frameContext.scene->hasBounds()) {
                return makeSceneFitLightViewProjection(*frameContext.scene, lighting, settings);
            }

            return makeFixedLightViewProjection(lighting, settings);
        }

        rhi::Extent2D makeShadowExtent(const ShadowSettings& settings) {
            return rhi::Extent2D{settings.mapExtent, settings.mapExtent};
        }
    } // namespace

    ShadowPass::~ShadowPass() = default;

    void ShadowPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool ShadowPass::prepare(FrameContext& frameContext) {
        if (!isShadowEnabled(frameContext)) {
            frameContext.shadowMapView = nullptr;
            frameContext.shadowSampler = nullptr;
            frameContext.shadowStrength = 0.0f;
            return true;
        }

        if (!frameContext.context) {
            ARK_ERROR("ShadowPass requires DeviceContext");
            return false;
        }

        const rhi::Extent2D shadowExtent = makeShadowExtent(frameContext.view->shadowSettings());
        if (!ensureShadowTarget(shadowExtent)) {
            return false;
        }

        if (!frameContext.queue || frameContext.queue->empty()) {
            frameContext.shadowMapView = m_ShadowMapView.get();
            frameContext.shadowSampler = m_ShadowSampler.get();
            frameContext.lightViewProjection = makeLightViewProjection(frameContext);
            frameContext.shadowStrength = frameContext.view->shadowSettings().strength;
            frameContext.shadowBias = frameContext.view->shadowSettings().bias;
            return true;
        }

        for (const DrawItem& item : frameContext.queue->drawItems()) {
            if (!item.isDrawable()) {
                ARK_ERROR("ShadowPass queue contains an invalid draw item");
                return false;
            }

            if (!item.mesh->upload(*frameContext.context)) {
                return false;
            }
        }

        frameContext.shadowMapView = m_ShadowMapView.get();
        frameContext.shadowSampler = m_ShadowSampler.get();
        frameContext.lightViewProjection = makeLightViewProjection(frameContext);
        frameContext.shadowStrength = frameContext.view->shadowSettings().strength;
        frameContext.shadowBias = frameContext.view->shadowSettings().bias;
        return true;
    }

    bool ShadowPass::execute(FrameContext& frameContext) {
        if (!isShadowEnabled(frameContext) || !frameContext.queue || frameContext.queue->empty()) {
            return true;
        }

        if (!frameContext.context || !m_ShadowMap || !m_ShadowMapView || !m_ShadowSampler) {
            ARK_ERROR("ShadowPass requires prepared shadow resources");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!m_Pipeline || frameSlot >= m_DescriptorSets.size() ||
            !m_DescriptorSets[frameSlot] || !m_ShadowBuffers[frameSlot]) {
            ARK_ERROR("ShadowPass requires descriptor and pipeline resources");
            return false;
        }

        const std::array<rhi::ResourceBarrier, 1> toDepthWrite{{
            rhi::ResourceBarrier{
                .texture = m_ShadowMap.get(),
                .before = m_ShadowMap->getState(),
                .after = rhi::ResourceState::DepthStencilWrite,
            },
        }};
        frameContext.context->pipelineBarrier(toDepthWrite);

        if (!beginShadowRendering(frameContext)) {
            return false;
        }

        setViewportAndScissor(frameContext);
        frameContext.context->setPipeline(*m_Pipeline);
        frameContext.context->bindDescriptorSet(0, *m_DescriptorSets[frameSlot]);

        for (const DrawItem& item : frameContext.queue->drawItems()) {
            if (item.material &&
                item.material->renderState().alphaMode == asset::AlphaMode::Blend) {
                continue;
            }

            if (!item.isDrawable() || !updateShadowUniform(frameContext, frameSlot)) {
                frameContext.context->endRendering();
                return false;
            }

            ShadowUniform uniform{};
            uniform.lightViewProjection = frameContext.lightViewProjection;
            uniform.model = item.modelMatrix;
            if (!frameContext.context->updateBuffer(*m_ShadowBuffers[frameSlot], &uniform, sizeof(uniform))) {
                frameContext.context->endRendering();
                return false;
            }

            item.mesh->bind(*frameContext.context);
            frameContext.context->drawIndexed(item.mesh->makeDrawIndexedDesc());
        }

        frameContext.context->endRendering();

        const std::array<rhi::ResourceBarrier, 1> toShaderResource{{
            rhi::ResourceBarrier{
                .texture = m_ShadowMap.get(),
                .before = m_ShadowMap->getState(),
                .after = rhi::ResourceState::ShaderResource,
            },
        }};
        frameContext.context->pipelineBarrier(toShaderResource);
        return true;
    }

    bool ShadowPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("ShadowPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ShadowDescriptorSetLayout";
        layoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
        });
        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        for (u32 frameSlot = 0; frameSlot < FramesInFlight; ++frameSlot) {
            rhi::BufferDesc bufferDesc{};
            bufferDesc.debugName = "ShadowUniformBuffer";
            bufferDesc.size = sizeof(ShadowUniform);
            bufferDesc.usage = rhi::BufferUsage::Uniform;
            bufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_ShadowBuffers[frameSlot] = m_Device->createBuffer(bufferDesc);
            m_DescriptorSets[frameSlot] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_ShadowBuffers[frameSlot] || !m_DescriptorSets[frameSlot]) {
                return false;
            }

            rhi::BufferDescriptor bufferDescriptor{};
            bufferDescriptor.buffer = m_ShadowBuffers[frameSlot].get();
            bufferDescriptor.range = sizeof(ShadowUniform);
            m_DescriptorSets[frameSlot]->updateUniformBuffer(0, bufferDescriptor);
        }

        return true;
    }

    bool ShadowPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("ShadowPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "ShadowVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("shadow.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "ShadowFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("shadow.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool ShadowPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("ShadowPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ShadowPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        if (!m_PipelineLayout) {
            return false;
        }

        if (!m_VertexShader || !m_FragmentShader) {
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

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "ShadowDepthPipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::Back;
        pipelineDesc.rasterState.frontFace = rhi::FrontFace::CounterClockwise;
        pipelineDesc.depthStencilState.enableDepthTest = true;
        pipelineDesc.depthStencilState.enableDepthWrite = true;
        pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::Less;
        pipelineDesc.colorFormat = rhi::Format::Unknown;
        pipelineDesc.depthFormat = rhi::Format::D32Float;
        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        return m_Pipeline != nullptr;
    }

    bool ShadowPass::ensureShadowTarget(rhi::Extent2D extent) {
        if (!m_Device) {
            ARK_ERROR("ShadowPass requires RenderDevice for shadow target");
            return false;
        }

        if (m_ShadowMap && m_ShadowMapView && m_ShadowSampler &&
            m_ShadowExtent.width == extent.width && m_ShadowExtent.height == extent.height) {
            return true;
        }

        m_ShadowMapView.reset();
        m_ShadowMap.reset();
        m_ShadowSampler.reset();
        m_ShadowExtent = extent;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = extent;
        textureDesc.format = rhi::Format::D32Float;
        textureDesc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::ShaderResource;
        m_ShadowMap = m_Device->createTexture(textureDesc);
        if (!m_ShadowMap) {
            return false;
        }

        rhi::TextureViewDesc viewDesc{};
        viewDesc.format = textureDesc.format;
        m_ShadowMapView = m_Device->createTextureView(*m_ShadowMap, viewDesc);
        if (!m_ShadowMapView) {
            return false;
        }

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = "ShadowMapSampler";
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.mipFilter = rhi::FilterMode::Nearest;
        samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
        m_ShadowSampler = m_Device->createSampler(samplerDesc);
        return m_ShadowSampler != nullptr;
    }

    bool ShadowPass::updateShadowUniform(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context || frameSlot >= m_ShadowBuffers.size() || !m_ShadowBuffers[frameSlot]) {
            return false;
        }

        return true;
    }

    bool ShadowPass::beginShadowRendering(FrameContext& frameContext) {
        rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = m_ShadowExtent;
        renderingDesc.depthStencilAttachment.view = m_ShadowMapView.get();
        renderingDesc.depthStencilAttachment.loadOp = rhi::LoadOp::Clear;
        renderingDesc.depthStencilAttachment.storeOp = rhi::StoreOp::Store;
        renderingDesc.depthStencilAttachment.clearDepth = 1.0f;
        return frameContext.context->beginRendering(renderingDesc);
    }

    void ShadowPass::setViewportAndScissor(FrameContext& frameContext) {
        rhi::Viewport viewport{};
        viewport.width = static_cast<float>(m_ShadowExtent.width);
        viewport.height = static_cast<float>(m_ShadowExtent.height);
        frameContext.context->setViewport(viewport);

        rhi::ScissorRect scissor{};
        scissor.width = m_ShadowExtent.width;
        scissor.height = m_ShadowExtent.height;
        frameContext.context->setScissorRect(scissor);
    }
} // namespace ark
