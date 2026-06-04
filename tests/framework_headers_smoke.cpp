#include "app/Application.h"
#include "app/GlfwWindow.h"
#include "app/Window.h"
#include "asset/GltfLoader.h"
#include "asset/ShaderCompiler.h"
#include "asset/TextureLoader.h"
#include "core/Assert.h"
#include "core/FileSystem.h"
#include "core/Log.h"
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
#include "rhi/Buffer.h"
#include "rhi/DescriptorSet.h"
#include "rhi/DescriptorSetLayout.h"
#include "rhi/DeviceContext.h"
#include "rhi/Fence.h"
#include "rhi/FrameResource.h"
#include "rhi/PipelineLayout.h"
#include "rhi/PipelineState.h"
#include "rhi/RHICommon.h"
#include "rhi/RenderDevice.h"
#include "rhi/ResourceBarrier.h"
#include "rhi/Sampler.h"
#include "rhi/Shader.h"
#include "rhi/SwapChain.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"
#include "rhi/vulkan/VulkanAllocator.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanCommandPool.h"
#include "rhi/vulkan/VulkanCommandQueue.h"
#include "rhi/vulkan/VulkanDeletionQueue.h"
#include "rhi/vulkan/VulkanDescriptorAllocator.h"
#include "rhi/vulkan/VulkanDescriptorSet.h"
#include "rhi/vulkan/VulkanDescriptorSetLayout.h"
#include "rhi/vulkan/VulkanDeviceContext.h"
#include "rhi/vulkan/VulkanFrameResource.h"
#include "rhi/vulkan/VulkanImGuiBackend.h"
#include "rhi/vulkan/VulkanPipelineLayout.h"
#include "rhi/vulkan/VulkanPipelineState.h"
#include "rhi/vulkan/VulkanRenderDevice.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShader.h"
#include "rhi/vulkan/VulkanSwapChain.h"
#include "rhi/vulkan/VulkanSync.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"

#include <cstdlib>

int main() {
    ark::rhi::NativeWindowHandle nativeWindow{};
    ark::rhi::SwapChainDesc swapChainDesc{};
    swapChainDesc.nativeWindow = nativeWindow;

    ark::FrameContext frameContext{};
    ark::RenderGraph renderGraph;
    renderGraph.execute(frameContext);

    ark::Timer timer;
    timer.reset();

    return EXIT_SUCCESS;
}
