#include "app/Application.h"
#include "app/GlfwWindow.h"
#include "app/Input.h"
#include "app/SandboxCameraController.h"
#include "app/SandboxLaunchOptions.h"
#include "app/SandboxRuntimeSettings.h"
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
#include "renderer/FrameOverlay.h"
#include "renderer/Bounds.h"
#include "renderer/effects/ibl/CubemapOrientation.h"
#include "renderer/effects/ibl/EnvironmentBrdfLutGenerator.h"
#include "renderer/EnvironmentBrdfLutResource.h"
#include "renderer/effects/ibl/EnvironmentCubeConverter.h"
#include "renderer/EnvironmentCubeResource.h"
#include "renderer/EnvironmentResource.h"
#include "renderer/effects/ibl/EnvironmentIrradianceGenerator.h"
#include "renderer/effects/ibl/EnvironmentSpecularPrefilterGenerator.h"
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
#include "renderer/effects/sky/SandboxEnvironment.h"
#include "renderer/SceneResource.h"
#include "renderer/effects/shadow/ShadowCascadeBuilder.h"
#include "renderer/TextureCache.h"
#include "renderer/TextureResource.h"
#include "renderer/material/Material.h"
#include "renderer/material/MaterialResource.h"
#include "renderer/material/MaterialSystem.h"
#include "renderer/effects/bloom/BloomPass.h"
#include "renderer/passes/ClearPass.h"
#include "renderer/passes/CubePass.h"
#include "renderer/passes/ForwardPass.h"
#include "renderer/passes/ImGuiPass.h"
#include "renderer/effects/shadow/ShadowPass.h"
#include "renderer/effects/sky/SkyboxPass.h"
#include "renderer/effects/tone_mapping/ToneMappingPass.h"
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
#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cstdlib>
#include <string_view>

