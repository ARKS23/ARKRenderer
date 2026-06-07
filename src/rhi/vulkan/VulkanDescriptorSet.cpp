#include "rhi/vulkan/VulkanDescriptorSet.h"

#include "core/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"

namespace ark::rhi::vulkan {
    VulkanDescriptorSet::VulkanDescriptorSet(VkDevice device, VkDescriptorSet descriptorSet,
                                             const VulkanDescriptorSetLayout& layout)
        : m_Device(device), m_DescriptorSet(descriptorSet), m_Layout(&layout) {
        // Descriptor set 从 manager 的 pool 分配，这里只借用 layout 用于更新前校验。
    }

    VulkanDescriptorSet::VulkanDescriptorSet(VulkanDescriptorSet&& other) noexcept
        : m_Device(other.m_Device), m_DescriptorSet(other.m_DescriptorSet), m_Layout(other.m_Layout) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_DescriptorSet = VK_NULL_HANDLE;
        other.m_Layout = nullptr;
    }

    VulkanDescriptorSet& VulkanDescriptorSet::operator=(VulkanDescriptorSet&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        m_Device = other.m_Device;
        m_DescriptorSet = other.m_DescriptorSet;
        m_Layout = other.m_Layout;

        other.m_Device = VK_NULL_HANDLE;
        other.m_DescriptorSet = VK_NULL_HANDLE;
        other.m_Layout = nullptr;
        return *this;
    }

    void VulkanDescriptorSet::updateUniformBuffer(u32 binding, const BufferDescriptor& buffer) {
        if (m_Device == VK_NULL_HANDLE || m_DescriptorSet == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateUniformBuffer requires a valid descriptor set");
            return;
        }

        bool bindingFound = false;
        if (m_Layout) {
            for (const DescriptorBindingDesc& bindingDesc : m_Layout->getDesc().bindings) {
                if (bindingDesc.binding == binding) {
                    bindingFound = bindingDesc.type == DescriptorType::UniformBuffer;
                    break;
                }
            }
        }

        if (!bindingFound) {
            ARK_ERROR("VulkanDescriptorSet::updateUniformBuffer requires a uniform buffer binding");
            return;
        }

        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer.buffer);
        if (!vulkanBuffer || vulkanBuffer->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateUniformBuffer requires VulkanBuffer");
            return;
        }

        const BufferDesc& bufferDesc = vulkanBuffer->getDesc();
        if (buffer.offset >= bufferDesc.size) {
            ARK_ERROR("VulkanDescriptorSet::updateUniformBuffer offset is out of range");
            return;
        }

        const u64 availableRange = bufferDesc.size - buffer.offset;
        // range 为 0 表示绑定 offset 之后的整个 buffer，方便 uniform buffer 的最小用法。
        const u64 descriptorRange = buffer.range == 0 ? availableRange : buffer.range;
        if (descriptorRange == 0 || descriptorRange > availableRange) {
            ARK_ERROR("VulkanDescriptorSet::updateUniformBuffer range is out of range");
            return;
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vulkanBuffer->getHandle();
        bufferInfo.offset = buffer.offset;
        bufferInfo.range = descriptorRange;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    VkDescriptorSet VulkanDescriptorSet::getHandle() const {
        return m_DescriptorSet;
    }
} // namespace ark::rhi::vulkan
