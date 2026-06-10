#include "rhi/vulkan/VulkanPipelineState.h"

#include "rhi/vulkan/VulkanPipelineLayout.h"
#include "rhi/vulkan/VulkanShader.h"

#include <array>
#include <stdexcept>

namespace ark::rhi::vulkan {
    namespace {
        VkPrimitiveTopology toVkPrimitiveTopology(PrimitiveTopology topology) {
            switch (topology) {
            case PrimitiveTopology::PointList:
                return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::LineList:
                return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip:
                return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::TriangleList:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            }

            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }

        VkVertexInputRate toVkVertexInputRate(VertexInputRate inputRate) {
            switch (inputRate) {
            case VertexInputRate::PerVertex:
                return VK_VERTEX_INPUT_RATE_VERTEX;
            case VertexInputRate::PerInstance:
                return VK_VERTEX_INPUT_RATE_INSTANCE;
            }

            return VK_VERTEX_INPUT_RATE_VERTEX;
        }

        VkPolygonMode toVkPolygonMode(PolygonMode polygonMode) {
            switch (polygonMode) {
            case PolygonMode::Fill:
                return VK_POLYGON_MODE_FILL;
            case PolygonMode::Line:
                return VK_POLYGON_MODE_LINE;
            }

            return VK_POLYGON_MODE_FILL;
        }

        VkCullModeFlags toVkCullMode(CullMode cullMode) {
            switch (cullMode) {
            case CullMode::None:
                return VK_CULL_MODE_NONE;
            case CullMode::Front:
                return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back:
                return VK_CULL_MODE_BACK_BIT;
            }

            return VK_CULL_MODE_NONE;
        }

        VkFrontFace toVkFrontFace(FrontFace frontFace) {
            switch (frontFace) {
            case FrontFace::CounterClockwise:
                return VK_FRONT_FACE_COUNTER_CLOCKWISE;
            case FrontFace::Clockwise:
                return VK_FRONT_FACE_CLOCKWISE;
            }

            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        }

        VkCompareOp toVkCompareOp(CompareOp compareOp) {
            switch (compareOp) {
            case CompareOp::Never:
                return VK_COMPARE_OP_NEVER;
            case CompareOp::Less:
                return VK_COMPARE_OP_LESS;
            case CompareOp::Equal:
                return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual:
                return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater:
                return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual:
                return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual:
                return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always:
                return VK_COMPARE_OP_ALWAYS;
            }

            return VK_COMPARE_OP_LESS;
        }

        VkBlendFactor toVkBlendFactor(BlendFactor factor) {
            switch (factor) {
            case BlendFactor::Zero:
                return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One:
                return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcAlpha:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            }

            return VK_BLEND_FACTOR_ONE;
        }

        VkBlendOp toVkBlendOp(BlendOp op) {
            switch (op) {
            case BlendOp::Add:
                return VK_BLEND_OP_ADD;
            }

            return VK_BLEND_OP_ADD;
        }

        VulkanShader* requireVulkanShader(Shader* shader, const char* name) {
            VulkanShader* vulkanShader = dynamic_cast<VulkanShader*>(shader);
            if (!vulkanShader || vulkanShader->getHandle() == VK_NULL_HANDLE) {
                throw std::runtime_error(name);
            }

            return vulkanShader;
        }

        VulkanPipelineLayout* requireVulkanPipelineLayout(PipelineLayout* layout) {
            VulkanPipelineLayout* vulkanLayout = dynamic_cast<VulkanPipelineLayout*>(layout);
            if (!vulkanLayout || vulkanLayout->getHandle() == VK_NULL_HANDLE) {
                throw std::runtime_error("VulkanPipelineState requires VulkanPipelineLayout");
            }

            return vulkanLayout;
        }
    } // namespace

