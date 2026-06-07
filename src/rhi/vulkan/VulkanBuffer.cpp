#include "rhi/vulkan/VulkanBuffer.h"

#include "core/Log.h"

#include <cstring>
#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        VkBufferUsageFlags toVkBufferUsage(BufferUsage usage) {
            VkBufferUsageFlags flags = 0;

            if (hasBufferUsage(usage, BufferUsage::Vertex)) {
                flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Index)) {
                flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Uniform)) {
                flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::Storage)) {
                flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::TransferSrc)) {
                flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            }
            if (hasBufferUsage(usage, BufferUsage::TransferDst)) {
                flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            }

            return flags;
        }

        VmaAllocationCreateInfo makeAllocationCreateInfo(MemoryUsage memoryUsage) {
            VmaAllocationCreateInfo createInfo{};
            createInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (memoryUsage == MemoryUsage::CpuToGpu) {
                createInfo.flags =
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                return createInfo;
            }

            createInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            return createInfo;
        }
    } // namespace

    VulkanBuffer::VulkanBuffer(VmaAllocator allocator, const BufferDesc& desc) : m_Allocator(allocator), m_Desc(desc) {
        if (m_Allocator == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanBuffer requires a valid VMA allocator");
        }

        if (m_Desc.size == 0) {
            throw std::runtime_error("VulkanBuffer requires non-zero size");
        }

        const VkBufferUsageFlags usageFlags = toVkBufferUsage(m_Desc.usage);
        if (usageFlags == 0) {
            throw std::runtime_error("VulkanBuffer requires at least one usage flag");
        }

        if (m_Desc.initialData && m_Desc.memoryUsage == MemoryUsage::GpuOnly) {
            throw std::runtime_error("GpuOnly initialData upload is not implemented yet");
        }

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = m_Desc.size;
        bufferCreateInfo.usage = usageFlags;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo = makeAllocationCreateInfo(m_Desc.memoryUsage);
        VmaAllocationInfo allocationInfo{};
        if (!ARK_VK_CHECK(
                vmaCreateBuffer(m_Allocator, &bufferCreateInfo, &allocationCreateInfo, &m_Buffer, &m_Allocation,
                                &allocationInfo))) {
            throw std::runtime_error("vmaCreateBuffer failed");
        }

        if (m_Desc.initialData) {
            // Phase 0.4 先支持 CPU 可见 buffer 的直接初始化；GPU-only 上传会在资源上传系统里补齐。
            void* mappedData = allocationInfo.pMappedData;
            bool mappedByConstructor = false;
            if (!mappedData) {
                if (!ARK_VK_CHECK(vmaMapMemory(m_Allocator, m_Allocation, &mappedData))) {
                    throw std::runtime_error("vmaMapMemory failed");
                }
                mappedByConstructor = true;
            }

            std::memcpy(mappedData, m_Desc.initialData, static_cast<usize>(m_Desc.size));
            vmaFlushAllocation(m_Allocator, m_Allocation, 0, VK_WHOLE_SIZE);

            if (mappedByConstructor) {
                vmaUnmapMemory(m_Allocator, m_Allocation);
            }
        }
    }

    VulkanBuffer::~VulkanBuffer() {
        reset();
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : m_Allocator(other.m_Allocator),
          m_Buffer(other.m_Buffer),
          m_Allocation(other.m_Allocation),
          m_Desc(other.m_Desc) {
        other.m_Allocator = VK_NULL_HANDLE;
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Allocator = other.m_Allocator;
        m_Buffer = other.m_Buffer;
        m_Allocation = other.m_Allocation;
        m_Desc = other.m_Desc;

        other.m_Allocator = VK_NULL_HANDLE;
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
        return *this;
    }

    const BufferDesc& VulkanBuffer::getDesc() const {
        return m_Desc;
    }

    bool VulkanBuffer::updateData(const void* data, u64 size, u64 offset) {
        if (!data || size == 0) {
            ARK_ERROR("VulkanBuffer::updateData requires non-empty data");
            return false;
        }

        if (m_Allocator == VK_NULL_HANDLE || m_Allocation == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanBuffer::updateData requires a valid allocation");
            return false;
        }

        if (m_Desc.memoryUsage != MemoryUsage::CpuToGpu) {
            ARK_ERROR("VulkanBuffer::updateData only supports CpuToGpu buffers");
            return false;
        }

        if (offset > m_Desc.size || size > m_Desc.size - offset) {
            ARK_ERROR("VulkanBuffer::updateData range is out of bounds");
            return false;
        }

        VmaAllocationInfo allocationInfo{};
        vmaGetAllocationInfo(m_Allocator, m_Allocation, &allocationInfo);

        void* mappedData = allocationInfo.pMappedData;
        bool mappedForUpdate = false;
        if (!mappedData) {
            if (!ARK_VK_CHECK(vmaMapMemory(m_Allocator, m_Allocation, &mappedData))) {
                return false;
            }
            mappedForUpdate = true;
        }

        std::memcpy(static_cast<u8*>(mappedData) + offset, data, static_cast<usize>(size));
        // VMA 会根据内存类型处理 flush；对 coherent 内存这一步通常是轻量 no-op。
        if (!ARK_VK_CHECK(vmaFlushAllocation(m_Allocator, m_Allocation, offset, size))) {
            if (mappedForUpdate) {
                vmaUnmapMemory(m_Allocator, m_Allocation);
            }
            return false;
        }

        if (mappedForUpdate) {
            vmaUnmapMemory(m_Allocator, m_Allocation);
        }

        return true;
    }

    VkBuffer VulkanBuffer::getHandle() const {
        return m_Buffer;
    }

    VmaAllocation VulkanBuffer::getAllocation() const {
        return m_Allocation;
    }

    void VulkanBuffer::reset() {
        if (m_Allocator != VK_NULL_HANDLE && m_Buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
        }

        m_Allocator = VK_NULL_HANDLE;
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
