#include "rhi/vulkan/VulkanDescriptorSetLayout.h"

#include <stdexcept>
#include <vector>

namespace ark::rhi::vulkan {
    namespace {
        VkDescriptorType toVkDescriptorType(DescriptorType type) {
            switch (type) {
            case DescriptorType::UniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case DescriptorType::SampledImage:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case DescriptorType::Sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            }

            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        VkShaderStageFlags toVkShaderStageFlags(ShaderStageFlags stages) {
            VkShaderStageFlags flags = 0;

            if (hasShaderStage(stages, ShaderStageFlags::Vertex)) {
                flags |= VK_SHADER_STAGE_VERTEX_BIT;
            }
            if (hasShaderStage(stages, ShaderStageFlags::Fragment)) {
                flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            if (hasShaderStage(stages, ShaderStageFlags::Compute)) {
                flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            }

            return flags;
        }
    } // namespace

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, const DescriptorSetLayoutDesc& desc)
        : m_Device(device), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanDescriptorSetLayout requires a valid VkDevice");
        }

        // Vulkan 创建时会复制 layout binding 信息，局部数组只需活到 vkCreateDescriptorSetLayout 返回。
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(m_Desc.bindings.size());

        for (const DescriptorBindingDesc& binding : m_Desc.bindings) {
            if (binding.count == 0) {
                throw std::runtime_error("Descriptor binding count must be greater than zero");
            }

            const VkShaderStageFlags stageFlags = toVkShaderStageFlags(binding.stages);
            if (stageFlags == 0) {
                throw std::runtime_error("Descriptor binding requires at least one shader stage");
            }

            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = binding.binding,
                .descriptorType = toVkDescriptorType(binding.type),
                .descriptorCount = binding.count,
                .stageFlags = stageFlags,
                .pImmutableSamplers = nullptr,
            });
        }

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<u32>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (!ARK_VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &createInfo, nullptr, &m_Layout))) {
            throw std::runtime_error("vkCreateDescriptorSetLayout failed");
        }
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
        reset();
    }

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&& other) noexcept
        : m_Device(other.m_Device), m_Layout(other.m_Layout), m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Layout = VK_NULL_HANDLE;
    }

    VulkanDescriptorSetLayout& VulkanDescriptorSetLayout::operator=(VulkanDescriptorSetLayout&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_Layout = other.m_Layout;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Layout = VK_NULL_HANDLE;
        return *this;
    }

    const DescriptorSetLayoutDesc& VulkanDescriptorSetLayout::getDesc() const {
        return m_Desc;
    }

    VkDescriptorSetLayout VulkanDescriptorSetLayout::getHandle() const {
        return m_Layout;
    }

    void VulkanDescriptorSetLayout::reset() {
        if (m_Device != VK_NULL_HANDLE && m_Layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Layout = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
