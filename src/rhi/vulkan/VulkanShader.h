#pragma once

#include "rhi/Shader.h"
#include "rhi/vulkan/VulkanCommon.h"

namespace ark::rhi::vulkan {
    class VulkanShader final : public Shader {
    public:
        VulkanShader(VkDevice device, const ShaderDesc& desc);
        ~VulkanShader() override;

        VulkanShader(const VulkanShader&) = delete;
        VulkanShader& operator=(const VulkanShader&) = delete;

        VulkanShader(VulkanShader&& other) noexcept;
        VulkanShader& operator=(VulkanShader&& other) noexcept;

        const ShaderDesc& getDesc() const override;

        VkShaderModule getHandle() const;
        VkShaderStageFlagBits getStageFlag() const;

    private:
        void reset();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
        ShaderDesc m_Desc;
    };
} // namespace ark::rhi::vulkan
