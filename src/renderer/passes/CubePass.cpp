#include "renderer/passes/CubePass.h"

#include "asset/ShaderLoader.h"
#include "asset/TextureLoader.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "rhi/DeviceContext.h"
#include "rhi/SwapChain.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <limits>

namespace ark {
    namespace {
        struct CubeVertex {
            float position[3];
            float uv[2];
        };

        struct CheckerPixel {
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;
            u8 a = 255;
        };
        static_assert(sizeof(CheckerPixel) == 4, "CheckerPixel must be tightly packed RGBA8");

        struct alignas(16) CameraUniform {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 projection;
        };

        constexpr u32 CheckerboardWidth = 128;
        constexpr u32 CheckerboardHeight = 128;
        constexpr u32 CheckerboardTileSize = 16;
        constexpr const char* CubeTextureAssetPath = "assets/textures/xiaowei.png";

        // 每个面使用独立 4 个顶点，保证 UV 不会被 8 个共享角点错误复用。
        constexpr std::array<CubeVertex, 24> CubeVertices{{
            CubeVertex{{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}},
            CubeVertex{{1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}},
            CubeVertex{{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}},
            CubeVertex{{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}},

            CubeVertex{{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}},
            CubeVertex{{-1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}},
            CubeVertex{{-1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
            CubeVertex{{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},

            CubeVertex{{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}},
            CubeVertex{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f}},
            CubeVertex{{-1.0f, 1.0f, -1.0f}, {1.0f, 0.0f}},
            CubeVertex{{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},

            CubeVertex{{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}},
            CubeVertex{{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}},
            CubeVertex{{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
            CubeVertex{{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}},

            CubeVertex{{-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f}},
            CubeVertex{{1.0f, 1.0f, -1.0f}, {1.0f, 1.0f}},
            CubeVertex{{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
            CubeVertex{{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},

            CubeVertex{{-1.0f, -1.0f, 1.0f}, {0.0f, 1.0f}},
            CubeVertex{{1.0f, -1.0f, 1.0f}, {1.0f, 1.0f}},
            CubeVertex{{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f}},
            CubeVertex{{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}},
        }};

        constexpr std::array<u16, 36> CubeIndices{{
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23,
        }};

        asset::ImageData makeCheckerboardTexture() {
            asset::ImageData image{};
            image.width = CheckerboardWidth;
            image.height = CheckerboardHeight;
            image.format = asset::ImageFormat::Rgba8Unorm;
            image.bytesPerPixel = sizeof(CheckerPixel);
            image.debugName = "GeneratedCheckerboard";
            image.pixels.resize(static_cast<usize>(image.width) * static_cast<usize>(image.height) * image.bytesPerPixel);
            auto* pixels = reinterpret_cast<CheckerPixel*>(image.pixels.data());

            for (u32 y = 0; y < CheckerboardHeight; ++y) {
                for (u32 x = 0; x < CheckerboardWidth; ++x) {
                    const bool bright = ((x / CheckerboardTileSize) + (y / CheckerboardTileSize)) % 2 == 0;
                    CheckerPixel& pixel = pixels[y * CheckerboardWidth + x];
                    pixel.r = bright ? 240 : 30;
                    pixel.g = bright ? 235 : 70;
                    pixel.b = bright ? 210 : 145;
                    pixel.a = 255;
                }
            }

            return image;
        }

        Path findCubeTextureFile() {
            const std::array<Path, 3> candidates{
                Path{CubeTextureAssetPath},
                Path{"../"} / CubeTextureAssetPath,
                Path{"../../"} / CubeTextureAssetPath,
            };

            return findFirstExistingPath(candidates);
        }

        asset::ImageData loadCubeTextureImage() {
            const Path texturePath = findCubeTextureFile();
            if (!texturePath.empty()) {
                asset::ImageData image = asset::loadImageRgba8(texturePath);
                if (!image.empty()) {
                    ARK_INFO("Loaded cube texture: {}", texturePath.string());
                    return image;
                }

                ARK_WARN("Failed to load cube texture, fallback to checkerboard: {}", texturePath.string());
            } else {
                ARK_WARN("Cube texture file not found, fallback to checkerboard: {}", CubeTextureAssetPath);
            }

            return makeCheckerboardTexture();
        }

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

        createMeshResources();

        rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        descriptorSetLayoutDesc.debugName = "CubeDescriptorSetLayout";
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 0,
            .type = rhi::DescriptorType::UniformBuffer,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Vertex,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 1,
            .type = rhi::DescriptorType::SampledImage,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        descriptorSetLayoutDesc.bindings.push_back(rhi::DescriptorBindingDesc{
            .binding = 2,
            .type = rhi::DescriptorType::Sampler,
            .count = 1,
            .stages = rhi::ShaderStageFlags::Fragment,
        });
        m_DescriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutDesc);

        createTextureResources();

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

            rhi::SampledImageDescriptor textureDescriptor{};
            textureDescriptor.view = m_TextureView.get();
            m_DescriptorSets[frameSlot]->updateSampledImage(1, textureDescriptor);

            rhi::SamplerDescriptor samplerDescriptor{};
            samplerDescriptor.sampler = m_Sampler.get();
            m_DescriptorSets[frameSlot]->updateSampler(2, samplerDescriptor);
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "TexturedCubeVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("textured_cube.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = device.createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "TexturedCubeFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("textured_cube.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = device.createShader(fragmentShaderDesc);
        }

        createPipelineResources();
    }

    bool CubePass::prepare(FrameContext& frameContext) {
        return uploadMesh(frameContext) && uploadTexture(frameContext);
    }

    bool CubePass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !m_VertexBuffer || !m_IndexBuffer || !m_MeshUploaded || !m_TextureUploaded) {
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

    bool CubePass::createMeshResources() {
        if (!m_Device) {
            ARK_ERROR("CubePass requires device for mesh resources");
            return false;
        }

        rhi::BufferDesc vertexBufferDesc{};
        vertexBufferDesc.debugName = "CubeVertexBuffer";
        vertexBufferDesc.size = sizeof(CubeVertices);
        vertexBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
        vertexBufferDesc.memoryUsage = rhi::MemoryUsage::GpuOnly;
        m_VertexBuffer = m_Device->createBuffer(vertexBufferDesc);

        rhi::BufferDesc indexBufferDesc{};
        indexBufferDesc.debugName = "CubeIndexBuffer";
        indexBufferDesc.size = sizeof(CubeIndices);
        indexBufferDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
        indexBufferDesc.memoryUsage = rhi::MemoryUsage::GpuOnly;
        m_IndexBuffer = m_Device->createBuffer(indexBufferDesc);

        // 静态 mesh 数据先写入 staging，后续在 prepare() 中 copy 到 GPU-only buffer。
        rhi::BufferDesc vertexStagingBufferDesc{};
        vertexStagingBufferDesc.debugName = "CubeVertexStagingBuffer";
        vertexStagingBufferDesc.size = sizeof(CubeVertices);
        vertexStagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        vertexStagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        vertexStagingBufferDesc.initialData = CubeVertices.data();
        m_VertexStagingBuffer = m_Device->createBuffer(vertexStagingBufferDesc);

        rhi::BufferDesc indexStagingBufferDesc{};
        indexStagingBufferDesc.debugName = "CubeIndexStagingBuffer";
        indexStagingBufferDesc.size = sizeof(CubeIndices);
        indexStagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        indexStagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        indexStagingBufferDesc.initialData = CubeIndices.data();
        m_IndexStagingBuffer = m_Device->createBuffer(indexStagingBufferDesc);

        return m_VertexBuffer && m_IndexBuffer && m_VertexStagingBuffer && m_IndexStagingBuffer;
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

    bool CubePass::createTextureResources() {
        if (!m_Device) {
            ARK_ERROR("CubePass requires device for texture resources");
            return false;
        }

        const asset::ImageData textureImage = loadCubeTextureImage();
        if (textureImage.empty() || textureImage.format != asset::ImageFormat::Rgba8Unorm ||
            textureImage.bytesPerPixel != sizeof(CheckerPixel)) {
            ARK_ERROR("CubePass requires RGBA8 texture image");
            return false;
        }

        if (textureImage.width > std::numeric_limits<u32>::max() / textureImage.bytesPerPixel) {
            ARK_ERROR("CubePass texture row pitch overflow: {}", textureImage.debugName);
            return false;
        }

        m_TextureExtent = rhi::Extent2D{textureImage.width, textureImage.height};
        m_TextureBytesPerPixel = textureImage.bytesPerPixel;
        m_TextureRowPitch = textureImage.width * textureImage.bytesPerPixel;

        rhi::BufferDesc stagingBufferDesc{};
        stagingBufferDesc.debugName = "CubeTextureStagingBuffer";
        stagingBufferDesc.size = textureImage.byteSize();
        stagingBufferDesc.usage = rhi::BufferUsage::TransferSrc;
        stagingBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        stagingBufferDesc.initialData = textureImage.pixels.data();
        m_TextureStagingBuffer = m_Device->createBuffer(stagingBufferDesc);

        rhi::TextureDesc textureDesc{};
        textureDesc.extent = m_TextureExtent;
        textureDesc.format = rhi::Format::RGBA8Unorm;
        textureDesc.mipLevels = 1;
        textureDesc.arrayLayers = 1;
        textureDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::TransferDst;
        m_Texture = m_Device->createTexture(textureDesc);

        rhi::TextureViewDesc textureViewDesc{};
        textureViewDesc.format = textureDesc.format;
        m_TextureView = m_Device->createTextureView(*m_Texture, textureViewDesc);

        rhi::SamplerDesc samplerDesc{};
        samplerDesc.debugName = "CubeTextureSampler";
        samplerDesc.minFilter = rhi::FilterMode::Nearest;
        samplerDesc.magFilter = rhi::FilterMode::Nearest;
        samplerDesc.mipFilter = rhi::FilterMode::Nearest;
        samplerDesc.addressU = rhi::AddressMode::Repeat;
        samplerDesc.addressV = rhi::AddressMode::Repeat;
        samplerDesc.addressW = rhi::AddressMode::Repeat;
        m_Sampler = m_Device->createSampler(samplerDesc);

        return m_TextureStagingBuffer && m_Texture && m_TextureView && m_Sampler;
    }

    bool CubePass::ensurePipeline(FrameContext& frameContext) {
        if (!m_Device || !frameContext.swapChain) {
            ARK_ERROR("CubePass requires RenderDevice and SwapChain");
            return false;
        }

        const rhi::SwapChainDesc& swapChainDesc = frameContext.swapChain->getDesc();
        const rhi::Format colorFormat = swapChainDesc.colorFormat;
        const rhi::Format depthFormat = swapChainDesc.depthFormat;
        if (m_Pipeline && m_PipelineColorFormat == colorFormat && m_PipelineDepthFormat == depthFormat) {
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
            .format = rhi::Format::R32G32Float,
            .offset = offsetof(CubeVertex, uv),
        });

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "TexturedCubePipeline";
        pipelineDesc.vertexShader = m_VertexShader.get();
        pipelineDesc.fragmentShader = m_FragmentShader.get();
        pipelineDesc.layout = m_PipelineLayout.get();
        pipelineDesc.vertexInput.buffers.push_back(vertexLayout);
        pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
        pipelineDesc.depthStencilState.enableDepthTest = true;
        pipelineDesc.depthStencilState.enableDepthWrite = true;
        pipelineDesc.depthStencilState.depthCompareOp = rhi::CompareOp::Less;
        pipelineDesc.colorFormat = colorFormat;
        pipelineDesc.depthFormat = depthFormat;

        m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc);
        m_PipelineColorFormat = colorFormat;
        m_PipelineDepthFormat = depthFormat;
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

    bool CubePass::uploadMesh(FrameContext& frameContext) {
        if (m_MeshUploaded) {
            return true;
        }

        if (!frameContext.context || !m_VertexStagingBuffer || !m_IndexStagingBuffer || !m_VertexBuffer ||
            !m_IndexBuffer) {
            ARK_ERROR("CubePass requires mesh upload resources");
            return false;
        }

        rhi::BufferUploadDesc vertexUploadDesc{};
        vertexUploadDesc.sourceBuffer = m_VertexStagingBuffer.get();
        vertexUploadDesc.destinationBuffer = m_VertexBuffer.get();
        vertexUploadDesc.size = sizeof(CubeVertices);

        rhi::BufferUploadDesc indexUploadDesc{};
        indexUploadDesc.sourceBuffer = m_IndexStagingBuffer.get();
        indexUploadDesc.destinationBuffer = m_IndexBuffer.get();
        indexUploadDesc.size = sizeof(CubeIndices);

        // 首帧上传后目标 buffer 只作为 vertex/index 读取，staging 交给当前 frame 延迟释放。
        const bool vertexUploaded = frameContext.context->uploadBufferData(vertexUploadDesc);
        const bool indexUploaded = frameContext.context->uploadBufferData(indexUploadDesc);
        if (vertexUploaded && indexUploaded) {
            if (!frameContext.context->deferReleaseBuffer(m_VertexStagingBuffer) ||
                !frameContext.context->deferReleaseBuffer(m_IndexStagingBuffer)) {
                ARK_ERROR("CubePass failed to defer mesh staging buffers");
                return false;
            }

            m_MeshUploaded = true;
        }

        return m_MeshUploaded;
    }

    bool CubePass::uploadTexture(FrameContext& frameContext) {
        if (m_TextureUploaded) {
            return true;
        }

        if (!frameContext.context || !m_TextureStagingBuffer || !m_Texture) {
            ARK_ERROR("CubePass requires texture upload resources");
            return false;
        }

        rhi::TextureUploadDesc uploadDesc{};
        uploadDesc.sourceBuffer = m_TextureStagingBuffer.get();
        uploadDesc.texture = m_Texture.get();
        uploadDesc.extent = m_TextureExtent;
        uploadDesc.rowPitch = m_TextureRowPitch;
        uploadDesc.bytesPerPixel = m_TextureBytesPerPixel;

        // 只在首次可录制命令帧上传；之后 texture 保持 ShaderResource 状态供 fragment shader 采样。
        m_TextureUploaded = frameContext.context->uploadTextureData(uploadDesc);
        if (m_TextureUploaded && !frameContext.context->deferReleaseBuffer(m_TextureStagingBuffer)) {
            ARK_ERROR("CubePass failed to defer texture staging buffer");
            return false;
        }

        return m_TextureUploaded;
    }
} // namespace ark
