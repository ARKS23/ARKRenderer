#include "rhi/vulkan/VulkanDescriptorManager.h"

#include "core/Log.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace ark::rhi::vulkan {
    namespace {
        constexpr u32 MaxDescriptorSetsPerPool = 256;
        constexpr u32 MaxUniformBufferDescriptorsPerPool = 256;
        constexpr u32 MaxSampledImageDescriptorsPerPool = 256;
        constexpr u32 MaxSamplerDescriptorsPerPool = 256;

        bool isPoolCapacityError(VkResult result) {
            return result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
        }
    } // namespace

    VulkanDescriptorManager::VulkanDescriptorManager(VkDevice device) : m_Device(device) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid VkDevice");
        }

        m_Pools.push_back(createPool());
    }

    VulkanDescriptorManager::~VulkanDescriptorManager() {
        reset();
    }

    VulkanDescriptorManager::VulkanDescriptorManager(VulkanDescriptorManager&& other) noexcept
        : m_Device(other.m_Device), m_Pools(std::move(other.m_Pools)) {
        other.m_Device = VK_NULL_HANDLE;
    }

    VulkanDescriptorManager& VulkanDescriptorManager::operator=(VulkanDescriptorManager&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_Pools = std::move(other.m_Pools);

        other.m_Device = VK_NULL_HANDLE;
        return *this;
    }

    VkDescriptorSet VulkanDescriptorManager::allocateDescriptorSet(const VulkanDescriptorSetLayout& layout) {
        if (m_Device == VK_NULL_HANDLE || m_Pools.empty()) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid descriptor pool");
        }

        const VkDescriptorSetLayout vkLayout = layout.getHandle();
        if (vkLayout == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorManager requires a valid descriptor set layout");
        }

        VkResult result = VK_SUCCESS;
        VkDescriptorSet descriptorSet = allocateFromPool(m_Pools.back(), vkLayout, result);
        if (result == VK_SUCCESS) {
            return descriptorSet;
        }

        if (!isPoolCapacityError(result)) {
            throw std::runtime_error(std::string("vkAllocateDescriptorSets failed: ") + vkResultName(result));
        }

        // 当前 pool 耗尽时新建 pool；descriptor set 仍随所属 pool 一起销毁。
        m_Pools.push_back(createPool());
        ARK_INFO("Vulkan descriptor pool grown: poolCount={}", m_Pools.size());

        descriptorSet = allocateFromPool(m_Pools.back(), vkLayout, result);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::string("vkAllocateDescriptorSets failed after growing descriptor pool: ") +
                                     vkResultName(result));
        }

        return descriptorSet;
    }

    VkDescriptorPool VulkanDescriptorManager::createPool() const {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = MaxUniformBufferDescriptorsPerPool,
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = MaxSampledImageDescriptorsPerPool,
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = MaxSamplerDescriptorsPerPool,
            },
        }};

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.maxSets = MaxDescriptorSetsPerPool;
        createInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();

        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!ARK_VK_CHECK(vkCreateDescriptorPool(m_Device, &createInfo, nullptr, &pool))) {
            throw std::runtime_error("vkCreateDescriptorPool failed");
        }

        return pool;
    }

    VkDescriptorSet VulkanDescriptorManager::allocateFromPool(VkDescriptorPool pool,
                                                              VkDescriptorSetLayout layout,
                                                              VkResult& result) const {
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = pool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &layout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        result = vkAllocateDescriptorSets(m_Device, &allocateInfo, &descriptorSet);
        return descriptorSet;
    }

    void VulkanDescriptorManager::reset() {
        // VkDescriptorPool 销毁时会隐式释放从该 pool 分配出的 descriptor set。
        if (m_Device != VK_NULL_HANDLE) {
            for (VkDescriptorPool pool : m_Pools) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(m_Device, pool, nullptr);
                }
            }
        }

        m_Device = VK_NULL_HANDLE;
        m_Pools.clear();
    }
} // namespace ark::rhi::vulkan
