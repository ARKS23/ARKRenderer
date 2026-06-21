#include "renderer/effects/bloom/BloomPass.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderView.h"
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

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace ark {
    namespace {
        constexpr rhi::Format BloomFormat = rhi::Format::RGBA16Float;

        struct alignas(16) BloomUniform {
            u32 mode = 0;
            float intensity = 0.0f;
            float scatter = 0.6f;
            float threshold = 1.0f;
            float softKnee = 0.5f;
            float texelSizeX = 1.0f;
            float texelSizeY = 1.0f;
            float padding0 = 0.0f;
        };

        static_assert(sizeof(BloomUniform) == 32);

        bool sameExtent(rhi::Extent2D lhs, rhi::Extent2D rhs) {
            return lhs.width == rhs.width && lhs.height == rhs.height;
        }

        rhi::Extent2D halfExtent(rhi::Extent2D extent) {
            return rhi::Extent2D{
                .width = std::max(1u, extent.width / 2u),
                .height = std::max(1u, extent.height / 2u),
            };
        }

        u32 calculateBloomLevelCount(rhi::Extent2D extent, u32 maxMipCount) {
            if (!rhi::isValidExtent(extent) || maxMipCount == 0) {
                return 0;
            }

            rhi::Extent2D levelExtent = halfExtent(extent);
            u32 levels = 0;
            while (rhi::isValidExtent(levelExtent) && levels < maxMipCount) {
                ++levels;
                if (levelExtent.width == 1 && levelExtent.height == 1) {
                    break;
                }

                levelExtent = halfExtent(levelExtent);
            }

            return levels;
        }

        rhi::Extent2D textureViewExtent(const rhi::TextureView& view) {
            const rhi::Texture* texture = view.getTexture();
            if (!texture) {
                return {};
            }

            return texture->getDesc().extent;
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

        BloomUniform makeBloomUniform(u32 mode,
                                      const BloomSettings& settings,
                                      rhi::Extent2D sourceExtent) {
            BloomUniform uniform{};
            uniform.mode = mode;
            uniform.intensity = settings.intensity;
            uniform.scatter = settings.scatter;
            uniform.threshold = settings.threshold;
            uniform.softKnee = settings.softKnee;
            uniform.texelSizeX = sourceExtent.width > 0 ? 1.0f / static_cast<float>(sourceExtent.width) : 1.0f;
            uniform.texelSizeY = sourceExtent.height > 0 ? 1.0f / static_cast<float>(sourceExtent.height) : 1.0f;
            return uniform;
        }
    } // namespace

    BloomPass::~BloomPass() = default;

    void BloomPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool BloomPass::prepare(FrameContext& frameContext) {
        const PostProcessingSettings defaultSettings{};
        const PostProcessingSettings& postProcessing =
            frameContext.view ? frameContext.view->postProcessingSettings() : defaultSettings;
        const BloomSettings& settings = postProcessing.bloom;
        if (!settings.enabled) {
            return true;
        }

        if (!frameContext.context || !frameContext.sceneColorView) {
            ARK_ERROR("BloomPass requires DeviceContext and scene color view");
            return false;
        }

        if (!ensureTargets(frameContext, frameContext.extent, settings)) {
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        const usize drawCount = static_cast<usize>(m_LevelCount) * 2u;
        return ensureDrawResources(frameSlot, drawCount);
    }

    bool BloomPass::execute(FrameContext& frameContext) {
        const PostProcessingSettings defaultSettings{};
        const PostProcessingSettings& postProcessing =
            frameContext.view ? frameContext.view->postProcessingSettings() : defaultSettings;
        const BloomSettings& settings = postProcessing.bloom;
        if (!settings.enabled) {
            return true;
        }

        if (!frameContext.context || !frameContext.sceneColorView) {
            ARK_ERROR("BloomPass requires DeviceContext and scene color view");
            return false;
        }

        if (!ensureTargets(frameContext, frameContext.extent, settings)) {
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        const usize drawCount = static_cast<usize>(m_LevelCount) * 2u;
        if (!ensureDrawResources(frameSlot, drawCount)) {
            return false;
        }

        rhi::TextureView* originalSceneColorView = frameContext.sceneColorView;
        usize drawIndex = 0;
        if (!recordFullscreenPass(frameContext,
                                  frameSlot,
                                  drawIndex++,
                                  Mode::Prefilter,
                                  *originalSceneColorView,
                                  *originalSceneColorView,
                                  m_DownsampleTargets[0],
                                  settings)) {
            return false;
        }

        for (u32 level = 1; level < m_LevelCount; ++level) {
            rhi::TextureView& source = *m_DownsampleTargets[level - 1].view;
            if (!recordFullscreenPass(frameContext,
                                      frameSlot,
                                      drawIndex++,
                                      Mode::Downsample,
                                      source,
                                      source,
                                      m_DownsampleTargets[level],
                                      settings)) {
                return false;
            }
        }

        rhi::TextureView* bloomResultView = m_DownsampleTargets[0].view.get();
        if (m_LevelCount > 1) {
            for (u32 level = m_LevelCount - 1; level > 0; --level) {
                const u32 targetLevel = level - 1;
                rhi::TextureView* smallerView =
                    level == m_LevelCount - 1 ? m_DownsampleTargets[level].view.get()
                                              : m_UpsampleTargets[level].view.get();
                rhi::TextureView* currentView = m_DownsampleTargets[targetLevel].view.get();
                if (!smallerView || !currentView ||
                    !recordFullscreenPass(frameContext,
                                          frameSlot,
                                          drawIndex++,
                                          Mode::Upsample,
                                          *smallerView,
                                          *currentView,
                                          m_UpsampleTargets[targetLevel],
                                          settings)) {
                    return false;
                }
            }

            bloomResultView = m_UpsampleTargets[0].view.get();
        }

        if (!bloomResultView ||
            !recordFullscreenPass(frameContext,
                                  frameSlot,
                                  drawIndex++,
                                  Mode::Composite,
                                  *originalSceneColorView,
                                  *bloomResultView,
                                  m_CompositeTarget,
                                  settings)) {
            return false;
        }

        frameContext.sceneColorView = m_CompositeTarget.view.get();
        return frameContext.sceneColorView != nullptr;
    }

    bool BloomPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("BloomPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc layoutDesc{};
        layoutDesc.debugName = "BloomDescriptorSetLayout";
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

        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(layoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = "BloomLinearClampSampler";
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.mipFilter = rhi::FilterMode::Linear;
        samplerDesc.addressU = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressV = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressW = rhi::AddressMode::ClampToEdge;
        m_Sampler = m_Device->createSampler(samplerDesc);
        return m_Sampler != nullptr;
    }

    bool BloomPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("BloomPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "BloomVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("tonemap.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "BloomFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("bloom.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool BloomPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("BloomPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "BloomPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    bool BloomPass::ensureTargets(FrameContext& frameContext, rhi::Extent2D extent, const BloomSettings& settings) {
        if (!m_Device) {
            ARK_ERROR("BloomPass requires RenderDevice before creating targets");
            return false;
        }

        if (!frameContext.context) {
            ARK_ERROR("BloomPass requires DeviceContext before creating targets");
            return false;
        }

        if (!rhi::isValidExtent(extent)) {
            ARK_ERROR("BloomPass requires a valid extent");
            return false;
        }

        const u32 levelCount = calculateBloomLevelCount(extent, settings.maxMipCount);
        if (levelCount == 0) {
            ARK_ERROR("BloomPass requires at least one bloom level");
            return false;
        }

        if (sameExtent(m_Extent, extent) && m_LevelCount == levelCount &&
            m_DownsampleTargets.size() == levelCount &&
            m_CompositeTarget.view) {
            return true;
        }

        if (!releaseTargetsDeferred(frameContext)) {
            return false;
        }

        m_LevelCount = 0;

        m_DownsampleTargets.resize(levelCount);
        m_UpsampleTargets.resize(levelCount);

        rhi::Extent2D levelExtent = halfExtent(extent);
        for (u32 level = 0; level < levelCount; ++level) {
            const std::string downsampleName = "BloomDownsampleTarget." + std::to_string(level);
            if (!createTarget(m_DownsampleTargets[level], levelExtent, downsampleName.c_str())) {
                return false;
            }

            const std::string upsampleName = "BloomUpsampleTarget." + std::to_string(level);
            if (!createTarget(m_UpsampleTargets[level], levelExtent, upsampleName.c_str())) {
                return false;
            }

            levelExtent = halfExtent(levelExtent);
        }

        if (!createTarget(m_CompositeTarget, extent, "BloomCompositeTarget")) {
            return false;
        }

        m_Extent = extent;
        m_LevelCount = levelCount;
        return true;
    }

    bool BloomPass::ensureDrawResources(u32 frameSlot, usize drawCount) {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("BloomPass requires descriptor layout before draw resources");
            return false;
        }

        if (frameSlot >= m_DrawResources.size()) {
            ARK_ERROR("BloomPass frame slot is out of range");
            return false;
        }

        std::vector<DrawResources>& resources = m_DrawResources[frameSlot];
        while (resources.size() < drawCount) {
            const usize resourceIndex = resources.size();

            DrawResources drawResources{};
            rhi::BufferDesc uniformBufferDesc{};
            uniformBufferDesc.debugName = "BloomUniformBuffer." + std::to_string(resourceIndex);
            uniformBufferDesc.size = sizeof(BloomUniform);
            uniformBufferDesc.usage = rhi::BufferUsage::Uniform;
            uniformBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            drawResources.uniformBuffer = m_Device->createBuffer(uniformBufferDesc);
            drawResources.descriptorSet = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!drawResources.uniformBuffer || !drawResources.descriptorSet) {
                return false;
            }

            rhi::BufferDescriptor uniformDescriptor{};
            uniformDescriptor.buffer = drawResources.uniformBuffer.get();
            uniformDescriptor.range = sizeof(BloomUniform);
            drawResources.descriptorSet->updateUniformBuffer(3, uniformDescriptor);

            resources.push_back(std::move(drawResources));
        }

        return true;
    }

    bool BloomPass::releaseTargetDeferred(rhi::DeviceContext& context, Target& target) {
        // UI 调整 max mips / viewport 时会运行期重建 bloom RT；旧资源必须等当前 frame slot 的 fence signal 后再析构。
        if (target.view && !context.deferReleaseTextureView(target.view)) {
            return false;
        }
        if (target.texture && !context.deferReleaseTexture(target.texture)) {
            return false;
        }

        target.extent = {};
        return true;
    }

    bool BloomPass::releaseTargetsDeferred(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("BloomPass requires DeviceContext for deferred target release");
            return false;
        }

        for (Target& target : m_DownsampleTargets) {
            if (!releaseTargetDeferred(*frameContext.context, target)) {
                return false;
            }
        }
        for (Target& target : m_UpsampleTargets) {
            if (!releaseTargetDeferred(*frameContext.context, target)) {
                return false;
            }
        }
        if (!releaseTargetDeferred(*frameContext.context, m_CompositeTarget)) {
            return false;
        }

        m_DownsampleTargets.clear();
        m_UpsampleTargets.clear();
        m_CompositeTarget = {};
        m_Extent = {};
        return true;
    }

    bool BloomPass::createTarget(Target& target, rhi::Extent2D extent, const char* debugName) {
        if (!m_Device || !rhi::isValidExtent(extent)) {
            return false;
        }

        target = {};
        target.extent = extent;

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = extent;
        textureDesc.format = BloomFormat;
        textureDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        target.texture = m_Device->createTexture(textureDesc);
        if (!target.texture) {
            ARK_ERROR("BloomPass failed to create {}", debugName);
            return false;
        }

        rhi::TextureViewDesc viewDesc{};
        viewDesc.format = BloomFormat;
        target.view = m_Device->createTextureView(*target.texture, viewDesc);
        if (!target.view) {
            ARK_ERROR("BloomPass failed to create {} view", debugName);
            return false;
        }

        return true;
    }

    bool BloomPass::recordFullscreenPass(FrameContext& frameContext,
                                         u32 frameSlot,
                                         usize drawIndex,
                                         Mode mode,
                                         rhi::TextureView& source0,
                                         rhi::TextureView& source1,
                                         Target& target,
                                         const BloomSettings& settings) {
        if (!frameContext.context || !target.texture || !target.view || !m_Sampler) {
            ARK_ERROR("BloomPass requires context, target and sampler for fullscreen pass");
            return false;
        }

        rhi::PipelineState* pipeline = getOrCreatePipeline();
        if (!pipeline) {
            return false;
        }

        if (frameSlot >= m_DrawResources.size() || drawIndex >= m_DrawResources[frameSlot].size()) {
            ARK_ERROR("BloomPass draw resources were not prepared");
            return false;
        }

        DrawResources& resources = m_DrawResources[frameSlot][drawIndex];
        if (!resources.uniformBuffer || !resources.descriptorSet) {
            ARK_ERROR("BloomPass draw resource is incomplete");
            return false;
        }

        rhi::SampledImageDescriptor source0Descriptor{};
        source0Descriptor.view = &source0;
        resources.descriptorSet->updateSampledImage(0, source0Descriptor);

        rhi::SampledImageDescriptor source1Descriptor{};
        source1Descriptor.view = &source1;
        resources.descriptorSet->updateSampledImage(1, source1Descriptor);

        rhi::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.sampler = m_Sampler.get();
        resources.descriptorSet->updateSampler(2, samplerDescriptor);

        const rhi::Extent2D texelSourceExtent = mode == Mode::Composite ? textureViewExtent(source1)
                                                                         : textureViewExtent(source0);
        const BloomUniform uniform =
            makeBloomUniform(static_cast<u32>(mode), settings, texelSourceExtent);
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
        renderingDesc.colorAttachment.clearColor = rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
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

    rhi::PipelineState* BloomPass::getOrCreatePipeline() {
        if (!m_Device) {
            ARK_ERROR("BloomPass requires RenderDevice");
            return nullptr;
        }

        if (m_Pipeline) {
            return m_Pipeline.get();
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("BloomPass requires shader modules and pipeline layout");
            return nullptr;
        }

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "BloomPipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = false;
        pipelineDesc.depthStencilState.enableDepthWrite = false;
        pipelineDesc.colorFormat = BloomFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        return m_Pipeline.get();
    }
} // namespace ark