int main() {
    ark::ApplicationDesc applicationDesc{};
    applicationDesc.defaultScene.modelPath = "assets/models/forward_multinode_fixture.gltf";
    applicationDesc.defaultScene.modelTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f});
    applicationDesc.defaultScene.additionalModels.push_back(ark::SceneAdditionalModelDesc{
        "assets/models/DamagedHelmet/DamagedHelmet.gltf",
        glm::mat4{1.0f},
        "FrameworkAdditionalModel",
    });
    applicationDesc.defaultScene.environmentPath = "assets/environments/local_test.hdr";
    applicationDesc.defaultScene.environmentIntensity = 0.75f;
    applicationDesc.defaultScene.overrideLighting = true;
    applicationDesc.defaultScene.lighting.mainLight.direction = glm::vec3{-0.75f, -0.45f, -0.35f};
    applicationDesc.rendererQuality.environmentBake.brdfLutSampleCount = 512;
    applicationDesc.view.toneMapping.operatorType = ark::ToneMappingOperator::ACES;
    applicationDesc.view.shadows.enabled = true;
    applicationDesc.view.shadows.strength = 0.5f;
    applicationDesc.view.shadows.mapExtent = 2048;
    applicationDesc.view.shadows.filterMode = ark::ShadowFilterMode::Pcf3x3;
    applicationDesc.view.shadows.filterRadiusTexels = 1.5f;
    applicationDesc.useDebugOrientationEnvironment = true;
    applicationDesc.debugUiEnabled = true;

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
    ark::rhi::TextureViewDesc shadowArrayViewDesc{};
    shadowArrayViewDesc.format = ark::rhi::Format::D32Float;
    shadowArrayViewDesc.arrayLayerCount = ark::MaxShadowCascadeCount;
    shadowArrayViewDesc.type = ark::rhi::TextureViewType::Texture2DArray;

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
    rendererDesc.defaultScene.modelPath = "assets/models/forward_multinode_fixture.gltf";
    rendererDesc.defaultScene.modelTransform = applicationDesc.defaultScene.modelTransform;
    rendererDesc.defaultScene.additionalModels = applicationDesc.defaultScene.additionalModels;
    rendererDesc.defaultScene.environmentPath = "assets/environments/local_test.hdr";
    rendererDesc.defaultScene.environmentIntensity = applicationDesc.defaultScene.environmentIntensity;
    rendererDesc.defaultScene.overrideLighting = applicationDesc.defaultScene.overrideLighting;
    rendererDesc.defaultScene.lighting = applicationDesc.defaultScene.lighting;
    rendererDesc.defaultScene.environmentFallback = ark::SceneEnvironmentFallbackPolicy::DebugOrientation;
    ark::RendererQualityDesc rendererQualityDesc{};
    rendererQualityDesc.environmentBake.specularPrefilterSampleCount = 256;
    rendererDesc.quality = rendererQualityDesc;
    ark::PostProcessingSettings applicationPostProcessing{};
    applicationPostProcessing.bloom.enabled = true;
    applicationPostProcessing.bloom.intensity = 0.06f;
    applicationPostProcessing.bloom.scatter = 0.55f;
    applicationDesc.view.postProcessing = applicationPostProcessing;
    const ark::RendererQualityDesc sanitizedRendererQualityDesc =
        ark::sanitizeRendererQualityDesc(rendererDesc.quality);
    if (sanitizedRendererQualityDesc.environmentBake.specularPrefilterSampleCount != 256) {
        return EXIT_FAILURE;
    }
    if (!applicationDesc.view.postProcessing.bloom.enabled ||
        applicationDesc.view.postProcessing.bloom.intensity != 0.06f) {
        return EXIT_FAILURE;
    }
    if (rendererDesc.defaultScene.modelTransform[0][0] != 2.0f ||
        rendererDesc.defaultScene.environmentIntensity != 0.75f ||
        !rendererDesc.defaultScene.overrideLighting ||
        rendererDesc.defaultScene.lighting.mainLight.direction.y != -0.45f) {
        return EXIT_FAILURE;
    }
    if (!applicationDesc.view.shadows.enabled ||
        applicationDesc.view.shadows.strength != 0.5f ||
        applicationDesc.view.shadows.mapExtent != 2048 ||
        !applicationDesc.view.shadows.fitSceneBounds ||
        applicationDesc.view.shadows.filterMode != ark::ShadowFilterMode::Pcf3x3 ||
        applicationDesc.view.shadows.filterRadiusTexels != 1.5f) {
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
    const ark::RendererScenePreset parsedBloomRendererScenePreset =
        ark::parseRendererScenePreset("bloom-validation");
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
    if (sandboxApplicationDesc.defaultScene.modelPath.filename() !=
            "specular_ibl_validation_fixture.gltf" ||
        sandboxApplicationDesc.rendererQuality.environmentBake.specularPrefilterSampleCount != 256 ||
        parsedRendererScenePreset != ark::RendererScenePreset::DebugOrientation ||
        parsedBloomRendererScenePreset != ark::RendererScenePreset::BloomValidation ||
        parsedRendererQualityPreset != ark::RendererQualityPreset::High) {
        return EXIT_FAILURE;
    }
    constexpr std::array<std::string_view, 4> bloomArguments{
        "--preset",
        "bloom-validation",
        "--bloom",
        "--bloom-intensity=0.12",
    };
    const ark::ApplicationDesc bloomSandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(bloomArguments);
    if (!bloomSandboxApplicationDesc.view.postProcessing.bloom.enabled ||
        bloomSandboxApplicationDesc.view.postProcessing.bloom.intensity != 0.12f) {
        return EXIT_FAILURE;
    }
    if (bloomSandboxApplicationDesc.defaultScene.modelPath.filename() !=
        "bloom_validation_fixture.gltf") {
        return EXIT_FAILURE;
    }
    constexpr std::array<std::string_view, 5> shadowArguments{
        "--preset",
        "shadow-validation",
        "--shadow-strength=0.4",
        "--shadow-bounds",
        "24.0",
    };
    const ark::ApplicationDesc shadowSandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(shadowArguments);
    if (shadowSandboxApplicationDesc.defaultScene.modelPath.filename() != "sponza.gltf" ||
        !shadowSandboxApplicationDesc.useDebugOrientationEnvironment ||
        !shadowSandboxApplicationDesc.view.shadows.enabled ||
        shadowSandboxApplicationDesc.view.shadows.strength != 0.4f ||
        shadowSandboxApplicationDesc.view.shadows.orthographicHalfExtent != 64.0f ||
        shadowSandboxApplicationDesc.view.shadows.farPlane != 256.0f ||
        shadowSandboxApplicationDesc.view.shadows.lightDistance != 96.0f ||
        shadowSandboxApplicationDesc.view.shadows.fitSceneBounds) {
        return EXIT_FAILURE;
    }
    constexpr std::array<std::string_view, 2> toneMappingArguments{
        "--tone-mapping",
        "linear",
    };
    const ark::ApplicationDesc toneMappingSandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(toneMappingArguments);
    if (toneMappingSandboxApplicationDesc.view.toneMapping.operatorType !=
        ark::ToneMappingOperator::Linear) {
        return EXIT_FAILURE;
    }
    constexpr std::array<std::string_view, 1> noUiArguments{
        "--no-ui",
    };
    const ark::ApplicationDesc noUiSandboxApplicationDesc =
        ark::makeSandboxApplicationDesc(noUiArguments);
    if (noUiSandboxApplicationDesc.debugUiEnabled) {
        return EXIT_FAILURE;
    }
    ark::SceneResourceLoadDesc sceneResourceLoadDesc{};
    sceneResourceLoadDesc.modelPath = rendererDesc.defaultScene.modelPath;
    sceneResourceLoadDesc.modelTransform = rendererDesc.defaultScene.modelTransform;
    sceneResourceLoadDesc.additionalModels = rendererDesc.defaultScene.additionalModels;
    sceneResourceLoadDesc.environmentPath = rendererDesc.defaultScene.environmentPath;
    sceneResourceLoadDesc.environmentIntensity = rendererDesc.defaultScene.environmentIntensity;
    sceneResourceLoadDesc.overrideLighting = rendererDesc.defaultScene.overrideLighting;
    sceneResourceLoadDesc.lighting = rendererDesc.defaultScene.lighting;
    sceneResourceLoadDesc.environmentFallback = ark::SceneEnvironmentFallbackPolicy::DebugOrientation;
    ark::SceneResourceLoadReport sceneResourceLoadReport{};
    sceneResourceLoadReport.modelSource = ark::SceneModelSource::RequestedPath;
    sceneResourceLoadReport.environmentSource = ark::SceneEnvironmentSource::DebugOrientation;
    sceneResourceLoadReport.loadedModelCount = 2;
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
    inputSnapshot.debugUiTogglePressed = true;
    ark::SandboxCameraController sandboxCameraController{};
    sandboxCameraController.setViewportExtent(swapChainDesc.extent);
    sandboxCameraController.update(inputSnapshot);
    sandboxCameraController.writeTo(renderView);
    ark::SandboxRuntimeSettings runtimeSettings =
        ark::makeSandboxRuntimeSettings(applicationDesc);
    ark::applySandboxRuntimeSettings(renderView, runtimeSettings);
    ark::FrameOverlay* frameOverlay = nullptr;
    ark::ToneMappingSettings toneMappingSettings{};
    toneMappingSettings.exposure = 1.25f;
    toneMappingSettings.outputGamma = 2.2f;
    toneMappingSettings.operatorType = ark::ToneMappingOperator::ACES;
    renderView.setToneMappingSettings(toneMappingSettings);
    renderView.setClipRange(0.05f, 90.0f);
    ark::ShadowSettings shadowSettings{};
    shadowSettings.enabled = true;
    shadowSettings.strength = 0.75f;
    shadowSettings.bias = 0.002f;
    shadowSettings.mapExtent = 512;
    shadowSettings.filterMode = ark::ShadowFilterMode::Pcf5x5;
    shadowSettings.filterRadiusTexels = 2.0f;
    shadowSettings.cascades.enabled = true;
    shadowSettings.cascades.cascadeCount = 2;
    shadowSettings.cascades.splitLambda = 0.7f;
    shadowSettings.cascades.maxDistance = 120.0f;
    shadowSettings.cascades.cascadeExtent = 1024;
    renderView.setShadowSettings(shadowSettings);
    ark::VisibilitySettings visibilitySettings{};
    visibilitySettings.enableFrustumCulling = true;
    renderView.setVisibilitySettings(visibilitySettings);
    if (renderView.toneMappingSettings().exposure != 1.25f ||
        renderView.toneMappingSettings().outputGamma != 2.2f ||
        renderView.toneMappingSettings().operatorType != ark::ToneMappingOperator::ACES ||
        !renderView.shadowSettings().enabled ||
        renderView.shadowSettings().strength != 0.75f ||
        renderView.shadowSettings().bias != 0.002f ||
        renderView.shadowSettings().mapExtent != 512 ||
        renderView.shadowSettings().filterMode != ark::ShadowFilterMode::Pcf5x5 ||
        renderView.shadowSettings().filterRadiusTexels != 2.0f ||
        !renderView.shadowSettings().fitSceneBounds ||
        !renderView.shadowSettings().cascades.enabled ||
        renderView.shadowSettings().cascades.cascadeCount != 2 ||
        renderView.shadowSettings().cascades.splitLambda != 0.7f ||
        renderView.shadowSettings().cascades.maxDistance != 120.0f ||
        renderView.shadowSettings().cascades.cascadeExtent != 1024 ||
        !renderView.visibilitySettings().enableFrustumCulling ||
        renderView.cameraNearPlane() != 0.05f ||
        renderView.cameraFarPlane() != 90.0f ||
        ark::parseShadowFilterMode("pcf-3x3") != ark::ShadowFilterMode::Pcf3x3 ||
        ark::parseShadowFilterMode("pcf5") != ark::ShadowFilterMode::Pcf5x5 ||
        ark::parseToneMappingOperator("filmic") != ark::ToneMappingOperator::ACES) {
        return EXIT_FAILURE;
    }
    const ark::CascadeSplitDistances splitDistances =
        ark::computeCascadeSplitDistances(0.1f, 10.0f, 2, 0.5f);
    if (!splitDistances.isValid() ||
        splitDistances.cascadeCount != 2 ||
        splitDistances.distances[2] != 10.0f) {
        return EXIT_FAILURE;
    }
    ark::CascadeShadowFrameData cascadeFrameData{};
    cascadeFrameData.enabled = true;
    cascadeFrameData.cascadeCount = 2;
    cascadeFrameData.cascadeExtent = 1024;
    cascadeFrameData.cascades[0].nearDistance = 0.1f;
    cascadeFrameData.cascades[0].farDistance = 16.0f;
    frameContext.cascadeShadows = cascadeFrameData;
    if (!frameContext.cascadeShadows.isEnabled() ||
        frameContext.cascadeShadows.cascadeCount != 2 ||
        frameContext.cascadeShadows.cascades[0].farDistance != 16.0f) {
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
    (void)shadowArrayViewDesc;
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
    (void)runtimeSettings;
    (void)frameOverlay;
    (void)inputSnapshot;
    (void)sandboxCameraController;
    (void)renderQueue;
    (void)frameRenderer;

    return EXIT_SUCCESS;
}
