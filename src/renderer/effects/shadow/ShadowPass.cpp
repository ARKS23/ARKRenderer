#include "renderer/effects/shadow/ShadowPass.h"

#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/core/Bounds.h"
#include "renderer/core/FrameContext.h"
#include "renderer/resources/MeshResource.h"
#include "renderer/core/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/resources/TextureResource.h"
#include "renderer/effects/shadow/ShadowCascadeBuilder.h"
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
#include <string>
#include <utility>

namespace ark {
    namespace {
        struct alignas(16) ShadowUniform {
            glm::mat4 lightViewProjection{1.0f};
            glm::mat4 model{1.0f};
        };

        static_assert(sizeof(ShadowUniform) == 128);

        struct alignas(16) ShadowMaterialUniform {
            glm::vec4 baseColorFactor{1.0f};
            glm::vec4 baseColorUvTransform0{0.0f, 0.0f, 1.0f, 1.0f};
            glm::vec4 baseColorUvTransform1{0.0f};
            float alphaCutoff = 0.5f;
            float alphaMode = 0.0f;
            float baseColorTexCoord = 0.0f;
            float padding = 0.0f;
        };

        static_assert(sizeof(ShadowMaterialUniform) == 64);

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

        bool computeTexelSize(float rangeMin, float rangeMax, u32 mapExtent, float& texelSize) {
            if (mapExtent == 0) {
                return false;
            }

            const float rangeSize = rangeMax - rangeMin;
            if (!std::isfinite(rangeSize) || rangeSize <= 0.0f) {
                return false;
            }

            texelSize = rangeSize / static_cast<float>(mapExtent);
            if (!std::isfinite(texelSize) || texelSize <= 0.0f) {
                return false;
            }

            return true;
        }

        void snapHorizontalRangeToReferenceTexelGrid(float& rangeMin,
                                                     float& rangeMax,
                                                     float referenceCoord,
                                                     u32 mapExtent) {
            float texelSize = 0.0f;
            if (!computeTexelSize(rangeMin, rangeMax, mapExtent, texelSize)) {
                return;
            }

            if (!std::isfinite(referenceCoord)) {
                return;
            }

            const float referenceTexelCoord = (referenceCoord - rangeMin) / texelSize;
            const float snappedReferenceOffset =
                std::floor(referenceTexelCoord + 0.5f) * texelSize;
            const float snappedRangeMin = referenceCoord - snappedReferenceOffset;
            const float offset = snappedRangeMin - rangeMin;
            rangeMin += offset;
            rangeMax += offset;
        }

        void snapVerticalRangeToReferenceTexelGrid(float& rangeMin,
                                                   float& rangeMax,
                                                   float referenceCoord,
                                                   u32 mapExtent) {
            float texelSize = 0.0f;
            if (!computeTexelSize(rangeMin, rangeMax, mapExtent, texelSize)) {
                return;
            }

            if (!std::isfinite(referenceCoord)) {
                return;
            }

            // The Vulkan projection flips Y by negating the scale term after glm::orthoRH_ZO.
            // With that convention, final shadow texel Y is proportional to -(bottom + y).
            const float referenceTexelCoord = -(rangeMin + referenceCoord) / texelSize;
            const float snappedReferenceOffset =
                std::floor(referenceTexelCoord + 0.5f) * texelSize;
            const float snappedRangeMin = -referenceCoord - snappedReferenceOffset;
            const float offset = snappedRangeMin - rangeMin;
            rangeMin += offset;
            rangeMax += offset;
        }

