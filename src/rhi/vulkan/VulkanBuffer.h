#pragma once

#include "rhi/Buffer.h"
#include "rhi/vulkan/VulkanCommon.h"
#include "rhi/vulkan/VulkanAllocator.h"

namespace ark::rhi::vulkan {
    class VulkanBuffer final : public Buffer {
    public:
        VulkanBuffer(VmaAllocator allocator, const BufferDesc& desc);
        ~VulkanBuffer() override;

        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        const BufferDesc& getDesc() const override;

        bool updateData(const void* data, u64 size, u64 offset);

        VkBuffer getHandle() const;
        VmaAllocation getAllocation() const;

    private:
        void reset();

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        BufferDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
