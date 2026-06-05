#include "rhi/vulkan/VulkanPipelineLayout.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    VulkanPipelineLayout::VulkanPipelineLayout(VkDevice device, const PipelineLayoutDesc& desc)
        : m_Device(device), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanPipelineLayout requires a valid VkDevice");
        }

        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        if (!ARK_VK_CHECK(vkCreatePipelineLayout(m_Device, &createInfo, nullptr, &m_PipelineLayout))) {
            throw std::runtime_error("vkCreatePipelineLayout failed");
        }
    }

    VulkanPipelineLayout::~VulkanPipelineLayout() {
        reset();
    }

    VulkanPipelineLayout::VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept
        : m_Device(other.m_Device), m_PipelineLayout(other.m_PipelineLayout), m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_PipelineLayout = VK_NULL_HANDLE;
    }

    VulkanPipelineLayout& VulkanPipelineLayout::operator=(VulkanPipelineLayout&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_PipelineLayout = other.m_PipelineLayout;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_PipelineLayout = VK_NULL_HANDLE;
        return *this;
    }

    const PipelineLayoutDesc& VulkanPipelineLayout::getDesc() const {
        return m_Desc;
    }

    VkPipelineLayout VulkanPipelineLayout::getHandle() const {
        return m_PipelineLayout;
    }

    void VulkanPipelineLayout::reset() {
        if (m_Device != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_PipelineLayout = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
