cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
endif()

project(ARKRenderer
    VERSION 0.1.0
    DESCRIPTION "A Vulkan renderer playground."
    LANGUAGES CXX)

include(CTest)

option(ARK_BUILD_TESTS "Build ARKRenderer test executables." ON)

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(SPIRV-Headers CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)

set(ARK_DXC_SUPPORTED OFF)
if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64"))
    set(ARK_DXC_SUPPORTED ON)
endif()

if(ARK_DXC_SUPPORTED)
    find_package(directx-dxc CONFIG REQUIRED)
endif()

add_library(ark_renderer INTERFACE)
add_library(ARKRenderer::ark_renderer ALIAS ark_renderer)

target_compile_features(ark_renderer INTERFACE cxx_std_20)

target_include_directories(ark_renderer
    INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        ${Stb_INCLUDE_DIR}
        ${TINYGLTF_INCLUDE_DIRS})

target_compile_definitions(ark_renderer
    INTERFACE
        GLFW_INCLUDE_NONE
        VK_NO_PROTOTYPES
        SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

target_link_libraries(ark_renderer
    INTERFACE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        volk::volk
        volk::volk_headers
        vk-bootstrap::vk-bootstrap
        SPIRV-Headers::SPIRV-Headers
        unofficial::spirv-reflect
        glfw
        glm::glm
        spdlog::spdlog
        fmt::fmt
        imgui::imgui
)

if(ARK_DXC_SUPPORTED)
    target_compile_definitions(ark_renderer INTERFACE ARK_HAS_DXC=1)
    target_link_libraries(ark_renderer INTERFACE Microsoft::DirectXShaderCompiler)
else()
    target_compile_definitions(ark_renderer INTERFACE ARK_HAS_DXC=0)
endif()

if(MSVC)
    target_compile_options(ark_renderer INTERFACE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(ark_renderer INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(ARK_BUILD_TESTS AND BUILD_TESTING)
    add_executable(ark_dependency_smoke tests/dependency_smoke.cpp)
    target_link_libraries(ark_dependency_smoke PRIVATE ARKRenderer::ark_renderer)

    add_test(NAME ark_dependency_smoke COMMAND ark_dependency_smoke)
endif()
