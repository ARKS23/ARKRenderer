#include "rhi/vulkan/VulkanSampler.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        // 当前只落地 Phase 0.6 需要的最小 sampler 状态，后续再扩展 anisotropy / compare / LOD。
        VkFilter toVkFilter(FilterMode filter) {
            switch (filter) {
            case FilterMode::Nearest:
                return VK_FILTER_NEAREST;
            case FilterMode::Linear:
                return VK_FILTER_LINEAR;
            }

            return VK_FILTER_LINEAR;
        }

        VkSamplerMipmapMode toVkMipmapMode(FilterMode filter) {
            switch (filter) {
            case FilterMode::Nearest:
                return VK_SAMPLER_MIPMAP_MODE_NEAREST;
            case FilterMode::Linear:
                return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            }

            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        VkSamplerAddressMode toVkAddressMode(AddressMode mode) {
            switch (mode) {
            case AddressMode::Repeat:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::ClampToEdge:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AddressMode::MirroredRepeat:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            }

            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    } // namespace

    VulkanSampler::VulkanSampler(VkDevice device, const SamplerDesc& desc) : m_Device(device), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanSampler requires a valid VkDevice");
        }

        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = toVkFilter(m_Desc.magFilter);
        createInfo.minFilter = toVkFilter(m_Desc.minFilter);
        createInfo.mipmapMode = toVkMipmapMode(m_Desc.mipFilter);
        createInfo.addressModeU = toVkAddressMode(m_Desc.addressU);
        createInfo.addressModeV = toVkAddressMode(m_Desc.addressV);
        createInfo.addressModeW = toVkAddressMode(m_Desc.addressW);
        createInfo.mipLodBias = 0.0f;
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0f;
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        createInfo.minLod = 0.0f;
        createInfo.maxLod = VK_LOD_CLAMP_NONE;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.unnormalizedCoordinates = VK_FALSE;

        if (!ARK_VK_CHECK(vkCreateSampler(m_Device, &createInfo, nullptr, &m_Sampler))) {
            throw std::runtime_error("vkCreateSampler failed");
        }
    }

    VulkanSampler::~VulkanSampler() {
        reset();
    }

    VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
        : m_Device(other.m_Device), m_Sampler(other.m_Sampler), m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Sampler = VK_NULL_HANDLE;
    }

    VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_Sampler = other.m_Sampler;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Sampler = VK_NULL_HANDLE;
        return *this;
    }

    const SamplerDesc& VulkanSampler::getDesc() const {
        return m_Desc;
    }

    VkSampler VulkanSampler::getHandle() const {
        return m_Sampler;
    }

    void VulkanSampler::reset() {
        if (m_Device != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device, m_Sampler, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Sampler = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
