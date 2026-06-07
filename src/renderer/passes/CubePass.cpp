#include "renderer/passes/CubePass.h"

#include "asset/ShaderLoader.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "rhi/DeviceContext.h"
#include "rhi/SwapChain.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>

namespace ark {
    namespace {
        struct CubeVertex {
            float position[3];
            float color[3];
        };

        struct alignas(16) CameraUniform {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 projection;
        };

        constexpr std::array<CubeVertex, 8> CubeVertices{{
            CubeVertex{{-1.0f, -1.0f, -1.0f}, {0.95f, 0.25f, 0.25f}},
            CubeVertex{{1.0f, -1.0f, -1.0f}, {0.25f, 0.85f, 0.35f}},
            CubeVertex{{1.0f, 1.0f, -1.0f}, {0.25f, 0.45f, 1.0f}},
            CubeVertex{{-1.0f, 1.0f, -1.0f}, {0.95f, 0.80f, 0.25f}},
            CubeVertex{{-1.0f, -1.0f, 1.0f}, {0.90f, 0.35f, 0.95f}},
            CubeVertex{{1.0f, -1.0f, 1.0f}, {0.25f, 0.90f, 0.90f}},
            CubeVertex{{1.0f, 1.0f, 1.0f}, {1.0f, 0.55f, 0.25f}},
            CubeVertex{{-1.0f, 1.0f, 1.0f}, {0.75f, 0.95f, 0.35f}},
        }};

        constexpr std::array<u16, 36> CubeIndices{{
            4, 5, 6, 4, 6, 7,
            1, 0, 3, 1, 3, 2,
            0, 4, 7, 0, 7, 3,
            5, 1, 2, 5, 2, 6,
            3, 7, 6, 3, 6, 2,
            0, 1, 5, 0, 5, 4,
        }};

        CameraUniform makeCameraUniform(const FrameContext& frameContext) {
            const float aspect =
                frameContext.extent.height == 0
                    ? 1.0f
                    : static_cast<float>(frameContext.extent.width) / static_cast<float>(frameContext.extent.height);
            const float angle = static_cast<float>(frameContext.frameIndex) * 0.018f;

            CameraUniform uniform{};
            uniform.model = glm::rotate(glm::mat4{1.0f}, angle, glm::normalize(glm::vec3{0.35f, 1.0f, 0.2f}));
            uniform.view = glm::lookAt(glm::vec3{0.0f, 0.0f, -4.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            uniform.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
            // GLM 的 Vulkan depth 已由 GLM_FORCE_DEPTH_ZERO_TO_ONE 处理，Y 轴仍需翻转到 Vulkan clip space。
            uniform.projection[1][1] *= -1.0f;
            return uniform;
        }
    } // namespace

    CubePass::~CubePass() = default;

    void CubePass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        rhi::BufferDesc vertexBufferDesc{};
        vertexBufferDesc.debugName = "CubeVertexBuffer";
        vertexBufferDesc.size = sizeof(CubeVertices);
        vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
        vertexBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        vertexBufferDesc.initialData = CubeVertices.data();
        m_VertexBuffer = device.createBuffer(vertexBufferDesc);

        rhi::BufferDesc indexBufferDesc{};
        indexBufferDesc.debugName = "CubeIndexBuffer";
        indexBufferDesc.size = sizeof(CubeIndices);
        indexBufferDesc.usage = rhi::BufferUsage::Index;
        indexBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        indexBufferDesc.initialData = CubeIndices.data();
        m_IndexBuffer = device.createBuffer(indexBufferDesc);

        rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        descriptorSetLayoutDesc.debugName = "CubeDescriptorSetLayout";
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
        });
        m_DescriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutDesc);

        for (u32 frameSlot = 0; frameSlot < FramesInFlight; ++frameSlot) {
            rhi::BufferDesc cameraBufferDesc{};
            cameraBufferDesc.debugName = "CubeCameraUniformBuffer";
            cameraBufferDesc.size = sizeof(CameraUniform);
            cameraBufferDesc.usage = rhi::BufferUsage::Uniform;
            cameraBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_CameraBuffers[frameSlot] = device.createBuffer(cameraBufferDesc);

            m_DescriptorSets[frameSlot] = device.createDescriptorSet(*m_DescriptorSetLayout);
            rhi::BufferDescriptor cameraDescriptor{};
            cameraDescriptor.buffer = m_CameraBuffers[frameSlot].get();
            cameraDescriptor.range = sizeof(CameraUniform);
            m_DescriptorSets[frameSlot]->updateUniformBuffer(0, cameraDescriptor);
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "CubeVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("cube.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = device.createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "CubeFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("cube.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = device.createShader(fragmentShaderDesc);
        }

        createPipelineResources();
    }

    bool CubePass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !m_VertexBuffer || !m_IndexBuffer) {
            ARK_ERROR("CubePass requires DeviceContext and mesh buffers");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!updateCameraUniform(frameContext, frameSlot) || !ensurePipeline(frameContext)) {
            return false;
        }

        // 每个 frame slot 使用独立 uniform buffer / descriptor set，避免覆盖 GPU 仍在读取的数据。
        frameContext.context->setPipeline(*m_Pipeline);
        frameContext.context->bindDescriptorSet(0, *m_DescriptorSets[frameSlot]);
        frameContext.context->setVertexBuffer(0, *m_VertexBuffer);
        frameContext.context->setIndexBuffer(*m_IndexBuffer, rhi::IndexType::UInt16);

        rhi::DrawIndexedDesc drawDesc{};
        drawDesc.indexCount = static_cast<u32>(CubeIndices.size());
        frameContext.context->drawIndexed(drawDesc);
        return true;
    }

    bool CubePass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("CubePass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "CubePipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    bool CubePass::ensurePipeline(FrameContext& frameContext) {
        if (!m_Device || !frameContext.swapChain) {
            ARK_ERROR("CubePass requires RenderDevice and SwapChain");
            return false;
        }

        const rhi::Format colorFormat = frameContext.swapChain->getDesc().colorFormat;
        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return true;
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("CubePass requires shader modules and pipeline layout");
            return false;
        }

        rhi::VertexBufferLayoutDesc vertexLayout{};
        vertexLayout.binding = 0;
        vertexLayout.stride = sizeof(CubeVertex);
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 0,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(CubeVertex, position),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 1,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(CubeVertex, color),
        });

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "CubePipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.colorFormat = colorFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        return m_Pipeline != nullptr;
    }

    bool CubePass::updateCameraUniform(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context || frameSlot >= m_CameraBuffers.size() || !m_CameraBuffers[frameSlot]) {
            ARK_ERROR("CubePass requires per-frame camera buffer");
            return false;
        }

        const CameraUniform cameraUniform = makeCameraUniform(frameContext);
        return frameContext.context->updateBuffer(*m_CameraBuffers[frameSlot], &cameraUniform, sizeof(cameraUniform));
    }
} // namespace ark
