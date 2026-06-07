#pragma once

#include "rhi/DescriptorSet.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanDescriptorSetLayout;

    class VulkanDescriptorSet final : public DescriptorSet {
    public:
        VulkanDescriptorSet(VkDevice device, VkDescriptorSet descriptorSet, const VulkanDescriptorSetLayout& layout);
        ~VulkanDescriptorSet() override = default;

        VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
        VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;

        VulkanDescriptorSet(VulkanDescriptorSet&& other) noexcept;
        VulkanDescriptorSet& operator=(VulkanDescriptorSet&& other) noexcept;

        void updateUniformBuffer(u32 binding, const BufferDescriptor& buffer) override;
        void updateSampledImage(u32 binding, const SampledImageDescriptor& image) override;
        void updateSampler(u32 binding, const SamplerDescriptor& sampler) override;

        VkDescriptorSet getHandle() const;

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        // 借用 layout 做绑定校验；descriptor pool 生命周期由 VulkanDescriptorManager 管理。
        const VulkanDescriptorSetLayout* m_Layout = nullptr;
    };
} // namespace ark::rhi::vulkan
