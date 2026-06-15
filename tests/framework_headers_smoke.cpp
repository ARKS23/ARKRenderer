#include "app/Application.h"
#include "app/GlfwWindow.h"
#include "app/Input.h"
#include "app/SandboxCameraController.h"
#include "app/SandboxLaunchOptions.h"
#include "app/Window.h"
#include "asset/GltfLoader.h"
#include "asset/MeshData.h"
#include "asset/ShaderCompiler.h"
#include "asset/ShaderLoader.h"
#include "asset/TextureLoader.h"
#include "core/Assert.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Memory.h"
#include "core/NonCopyable.h"
#include "core/Result.h"
#include "core/Timer.h"
#include "core/Types.h"
#include "renderer/FrameContext.h"
#include "renderer/FrameRenderer.h"
#include "renderer/CubemapOrientation.h"
#include "renderer/EnvironmentBrdfLutGenerator.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/EnvironmentIrradianceGenerator.h"
#include "renderer/EnvironmentSpecularPrefilterGenerator.h"
#include "renderer/MeshResource.h"
#include "renderer/ModelResource.h"
#include "renderer/RenderGraph.h"
#include "renderer/RenderPass.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"
#include "renderer/RendererPreset.h"
#include "renderer/RendererQuality.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/SandboxEnvironment.h"
#include "renderer/SceneResource.h"
#include "renderer/TextureCache.h"
#include "renderer/TextureResource.h"
#include "renderer/material/Material.h"
#include "renderer/material/MaterialResource.h"
#include "renderer/material/MaterialSystem.h"
#include "renderer/passes/BloomPass.h"
#include "renderer/passes/ClearPass.h"
#include "renderer/passes/CubePass.h"
#include "renderer/passes/ForwardPass.h"
#include "renderer/passes/ImGuiPass.h"
#include "renderer/passes/ShadowPass.h"
#include "renderer/passes/SkyboxPass.h"
#include "renderer/passes/ToneMappingPass.h"
#include "renderer/passes/TrianglePass.h"
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/Fence.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderBackend.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"
#include "rhi/VertexInput.h"
#include "rhi/vulkan/VulkanAllocator.h"
#include "rhi/vulkan/VulkanBindlessResourceManager.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanCommandContext.h"
#include "rhi/vulkan/VulkanCommandPool.h"
#include "rhi/vulkan/VulkanCommandQueue.h"
#include "rhi/vulkan/VulkanCommon.h"
#include "rhi/vulkan/VulkanDeletionQueue.h"
#include "rhi/vulkan/VulkanDescriptorManager.h"
#include "rhi/vulkan/VulkanDescriptorSet.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanFrameResource.h"
#include "rhi/vulkan/VulkanImGuiBackend.h"
#include "rhi/vulkan/VulkanPipelineCache.h"
#include "rhi/vulkan/VulkanPipelineLayout.h"
#include "rhi/vulkan/VulkanPipelineState.h"
#include "rhi/vulkan/VulkanResourceManager.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShader.h"
#include "rhi/vulkan/VulkanSwapChain.h"
#include "rhi/vulkan/VulkanSync.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdlib>
#include <string_view>