        void stabilizeLightProjectionRange(float& left,
                                           float& right,
                                           float& bottom,
                                           float& top,
                                           const glm::mat4& lightView,
                                           const ShadowSettings& settings) {
            if (!settings.stabilizeProjection) {
                return;
            }

            const glm::vec4 stableReference = lightView * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
            snapHorizontalRangeToReferenceTexelGrid(left, right, stableReference.x, settings.mapExtent);
            snapVerticalRangeToReferenceTexelGrid(bottom, top, stableReference.y, settings.mapExtent);
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

            // 当前仍是单张 directional shadow map；先把 scene-fit projection 稳定到 shadow texel grid。
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
            stabilizeLightProjectionRange(left, right, bottom, top, lightView, settings);

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

        bool wantsCascadeShadowTarget(const ShadowSettings& settings) {
            return settings.cascades.enabled && settings.cascades.cascadeCount > 0;
        }

        bool isBlendShadowSkipped(const DrawItem& item) {
            return item.material &&
                   item.material->renderState().alphaMode == asset::AlphaMode::Blend;
        }

        glm::vec4 makeOffsetScale(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.offset[0], transform.offset[1], transform.scale[0], transform.scale[1]};
        }

        glm::vec4 makeRotation(const MaterialTextureTransform& transform) {
            return glm::vec4{transform.rotation, 0.0f, 0.0f, 0.0f};
        }

        ShadowMaterialUniform makeShadowMaterialUniform(const MaterialResource& material) {
            const MaterialFactors& factors = material.factors();
            const MaterialRenderState& renderState = material.renderState();
            const MaterialTextureCoordinateSet& textureCoordinates = material.textureCoordinates();
            const MaterialTextureTransformSet& textureTransforms = material.textureTransforms();

            ShadowMaterialUniform uniform{};
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

        rhi::CullMode makeShadowCullMode(const MaterialRenderState& renderState) {
            return renderState.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        }

        void clearFrameShadowBindings(FrameContext& frameContext) {
            frameContext.shadowMapView = nullptr;
            frameContext.shadowSampler = nullptr;
            frameContext.shadowStrength = 0.0f;
            frameContext.shadowBias = ShadowSettings{}.bias;
            frameContext.shadowFilterMode = static_cast<float>(ShadowFilterMode::Hard);
            frameContext.shadowFilterRadiusTexels = 0.0f;
            // 未来 CSM 数据和单 shadow binding 同生命周期；清空时避免 ForwardPass 读到旧帧 cascade。
            frameContext.cascadeShadows = {};
        }

        void publishFrameShadowBindings(FrameContext& frameContext,
                                        rhi::TextureView* shadowMapView,
                                        rhi::Sampler* shadowSampler) {
            const SceneLighting defaultLighting{};
            const SceneLighting& lighting = frameContext.scene ? frameContext.scene->lighting() : defaultLighting;
            const ShadowSettings& settings = frameContext.view->shadowSettings();
            frameContext.shadowMapView = shadowMapView;
            frameContext.shadowSampler = shadowSampler;
            frameContext.lightViewProjection = makeLightViewProjection(frameContext);
            frameContext.shadowStrength = settings.strength;
            frameContext.shadowBias = settings.bias;
            frameContext.shadowFilterMode = static_cast<float>(settings.filterMode);
            frameContext.shadowFilterRadiusTexels = settings.filterRadiusTexels;
            const Bounds3* casterBounds =
                frameContext.scene && frameContext.scene->hasBounds() ? &frameContext.scene->bounds() : nullptr;
            frameContext.cascadeShadows =
                buildCascadeShadowFrameData(*frameContext.view, lighting, settings, casterBounds);
        }
    } // namespace

    ShadowPass::~ShadowPass() = default;

    void ShadowPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    ShadowPass::ShadowTargetDesc ShadowPass::makeShadowTargetDesc(const ShadowSettings& settings) {
        ShadowTargetDesc targetDesc{};
        if (wantsCascadeShadowTarget(settings)) {
            // CSM 每一级 cascade 独占一个 array layer，边长使用 cascadeExtent 而不是单图 mapExtent。
            targetDesc.extent = rhi::Extent2D{settings.cascades.cascadeExtent,
                                              settings.cascades.cascadeExtent};
            targetDesc.layerCount = std::min(settings.cascades.cascadeCount, MaxShadowCascadeCount);
            targetDesc.useTextureArray = true;
            return targetDesc;
        }

        targetDesc.extent = rhi::Extent2D{settings.mapExtent, settings.mapExtent};
        targetDesc.layerCount = 1;
        targetDesc.useTextureArray = false;
        return targetDesc;
    }

