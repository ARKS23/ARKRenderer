#include "rhi/vulkan/VulkanCommandContext.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDescriptorSet.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanPipelineState.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace ark::rhi::vulkan {
    namespace {
        // Phase 0.3 先使用双缓冲帧资源，后续可根据 swapchain image count 或配置调整。
        constexpr u32 FramesInFlight = 2;

        // ResourceState 是上层渲染语义，VkImageLayout 是 Vulkan 对 image 访问方式的具体约束。
        VkImageLayout toVkImageLayout(ResourceState state) {
            switch (state) {
            case ResourceState::Undefined:
                return VK_IMAGE_LAYOUT_UNDEFINED;
            case ResourceState::Present:
                return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            case ResourceState::RenderTarget:
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case ResourceState::DepthStencilWrite:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case ResourceState::DepthStencilRead:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case ResourceState::CopyDst:
                return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case ResourceState::ShaderResource:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case ResourceState::CopySrc:
                return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            default:
                return VK_IMAGE_LAYOUT_GENERAL;
            }
        }

        // src/dst access 表达 barrier 前后分别需要等待或开放哪些内存访问。
        VkAccessFlags toVkAccessMask(ResourceState state) {
            switch (state) {
            case ResourceState::RenderTarget:
                return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case ResourceState::DepthStencilWrite:
                return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            case ResourceState::DepthStencilRead:
                return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            case ResourceState::CopyDst:
                return VK_ACCESS_TRANSFER_WRITE_BIT;
            case ResourceState::ShaderResource:
                return VK_ACCESS_SHADER_READ_BIT;
            case ResourceState::CopySrc:
                return VK_ACCESS_TRANSFER_READ_BIT;
            case ResourceState::Present:
            case ResourceState::Undefined:
                return 0;
            default:
                return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            }
        }

        // source stage 表达旧状态可能在哪个阶段产生访问。
        // 例如 CopyDst 的写入发生在 transfer stage。
        VkPipelineStageFlags toSourceStage(ResourceState state) {
            switch (state) {
            case ResourceState::Undefined:
                return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            case ResourceState::Present:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case ResourceState::CopyDst:
            case ResourceState::CopySrc:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case ResourceState::ShaderResource:
                return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            case ResourceState::RenderTarget:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case ResourceState::DepthStencilWrite:
            case ResourceState::DepthStencilRead:
                return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            default:
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            }
        }

        // destination stage 表达新状态接下来会在哪个阶段被访问。
        // 例如进入 CopyDst 后，clear/copy 命令会在 transfer stage 访问该 image。
        VkPipelineStageFlags toDestinationStage(ResourceState state) {
            switch (state) {
            case ResourceState::Present:
                return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            case ResourceState::CopyDst:
            case ResourceState::CopySrc:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case ResourceState::ShaderResource:
                return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            case ResourceState::RenderTarget:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case ResourceState::DepthStencilWrite:
            case ResourceState::DepthStencilRead:
                return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            default:
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            }
        }

        VkImageAspectFlags toImageAspectMask(Format format) {
            switch (format) {
            case Format::D24UnormS8UInt:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case Format::D32Float:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        VkAttachmentLoadOp toVkLoadOp(LoadOp loadOp) {
            switch (loadOp) {
            case LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }

            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        VkAttachmentStoreOp toVkStoreOp(StoreOp storeOp) {
            switch (storeOp) {
            case StoreOp::Store:
                return VK_ATTACHMENT_STORE_OP_STORE;
            case StoreOp::DontCare:
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }

            return VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkIndexType toVkIndexType(IndexType indexType) {
            switch (indexType) {
            case IndexType::UInt16:
                return VK_INDEX_TYPE_UINT16;
            case IndexType::UInt32:
                return VK_INDEX_TYPE_UINT32;
            }

            return VK_INDEX_TYPE_UINT32;
        }

        VkClearValue toVkClearValue(const ClearColor& color) {
            VkClearValue clearValue{};
            clearValue.color.float32[0] = color.r;
            clearValue.color.float32[1] = color.g;
            clearValue.color.float32[2] = color.b;
            clearValue.color.float32[3] = color.a;
            return clearValue;
        }

        bool checkedMultiply(u64 lhs, u64 rhs, u64& result) {
            if (lhs != 0 && rhs > std::numeric_limits<u64>::max() / lhs) {
                return false;
            }

            result = lhs * rhs;
            return true;
        }

        VkAccessFlags toBufferUploadReadAccessMask(BufferUsage usage) {
            VkAccessFlags accessMask = 0;

            if (hasBufferUsage(usage, BufferUsage::Vertex)) {
                accessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Index)) {
                accessMask |= VK_ACCESS_INDEX_READ_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Uniform)) {
                accessMask |= VK_ACCESS_UNIFORM_READ_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Storage)) {
                accessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::TransferSrc)) {
                accessMask |= VK_ACCESS_TRANSFER_READ_BIT;
            }

            return accessMask != 0 ? accessMask : VK_ACCESS_MEMORY_READ_BIT;
        }

        VkPipelineStageFlags toBufferUploadReadStageMask(BufferUsage usage) {
            VkPipelineStageFlags stageMask = 0;

            if (hasBufferUsage(usage, BufferUsage::Vertex) || hasBufferUsage(usage, BufferUsage::Index)) {
                stageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Uniform) || hasBufferUsage(usage, BufferUsage::Storage)) {
                stageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::TransferSrc)) {
                stageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            }

            return stageMask != 0 ? stageMask : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        u32 textureUploadBytesPerPixel(Format format) {
            switch (format) {
            case Format::RGBA8Unorm:
            case Format::RGBA8Srgb:
                return 4;
            case Format::RGBA16Float:
                return 8;
            case Format::RGBA32Float:
                return 16;
            default:
                return 0;
            }
        }

        void recordTextureSubresourceBarrier(VkCommandBuffer commandBuffer,
                                             VulkanTexture& texture,
                                             u32 baseMipLevel,
                                             u32 levelCount,
                                             VkImageLayout oldLayout,
                                             VkImageLayout newLayout,
                                             VkAccessFlags srcAccessMask,
                                             VkAccessFlags dstAccessMask,
                                             VkPipelineStageFlags srcStageMask,
                                             VkPipelineStageFlags dstStageMask) {
            VkImageMemoryBarrier imageBarrier{};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.srcAccessMask = srcAccessMask;
            imageBarrier.dstAccessMask = dstAccessMask;
            imageBarrier.oldLayout = oldLayout;
            imageBarrier.newLayout = newLayout;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = texture.getHandle();
            imageBarrier.subresourceRange.aspectMask = toImageAspectMask(texture.getDesc().format);
            imageBarrier.subresourceRange.baseMipLevel = baseMipLevel;
            imageBarrier.subresourceRange.levelCount = levelCount;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1,
                                 &imageBarrier);
        }
    } // namespace

    VulkanCommandContext::VulkanCommandContext(VulkanDevice& device) : m_Device(device) {
        // 每个 frame slot 都有独立的命令和同步对象，避免 CPU 覆盖 GPU 仍在使用的资源。
        m_FrameResources.reserve(FramesInFlight);
        for (u32 slot = 0; slot < FramesInFlight; ++slot) {
            m_FrameResources.push_back(
                makeScope<VulkanFrameResource>(m_Device.getDevice(), m_Device.getGraphicsQueueFamily(), slot));
        }

        ARK_INFO("Vulkan command context created: framesInFlight={}", FramesInFlight);
    }

    VulkanCommandContext::~VulkanCommandContext() = default;

    FrameResource& VulkanCommandContext::beginFrame() {
        // 复用 frame slot 前必须等待 GPU 完成上一轮提交，避免重置仍在使用的 command pool。
        VulkanFrameResource* frame = currentVulkanFrame();
        frame->frameIndex = m_FrameIndex;
        frame->frameSlot = m_FrameSlot;

        // fence 在 submit 时交给 queue；它 signal 后，说明该 frame slot 的命令资源可以复用。
        const VkFence fence = frame->getInFlightFence();
        if (fence != VK_NULL_HANDLE) {
            ARK_VK_CHECK(vkWaitForFences(m_Device.getDevice(), 1, &fence, VK_TRUE, std::numeric_limits<u64>::max()));
        }

        if (frame->deferredDeletion) {
            // fence signal 后才能释放上一轮提交中仍可能被 GPU 使用的上传 staging 资源。
            frame->deferredDeletion->flush();
        }

        return *frame;
    }

    bool VulkanCommandContext::begin(FrameResource& frameResource) {
        // begin 只负责开始命令录制；acquire image 必须在 Renderer/SwapChain 阶段先完成。
        VulkanFrameResource* frame = toVulkanFrameResource(frameResource);
        if (!frame) {
            ARK_ERROR("VulkanCommandContext::begin requires VulkanFrameResource");
            return false;
        }

        if (!frame->commandPool || !frame->commandBuffer) {
            ARK_ERROR("VulkanCommandContext::begin requires command resources");
            return false;
        }

        // reset command pool 会让该 pool 分配出的 command buffer 回到可重新录制状态。
        if (!frame->commandPool->reset()) {
            return false;
        }

        // 当前 command buffer 以 one-time submit 方式录制：本帧录制，本帧提交。
        if (!frame->commandBuffer->begin()) {
            return false;
        }

        m_RecordingFrame = frame;
        m_IsRecording = true;
        m_IsRendering = false;
        m_CurrentGraphicsPipelineLayout = VK_NULL_HANDLE;
        return true;
    }

    bool VulkanCommandContext::end() {
        // end 结束当前 command buffer，submit 前必须调用。
        if (!m_RecordingFrame || !m_IsRecording) {
            ARK_ERROR("VulkanCommandContext::end called without active recording");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::end called while rendering is active");
            return false;
        }

        if (!m_RecordingFrame->commandBuffer->end()) {
            return false;
        }

        m_IsRecording = false;
        m_CurrentGraphicsPipelineLayout = VK_NULL_HANDLE;
        return true;
    }

    bool VulkanCommandContext::submit(const SubmitDesc& desc) {
        // submit 串接 acquire semaphore、render finished semaphore 和 in-flight fence。
        VulkanFrameResource* frame = desc.frameResource ? toVulkanFrameResource(*desc.frameResource) : m_RecordingFrame;
        if (!frame) {
            ARK_ERROR("VulkanCommandContext::submit requires VulkanFrameResource");
            return false;
        }

        const VkCommandBuffer commandBuffer = frame->getCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::submit requires command buffer");
            return false;
        }

        // submit 前重置 fence；GPU 完成这次提交后会重新 signal 它。
        const VkFence fence = frame->getInFlightFence();
        if (fence != VK_NULL_HANDLE) {
            if (!ARK_VK_CHECK(vkResetFences(m_Device.getDevice(), 1, &fence))) {
                return false;
            }
        }

        // imageAvailableSemaphore 由 acquire signal，queue submit 等它后才能写当前 backbuffer。
        const VkSemaphore waitSemaphore = desc.waitForSwapChainImage ? frame->getImageAvailableSemaphore() : VK_NULL_HANDLE;
        // renderFinishedSemaphore 由 submit signal，present 等它后才能呈现当前 backbuffer。
        const VkSemaphore signalSemaphore =
            desc.signalRenderFinished ? frame->getRenderFinishedSemaphore() : VK_NULL_HANDLE;
        // acquire semaphore 需要保护后续对 backbuffer 的写入；当前兼容 dynamic rendering 和旧 transfer clear。
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (waitSemaphore != VK_NULL_HANDLE) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &waitSemaphore;
            submitInfo.pWaitDstStageMask = &waitStage;
        }

        if (signalSemaphore != VK_NULL_HANDLE) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSemaphore;
        }

        // vkQueueSubmit 是异步提交；返回成功只表示提交进入队列，不表示 GPU 已经执行完。
        if (!ARK_VK_CHECK(vkQueueSubmit(m_Device.getGraphicsQueue(), 1, &submitInfo, fence))) {
            return false;
        }

        m_RecordingFrame = nullptr;
        m_CurrentGraphicsPipelineLayout = VK_NULL_HANDLE;
        return true;
    }

    void VulkanCommandContext::advanceFrame() {
        // Renderer 在 present 后推进 frame slot，下一帧会使用另一个 per-frame 资源槽。
        ++m_FrameIndex;
        m_FrameSlot = static_cast<u32>(m_FrameIndex % m_FrameResources.size());
    }

    bool VulkanCommandContext::beginRendering(const RenderingDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::beginRendering", commandBuffer)) {
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::beginRendering called while rendering is active");
            return false;
        }

        if (!isValidExtent(desc.extent)) {
            ARK_ERROR("VulkanCommandContext::beginRendering requires valid render extent");
            return false;
        }

        // RHI 的 RenderingDesc 在这里翻译为 Vulkan dynamic rendering 的附件描述。
        // 当前只接入一个 color attachment；depth/stencil 会在后续阶段扩展。
        VulkanTextureView* colorView = dynamic_cast<VulkanTextureView*>(desc.colorAttachment.view);
        if (!colorView || colorView->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::beginRendering requires Vulkan color attachment view");
            return false;
        }

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = colorView->getHandle();
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = toVkLoadOp(desc.colorAttachment.loadOp);
        colorAttachment.storeOp = toVkStoreOp(desc.colorAttachment.storeOp);
        colorAttachment.clearValue = toVkClearValue(desc.colorAttachment.clearColor);

        VkRenderingAttachmentInfo depthAttachment{};
        if (desc.depthStencilAttachment.view) {
            VulkanTextureView* depthView = dynamic_cast<VulkanTextureView*>(desc.depthStencilAttachment.view);
            if (!depthView || depthView->getHandle() == VK_NULL_HANDLE) {
                ARK_ERROR("VulkanCommandContext::beginRendering requires Vulkan depth attachment view");
                return false;
            }

            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depthView->getHandle();
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = toVkLoadOp(desc.depthStencilAttachment.loadOp);
            depthAttachment.storeOp = toVkStoreOp(desc.depthStencilAttachment.storeOp);
            depthAttachment.clearValue.depthStencil.depth = desc.depthStencilAttachment.clearDepth;
            depthAttachment.clearValue.depthStencil.stencil = desc.depthStencilAttachment.clearStencil;
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = VkOffset2D{0, 0};
        renderingInfo.renderArea.extent = VkExtent2D{desc.extent.width, desc.extent.height};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        if (desc.depthStencilAttachment.view) {
            // stencil attachment 暂不参与默认 D32Float 路径；D24S8 后续再按格式拆分接入。
            renderingInfo.pDepthAttachment = &depthAttachment;
        }

        // vkCmdBeginRendering 只录制命令，真正的附件读写发生在 GPU 执行 command buffer 时。
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        m_IsRendering = true;
        return true;
    }

    void VulkanCommandContext::endRendering() {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::endRendering", commandBuffer)) {
            return;
        }

        if (!requireActiveRendering("VulkanCommandContext::endRendering")) {
            return;
        }

        vkCmdEndRendering(commandBuffer);
        m_IsRendering = false;
    }

    void VulkanCommandContext::setViewport(const Viewport& viewport) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::setViewport", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::setViewport")) {
            return;
        }

        // Pipeline 将 viewport 声明为 dynamic state，因此每帧由 FrameRenderer 按 backbuffer 尺寸设置。
        VkViewport vkViewport{};
        vkViewport.x = viewport.x;
        vkViewport.y = viewport.y;
        vkViewport.width = viewport.width;
        vkViewport.height = viewport.height;
        vkViewport.minDepth = viewport.minDepth;
        vkViewport.maxDepth = viewport.maxDepth;
        vkCmdSetViewport(commandBuffer, 0, 1, &vkViewport);
    }

    void VulkanCommandContext::setScissorRect(const ScissorRect& rect) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::setScissorRect", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::setScissorRect")) {
            return;
        }

        VkRect2D scissor{};
        scissor.offset = VkOffset2D{rect.x, rect.y};
        scissor.extent = VkExtent2D{rect.width, rect.height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void VulkanCommandContext::setPipeline(PipelineState& pipeline) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::setPipeline", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::setPipeline")) {
            return;
        }

        // RHI 层只暴露 PipelineState；进入 Vulkan 后端时还原为 VulkanPipelineState 以取得 VkPipeline。
        VulkanPipelineState* vulkanPipeline = dynamic_cast<VulkanPipelineState*>(&pipeline);
        if (!vulkanPipeline || vulkanPipeline->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setPipeline requires VulkanPipelineState");
            return;
        }

        const VkPipelineLayout pipelineLayout = vulkanPipeline->getLayoutHandle();
        if (pipelineLayout == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setPipeline requires pipeline layout");
            return;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->getHandle());
        // descriptor set 绑定需要使用当前 graphics pipeline layout，避免上层直接接触 Vulkan handle。
        m_CurrentGraphicsPipelineLayout = pipelineLayout;
    }

    void VulkanCommandContext::bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::bindDescriptorSet", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::bindDescriptorSet")) {
            return;
        }

        if (m_CurrentGraphicsPipelineLayout == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::bindDescriptorSet requires a bound graphics pipeline");
            return;
        }

        VulkanDescriptorSet* vulkanDescriptorSet = dynamic_cast<VulkanDescriptorSet*>(&descriptorSet);
        if (!vulkanDescriptorSet || vulkanDescriptorSet->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::bindDescriptorSet requires VulkanDescriptorSet");
            return;
        }

        const VkDescriptorSet vkDescriptorSet = vulkanDescriptorSet->getHandle();
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CurrentGraphicsPipelineLayout,
                                setIndex, 1, &vkDescriptorSet, 0, nullptr);
    }

    bool VulkanCommandContext::updateBuffer(Buffer& buffer, const void* data, u64 size, u64 offset) {
        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(&buffer);
        if (!vulkanBuffer || vulkanBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::updateBuffer requires VulkanBuffer");
            return false;
        }

        // 这里只处理 CPU 可见内存的直接写入；GPU-only 上传后续由 upload system 接管。
        return vulkanBuffer->updateData(data, size, offset);
    }

    bool VulkanCommandContext::uploadTextureData(const TextureUploadDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::uploadTextureData", commandBuffer)) {
            return false;
        }

        if (m_IsRendering) {
            // Vulkan copy 命令不能录在 vkCmdBeginRendering/vkCmdEndRendering 之间。
            ARK_ERROR("VulkanCommandContext::uploadTextureData must be recorded outside rendering scope");
            return false;
        }

        VulkanBuffer* sourceBuffer = dynamic_cast<VulkanBuffer*>(desc.sourceBuffer);
        if (!sourceBuffer || sourceBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData requires VulkanBuffer source");
            return false;
        }

        VulkanTexture* texture = dynamic_cast<VulkanTexture*>(desc.texture);
        if (!texture || texture->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData requires VulkanTexture target");
            return false;
        }

        if (!isValidExtent(desc.extent)) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData requires valid extent");
            return false;
        }

        if (desc.mipLevel >= texture->getDesc().mipLevels || desc.arrayLayer >= texture->getDesc().arrayLayers) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData subresource is out of range");
            return false;
        }

        const TextureDesc& textureDesc = texture->getDesc();
        const u32 expectedBytesPerPixel = textureUploadBytesPerPixel(textureDesc.format);
        if (expectedBytesPerPixel == 0) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData does not support texture format {}",
                      formatName(textureDesc.format));
            return false;
        }

        if (!hasTextureUsage(textureDesc.usage, TextureUsage::TransferDst)) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData target texture requires TransferDst usage");
            return false;
        }

        const BufferDesc& sourceDesc = sourceBuffer->getDesc();
        if (!hasBufferUsage(sourceDesc.usage, BufferUsage::TransferSrc)) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData source buffer requires TransferSrc usage");
            return false;
        }

        if (desc.mipLevel != 0 || desc.arrayLayer != 0) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData currently supports only mip 0 and array layer 0");
            return false;
        }

        if (desc.extent.width > textureDesc.extent.width || desc.extent.height > textureDesc.extent.height) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData extent exceeds target texture extent");
            return false;
        }

        if (desc.bytesPerPixel != expectedBytesPerPixel) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData expected {} bytes per pixel for format {}",
                      expectedBytesPerPixel,
                      formatName(textureDesc.format));
            return false;
        }

        if (desc.sourceOffset % desc.bytesPerPixel != 0) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData source offset must align to pixel size");
            return false;
        }

        u64 tightlyPackedRowPitch = 0;
        if (!checkedMultiply(desc.extent.width, desc.bytesPerPixel, tightlyPackedRowPitch)) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData row pitch overflow");
            return false;
        }

        const u64 resolvedRowPitch = desc.rowPitch == 0 ? tightlyPackedRowPitch : desc.rowPitch;
        if (resolvedRowPitch != tightlyPackedRowPitch) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData currently supports tightly packed rows only");
            return false;
        }

        u64 uploadByteSize = 0;
        if (!checkedMultiply(resolvedRowPitch, desc.extent.height, uploadByteSize)) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData upload size overflow");
            return false;
        }

        if (desc.sourceOffset > sourceDesc.size || uploadByteSize > sourceDesc.size - desc.sourceOffset) {
            ARK_ERROR("VulkanCommandContext::uploadTextureData source range is out of bounds");
            return false;
        }

        // 首次上传从 Undefined 转为 CopyDst，允许 transfer stage 写入 image。
        const std::array<ResourceBarrier, 1> toCopyDst{{
            ResourceBarrier{
                .texture = texture,
                .before = texture->getState(),
                .after = ResourceState::CopyDst,
            },
        }};
        pipelineBarrier(toCopyDst);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = desc.sourceOffset;
        // Phase 0.7.2 先只接收 tightly packed 数据；非 0 row length 留给后续 rowPitch 扩展。
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = desc.mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = desc.arrayLayer;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = VkExtent3D{desc.extent.width, desc.extent.height, 1};

        vkCmdCopyBufferToImage(commandBuffer, sourceBuffer->getHandle(), texture->getHandle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (textureDesc.mipLevels == 1) {
            // 单 mip 仍沿用旧路径：copy 完成后直接进入 shader 只读布局。
            const std::array<ResourceBarrier, 1> toShaderResource{{
                ResourceBarrier{
                    .texture = texture,
                    .before = texture->getState(),
                    .after = ResourceState::ShaderResource,
                },
            }};
            pipelineBarrier(toShaderResource);
        }

        return true;
    }

    bool VulkanCommandContext::copyTextureToBuffer(const TextureReadbackDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::copyTextureToBuffer", commandBuffer)) {
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer must be recorded outside rendering scope");
            return false;
        }

        VulkanTexture* texture = dynamic_cast<VulkanTexture*>(desc.texture);
        if (!texture || texture->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer requires VulkanTexture source");
            return false;
        }

        VulkanBuffer* destinationBuffer = dynamic_cast<VulkanBuffer*>(desc.destinationBuffer);
        if (!destinationBuffer || destinationBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer requires VulkanBuffer destination");
            return false;
        }

        if (!isValidExtent(desc.extent)) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer requires valid extent");
            return false;
        }

        const TextureDesc& textureDesc = texture->getDesc();
        if (desc.mipLevel >= textureDesc.mipLevels || desc.arrayLayer >= textureDesc.arrayLayers) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer subresource is out of range");
            return false;
        }

        const u32 expectedBytesPerPixel = textureUploadBytesPerPixel(textureDesc.format);
        if (expectedBytesPerPixel == 0) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer does not support texture format {}",
                      formatName(textureDesc.format));
            return false;
        }

        if (!hasTextureUsage(textureDesc.usage, TextureUsage::TransferSrc)) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer source texture requires TransferSrc usage");
            return false;
        }

        const BufferDesc& destinationDesc = destinationBuffer->getDesc();
        if (!hasBufferUsage(destinationDesc.usage, BufferUsage::TransferDst)) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer destination buffer requires TransferDst usage");
            return false;
        }

        if (destinationDesc.memoryUsage != MemoryUsage::GpuToCpu) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer destination buffer requires GpuToCpu memory");
            return false;
        }

        const u32 mipWidth = std::max(1u, textureDesc.extent.width >> desc.mipLevel);
        const u32 mipHeight = std::max(1u, textureDesc.extent.height >> desc.mipLevel);
        if (desc.extent.width > mipWidth || desc.extent.height > mipHeight) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer extent exceeds source texture extent");
            return false;
        }

        if (desc.bytesPerPixel != expectedBytesPerPixel) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer expected {} bytes per pixel for format {}",
                      expectedBytesPerPixel,
                      formatName(textureDesc.format));
            return false;
        }

        if (desc.destinationOffset % desc.bytesPerPixel != 0) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer destination offset must align to pixel size");
            return false;
        }

        u64 tightlyPackedRowPitch = 0;
        if (!checkedMultiply(desc.extent.width, desc.bytesPerPixel, tightlyPackedRowPitch)) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer row pitch overflow");
            return false;
        }

        const u64 resolvedRowPitch = desc.rowPitch == 0 ? tightlyPackedRowPitch : desc.rowPitch;
        if (resolvedRowPitch != tightlyPackedRowPitch) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer currently supports tightly packed rows only");
            return false;
        }

        u64 readbackByteSize = 0;
        if (!checkedMultiply(resolvedRowPitch, desc.extent.height, readbackByteSize)) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer readback size overflow");
            return false;
        }

        if (desc.destinationOffset > destinationDesc.size ||
            readbackByteSize > destinationDesc.size - desc.destinationOffset) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer destination range is out of bounds");
            return false;
        }

        if (texture->getState() != ResourceState::CopySrc) {
            ARK_ERROR("VulkanCommandContext::copyTextureToBuffer requires texture in CopySrc state");
            return false;
        }

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = desc.destinationOffset;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = desc.mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = desc.arrayLayer;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = VkExtent3D{desc.extent.width, desc.extent.height, 1};

        vkCmdCopyImageToBuffer(commandBuffer,
                               texture->getHandle(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               destinationBuffer->getHandle(),
                               1,
                               &copyRegion);
        return true;
    }

    bool VulkanCommandContext::generateTextureMips(Texture& texture) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::generateTextureMips", commandBuffer)) {
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips must be recorded outside rendering scope");
            return false;
        }

        VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(&texture);
        if (!vulkanTexture || vulkanTexture->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips requires VulkanTexture");
            return false;
        }

        const TextureDesc& textureDesc = vulkanTexture->getDesc();
        if (textureDesc.mipLevels <= 1) {
            return true;
        }

        if (textureDesc.arrayLayers != 1) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips currently supports one array layer");
            return false;
        }

        if (textureDesc.format != Format::RGBA8Unorm && textureDesc.format != Format::RGBA8Srgb) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips currently supports RGBA8 textures only");
            return false;
        }

        if (!hasTextureUsage(textureDesc.usage, TextureUsage::TransferSrc) ||
            !hasTextureUsage(textureDesc.usage, TextureUsage::TransferDst) ||
            !hasTextureUsage(textureDesc.usage, TextureUsage::ShaderResource)) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips requires TransferSrc/TransferDst/ShaderResource usage");
            return false;
        }

        if (vulkanTexture->getState() != ResourceState::CopyDst) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips requires texture in CopyDst state");
            return false;
        }

        const VkFormat vkFormat = toVkFormat(textureDesc.format);
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.getPhysicalDevice(), vkFormat, &formatProperties);
        const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                      VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) {
            ARK_ERROR("VulkanCommandContext::generateTextureMips format does not support linear blit");
            return false;
        }

        u32 srcWidth = textureDesc.extent.width;
        u32 srcHeight = textureDesc.extent.height;

        for (u32 mipLevel = 1; mipLevel < textureDesc.mipLevels; ++mipLevel) {
            const u32 dstWidth = srcWidth > 1 ? srcWidth / 2 : 1;
            const u32 dstHeight = srcHeight > 1 ? srcHeight / 2 : 1;

            // 上一层作为 blit source，当前层作为 blit destination。
            recordTextureSubresourceBarrier(commandBuffer,
                                            *vulkanTexture,
                                            mipLevel - 1,
                                            1,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_ACCESS_TRANSFER_WRITE_BIT,
                                            VK_ACCESS_TRANSFER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit{};
            blit.srcOffsets[0] = VkOffset3D{0, 0, 0};
            blit.srcOffsets[1] = VkOffset3D{static_cast<i32>(srcWidth), static_cast<i32>(srcHeight), 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mipLevel - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = VkOffset3D{0, 0, 0};
            blit.dstOffsets[1] = VkOffset3D{static_cast<i32>(dstWidth), static_cast<i32>(dstHeight), 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mipLevel;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(commandBuffer,
                           vulkanTexture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vulkanTexture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_LINEAR);

            recordTextureSubresourceBarrier(commandBuffer,
                                            *vulkanTexture,
                                            mipLevel - 1,
                                            1,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_ACCESS_TRANSFER_READ_BIT,
                                            VK_ACCESS_SHADER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            srcWidth = dstWidth;
            srcHeight = dstHeight;
        }

        recordTextureSubresourceBarrier(commandBuffer,
                                        *vulkanTexture,
                                        textureDesc.mipLevels - 1,
                                        1,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_ACCESS_SHADER_READ_BIT,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vulkanTexture->setState(ResourceState::ShaderResource);
        return true;
    }

    bool VulkanCommandContext::uploadBufferData(const BufferUploadDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::uploadBufferData", commandBuffer)) {
            return false;
        }

        if (m_IsRendering) {
            // buffer copy 同样必须发生在 dynamic rendering scope 外。
            ARK_ERROR("VulkanCommandContext::uploadBufferData must be recorded outside rendering scope");
            return false;
        }

        VulkanBuffer* sourceBuffer = dynamic_cast<VulkanBuffer*>(desc.sourceBuffer);
        if (!sourceBuffer || sourceBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData requires VulkanBuffer source");
            return false;
        }

        VulkanBuffer* destinationBuffer = dynamic_cast<VulkanBuffer*>(desc.destinationBuffer);
        if (!destinationBuffer || destinationBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData requires VulkanBuffer destination");
            return false;
        }

        if (desc.size == 0) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData requires non-zero size");
            return false;
        }

        const BufferDesc& sourceDesc = sourceBuffer->getDesc();
        if (!hasBufferUsage(sourceDesc.usage, BufferUsage::TransferSrc)) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData source buffer requires TransferSrc usage");
            return false;
        }

        const BufferDesc& destinationDesc = destinationBuffer->getDesc();
        if (!hasBufferUsage(destinationDesc.usage, BufferUsage::TransferDst)) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData destination buffer requires TransferDst usage");
            return false;
        }

        if (desc.sourceOffset > sourceDesc.size || desc.size > sourceDesc.size - desc.sourceOffset) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData source range is out of bounds");
            return false;
        }

        if (desc.destinationOffset > destinationDesc.size || desc.size > destinationDesc.size - desc.destinationOffset) {
            ARK_ERROR("VulkanCommandContext::uploadBufferData destination range is out of bounds");
            return false;
        }

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = desc.sourceOffset;
        copyRegion.dstOffset = desc.destinationOffset;
        copyRegion.size = desc.size;
        vkCmdCopyBuffer(commandBuffer, sourceBuffer->getHandle(), destinationBuffer->getHandle(), 1, &copyRegion);

        // copy 后让 transfer write 对后续 vertex/index/uniform 等读取可见。
        VkBufferMemoryBarrier bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufferBarrier.dstAccessMask = toBufferUploadReadAccessMask(destinationDesc.usage);
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = destinationBuffer->getHandle();
        bufferBarrier.offset = desc.destinationOffset;
        bufferBarrier.size = desc.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             toBufferUploadReadStageMask(destinationDesc.usage), 0, 0, nullptr, 1, &bufferBarrier, 0,
                             nullptr);
        return true;
    }

    bool VulkanCommandContext::deferReleaseBuffer(Scope<Buffer>& buffer) {
        if (!buffer) {
            return true;
        }

        if (!m_RecordingFrame || !m_IsRecording) {
            ARK_ERROR("VulkanCommandContext::deferReleaseBuffer requires active frame recording");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::deferReleaseBuffer must be called outside rendering scope");
            return false;
        }

        if (!m_RecordingFrame->deferredDeletion) {
            ARK_ERROR("VulkanCommandContext::deferReleaseBuffer requires deferred deletion queue");
            return false;
        }

        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
        if (!vulkanBuffer || vulkanBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::deferReleaseBuffer requires VulkanBuffer");
            return false;
        }

        m_RecordingFrame->deferredDeletion->deferReleaseBuffer(std::move(buffer));
        return true;
    }

    bool VulkanCommandContext::deferReleaseTexture(Scope<Texture>& texture) {
        if (!texture) {
            return true;
        }

        if (!m_RecordingFrame || !m_IsRecording) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTexture requires active frame recording");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTexture must be called outside rendering scope");
            return false;
        }

        if (!m_RecordingFrame->deferredDeletion) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTexture requires deferred deletion queue");
            return false;
        }

        VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
        if (!vulkanTexture || vulkanTexture->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTexture requires VulkanTexture");
            return false;
        }

        m_RecordingFrame->deferredDeletion->deferReleaseTexture(std::move(texture));
        return true;
    }

    bool VulkanCommandContext::deferReleaseTextureView(Scope<TextureView>& textureView) {
        if (!textureView) {
            return true;
        }

        if (!m_RecordingFrame || !m_IsRecording) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTextureView requires active frame recording");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTextureView must be called outside rendering scope");
            return false;
        }

        if (!m_RecordingFrame->deferredDeletion) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTextureView requires deferred deletion queue");
            return false;
        }

        VulkanTextureView* vulkanTextureView = dynamic_cast<VulkanTextureView*>(textureView.get());
        if (!vulkanTextureView || vulkanTextureView->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::deferReleaseTextureView requires VulkanTextureView");
            return false;
        }

        m_RecordingFrame->deferredDeletion->deferReleaseTextureView(std::move(textureView));
        return true;
    }

    bool VulkanCommandContext::deferReleaseSampler(Scope<Sampler>& sampler) {
        if (!sampler) {
            return true;
        }

        if (!m_RecordingFrame || !m_IsRecording) {
            ARK_ERROR("VulkanCommandContext::deferReleaseSampler requires active frame recording");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::deferReleaseSampler must be called outside rendering scope");
            return false;
        }

        if (!m_RecordingFrame->deferredDeletion) {
            ARK_ERROR("VulkanCommandContext::deferReleaseSampler requires deferred deletion queue");
            return false;
        }

        VulkanSampler* vulkanSampler = dynamic_cast<VulkanSampler*>(sampler.get());
        if (!vulkanSampler || vulkanSampler->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::deferReleaseSampler requires VulkanSampler");
            return false;
        }

        m_RecordingFrame->deferredDeletion->deferReleaseSampler(std::move(sampler));
        return true;
    }

    void VulkanCommandContext::setVertexBuffer(u32 slot, Buffer& buffer, u64 offset) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::setVertexBuffer", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::setVertexBuffer")) {
            return;
        }

        // 这里只绑定已经创建好的 VkBuffer；buffer 内存上传和生命周期由 RenderDevice / VulkanBuffer 负责。
        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(&buffer);
        if (!vulkanBuffer || vulkanBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setVertexBuffer requires VulkanBuffer");
            return;
        }

        const VkBuffer vkBuffer = vulkanBuffer->getHandle();
        const VkDeviceSize vkOffset = offset;
        vkCmdBindVertexBuffers(commandBuffer, slot, 1, &vkBuffer, &vkOffset);
    }

    void VulkanCommandContext::setIndexBuffer(Buffer& buffer, IndexType indexType, u64 offset) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::setIndexBuffer", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::setIndexBuffer")) {
            return;
        }

        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(&buffer);
        if (!vulkanBuffer || vulkanBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setIndexBuffer requires VulkanBuffer");
            return;
        }

        vkCmdBindIndexBuffer(commandBuffer, vulkanBuffer->getHandle(), offset, toVkIndexType(indexType));
    }

    void VulkanCommandContext::draw(const DrawDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::draw", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::draw")) {
            return;
        }

        // draw 不做隐式状态修复；pipeline、vertex buffer、viewport/scissor 必须由上层按顺序绑定。
        if (desc.vertexCount == 0 || desc.instanceCount == 0) {
            ARK_ERROR("VulkanCommandContext::draw requires non-zero vertex and instance count");
            return;
        }

        vkCmdDraw(commandBuffer, desc.vertexCount, desc.instanceCount, desc.firstVertex, desc.firstInstance);
    }

    void VulkanCommandContext::drawIndexed(const DrawIndexedDesc& desc) {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!requireActiveCommandBuffer("VulkanCommandContext::drawIndexed", commandBuffer) ||
            !requireActiveRendering("VulkanCommandContext::drawIndexed")) {
            return;
        }

        if (desc.indexCount == 0 || desc.instanceCount == 0) {
            ARK_ERROR("VulkanCommandContext::drawIndexed requires non-zero index and instance count");
            return;
        }

        vkCmdDrawIndexed(commandBuffer, desc.indexCount, desc.instanceCount, desc.firstIndex, desc.vertexOffset,
                         desc.firstInstance);
    }

    void VulkanCommandContext::pipelineBarrier(std::span<const ResourceBarrier> barriers) {
        // Phase 0.3 只处理 texture image barrier；buffer barrier 后续随资源系统补齐。
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::pipelineBarrier requires active command buffer");
            return;
        }

        for (const ResourceBarrier& barrier : barriers) {
            VulkanTexture* texture = dynamic_cast<VulkanTexture*>(barrier.texture);
            if (!texture || texture->getHandle() == VK_NULL_HANDLE) {
                ARK_ERROR("VulkanCommandContext::pipelineBarrier requires VulkanTexture");
                continue;
            }

            // 当前以 texture 内部记录的状态为准，barrier.before 先作为调用方语义保留。
            const ResourceState before = texture->getState();
            const ResourceState after = barrier.after;

            // VkImageMemoryBarrier 描述单张 image 的 layout 和访问状态转换。
            VkImageMemoryBarrier imageBarrier{};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.srcAccessMask = toVkAccessMask(before);
            imageBarrier.dstAccessMask = toVkAccessMask(after);
            imageBarrier.oldLayout = toVkImageLayout(before);
            imageBarrier.newLayout = toVkImageLayout(after);
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = texture->getHandle();
            // barrier 的 aspect 必须与 image format 对齐，depth image 不能使用 color aspect。
            imageBarrier.subresourceRange.aspectMask = toImageAspectMask(texture->getDesc().format);
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = texture->getDesc().mipLevels;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = texture->getDesc().arrayLayers;

            // 录制 barrier 命令；真正的同步发生在 GPU 执行 command buffer 时。
            vkCmdPipelineBarrier(commandBuffer, toSourceStage(before), toDestinationStage(after), 0, 0, nullptr, 0,
                                 nullptr, 1, &imageBarrier);

            // RHI 层记录新状态，后续 barrier 可以从 texture 当前状态继续推导。
            texture->setState(after);
        }
    }

    void VulkanCommandContext::clearRenderTarget(TextureView& renderTargetView, const ClearColor& color) {
        // vkCmdClearColorImage 要求 image 已经处于 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL。
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::clearRenderTarget requires active command buffer");
            return;
        }

        // 从 RHI TextureView 回溯到 VulkanTexture，拿到底层 VkImage。
        VulkanTextureView* vulkanView = dynamic_cast<VulkanTextureView*>(&renderTargetView);
        if (!vulkanView || !vulkanView->getVulkanTexture()) {
            ARK_ERROR("VulkanCommandContext::clearRenderTarget requires VulkanTextureView");
            return;
        }

        // clear value 使用 RGBA float，当前 swapchain unorm 格式会由 Vulkan 做格式转换。
        VulkanTexture* texture = vulkanView->getVulkanTexture();
        VkClearColorValue clearValue{};
        clearValue.float32[0] = color.r;
        clearValue.float32[1] = color.g;
        clearValue.float32[2] = color.b;
        clearValue.float32[3] = color.a;

        // 只清理 view 描述的 mip/layer 范围；当前 swapchain view 通常是 mip0/layer0。
        const TextureViewDesc& viewDesc = vulkanView->getDesc();
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = viewDesc.baseMipLevel;
        range.levelCount = viewDesc.mipLevelCount;
        range.baseArrayLayer = viewDesc.baseArrayLayer;
        range.layerCount = viewDesc.arrayLayerCount;

        // Phase 0.3 使用 vkCmdClearColorImage 验证最小清屏闭环，后续再上移到 ClearPass。
        vkCmdClearColorImage(commandBuffer, texture->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1,
                             &range);
    }

    VulkanFrameResource* VulkanCommandContext::toVulkanFrameResource(FrameResource& frameResource) {
        return dynamic_cast<VulkanFrameResource*>(&frameResource);
    }

    VulkanFrameResource* VulkanCommandContext::currentVulkanFrame() {
        return m_FrameResources[m_FrameSlot].get();
    }

    VkCommandBuffer VulkanCommandContext::currentCommandBuffer() const {
        return m_RecordingFrame ? m_RecordingFrame->getCommandBuffer() : VK_NULL_HANDLE;
    }

    bool VulkanCommandContext::requireActiveCommandBuffer(const char* operation, VkCommandBuffer& commandBuffer) const {
        // 统一检查当前是否处于 command recording，避免每个命令散落一份重复错误处理。
        commandBuffer = currentCommandBuffer();
        if (commandBuffer != VK_NULL_HANDLE) {
            return true;
        }

        ARK_ERROR("{} requires active command buffer", operation);
        return false;
    }

    bool VulkanCommandContext::requireActiveRendering(const char* operation) const {
        // draw path 的状态绑定必须发生在 beginRendering/endRendering 之间。
        if (m_IsRendering) {
            return true;
        }

        ARK_ERROR("{} requires active rendering", operation);
        return false;
    }
} // namespace ark::rhi::vulkan
