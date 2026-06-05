#include "rhi/vulkan/VulkanCommandContext.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanPipelineState.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <limits>

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
            case ResourceState::CopyDst:
                return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            default:
                return VK_IMAGE_LAYOUT_GENERAL;
            }
        }

        // src/dst access 表达 barrier 前后分别需要等待或开放哪些内存访问。
        VkAccessFlags toVkAccessMask(ResourceState state) {
            switch (state) {
            case ResourceState::RenderTarget:
                return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case ResourceState::CopyDst:
                return VK_ACCESS_TRANSFER_WRITE_BIT;
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
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case ResourceState::RenderTarget:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case ResourceState::RenderTarget:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            default:
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
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
        return true;
    }

    void VulkanCommandContext::advanceFrame() {
        // Renderer 在 present 后推进 frame slot，下一帧会使用另一个 per-frame 资源槽。
        ++m_FrameIndex;
        m_FrameSlot = static_cast<u32>(m_FrameIndex % m_FrameResources.size());
    }

    bool VulkanCommandContext::beginRendering(const RenderingDesc& desc) {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::beginRendering requires active command buffer");
            return false;
        }

        if (m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::beginRendering called while rendering is active");
            return false;
        }

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

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = VkOffset2D{0, 0};
        renderingInfo.renderArea.extent = VkExtent2D{desc.extent.width, desc.extent.height};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        m_IsRendering = true;
        return true;
    }

    void VulkanCommandContext::endRendering() {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::endRendering requires active command buffer");
            return;
        }

        if (!m_IsRendering) {
            ARK_ERROR("VulkanCommandContext::endRendering called without active rendering");
            return;
        }

        vkCmdEndRendering(commandBuffer);
        m_IsRendering = false;
    }

    void VulkanCommandContext::setViewport(const Viewport& viewport) {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setViewport requires active command buffer");
            return;
        }

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
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setScissorRect requires active command buffer");
            return;
        }

        VkRect2D scissor{};
        scissor.offset = VkOffset2D{rect.x, rect.y};
        scissor.extent = VkExtent2D{rect.width, rect.height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void VulkanCommandContext::setPipeline(PipelineState& pipeline) {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setPipeline requires active command buffer");
            return;
        }

        VulkanPipelineState* vulkanPipeline = dynamic_cast<VulkanPipelineState*>(&pipeline);
        if (!vulkanPipeline || vulkanPipeline->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setPipeline requires VulkanPipelineState");
            return;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->getHandle());
    }

    void VulkanCommandContext::bindDescriptorSet(u32 setIndex, DescriptorSet& descriptorSet) {
        (void)setIndex;
        (void)descriptorSet;
    }

    void VulkanCommandContext::setVertexBuffer(u32 slot, Buffer& buffer, u64 offset) {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setVertexBuffer requires active command buffer");
            return;
        }

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
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::setIndexBuffer requires active command buffer");
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
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::draw requires active command buffer");
            return;
        }

        vkCmdDraw(commandBuffer, desc.vertexCount, desc.instanceCount, desc.firstVertex, desc.firstInstance);
    }

    void VulkanCommandContext::drawIndexed(const DrawIndexedDesc& desc) {
        const VkCommandBuffer commandBuffer = currentCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanCommandContext::drawIndexed requires active command buffer");
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
            // 当前只处理整张 color image；mip/layer 范围来自 TextureDesc。
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
} // namespace ark::rhi::vulkan