    VulkanPipelineState::VulkanPipelineState(VkDevice device, const GraphicsPipelineDesc& desc)
        : m_Device(device), m_Desc(desc) {
        if (m_Device == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanPipelineState requires a valid VkDevice");
        }

        VulkanShader* vertexShader = requireVulkanShader(m_Desc.vertexShader, "GraphicsPipelineDesc requires vertex shader");
        VulkanShader* fragmentShader =
            requireVulkanShader(m_Desc.fragmentShader, "GraphicsPipelineDesc requires fragment shader");
        VulkanPipelineLayout* layout = requireVulkanPipelineLayout(m_Desc.layout);
        m_PipelineLayout = layout->getHandle();

        const VkFormat colorFormat = toVkFormat(m_Desc.colorFormat);
        if (colorFormat == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("GraphicsPipelineDesc requires color format");
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = vertexShader->getStageFlag();
        shaderStages[0].module = vertexShader->getHandle();
        shaderStages[0].pName = vertexShader->getDesc().entryPoint.c_str();

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = fragmentShader->getStageFlag();
        shaderStages[1].module = fragmentShader->getHandle();
        shaderStages[1].pName = fragmentShader->getDesc().entryPoint.c_str();

        std::vector<VkVertexInputBindingDescription> bindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
        bindingDescriptions.reserve(m_Desc.vertexInput.buffers.size());

        for (const VertexBufferLayoutDesc& vertexBuffer : m_Desc.vertexInput.buffers) {
            bindingDescriptions.push_back(VkVertexInputBindingDescription{
                .binding = vertexBuffer.binding,
                .stride = vertexBuffer.stride,
                .inputRate = toVkVertexInputRate(vertexBuffer.inputRate),
            });

            for (const VertexAttributeDesc& attribute : vertexBuffer.attributes) {
                attributeDescriptions.push_back(VkVertexInputAttributeDescription{
                    .location = attribute.location,
                    .binding = vertexBuffer.binding,
                    .format = toVkFormat(attribute.format),
                    .offset = attribute.offset,
                });
            }
        }

        VkPipelineVertexInputStateCreateInfo vertexInputState{};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = static_cast<u32>(bindingDescriptions.size());
        vertexInputState.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescriptions.size());
        vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = toVkPrimitiveTopology(m_Desc.topology);

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = toVkPolygonMode(m_Desc.rasterState.polygonMode);
        rasterizationState.cullMode = toVkCullMode(m_Desc.rasterState.cullMode);
        rasterizationState.frontFace = toVkFrontFace(m_Desc.rasterState.frontFace);
        rasterizationState.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = m_Desc.depthStencilState.enableDepthTest ? VK_TRUE : VK_FALSE;
        depthStencilState.depthWriteEnable = m_Desc.depthStencilState.enableDepthWrite ? VK_TRUE : VK_FALSE;
        depthStencilState.depthCompareOp = toVkCompareOp(m_Desc.depthStencilState.depthCompareOp);

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = m_Desc.blendState.colorAttachment.enableBlend ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor =
            toVkBlendFactor(m_Desc.blendState.colorAttachment.srcColorBlendFactor);
        colorBlendAttachment.dstColorBlendFactor =
            toVkBlendFactor(m_Desc.blendState.colorAttachment.dstColorBlendFactor);
        colorBlendAttachment.colorBlendOp = toVkBlendOp(m_Desc.blendState.colorAttachment.colorBlendOp);
        colorBlendAttachment.srcAlphaBlendFactor =
            toVkBlendFactor(m_Desc.blendState.colorAttachment.srcAlphaBlendFactor);
        colorBlendAttachment.dstAlphaBlendFactor =
            toVkBlendFactor(m_Desc.blendState.colorAttachment.dstAlphaBlendFactor);
        colorBlendAttachment.alphaBlendOp = toVkBlendOp(m_Desc.blendState.colorAttachment.alphaBlendOp);
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &colorBlendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        const VkFormat depthFormat = toVkFormat(m_Desc.depthFormat);
        VkPipelineRenderingCreateInfo renderingCreateInfo{};
        renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingCreateInfo.colorAttachmentCount = 1;
        renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
        renderingCreateInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = &renderingCreateInfo;
        pipelineCreateInfo.stageCount = static_cast<u32>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = m_PipelineLayout;
        pipelineCreateInfo.renderPass = VK_NULL_HANDLE;

        if (!ARK_VK_CHECK(
                vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_Pipeline))) {
            throw std::runtime_error("vkCreateGraphicsPipelines failed");
        }
    }

    VulkanPipelineState::~VulkanPipelineState() {
        reset();
    }

    VulkanPipelineState::VulkanPipelineState(VulkanPipelineState&& other) noexcept
        : m_Device(other.m_Device),
          m_Pipeline(other.m_Pipeline),
          m_PipelineLayout(other.m_PipelineLayout),
          m_Desc(other.m_Desc) {
        other.m_Device = VK_NULL_HANDLE;
        other.m_Pipeline = VK_NULL_HANDLE;
        other.m_PipelineLayout = VK_NULL_HANDLE;
    }

    VulkanPipelineState& VulkanPipelineState::operator=(VulkanPipelineState&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        m_Device = other.m_Device;
        m_Pipeline = other.m_Pipeline;
        m_PipelineLayout = other.m_PipelineLayout;
        m_Desc = other.m_Desc;

        other.m_Device = VK_NULL_HANDLE;
        other.m_Pipeline = VK_NULL_HANDLE;
        other.m_PipelineLayout = VK_NULL_HANDLE;
        return *this;
    }

    const GraphicsPipelineDesc& VulkanPipelineState::getDesc() const {
        return m_Desc;
    }

    VkPipeline VulkanPipelineState::getHandle() const {
        return m_Pipeline;
    }

    VkPipelineLayout VulkanPipelineState::getLayoutHandle() const {
        return m_PipelineLayout;
    }

    void VulkanPipelineState::reset() {
        if (m_Device != VK_NULL_HANDLE && m_Pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
        }

        m_Device = VK_NULL_HANDLE;
        m_Pipeline = VK_NULL_HANDLE;
        m_PipelineLayout = VK_NULL_HANDLE;
    }
} // namespace ark::rhi::vulkan
