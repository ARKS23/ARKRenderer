#include "rhi/vulkan/VulkanDescriptorManager.h"

#include "rhi/vulkan/VulkanDescriptorSetLayout.h"

#include <array>
#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        constexpr u32 MaxDescriptorSets = 256;
        constexpr u32 MaxUniformBufferDescriptors = 256;
    } // namespace

    VulkanDescriptorManager::VulkanDescriptorManager(VkDevice device) : m_Device(device) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid VkDevice");
        }

        // Phase 0.5 只支持 uniform buffer，先用固定 pool，后续再扩展为可增长 pool。
        const std::array<VkDescriptorPoolSize, 1> poolSizes{{
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = MaxUniformBufferDescriptors,
            },
        }};

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.maxSets = MaxDescriptorSets;
        createInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();

        if (!ARK_VK_CHECK(vkCreateDescriptorPool(m_Device, &createInfo, nullptr, &m_Pool))) {
            throw std::runtime_error("vkCreateDescriptorPool failed");
        }
    }

    VulkanDescriptorManager::~VulkanDescriptorManager() {
        reset();
    }

    VulkanDescriptorManager::VulkanDescriptorManager(VulkanDescriptorManager&& other) noexcept
        : m_Device(other.m_Device), m_Pool(other.m_Pool) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Pool = VK_NULL_HANDLE;
    }

    VulkanDescriptorManager& VulkanDescriptorManager::operator=(VulkanDescriptorManager&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_Pool = other.m_Pool;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Pool = VK_NULL_HANDLE;
        return *this;
    }

    VkDescriptorSet VulkanDescriptorManager::allocateDescriptorSet(const VulkanDescriptorSetLayout& layout) {
        if (m_Device == VK_NULL_HANDLE || m_Pool == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid descriptor pool");
        }

        const VkDescriptorSetLayout vkLayout = layout.getHandle();
        if (vkLayout == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid descriptor set layout");
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_Pool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &vkLayout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (!ARK_VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocateInfo, &descriptorSet))) {
            throw std::runtime_error("vkAllocateDescriptorSets failed");
        }

        return descriptorSet;
    }

    void VulkanDescriptorManager::reset() {
        // VkDescriptorPool 销毁时会隐式释放从该 pool 分配出的 descriptor set。
        if (m_Device != VK_NULL_HANDLE && m_Pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Pool = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
