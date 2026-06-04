cmake_minimum_required(VERSION 3.25)

# 在 project() 之前设置 vcpkg toolchain，确保 manifest mode 能接管依赖解析。
# 如果使用 CMakePresets.json，通常会由 preset 显式传入 CMAKE_TOOLCHAIN_FILE；
# 这里的环境变量 fallback 方便直接运行 `cmake -S . -B build` 的场景。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
endif()

project(ARKRenderer
    VERSION 0.1.0
    DESCRIPTION "A Vulkan renderer playground."
    LANGUAGES CXX)

include(CTest)

# 项目选项。BUILD_TESTING 由 CTest 提供，ARK_BUILD_TESTS 用于项目自身控制。
option(ARK_BUILD_TESTS "Build ARKRenderer test executables." ON)

# ------------------------------------------------------------------------------
# 第三方依赖
# ------------------------------------------------------------------------------

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

# tinygltf 在 vcpkg 中是 header-only 包，不提供 tinygltfConfig.cmake。
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)

set(ARK_DXC_SUPPORTED OFF)
if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64"))
    set(ARK_DXC_SUPPORTED ON)
endif()

if(ARK_DXC_SUPPORTED)
    find_package(directx-dxc CONFIG REQUIRED)
endif()

set(ARK_RENDERER_THIRD_PARTY_LIBS
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
    imgui::imgui)

if(ARK_DXC_SUPPORTED)
    list(APPEND ARK_RENDERER_THIRD_PARTY_LIBS Microsoft::DirectXShaderCompiler)
endif()

# ------------------------------------------------------------------------------
# ARKRenderer 主目标
# ------------------------------------------------------------------------------

# 目前 src/ 里主要是框架头文件骨架。使用 CONFIGURE_DEPENDS 可以在新增头文件后
# 自动触发 CMake 重新配置，方便早期快速搭建目录结构。
file(GLOB_RECURSE ARK_RENDERER_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h")

add_library(ark_renderer INTERFACE)
add_library(ARKRenderer::ark_renderer ALIAS ark_renderer)

target_sources(ark_renderer INTERFACE ${ARK_RENDERER_HEADERS})

source_group(TREE "${PROJECT_SOURCE_DIR}/src"
    PREFIX "src"
    FILES ${ARK_RENDERER_HEADERS})

target_compile_features(ark_renderer INTERFACE cxx_std_20)

target_include_directories(ark_renderer
    INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
        ${Stb_INCLUDE_DIR}
        ${TINYGLTF_INCLUDE_DIRS})

# Vulkan 头文件包含顺序和函数加载策略集中在这里约束，避免散落到各个模块。
target_compile_definitions(ark_renderer
    INTERFACE
        GLFW_INCLUDE_NONE
        VK_NO_PROTOTYPES
        SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

if(ARK_DXC_SUPPORTED)
    target_compile_definitions(ark_renderer INTERFACE ARK_HAS_DXC=1)
else()
    target_compile_definitions(ark_renderer INTERFACE ARK_HAS_DXC=0)
endif()

target_link_libraries(ark_renderer INTERFACE ${ARK_RENDERER_THIRD_PARTY_LIBS})

if(MSVC)
    target_compile_options(ark_renderer INTERFACE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(ark_renderer INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------------------
# 测试
# ------------------------------------------------------------------------------

function(ark_add_smoke_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ARKRenderer::ark_renderer)
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

if(ARK_BUILD_TESTS AND BUILD_TESTING)
    ark_add_smoke_test(ark_dependency_smoke tests/dependency_smoke.cpp)
    ark_add_smoke_test(ark_framework_headers_smoke tests/framework_headers_smoke.cpp)
endif()
