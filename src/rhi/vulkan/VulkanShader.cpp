#include "rhi/vulkan/VulkanShader.h"

#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        VkShaderStageFlagBits toVkShaderStage(ShaderStage stage) {
            switch (stage) {
            case ShaderStage::Vertex:
                return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderStage::Fragment:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderStage::Compute:
                return VK_SHADER_STAGE_COMPUTE_BIT;
            }

            return VK_SHADER_STAGE_VERTEX_BIT;
        }
    } // namespace

    VulkanShader::VulkanShader(VkDevice device, const ShaderDesc& desc) : m_Device(device), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanShader requires a valid VkDevice");
        }

        if (m_Desc.bytecode.empty()) {
            throw std::runtime_error("VulkanShader requires SPIR-V bytecode");
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = m_Desc.bytecode.size() * sizeof(u32);
        createInfo.pCode = m_Desc.bytecode.data();

        if (!ARK_VK_CHECK(vkCreateShaderModule(m_Device, &createInfo, nullptr, &m_ShaderModule))) {
            throw std::runtime_error("vkCreateShaderModule failed");
        }
    }

    VulkanShader::~VulkanShader() {
        reset();
    }

    VulkanShader::VulkanShader(VulkanShader&& other) noexcept
        : m_Device(other.m_Device), m_ShaderModule(other.m_ShaderModule), m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_ShaderModule = VK_NULL_HANDLE;
    }

    VulkanShader& VulkanShader::operator=(VulkanShader&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_ShaderModule = other.m_ShaderModule;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_ShaderModule = VK_NULL_HANDLE;
        return *this;
    }

    const ShaderDesc& VulkanShader::getDesc() const {
        return m_Desc;
    }

    VkShaderModule VulkanShader::getHandle() const {
        return m_ShaderModule;
    }

    VkShaderStageFlagBits VulkanShader::getStageFlag() const {
        return toVkShaderStage(m_Desc.stage);
    }

    void VulkanShader::reset() {
        if (m_Device != VK_NULL_HANDLE && m_ShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_Device, m_ShaderModule, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_ShaderModule = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
