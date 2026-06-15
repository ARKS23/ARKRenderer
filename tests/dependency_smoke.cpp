#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <spirv_reflect.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <volk.h>
#include <vulkan/vulkan.h>

#if ARK_HAS_DXC
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objbase.h>
#include <oaidl.h>
#include <dxcapi.h>
#endif

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    const glm::vec3 sampleVector{1.0F, 2.0F, 3.0F};

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ARKRenderer dependency smoke test";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = appInfo.apiVersion;

    vkb::InstanceBuilder instanceBuilder;
    SpvReflectShaderModule shaderModule{};
    tinygltf::Model model;

    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2{640.0F, 480.0F};
    ImGui::DestroyContext();

#if ARK_HAS_DXC
    IDxcUtils* dxcUtils = nullptr;
    (void)dxcUtils;
#endif

    int glfwMajor = 0;
    int glfwMinor = 0;
    int glfwRevision = 0;
    glfwGetVersion(&glfwMajor, &glfwMinor, &glfwRevision);

    const std::string report =
        fmt::format("ARKRenderer dependencies OK | Vulkan header {} | GLFW {}.{}.{} | ImGui {} | STB {} | glm.z {}",
                    VK_HEADER_VERSION, glfwMajor, glfwMinor, glfwRevision, IMGUI_VERSION, STBI_VERSION, sampleVector.z);

    spdlog::info("{}", report);
    std::cout << report << '\n';

    (void)allocatorInfo;
    (void)instanceBuilder;
    (void)shaderModule;
    (void)model;

    return EXIT_SUCCESS;
}
