#include "renderer/passes/TrianglePass.h"

#include "core/FileSystem.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "rhi/Buffer.h"
#include "rhi/DeviceContext.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RenderDevice.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <vector>

#ifndef ARK_SHADER_OUTPUT_DIR
#define ARK_SHADER_OUTPUT_DIR "shaders"
#endif

namespace ark {
    namespace {
        struct TriangleVertex {
            float position[2];
            float color[3];
        };

        constexpr std::array<TriangleVertex, 3> TriangleVertices{{
            TriangleVertex{{0.0f, -0.55f}, {1.0f, 0.18f, 0.18f}},
            TriangleVertex{{0.55f, 0.45f}, {0.18f, 0.85f, 0.32f}},
            TriangleVertex{{-0.55f, 0.45f}, {0.18f, 0.36f, 1.0f}},
        }};

        Path findShaderFile(const char* fileName) {
            // 正常路径来自 CMake 注入的 ARK_SHADER_OUTPUT_DIR；后两个候选主要方便直接运行可执行文件时调试。
            const std::array<Path, 3> candidates{
                Path{ARK_SHADER_OUTPUT_DIR} / fileName,
                Path{"shaders"} / fileName,
                Path{"build/msvc-vcpkg/shaders"} / fileName,
            };

            for (const Path& path : candidates) {
                if (std::filesystem::exists(path)) {
                    return path;
                }
            }

            return candidates.front();
        }

        std::vector<u32> readSpirvFile(const Path& path) {
            // ShaderDesc 使用 u32 word 数组保存 SPIR-V，因此这里直接按 word 读取。
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                ARK_ERROR("Failed to open shader file: {}", path.string());
                return {};
            }

            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0 || fileSize % static_cast<std::streamsize>(sizeof(u32)) != 0) {
                ARK_ERROR("Invalid SPIR-V shader size: {}", path.string());
                return {};
            }

            std::vector<u32> bytecode(static_cast<usize>(fileSize) / sizeof(u32));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(bytecode.data()), fileSize);

            if (!file) {
                ARK_ERROR("Failed to read shader file: {}", path.string());
                return {};
            }

            return bytecode;
        }
    } // namespace

    TrianglePass::~TrianglePass() = default;

    void TrianglePass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        // 第一版直接使用 CPU 可见 vertex buffer，避免过早引入 staging/upload 系统。
        rhi::BufferDesc vertexBufferDesc{};
        vertexBufferDesc.debugName = "TriangleVertexBuffer";
        vertexBufferDesc.size = sizeof(TriangleVertices);
        vertexBufferDesc.usage = rhi::BufferUsage::Vertex;
        vertexBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        vertexBufferDesc.initialData = TriangleVertices.data();
        m_VertexBuffer = device.createBuffer(vertexBufferDesc);

        // shader 编译发生在 CMake 阶段；运行时只加载 SPIR-V 并交给 RenderDevice 创建 Shader 对象。
        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "TriangleVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = readSpirvFile(findShaderFile("triangle.vert.spv"));
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = device.createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "TriangleFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = readSpirvFile(findShaderFile("triangle.frag.spv"));
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = device.createShader(fragmentShaderDesc);
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "TrianglePipelineLayout";
        // Phase 0.4 没有 descriptor / push constant，所以 pipeline layout 是空 layout。
        m_PipelineLayout = device.createPipelineLayout(layoutDesc);
    }

    bool TrianglePass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !m_VertexBuffer) {
            ARK_ERROR("TrianglePass requires DeviceContext and vertex buffer");
            return false;
        }

        if (!ensurePipeline(frameContext)) {
            return false;
        }

        // FrameRenderer 已经设置 viewport/scissor 并打开 dynamic rendering，这里只负责绑定三角形资源并提交 draw。
        frameContext.context->setPipeline(*m_Pipeline);
        frameContext.context->setVertexBuffer(0, *m_VertexBuffer);

        rhi::DrawDesc drawDesc{};
        drawDesc.vertexCount = static_cast<u32>(TriangleVertices.size());
        frameContext.context->draw(drawDesc);
        return true;
    }

    bool TrianglePass::ensurePipeline(FrameContext& frameContext) {
        if (!m_Device || !frameContext.swapChain) {
            ARK_ERROR("TrianglePass requires RenderDevice and SwapChain");
            return false;
        }

        const rhi::Format colorFormat = frameContext.swapChain->getDesc().colorFormat;
        // graphics pipeline 需要在创建时固定 color attachment format；swapchain format 变化时必须重建。
        if (m_Pipeline && m_PipelineColorFormat == colorFormat) {
            return true;
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("TrianglePass requires shader modules and pipeline layout");
            return false;
        }

        rhi::VertexBufferLayoutDesc vertexLayout{};
        vertexLayout.binding = 0;
        vertexLayout.stride = sizeof(TriangleVertex);
        // location 必须和 triangle.vert.hlsl 中的 [[vk::location(n)]] 对齐。
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 0,
            .format = rhi::Format::R32G32Float,
            .offset = offsetof(TriangleVertex, position),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 1,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(TriangleVertex, color),
        });

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "TrianglePipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = colorFormat;

        // pipeline 创建仍走 RHI，TrianglePass 不关心底层是 Vulkan pipeline 还是其他 API pipeline。
        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        return m_Pipeline != nullptr;
    }
} // namespace ark
