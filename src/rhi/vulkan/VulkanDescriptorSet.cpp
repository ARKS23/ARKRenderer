#include "rhi/vulkan/VulkanDescriptorSet.h"

#include "core/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanTextureView.h"

namespace ark::rhi::vulkan {
    namespace {
        bool hasDescriptorBinding(const VulkanDescriptorSetLayout* layout, u32 binding, DescriptorType type) {
            if (!layout) {
                return false;
            }

            for (const DescriptorBindingDesc& bindingDesc : layout->getDesc().bindings) {
                if (bindingDesc.binding == binding) {
                    return bindingDesc.type == type;
                }
            }

            return false;
        }
    } // namespace

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

        if (!hasDescriptorBinding(m_Layout, binding, DescriptorType::UniformBuffer)) {
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

    void VulkanDescriptorSet::updateSampledImage(u32 binding, const SampledImageDescriptor& image) {
        if (m_Device == VK_NULL_HANDLE || m_DescriptorSet == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateSampledImage requires a valid descriptor set");
            return;
        }

        if (!hasDescriptorBinding(m_Layout, binding, DescriptorType::SampledImage)) {
            ARK_ERROR("VulkanDescriptorSet::updateSampledImage requires a sampled image binding");
            return;
        }

        VulkanTextureView* vulkanView = dynamic_cast<VulkanTextureView*>(image.view);
        if (!vulkanView || vulkanView->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateSampledImage requires VulkanTextureView");
            return;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = vulkanView->getHandle();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Phase 0.6 使用 separate sampled image；sampler 由另一个 binding 单独写入。
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    void VulkanDescriptorSet::updateSampler(u32 binding, const SamplerDescriptor& sampler) {
        if (m_Device == VK_NULL_HANDLE || m_DescriptorSet == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateSampler requires a valid descriptor set");
            return;
        }

        if (!hasDescriptorBinding(m_Layout, binding, DescriptorType::Sampler)) {
            ARK_ERROR("VulkanDescriptorSet::updateSampler requires a sampler binding");
            return;
        }

        VulkanSampler* vulkanSampler = dynamic_cast<VulkanSampler*>(sampler.sampler);
        if (!vulkanSampler || vulkanSampler->getHandle() == VK_NULL_HANDLE) {
            ARK_ERROR("VulkanDescriptorSet::updateSampler requires VulkanSampler");
            return;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = vulkanSampler->getHandle();

        // 与 sampled image 分离写入，便于后续复用 sampler 或演进到材质系统。
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    VkDescriptorSet VulkanDescriptorSet::getHandle() const {
        return m_DescriptorSet;
    }
} // namespace ark::rhi::vulkan
