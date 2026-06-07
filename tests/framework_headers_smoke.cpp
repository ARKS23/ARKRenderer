#include "app/Application.h"
#include "app/GlfwWindow.h"
#include "app/Window.h"
#include "asset/GltfLoader.h"
#include "asset/ShaderCompiler.h"
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
#include "renderer/RenderGraph.h"
#include "renderer/RenderPass.h"
#include "renderer/RenderQueue.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"
#include "renderer/material/Material.h"
#include "renderer/material/MaterialSystem.h"
#include "renderer/passes/BloomPass.h"
#include "renderer/passes/ClearPass.h"
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

#include <cstdlib>

int main() {
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

    ark::rhi::ShaderDesc shaderDesc{};
    shaderDesc.debugName = "SmokeShader";
    shaderDesc.bytecode = {0x07230203};

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

    ark::rhi::RenderingDesc renderingDesc{};
    renderingDesc.extent = swapChainDesc.extent;
    renderingDesc.colorAttachment.loadOp = ark::rhi::LoadOp::Clear;
    renderingDesc.colorAttachment.storeOp = ark::rhi::StoreOp::Store;

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

    ark::Scope<ark::Timer> scopedTimer = ark::makeScope<ark::Timer>();
    ark::Ref<ark::Timer> sharedTimer = ark::makeRef<ark::Timer>();

    ark::RendererDesc rendererDesc{};
    rendererDesc.nativeWindow = nativeWindow;
    rendererDesc.extent = swapChainDesc.extent;

    ark::FrameContext frameContext{};
    ark::RenderGraph renderGraph;
    const bool renderGraphExecuted = renderGraph.execute(frameContext);
    ark::Scope<ark::FrameRenderer> frameRenderer = ark::createFrameRenderer();

    ark::Timer timer;
    timer.reset();

    (void)renderDeviceCreateInfo;
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
    (void)shaderDesc;
    (void)pipelineLayoutDesc;
    (void)vertexInputLayout;
    (void)graphicsPipelineDesc;
    (void)renderingDesc;
    (void)viewport;
    (void)scissorRect;
    (void)drawDesc;
    (void)drawIndexedDesc;
    (void)indexType;
    (void)scopedTimer;
    (void)sharedTimer;
    (void)rendererDesc;
    (void)renderGraphExecuted;
    (void)frameRenderer;

    return EXIT_SUCCESS;
}