    bool ShadowPass::prepare(FrameContext& frameContext) {
        if (!isShadowEnabled(frameContext)) {
            clearFrameShadowBindings(frameContext);
            return true;
        }

        if (!frameContext.context) {
            ARK_ERROR("ShadowPass requires DeviceContext");
            return false;
        }

        const ShadowTargetDesc targetDesc = makeShadowTargetDesc(frameContext.view->shadowSettings());
        if (!ensureShadowTarget(frameContext, targetDesc)) {
            return false;
        }

        if (!frameContext.queue || frameContext.queue->empty()) {
            publishFrameShadowBindings(frameContext, m_ShadowMapView.get(), m_ShadowSampler.get());
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

            if (!isBlendShadowSkipped(item) && !item.material->upload(*frameContext.context)) {
                return false;
            }
        }

        publishFrameShadowBindings(frameContext, m_ShadowMapView.get(), m_ShadowSampler.get());
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
        if (!m_SingleSidedPipeline || !m_DoubleSidedPipeline || frameSlot >= m_DrawResources.size()) {
            ARK_ERROR("ShadowPass requires descriptor and pipeline resources");
            return false;
        }

        const usize drawCount = frameContext.queue->size();
        const usize layerCount = frameContext.cascadeShadows.isEnabled()
                                     ? frameContext.cascadeShadows.cascadeCount
                                     : 1;
        if (layerCount == 0 || !ensureDrawResources(frameSlot, drawCount * layerCount)) {
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

        if (frameContext.cascadeShadows.isEnabled()) {
            // prepare 阶段按 settings 建资源，publish 阶段按相机/光源生成 cascade 数据；
            // 这里再做一次匹配检查，防止 UI 热更新时资源层数和帧数据错位。
            if (!m_ShadowUsesTextureArray ||
                frameContext.cascadeShadows.cascadeCount > m_ShadowLayerCount) {
                ARK_ERROR("ShadowPass CSM frame data does not match the prepared shadow texture array");
                return false;
            }

            // 整张 depth array 只做一次状态转换；每个 cascade 通过单层 view 依次清空并写入。
            for (u32 cascadeIndex = 0; cascadeIndex < frameContext.cascadeShadows.cascadeCount; ++cascadeIndex) {
                rhi::TextureView* cascadeView = shadowRenderTargetView(cascadeIndex);
                if (!cascadeView ||
                    !renderShadowLayer(frameContext,
                                       frameSlot,
                                       static_cast<usize>(cascadeIndex) * drawCount,
                                       *cascadeView,
                                       frameContext.cascadeShadows.cascades[cascadeIndex].lightViewProjection)) {
                    return false;
                }
            }
        } else {
            rhi::TextureView* shadowView = shadowRenderTargetView(0);
            if (!shadowView ||
                !renderShadowLayer(frameContext, frameSlot, 0, *shadowView, frameContext.lightViewProjection)) {
                return false;
            }
        }

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
        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
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

        auto makePipelineDesc = [&](const char* debugName, const MaterialRenderState& renderState) {
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
                .format = rhi::Format::R32G32Float,
                .offset = offsetof(asset::MeshVertex, uv0),
            });
            vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
                .location = 2,
                .format = rhi::Format::R32G32Float,
                .offset = offsetof(asset::MeshVertex, uv1),
            });

