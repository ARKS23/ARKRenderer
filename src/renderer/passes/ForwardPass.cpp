#include "renderer/passes/ForwardPass.h"

#include "asset/GltfLoader.h"
#include "asset/MeshData.h"
#include "asset/ShaderLoader.h"
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

namespace ark {
    namespace {
        struct alignas(16) CameraUniform {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 projection;
        };

        constexpr const char* ForwardTextureAssetPath = "assets/textures/xiaowei.png";
        constexpr const char* ForwardModelAssetPath = "assets/models/forward_fixture.gltf";

        constexpr asset::MeshVertex makeVertex(float x,
                                               float y,
                                               float z,
                                               float nx,
                                               float ny,
                                               float nz,
                                               float u,
                                               float v) {
            asset::MeshVertex vertex{};
            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.position[2] = z;
            vertex.normal[0] = nx;
            vertex.normal[1] = ny;
            vertex.normal[2] = nz;
            vertex.uv0[0] = u;
            vertex.uv0[1] = v;
            return vertex;
        }

        // 0.8.4 先用 generated cube fixture 验证 ForwardPass，后续由 glTF loader 提供 ModelData。
        asset::MeshPrimitiveData makeForwardMesh() {
            constexpr std::array<asset::MeshVertex, 24> vertices{{
                makeVertex(-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f),
                makeVertex(1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f),
                makeVertex(1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
                makeVertex(-1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),

                makeVertex(1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
                makeVertex(-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f),
                makeVertex(-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
                makeVertex(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),

                makeVertex(-1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                makeVertex(-1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f),
                makeVertex(-1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
                makeVertex(-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f),

                makeVertex(1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
                makeVertex(1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f),
                makeVertex(1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
                makeVertex(1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),

                makeVertex(-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
                makeVertex(1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f),
                makeVertex(1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f),
                makeVertex(-1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),

                makeVertex(-1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f),
                makeVertex(1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f),
                makeVertex(1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f),
                makeVertex(-1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f),
            }};

            constexpr std::array<u32, 36> indices{{
                0, 1, 2, 0, 2, 3,
                4, 5, 6, 4, 6, 7,
                8, 9, 10, 8, 10, 11,
                12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19,
                20, 21, 22, 20, 22, 23,
            }};

            asset::MeshPrimitiveData mesh{};
            mesh.debugName = "ForwardFixtureMesh";
            mesh.vertices.assign(vertices.begin(), vertices.end());
            mesh.indices.assign(indices.begin(), indices.end());
            return mesh;
        }

        Path findForwardTextureFile() {
            const std::array<Path, 3> candidates{
                Path{ForwardTextureAssetPath},
                Path{"../"} / ForwardTextureAssetPath,
                Path{"../../"} / ForwardTextureAssetPath,
            };

            return findFirstExistingPath(candidates);
        }

        asset::MaterialData makeForwardMaterial() {
            asset::MaterialData material{};
            material.debugName = "ForwardFixtureMaterial";
            material.baseColorTexturePath = findForwardTextureFile();
            if (material.baseColorTexturePath.empty()) {
                ARK_ERROR("ForwardPass texture file not found: {}", ForwardTextureAssetPath);
            }
            return material;
        }

        Path findForwardModelFile() {
            const std::array<Path, 3> candidates{
                Path{ForwardModelAssetPath},
                Path{"../"} / ForwardModelAssetPath,
                Path{"../../"} / ForwardModelAssetPath,
            };

            return findFirstExistingPath(candidates);
        }

        asset::ModelData loadForwardModelOrFallback() {
            const Path modelPath = findForwardModelFile();
            if (!modelPath.empty()) {
                asset::ModelData model = asset::loadGltfModel(modelPath);
                if (!model.empty() && !model.materials.empty()) {
                    return model;
                }

                ARK_WARN("Failed to load ForwardPass glTF model, fallback to generated mesh: {}", modelPath.string());
            } else {
                ARK_WARN("ForwardPass glTF model not found, fallback to generated mesh: {}", ForwardModelAssetPath);
            }

            asset::ModelData fallback{};
            fallback.debugName = "ForwardFallbackModel";
            fallback.meshes.push_back(makeForwardMesh());
            fallback.materials.push_back(makeForwardMaterial());
            return fallback;
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
            uniform.projection[1][1] *= -1.0f;
            return uniform;
        }
    } // namespace

    ForwardPass::~ForwardPass() = default;

    void ForwardPass::setup(rhi::RenderDevice& device) {
        m_Device = &device;

        m_ModelData = loadForwardModelOrFallback();
        if (m_ModelData.empty() || m_ModelData.materials.empty()) {
            ARK_ERROR("ForwardPass requires model mesh and material data");
            return;
        }

        createMeshResource();
        createMaterialResource();
        createDescriptorResources();
        createShaderResources();
        createPipelineResources();
    }

    bool ForwardPass::prepare(FrameContext& frameContext) {
        if (!frameContext.context) {
            ARK_ERROR("ForwardPass requires DeviceContext");
            return false;
        }

        return m_Mesh.upload(*frameContext.context) && m_Material.upload(*frameContext.context);
    }

    bool ForwardPass::execute(FrameContext& frameContext) {
        if (!frameContext.context || !m_Mesh.isReady() || !m_Material.isReady()) {
            ARK_ERROR("ForwardPass requires ready mesh and material resources");
            return false;
        }

        const u32 frameSlot =
            frameContext.frameResource ? frameContext.frameResource->frameSlot % FramesInFlight : 0;
        if (!updateCameraUniform(frameContext, frameSlot) || !ensurePipeline(frameContext)) {
            return false;
        }

        frameContext.context->setPipeline(*m_Pipeline);
        frameContext.context->bindDescriptorSet(0, *m_DescriptorSets[frameSlot]);
        m_Mesh.bind(*frameContext.context);
        frameContext.context->drawIndexed(m_Mesh.makeDrawIndexedDesc());
        return true;
    }

    bool ForwardPass::createMeshResource() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for mesh resource");
            return false;
        }

        if (m_ModelData.meshes.empty()) {
            ARK_ERROR("ForwardPass requires model mesh data");
            return false;
        }

        return m_Mesh.create(*m_Device, m_ModelData.meshes.front());
    }

    bool ForwardPass::createMaterialResource() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for material resource");
            return false;
        }

        if (m_ModelData.materials.empty()) {
            ARK_ERROR("ForwardPass requires model material data");
            return false;
        }

        return m_Material.create(*m_Device, m_ModelData.materials.front());
    }

    bool ForwardPass::createDescriptorResources() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for descriptor resources");
            return false;
        }

        rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
        descriptorSetLayoutDesc.debugName = "ForwardDescriptorSetLayout";
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
        m_DescriptorSetLayout = m_Device->createDescriptorSetLayout(descriptorSetLayoutDesc);
        if (!m_DescriptorSetLayout) {
            return false;
        }

        for (u32 frameSlot = 0; frameSlot < FramesInFlight; ++frameSlot) {
            rhi::BufferDesc cameraBufferDesc{};
            cameraBufferDesc.debugName = "ForwardCameraUniformBuffer";
            cameraBufferDesc.size = sizeof(CameraUniform);
            cameraBufferDesc.usage = rhi::BufferUsage::Uniform;
            cameraBufferDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
            m_CameraBuffers[frameSlot] = m_Device->createBuffer(cameraBufferDesc);

            m_DescriptorSets[frameSlot] = m_Device->createDescriptorSet(*m_DescriptorSetLayout);
            if (!m_CameraBuffers[frameSlot] || !m_DescriptorSets[frameSlot]) {
                return false;
            }

            rhi::BufferDescriptor cameraDescriptor{};
            cameraDescriptor.buffer = m_CameraBuffers[frameSlot].get();
            cameraDescriptor.range = sizeof(CameraUniform);
            m_DescriptorSets[frameSlot]->updateUniformBuffer(0, cameraDescriptor);
            m_Material.updateDescriptorSet(*m_DescriptorSets[frameSlot], 1, 2);
        }

        return true;
    }

    bool ForwardPass::createShaderResources() {
        if (!m_Device) {
            ARK_ERROR("ForwardPass requires device for shader resources");
            return false;
        }

        rhi::ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.debugName = "ForwardMeshVertexShader";
        vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
        vertexShaderDesc.bytecode = asset::loadCompiledShader("mesh.vert.spv");
        if (!vertexShaderDesc.bytecode.empty()) {
            m_VertexShader = m_Device->createShader(vertexShaderDesc);
        }

        rhi::ShaderDesc fragmentShaderDesc{};
        fragmentShaderDesc.debugName = "ForwardMeshFragmentShader";
        fragmentShaderDesc.stage = rhi::ShaderStage::Fragment;
        fragmentShaderDesc.bytecode = asset::loadCompiledShader("mesh.frag.spv");
        if (!fragmentShaderDesc.bytecode.empty()) {
            m_FragmentShader = m_Device->createShader(fragmentShaderDesc);
        }

        return m_VertexShader && m_FragmentShader;
    }

    bool ForwardPass::createPipelineResources() {
        if (!m_Device || !m_DescriptorSetLayout) {
            ARK_ERROR("ForwardPass requires device and descriptor set layout");
            return false;
        }

        rhi::PipelineLayoutDesc layoutDesc{};
        layoutDesc.debugName = "ForwardPipelineLayout";
        layoutDesc.descriptorSetLayouts.push_back(m_DescriptorSetLayout.get());
        m_PipelineLayout = m_Device->createPipelineLayout(layoutDesc);
        return m_PipelineLayout != nullptr;
    }

    bool ForwardPass::ensurePipeline(FrameContext& frameContext) {
        if (!m_Device || !frameContext.swapChain) {
            ARK_ERROR("ForwardPass requires RenderDevice and SwapChain");
            return false;
        }

        const rhi::SwapChainDesc& swapChainDesc = frameContext.swapChain->getDesc();
        const rhi::Format colorFormat = swapChainDesc.colorFormat;
        const rhi::Format depthFormat = swapChainDesc.depthFormat;
        if (m_Pipeline && m_PipelineColorFormat == colorFormat && m_PipelineDepthFormat == depthFormat) {
            return true;
        }

        if (!m_VertexShader || !m_FragmentShader || !m_PipelineLayout) {
            ARK_ERROR("ForwardPass requires shader modules and pipeline layout");
            return false;
        }

        rhi::VertexBufferLayoutDesc vertexLayout{};
        vertexLayout.binding = 0;
        vertexLayout.stride = sizeof(asset::MeshVertex);
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 0,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(asset::MeshVertex, position),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 1,
            .format = rhi::Format::R32G32B32Float,
            .offset = offsetof(asset::MeshVertex, normal),
        });
        vertexLayout.attributes.push_back(rhi::VertexAttributeDesc{
            .location = 2,
            .format = rhi::Format::R32G32Float,
            .offset = offsetof(asset::MeshVertex, uv0),
        });

        rhi::GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "ForwardMeshPipeline";
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

    bool ForwardPass::updateCameraUniform(FrameContext& frameContext, u32 frameSlot) {
        if (!frameContext.context || frameSlot >= m_CameraBuffers.size() || !m_CameraBuffers[frameSlot]) {
            ARK_ERROR("ForwardPass requires per-frame camera buffer");
            return false;
        }

        const CameraUniform cameraUniform = makeCameraUniform(frameContext);
        return frameContext.context->updateBuffer(*m_CameraBuffers[frameSlot], &cameraUniform, sizeof(cameraUniform));
    }
} // namespace ark
