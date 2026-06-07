#pragma once

#include "rhi/Sampler.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanSampler final : public Sampler {
    public:
        VulkanSampler(VkDevice device, const SamplerDesc& desc);
        ~VulkanSampler() override;

        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;

        VulkanSampler(VulkanSampler&& other) noexcept;
        VulkanSampler& operator=(VulkanSampler&& other) noexcept;

        const SamplerDesc& getDesc() const override;
        VkSampler getHandle() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        SamplerDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