            rhi::GraphicsPipelineDesc pipelineDesc{};
            pipelineDesc.debugName = debugName;
            pipelineDesc.vertexShader = m_VertexShader.get();
            pipelineDesc.fragmentShader = m_FragmentShader.get();
            pipelineDesc.layout = m_PipelineLayout.get();
            pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
            pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
            pipelineDesc.rasterState.cullMode = makeShadowCullMode(renderState);
            pipelineDesc.rasterState.frontFace = rhi::FrontFace::CounterClockwise;
            pipelineDesc.depthStencilState.enableDepthTest = true;
            pipelineDesc.depthStencilState.enableDepthWrite = true;
            pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::Less;
            pipelineDesc.colorFormat = rhi::Format::Unknown;
            pipelineDesc.depthFormat = rhi::Format::D32Float;
            return pipelineDesc;
        };

        MaterialRenderState singleSided{};
        singleSided.doubleSided = false;
        MaterialRenderState doubleSided{};
        doubleSided.doubleSided = true;
        m_SingleSidedPipeline = m_Device->createGraphicsPipeline(
            makePipelineDesc("ShadowDepthPipeline.SingleSided", singleSided));
        m_DoubleSidedPipeline = m_Device->createGraphicsPipeline(
            makePipelineDesc("ShadowDepthPipeline.DoubleSided", doubleSided));
        return m_SingleSidedPipeline && m_DoubleSidedPipeline;
    }

    bool ShadowPass::ensureShadowTarget(FrameContext& frameContext, const ShadowTargetDesc& targetDesc) {
        if (!m_Device) {
            ARK_ERROR("ShadowPass requires RenderDevice for shadow target");
            return false;
        }

        if (!frameContext.context) {
            ARK_ERROR("ShadowPass requires DeviceContext for shadow target");
            return false;
        }

        if (!rhi::isValidExtent(targetDesc.extent) ||
            targetDesc.layerCount == 0 ||
            targetDesc.layerCount > MaxShadowCascadeCount) {
            ARK_ERROR("ShadowPass shadow target desc is invalid");
            return false;
        }

        if (m_ShadowMap && m_ShadowMapView && m_ShadowSampler &&
            m_ShadowExtent.width == targetDesc.extent.width &&
            m_ShadowExtent.height == targetDesc.extent.height &&
            m_ShadowLayerCount == targetDesc.layerCount &&
            m_ShadowUsesTextureArray == targetDesc.useTextureArray) {
            return true;
        }

        if (!releaseShadowTargetDeferred(frameContext)) {
            return false;
        }

        m_ShadowExtent = targetDesc.extent;
        m_ShadowLayerCount = targetDesc.layerCount;
        m_ShadowUsesTextureArray = targetDesc.useTextureArray;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = targetDesc.extent;
        textureDesc.format = rhi::Format::D32Float;
        textureDesc.arrayLayers = targetDesc.layerCount;
        textureDesc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::ShaderResource;
        m_ShadowMap = m_Device->createTexture(textureDesc);
        if (!m_ShadowMap) {
            return false;
        }

        rhi::TextureViewDesc viewDesc{};
        viewDesc.format = textureDesc.format;
        viewDesc.arrayLayerCount = targetDesc.layerCount;
        // m_ShadowMapView 是 ForwardPass 后续采样入口：单图时是 Texture2D，CSM 时覆盖整张 array。
        viewDesc.type = targetDesc.useTextureArray ? rhi::TextureViewType::Texture2DArray
                                                   : rhi::TextureViewType::Texture2D;
        m_ShadowMapView = m_Device->createTextureView(*m_ShadowMap, viewDesc);
        if (!m_ShadowMapView) {
            return false;
        }

        if (targetDesc.useTextureArray) {
            for (u32 layerIndex = 0; layerIndex < targetDesc.layerCount; ++layerIndex) {
                rhi::TextureViewDesc layerViewDesc{};
                layerViewDesc.format = textureDesc.format;
                layerViewDesc.baseArrayLayer = layerIndex;
                layerViewDesc.arrayLayerCount = 1;
                layerViewDesc.type = rhi::TextureViewType::Texture2D;
                // CSM 采样使用整张 texture array view；渲染时绑定单层 2D view，
                // 让 dynamic rendering 每次只清理和写入一个 cascade layer。
                m_ShadowCascadeViews[layerIndex] = m_Device->createTextureView(*m_ShadowMap, layerViewDesc);
                if (!m_ShadowCascadeViews[layerIndex]) {
                    return false;
                }
            }
        }

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = "ShadowMapSampler";
        samplerDesc.minFilter = rhi::FilterMode::Nearest;
        samplerDesc.magFilter = rhi::FilterMode::Nearest;
        samplerDesc.mipFilter = rhi::FilterMode::Nearest;
        samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
        m_ShadowSampler = m_Device->createSampler(samplerDesc);
        return m_ShadowSampler != nullptr;
    }

    bool ShadowPass::releaseShadowTargetDeferred(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("ShadowPass requires DeviceContext for deferred shadow target release");
            return false;
        }

        // Shadow map 尺寸和 CSM layer 数都可能由 UI 实时调整；旧资源要延迟释放，
        // 避免上一帧 GPU 命令仍在读取时被 CPU 立即销毁。
        if (m_ShadowMapView && !frameContext.context->deferReleaseTextureView(m_ShadowMapView)) {
            return false;
        }
        for (Scope<rhi::TextureView>& cascadeView : m_ShadowCascadeViews) {
            if (cascadeView && !frameContext.context->deferReleaseTextureView(cascadeView)) {
                return false;
            }
        }
        if (m_ShadowSampler && !frameContext.context->deferReleaseSampler(m_ShadowSampler)) {
            return false;
        }
        if (m_ShadowMap && !frameContext.context->deferReleaseTexture(m_ShadowMap)) {
            return false;
        }

        m_ShadowExtent = {};
        m_ShadowLayerCount = 0;
        m_ShadowUsesTextureArray = false;
        return true;
    }

    bool ShadowPass::ensureDrawResources(u32 frameSlot, usize requiredCount) {
        if (!m_Device || !m_DescriptorSetLayout || frameSlot >= m_DrawResources.size()) {
            return false;
        }

        std::vector<ShadowDrawResources>& resources = m_DrawResources[frameSlot];
        while (resources.size() < requiredCount) {
            ShadowDrawResources drawResources{};

            rhi::BufferDesc bufferDesc{};
            bufferDesc.debugName = "ShadowUniformBuffer." + std::to_string(resources.size());
            bufferDesc.size = sizeof(ShadowUniform);
            bufferDesc.usage = rhi::BufferUsage::Uniform;
            bufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.uniformBuffer = m_Device->createBuffer(bufferDesc);

            rhi::BufferDesc materialBufferDesc{};
            materialBufferDesc.debugName = "ShadowMaterialUniformBuffer." + std::to_string(resources.size());
            materialBufferDesc.size = sizeof(ShadowMaterialUniform);
            materialBufferDesc.usage = rhi::BufferUsage::Uniform;
            materialBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.materialBuffer = m_Device->createBuffer(materialBufferDesc);

            drawResources.descriptorSet = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!drawResources.uniformBuffer || !drawResources.materialBuffer || !drawResources.descriptorSet) {
                return false;
            }

            rhi::BufferDescriptor bufferDescriptor{};
            bufferDescriptor.buffer = drawResources.uniformBuffer.get();
            bufferDescriptor.range = sizeof(ShadowUniform);
            drawResources.descriptorSet->updateUniformBuffer(0, bufferDescriptor);

            rhi::BufferDescriptor materialBufferDescriptor{};
            materialBufferDescriptor.buffer = drawResources.materialBuffer.get();
            materialBufferDescriptor.range = sizeof(ShadowMaterialUniform);
            drawResources.descriptorSet->updateUniformBuffer(3, materialBufferDescriptor);
            resources.push_back(std::move(drawResources));
        }

        return true;
    }

    bool ShadowPass::updateDrawResources(FrameContext& frameContext,
                                         ShadowDrawResources& drawResources,
                                         const MaterialResource& material,
                                         const glm::mat4& lightViewProjection,
                                         const glm::mat4& modelMatrix) {
        if (!frameContext.context || !drawResources.uniformBuffer || !drawResources.materialBuffer ||
            !drawResources.descriptorSet) {
            return false;
        }

        const MaterialTextureSet& textures = material.textures();
        if (!textures.baseColor || !textures.baseColor->textureView() || !textures.baseColor->sampler()) {
            ARK_ERROR("ShadowPass requires a ready baseColor texture for alpha mask shadow caster");
            return false;
        }

        ShadowUniform uniform{};
        uniform.lightViewProjection = lightViewProjection;
        uniform.model = modelMatrix;
        if (!frameContext.context->updateBuffer(*drawResources.uniformBuffer, &uniform, sizeof(uniform))) {
            return false;
        }

        const ShadowMaterialUniform materialUniform = makeShadowMaterialUniform(material);
        if (!frameContext.context->updateBuffer(*drawResources.materialBuffer, &materialUniform, sizeof(materialUniform))) {
            return false;
        }

        rhi::SampledImageDescriptor baseColorImageDescriptor{};
        baseColorImageDescriptor.view = textures.baseColor->textureView();
        drawResources.descriptorSet->updateSampledImage(1, baseColorImageDescriptor);

        rhi::SamplerDescriptor baseColorSamplerDescriptor{};
        baseColorSamplerDescriptor.sampler = textures.baseColor->sampler();
        drawResources.descriptorSet->updateSampler(2, baseColorSamplerDescriptor);
        return true;
    }

    rhi::PipelineState* ShadowPass::selectPipeline(const MaterialRenderState& renderState) const {
        return renderState.doubleSided ? m_DoubleSidedPipeline.get() : m_SingleSidedPipeline.get();
    }

    rhi::TextureView* ShadowPass::shadowRenderTargetView(u32 layerIndex) const {
        if (m_ShadowUsesTextureArray) {
            if (layerIndex >= m_ShadowCascadeViews.size()) {
                return nullptr;
            }
            // 渲染 attachment 必须是单层 view；采样用的 m_ShadowMapView 覆盖整张 array，不能直接拿来清一层。
            return m_ShadowCascadeViews[layerIndex].get();
        }

        return layerIndex == 0 ? m_ShadowMapView.get() : nullptr;
    }

    bool ShadowPass::beginShadowRendering(FrameContext& frameContext, rhi::TextureView& depthView) {
        rhi::RenderingDesc renderingDesc{};
        renderingDesc.extent = m_ShadowExtent;
        renderingDesc.depthStencilAttachment.view = &depthView;
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

    bool ShadowPass::renderShadowLayer(FrameContext& frameContext,
                                       u32 frameSlot,
                                       usize layerResourceBase,
                                       rhi::TextureView& depthView,
                                       const glm::mat4& lightViewProjection) {
        if (!beginShadowRendering(frameContext, depthView)) {
            return false;
        }

        setViewportAndScissor(frameContext);
        usize drawIndex = 0;

        // 单图阴影和 CSM 每层都执行同一份深度绘制逻辑，区别只在 light VP 和 depth attachment。
        for (const DrawItem& item : frameContext.queue->drawItems()) {
            if (isBlendShadowSkipped(item)) {
                ++drawIndex;
                continue;
            }

            const usize resourceIndex = layerResourceBase + drawIndex;
            if (!item.isDrawable() ||
                frameSlot >= m_DrawResources.size() ||
                resourceIndex >= m_DrawResources[frameSlot].size()) {
                frameContext.context->endRendering();
                return false;
            }

            ShadowDrawResources& drawResources = m_DrawResources[frameSlot][resourceIndex];
            if (!drawResources.uniformBuffer || !drawResources.materialBuffer || !drawResources.descriptorSet) {
                frameContext.context->endRendering();
                return false;
            }

            rhi::PipelineState* pipeline = selectPipeline(item.material->renderState());
            if (!pipeline ||
                !updateDrawResources(frameContext, drawResources, *item.material, lightViewProjection, item.modelMatrix)) {
                frameContext.context->endRendering();
                return false;
            }

            frameContext.context->setPipeline(*pipeline);
            // 每个 draw 使用独立 uniform buffer，避免同一地址在命令录制期间被后续 draw 覆盖。
            frameContext.context->bindDescriptorSet(0, *drawResources.descriptorSet);
            item.mesh->bind(*frameContext.context);
            frameContext.context->drawIndexed(item.mesh->makeDrawIndexedDesc());
            ++drawIndex;
        }

        frameContext.context->endRendering();
        return true;
    }
} // namespace ark