int main() {
    ark::ApplicationDesc applicationDesc{};
    applicationDesc.defaultModelPath = "assets/models/forward_multinode_fixture.gltf";
    applicationDesc.defaultEnvironmentPath = "assets/environments/local_test.hdr";
    applicationDesc.rendererQuality.environmentBake.brdfLutSampleCount = 512;
    applicationDesc.useDebugOrientationEnvironment = true;

    ark::rhi::NativeWindowHandle nativeWindow{};

    ark::rhi::RenderDeviceDesc renderDeviceDesc{};
    ark::rhi::RenderDeviceCreateInfo renderDeviceCreateInfo{};
    renderDeviceCreateInfo.desc = renderDeviceDesc;
    renderDeviceCreateInfo.desc.backend = ark::rhi::RenderBackendType::Vulkan;
    renderDeviceCreateInfo.nativeWindow = nativeWindow;

    ark::rhi::SwapChainDesc swapChainDesc{};
    ark::rhi::SwapChainCreateInfo swapChainCreateInfo{};
    swapChainCreateInfo.desc = swapChainDesc;
    ark::rhi::RenderBackendDesc renderBackendDesc{};
    renderBackendDesc.device = renderDeviceCreateInfo;
    renderBackendDesc.swapChain = swapChainDesc;

    const ark::rhi::SwapChainStatus swapChainStatus = ark::rhi::SwapChainStatus::Ready;
    ark::rhi::AcquireResult acquireResult{};
    ark::rhi::FrameResource frameResource{};
    frameResource.frameSlot = 0;
    frameResource.frameIndex = 1;

    ark::rhi::vulkan::VulkanFrameResource vulkanFrameResource{};
    vulkanFrameResource.frameSlot = 0;
    vulkanFrameResource.frameIndex = frameResource.frameIndex;

    const bool extentValid = ark::rhi::isValidExtent(swapChainDesc.extent);
    const char* colorFormatName = ark::rhi::vulkan::formatName(swapChainDesc.colorFormat);
    ark::rhi::BufferDesc bufferDesc{};
    bufferDesc.debugName = "SmokeVertexBuffer";
    bufferDesc.size = 256;
    bufferDesc.usage = ark::rhi::BufferUsage::Vertex | ark::rhi::BufferUsage::TransferDst;
    const bool bufferHasVertexUsage = ark::rhi::hasBufferUsage(bufferDesc.usage, ark::rhi::BufferUsage::Vertex);
    const ark::rhi::TextureUsage textureUsage =
        ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::DepthStencil;
    const bool textureHasDepthUsage = ark::rhi::hasTextureUsage(textureUsage, ark::rhi::TextureUsage::DepthStencil);
    const ark::rhi::TextureUsage sceneColorUsage =
        ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
    const bool textureHasShaderResourceUsage =
        ark::rhi::hasTextureUsage(sceneColorUsage, ark::rhi::TextureUsage::ShaderResource);
    ark::rhi::TextureDesc depthTextureDesc{};
    depthTextureDesc.extent = swapChainDesc.extent;
    depthTextureDesc.format = ark::rhi::Format::D32Float;
    depthTextureDesc.usage = ark::rhi::TextureUsage::DepthStencil;
    ark::rhi::TextureDesc cubeTextureDesc{};
    cubeTextureDesc.extent = ark::rhi::Extent2D{64, 64};
    cubeTextureDesc.format = ark::rhi::Format::RGBA16Float;
    cubeTextureDesc.mipLevels = 1;
    cubeTextureDesc.arrayLayers = 6;
    cubeTextureDesc.usage = ark::rhi::TextureUsage::RenderTarget | ark::rhi::TextureUsage::ShaderResource;
    cubeTextureDesc.type = ark::rhi::TextureType::Cube;
    ark::rhi::TextureViewDesc cubeTextureViewDesc{};
    cubeTextureViewDesc.format = cubeTextureDesc.format;
    cubeTextureViewDesc.arrayLayerCount = 6;
    cubeTextureViewDesc.type = ark::rhi::TextureViewType::Cube;

    ark::rhi::ResourceBarrier depthBarrier{};
    depthBarrier.before = ark::rhi::ResourceState::Undefined;
    depthBarrier.after = ark::rhi::ResourceState::DepthStencilWrite;

    ark::rhi::ShaderDesc shaderDesc{};
    shaderDesc.debugName = "SmokeShader";
    shaderDesc.bytecode = {0x07230203};

    ark::rhi::DescriptorBindingDesc descriptorBindingDesc{};
    descriptorBindingDesc.binding = 0;
    descriptorBindingDesc.type = ark::rhi::DescriptorType::UniformBuffer;
    descriptorBindingDesc.stages = ark::rhi::ShaderStageFlags::Vertex | ark::rhi::ShaderStageFlags::Fragment;
    const bool descriptorHasVertexStage =
        ark::rhi::hasShaderStage(descriptorBindingDesc.stages, ark::rhi::ShaderStageFlags::Vertex);

    ark::rhi::DescriptorSetLayoutDesc descriptorSetLayoutDesc{};
    descriptorSetLayoutDesc.debugName = "SmokeDescriptorSetLayout";
    descriptorSetLayoutDesc.bindings.push_back(descriptorBindingDesc);

    ark::rhi::PipelineLayoutDesc pipelineLayoutDesc{};
    pipelineLayoutDesc.debugName = "SmokePipelineLayout";

    ark::rhi::VertexBufferLayoutDesc vertexLayout{};
    vertexLayout.binding = 0;
    vertexLayout.stride = 24;
    vertexLayout.attributes.push_back(ark::rhi::VertexAttributeDesc{
        .location = 0,
        .format = ark::rhi::Format::R32G32B32Float,
        .offset = 0,
    });
    vertexLayout.attributes.push_back(ark::rhi::VertexAttributeDesc{
        .location = 1,
        .format = ark::rhi::Format::R32G32B32Float,
        .offset = 12,
    });

    ark::rhi::VertexInputLayoutDesc vertexInputLayout{};
    vertexInputLayout.buffers.push_back(vertexLayout);

    ark::rhi::GraphicsPipelineDesc graphicsPipelineDesc{};
    graphicsPipelineDesc.debugName = "SmokePipeline";
    graphicsPipelineDesc.vertexInput = vertexInputLayout;
    graphicsPipelineDesc.colorFormat = ark::rhi::Format::BGRA8Unorm;
    graphicsPipelineDesc.depthFormat = ark::rhi::Format::D32Float;
    graphicsPipelineDesc.depthStencilState.enableDepthTest = true;
    graphicsPipelineDesc.depthStencilState.enableDepthWrite = true;
    graphicsPipelineDesc.depthStencilState.depthCompareOp = ark::rhi::CompareOp::Less;
    graphicsPipelineDesc.blendState.colorAttachment.enableBlend = true;
    graphicsPipelineDesc.blendState.colorAttachment.srcColorBlendFactor = ark::rhi::BlendFactor::SrcAlpha;
    graphicsPipelineDesc.blendState.colorAttachment.dstColorBlendFactor = ark::rhi::BlendFactor::OneMinusSrcAlpha;
    graphicsPipelineDesc.blendState.colorAttachment.colorBlendOp = ark::rhi::BlendOp::Add;
    graphicsPipelineDesc.blendState.colorAttachment.srcAlphaBlendFactor = ark::rhi::BlendFactor::One;
    graphicsPipelineDesc.blendState.colorAttachment.dstAlphaBlendFactor = ark::rhi::BlendFactor::OneMinusSrcAlpha;
    graphicsPipelineDesc.blendState.colorAttachment.alphaBlendOp = ark::rhi::BlendOp::Add;

    ark::rhi::RenderingDesc renderingDesc{};
    renderingDesc.extent = swapChainDesc.extent;
    renderingDesc.colorAttachment.loadOp = ark::rhi::LoadOp::Clear;
    renderingDesc.colorAttachment.storeOp = ark::rhi::StoreOp::Store;
    renderingDesc.depthStencilAttachment.clearDepth = 1.0f;
    renderingDesc.depthStencilAttachment.loadOp = ark::rhi::LoadOp::Clear;

    ark::rhi::Viewport viewport{};
    viewport.width = static_cast<float>(swapChainDesc.extent.width);
    viewport.height = static_cast<float>(swapChainDesc.extent.height);

    ark::rhi::ScissorRect scissorRect{};
    scissorRect.width = swapChainDesc.extent.width;
    scissorRect.height = swapChainDesc.extent.height;

    ark::rhi::DrawDesc drawDesc{};
    drawDesc.vertexCount = 3;

    ark::rhi::DrawIndexedDesc drawIndexedDesc{};
    drawIndexedDesc.indexCount = 3;
    ark::rhi::IndexType indexType = ark::rhi::IndexType::UInt32;
    ark::rhi::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.range = bufferDesc.size;
    ark::rhi::SampledImageDescriptor sampledImageDescriptor{};
    ark::rhi::SamplerDescriptor samplerDescriptor{};
    ark::rhi::SamplerDesc samplerDesc{};
    samplerDesc.debugName = "SmokeSampler";
    samplerDesc.addressU = ark::rhi::AddressMode::ClampToEdge;
    samplerDesc.addressV = ark::rhi::AddressMode::MirroredRepeat;
    samplerDesc.minFilter = ark::rhi::FilterMode::Nearest;
    ark::rhi::TextureUploadDesc textureUploadDesc{};
    textureUploadDesc.extent = ark::rhi::Extent2D{128, 128};
    textureUploadDesc.rowPitch = textureUploadDesc.extent.width * 4;
    textureUploadDesc.bytesPerPixel = 4;
    ark::rhi::TextureUploadDesc hdrTextureUploadDesc{};
    hdrTextureUploadDesc.extent = ark::rhi::Extent2D{16, 8};
    hdrTextureUploadDesc.rowPitch = hdrTextureUploadDesc.extent.width * 16;
    hdrTextureUploadDesc.bytesPerPixel = 16;
    ark::rhi::BufferUploadDesc bufferUploadDesc{};
    bufferUploadDesc.sourceOffset = 0;
    bufferUploadDesc.destinationOffset = 0;
    bufferUploadDesc.size = bufferDesc.size;
    ark::rhi::BufferDesc readbackBufferDesc{};
    readbackBufferDesc.debugName = "SmokeReadbackBuffer";
    readbackBufferDesc.size = 256;
    readbackBufferDesc.usage = ark::rhi::BufferUsage::TransferDst;
    readbackBufferDesc.memoryUsage = ark::rhi::MemoryUsage::GpuToCpu;
    ark::rhi::TextureReadbackDesc textureReadbackDesc{};
    textureReadbackDesc.extent = ark::rhi::Extent2D{4, 4};
    textureReadbackDesc.rowPitch = textureReadbackDesc.extent.width * 16;
    textureReadbackDesc.bytesPerPixel = 16;
    ark::Scope<ark::rhi::Buffer> deferredBuffer;
    ark::asset::ImageData imageData{};
    imageData.width = 2;
    imageData.height = 2;
    imageData.format = ark::asset::ImageFormat::Rgba8Unorm;
    imageData.bytesPerPixel = 4;
    ark::asset::ImageData hdrImageData{};
    hdrImageData.width = 2;
    hdrImageData.height = 1;
    hdrImageData.format = ark::asset::ImageFormat::Rgba32Float;
    hdrImageData.bytesPerPixel = 16;
    ark::asset::MeshVertex meshVertex{};
    meshVertex.position[0] = 1.0f;
    meshVertex.uv1[0] = 0.5f;
    ark::asset::MeshPrimitiveData meshPrimitive{};
    meshPrimitive.vertices.push_back(meshVertex);
    meshPrimitive.indices.push_back(0);
    ark::asset::MaterialData materialData{};
    materialData.baseColorTexturePath = "assets/textures/xiaowei.png";
    materialData.baseColorTexture.path = materialData.baseColorTexturePath;
    materialData.baseColorTexture.texCoord = 1;
    materialData.baseColorTexture.hasSampler = true;
    materialData.baseColorTexture.sampler.minFilter = ark::asset::TextureFilter::Nearest;
    materialData.baseColorTexture.sampler.addressU = ark::asset::TextureAddressMode::MirroredRepeat;
    materialData.baseColorTexture.transform.offset[0] = 0.125f;
    materialData.baseColorTexture.transform.scale[0] = 2.0f;
    materialData.baseColorTexture.transform.hasTransform = true;
    materialData.alphaMode = ark::asset::AlphaMode::Mask;
    materialData.alphaCutoff = 0.42f;
    materialData.doubleSided = true;
    ark::asset::ModelData modelData{};
    modelData.meshes.push_back(meshPrimitive);
    modelData.materials.push_back(materialData);
    ark::asset::CameraData cameraData{};
    cameraData.debugName = "SmokePerspectiveCamera";
    cameraData.type = ark::asset::CameraProjectionType::Perspective;
    cameraData.perspective.yfov = 0.785398f;
    cameraData.perspective.aspectRatio = 1.777778f;
    cameraData.perspective.znear = 0.1f;
    cameraData.perspective.zfar = 100.0f;
    cameraData.perspective.hasZfar = true;
    modelData.cameras.push_back(cameraData);
    ark::asset::SceneCameraData sceneCameraData{};
    sceneCameraData.cameraIndex = 0;
    sceneCameraData.worldTransform.matrix[14] = 4.0f;
    sceneCameraData.debugName = "SmokeSceneCamera";
    modelData.sceneCameras.push_back(sceneCameraData);
    ark::asset::GltfLoader gltfLoader{};
    ark::MaterialResource materialResource{};
    ark::MaterialTextureCoordinateSet textureCoordinates{};
    textureCoordinates.normal = 1;
    if (textureCoordinates.normal != 1) {
        return EXIT_FAILURE;
    }
    ark::MaterialTextureTransform textureTransform{};
    textureTransform.offset[0] = 0.25f;
    textureTransform.scale[1] = 0.5f;
    ark::MaterialTextureTransformSet textureTransforms{};
    textureTransforms.baseColor = textureTransform;
    if (textureTransforms.baseColor.offset[0] != 0.25f || textureTransforms.baseColor.scale[1] != 0.5f) {
        return EXIT_FAILURE;
    }
    ark::TextureResource textureResource{};
    ark::TextureCache textureCache{};
    ark::TextureResourceDesc textureResourceDesc{};
    textureResourceDesc.colorSpace = ark::TextureColorSpace::Srgb;
    ark::EnvironmentResource environmentResource{};
    ark::EnvironmentResourceDesc environmentResourceDesc{};
    environmentResourceDesc.debugName = "SmokeEnvironment";
    ark::EnvironmentCubeResource environmentCubeResource{};
    ark::EnvironmentCubeResourceDesc environmentCubeResourceDesc{};
    environmentCubeResourceDesc.debugName = "SmokeEnvironmentCube";
    environmentCubeResourceDesc.faceExtent = cubeTextureDesc.extent;
    environmentCubeResourceDesc.format = cubeTextureDesc.format;
    environmentCubeResourceDesc.allowReadback = true;
    ark::EnvironmentCubeConverter environmentCubeConverter{};
    ark::EnvironmentCubeConversionDesc environmentCubeConversionDesc{};
    environmentCubeConversionDesc.source = &environmentResource;
    environmentCubeConversionDesc.target = &environmentCubeResource;
    environmentCubeConversionDesc.debugName = "SmokeEnvironmentCubeConversion";
    ark::EnvironmentIrradianceGenerator environmentIrradianceGenerator{};
    ark::EnvironmentIrradianceGenerationDesc environmentIrradianceGenerationDesc{};
    environmentIrradianceGenerationDesc.source = &environmentCubeResource;
    environmentIrradianceGenerationDesc.target = &environmentCubeResource;
    environmentIrradianceGenerationDesc.debugName = "SmokeEnvironmentIrradianceGeneration";
    ark::EnvironmentSpecularPrefilterGenerator environmentSpecularPrefilterGenerator{};
    ark::EnvironmentSpecularPrefilterDesc environmentSpecularPrefilterDesc{};
    environmentSpecularPrefilterDesc.source = &environmentCubeResource;
    environmentSpecularPrefilterDesc.target = &environmentCubeResource;
    environmentSpecularPrefilterDesc.sampleCount = 64;
    environmentSpecularPrefilterDesc.debugName = "SmokeEnvironmentSpecularPrefilter";
    ark::EnvironmentBrdfLutResource environmentBrdfLutResource{};
    ark::EnvironmentBrdfLutResourceDesc environmentBrdfLutResourceDesc{};
    environmentBrdfLutResourceDesc.debugName = "SmokeEnvironmentBrdfLut";
    environmentBrdfLutResourceDesc.extent = ark::rhi::Extent2D{128, 128};
    ark::EnvironmentBrdfLutGenerator environmentBrdfLutGenerator{};
    ark::EnvironmentBrdfLutGenerationDesc environmentBrdfLutGenerationDesc{};
    environmentBrdfLutGenerationDesc.target = &environmentBrdfLutResource;
    environmentBrdfLutGenerationDesc.sampleCount = 256;
    environmentBrdfLutGenerationDesc.debugName = "SmokeEnvironmentBrdfLutGeneration";
    ark::rhi::TextureView* smokeFaceView = environmentCubeResource.faceRenderTargetView(0);
    ark::rhi::TextureView* smokeFaceMipView = environmentCubeResource.faceMipRenderTargetView(0, 0);
    ark::rhi::Extent2D smokeMipExtent = environmentCubeResource.mipExtent(0);
    const ark::CubemapFaceContract& positiveXFace =
        ark::cubemapFaceContract(ark::CubemapFace::PositiveX);
    const ark::LinearColor positiveXDebugColor =
        ark::debugOrientationColorForDirection(positiveXFace.axis);
    ark::asset::ImageData debugOrientationEnvironment = ark::makeDebugOrientationEnvironmentImage();
    ark::asset::ImageData proceduralSandboxEnvironment = ark::makeProceduralSandboxEnvironmentImage();

    ark::Scope<ark::Timer> scopedTimer = ark::makeScope<ark::Timer>();
    ark::Ref<ark::Timer> sharedTimer = ark::makeRef<ark::Timer>();

    ark::RendererDesc rendererDesc{};
    rendererDesc.nativeWindow = nativeWindow;
    rendererDesc.extent = swapChainDesc.extent;
    rendererDesc.defaultModelPath = "assets/models/forward_multinode_fixture.gltf";
    rendererDesc.defaultEnvironmentPath = "assets/environments/local_test.hdr";
    rendererDesc.useDebugOrientationEnvironment = true;
    ark::RendererQualityDesc rendererQualityDesc{};
    rendererQualityDesc.environmentBake.specularPrefilterSampleCount = 256;
    rendererDesc.quality = rendererQualityDesc;
    ark::PostProcessingSettings applicationPostProcessing{};
    applicationPostProcessing.bloom.enabled = true;
    applicationPostProcessing.bloom.intensity = 0.06f;
    applicationPostProcessing.bloom.scatter = 0.55f;
    applicationDesc.postProcessing = applicationPostProcessing;
    const ark::RendererQualityDesc sanitizedRendererQualityDesc =
        ark::sanitizeRendererQualityDesc(rendererDesc.quality);
    if (sanitizedRendererQualityDesc.environmentBake.specularPrefilterSampleCount != 256) {
        return EXIT_FAILURE;
    }
    if (!applicationDesc.postProcessing.bloom.enabled ||
        applicationDesc.postProcessing.bloom.intensity != 0.06f) {
        return EXIT_FAILURE;
    }
    ark::RendererPresetDesc rendererPresetDesc{};
    rendererPresetDesc.scene = ark::RendererScenePreset::MaterialBall;
    rendererPresetDesc.quality = ark::RendererQualityPreset::Low;
    const ark::ResolvedRendererPreset resolvedRendererPreset =
        ark::resolveRendererPreset(rendererPresetDesc);
    if (resolvedRendererPreset.scene.modelPath.filename() != "material_ball_validation_fixture.gltf" ||
        resolvedRendererPreset.quality.environmentBake.brdfLutSampleCount != 512) {
        return EXIT_FAILURE;
    }
    const ark::RendererScenePreset parsedRendererScenePreset =
        ark::parseRendererScenePreset("debug_orientation");
    const ark::RendererQualityPreset parsedRendererQualityPreset =
        ark::parseRendererQualityPreset("high");
    constexpr std::array<std::string_view, 4> sandboxArguments{
        "--preset",
        "specular-validation",
        "--quality",
        "high",
    };
    const ark::SandboxLaunchOptions sandboxLaunchOptions =
        ark::parseSandboxLaunchOptions(sandboxArguments);
    const ark::ApplicationDesc sandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(sandboxLaunchOptions);
    if (sandboxApplicationDesc.defaultModelPath.filename() !=
            "specular_ibl_validation_fixture.gltf" ||
        sandboxApplicationDesc.rendererQuality.environmentBake.specularPrefilterSampleCount != 256 ||
        parsedRendererScenePreset != ark::RendererScenePreset::DebugOrientation ||
        parsedRendererQualityPreset != ark::RendererQualityPreset::High) {
        return EXIT_FAILURE;
    }
    constexpr std::array<std::string_view, 4> bloomArguments{
        "--preset",
        "material-ball",
        "--bloom",
        "--bloom-intensity=0.12",
    };
    const ark::ApplicationDesc bloomSandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(bloomArguments);
    if (!bloomSandboxApplicationDesc.postProcessing.bloom.enabled ||
        bloomSandboxApplicationDesc.postProcessing.bloom.intensity != 0.12f) {
        return EXIT_FAILURE;
    }
    ark::SceneResourceLoadDesc sceneResourceLoadDesc{};
    sceneResourceLoadDesc.modelPath = rendererDesc.defaultModelPath;
    sceneResourceLoadDesc.environmentPath = rendererDesc.defaultEnvironmentPath;
    sceneResourceLoadDesc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::DebugOrientation;
    ark::SceneResourceLoadReport sceneResourceLoadReport{};
    sceneResourceLoadReport.modelSource = ark::SceneModelSource::RequestedPath;
    sceneResourceLoadReport.environmentSource = ark::SceneEnvironmentSource::DebugOrientation;
    ark::SceneResource sceneResource{};

    ark::FrameContext frameContext{};
    frameContext.sceneColorView = sampledImageDescriptor.view;
    frameContext.environmentCube = &environmentCubeResource;
    frameContext.irradianceCube = &environmentCubeResource;
    frameContext.prefilteredSpecularCube = &environmentCubeResource;
    frameContext.brdfLut = &environmentBrdfLutResource;
    frameContext.colorFormat = ark::rhi::Format::RGBA16Float;
    frameContext.depthFormat = ark::rhi::Format::D32Float;
    if (!textureHasShaderResourceUsage || frameContext.colorFormat != ark::rhi::Format::RGBA16Float ||
        frameContext.depthFormat != ark::rhi::Format::D32Float ||
        frameContext.environmentCube != &environmentCubeResource ||
        frameContext.irradianceCube != &environmentCubeResource ||
        frameContext.prefilteredSpecularCube != &environmentCubeResource ||
        frameContext.brdfLut != &environmentBrdfLutResource) {
        return EXIT_FAILURE;
    }
    ark::MeshResource meshResource{};
    ark::ModelResource modelResource{};
    ark::RenderGraph renderGraph;
    const bool renderGraphExecuted = renderGraph.execute(frameContext);
    ark::RenderScene renderScene{};
    ark::SceneLighting sceneLighting{};
    sceneLighting.mainLight.direction = glm::vec3{0.0f, -1.0f, 0.0f};
    renderScene.setLighting(sceneLighting);
    ark::SceneEnvironment sceneEnvironment{};
    sceneEnvironment.environment = &environmentResource;
    sceneEnvironment.intensity = 1.5f;
    renderScene.setEnvironment(sceneEnvironment);
    if (!renderScene.environment().isEnabled() || renderScene.environment().intensity != 1.5f) {
        return EXIT_FAILURE;
    }
    ark::RenderView renderView{};
    renderView.setDefaultPerspective(swapChainDesc.extent);
    if (!renderView.setPerspectiveCamera(modelData.cameras.front(),
                                         modelData.sceneCameras.front().worldTransform,
                                         swapChainDesc.extent)) {
        return EXIT_FAILURE;
    }
    ark::InputSnapshot inputSnapshot{};
    inputSnapshot.cursorPosition = glm::vec2{1.0f, 2.0f};
    inputSnapshot.cursorDelta = glm::vec2{0.5f, -0.25f};
    inputSnapshot.scrollDelta = glm::vec2{0.0f, 1.0f};
    inputSnapshot.rightMouseDown = true;
    ark::SandboxCameraController sandboxCameraController{};
    sandboxCameraController.setViewportExtent(swapChainDesc.extent);
    sandboxCameraController.update(inputSnapshot);
    sandboxCameraController.writeTo(renderView);
    ark::ToneMappingSettings toneMappingSettings{};
    toneMappingSettings.exposure = 1.25f;
    toneMappingSettings.outputGamma = 2.2f;
    renderView.setToneMappingSettings(toneMappingSettings);
    if (renderView.toneMappingSettings().exposure != 1.25f ||
        renderView.toneMappingSettings().outputGamma != 2.2f) {
        return EXIT_FAILURE;
    }
    ark::RenderQueue renderQueue{};
    ark::Scope<ark::FrameRenderer> frameRenderer = ark::createFrameRenderer();

    ark::Timer timer;
    timer.reset();

    (void)renderDeviceCreateInfo;
    (void)applicationDesc;
    (void)swapChainCreateInfo;
    (void)renderBackendDesc;
    (void)swapChainStatus;
    (void)acquireResult;
    (void)frameResource;
    (void)vulkanFrameResource;
    (void)extentValid;
    (void)colorFormatName;
    (void)bufferDesc;
    (void)bufferHasVertexUsage;
    (void)textureUsage;
    (void)textureHasDepthUsage;
    (void)sceneColorUsage;
    (void)textureHasShaderResourceUsage;
    (void)depthTextureDesc;
    (void)cubeTextureDesc;
    (void)cubeTextureViewDesc;
    (void)depthBarrier;
    (void)shaderDesc;
    (void)descriptorBindingDesc;
    (void)descriptorHasVertexStage;
    (void)descriptorSetLayoutDesc;
    (void)pipelineLayoutDesc;
    (void)vertexInputLayout;
    (void)graphicsPipelineDesc;
    (void)renderingDesc;
    (void)viewport;
    (void)scissorRect;
    (void)drawDesc;
    (void)drawIndexedDesc;
    (void)indexType;
    (void)bufferDescriptor;
    (void)sampledImageDescriptor;
    (void)samplerDescriptor;
    (void)samplerDesc;
    (void)textureUploadDesc;
    (void)hdrTextureUploadDesc;
    (void)readbackBufferDesc;
    (void)textureReadbackDesc;
    (void)imageData;
    (void)hdrImageData;
    (void)modelData;
    (void)gltfLoader;
    (void)materialResource;
    (void)textureResource;
    (void)textureCache;
    (void)textureResourceDesc;
    (void)environmentResource;
    (void)environmentResourceDesc;
    (void)environmentCubeResource;
    (void)environmentCubeResourceDesc;
    (void)environmentCubeConverter;
    (void)environmentCubeConversionDesc;
    (void)environmentIrradianceGenerator;
    (void)environmentIrradianceGenerationDesc;
    (void)environmentSpecularPrefilterGenerator;
    (void)environmentSpecularPrefilterDesc;
    (void)environmentBrdfLutResource;
    (void)environmentBrdfLutResourceDesc;
    (void)environmentBrdfLutGenerator;
    (void)environmentBrdfLutGenerationDesc;
    (void)smokeFaceView;
    (void)smokeFaceMipView;
    (void)smokeMipExtent;
    (void)positiveXFace;
    (void)positiveXDebugColor;
    (void)debugOrientationEnvironment;
    (void)proceduralSandboxEnvironment;
    (void)scopedTimer;
    (void)sharedTimer;
    (void)rendererDesc;
    (void)rendererQualityDesc;
    (void)sanitizedRendererQualityDesc;
    (void)rendererPresetDesc;
    (void)resolvedRendererPreset;
    (void)parsedRendererScenePreset;
    (void)parsedRendererQualityPreset;
    (void)sandboxArguments;
    (void)sandboxLaunchOptions;
    (void)sandboxApplicationDesc;
    (void)sceneResourceLoadDesc;
    (void)sceneResourceLoadReport;
    (void)sceneResource;
    (void)meshResource;
    (void)modelResource;
    (void)renderGraphExecuted;
    (void)renderScene;
    (void)sceneLighting;
    (void)sceneEnvironment;
    (void)renderView;
    (void)inputSnapshot;
    (void)sandboxCameraController;
    (void)renderQueue;
    (void)frameRenderer;

    return EXIT_SUCCESS;
}
