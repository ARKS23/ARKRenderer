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

set(ARK_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(TO_CMAKE_PATH "${ARK_SHADER_OUTPUT_DIR}" ARK_SHADER_OUTPUT_DIR_NORMALIZED)

if(ARK_DXC_SUPPORTED)
    set(ARK_COMPILED_SHADERS)

    # Phase 0.4 使用 DXC 将 HLSL 编译为 SPIR-V，renderer 运行时只读取编译后的二进制。
    function(ark_compile_hlsl_shader source_file shader_profile output_name)
        set(output_file "${ARK_SHADER_OUTPUT_DIR}/${output_name}.spv")

        add_custom_command(
            OUTPUT "${output_file}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARK_SHADER_OUTPUT_DIR}"
            COMMAND "${DIRECTX_DXC_TOOL}"
                -spirv
                -fspv-target-env=vulkan1.3
                -E main
                -T ${shader_profile}
                "$<SHELL_PATH:${source_file}>"
                -Fo "$<SHELL_PATH:${output_file}>"
            DEPENDS "${source_file}"
            COMMENT "Compiling shader ${output_name}"
            VERBATIM)

        list(APPEND ARK_COMPILED_SHADERS "${output_file}")
        set(ARK_COMPILED_SHADERS "${ARK_COMPILED_SHADERS}" PARENT_SCOPE)
    endfunction()

    ark_compile_hlsl_shader("${PROJECT_SOURCE_DIR}/shaders/triangle.vert.hlsl" vs_6_0 triangle.vert)
    ark_compile_hlsl_shader("${PROJECT_SOURCE_DIR}/shaders/triangle.frag.hlsl" ps_6_0 triangle.frag)

    add_custom_target(ark_shaders ALL DEPENDS ${ARK_COMPILED_SHADERS})
else()
    add_custom_target(ark_shaders)
    message(WARNING "DXC is not available on this platform; Phase 0.4 triangle shaders will not be compiled.")
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

# 早期阶段使用 GLOB_RECURSE 方便快速增加骨架文件；CONFIGURE_DEPENDS
# 会在新增源码/头文件后触发 CMake 重新配置。
file(GLOB_RECURSE ARK_RENDERER_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h")
file(GLOB_RECURSE ARK_RENDERER_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp")

add_library(ark_renderer STATIC
    ${ARK_RENDERER_HEADERS}
    ${ARK_RENDERER_SOURCES})
add_library(ARKRenderer::ark_renderer ALIAS ark_renderer)

source_group(TREE "${PROJECT_SOURCE_DIR}/src"
    PREFIX "src"
    FILES
        ${ARK_RENDERER_HEADERS}
        ${ARK_RENDERER_SOURCES})

target_compile_features(ark_renderer PUBLIC cxx_std_20)

target_include_directories(ark_renderer
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
        ${Stb_INCLUDE_DIR}
        ${TINYGLTF_INCLUDE_DIRS})

# Vulkan 头文件包含顺序和函数加载策略集中在这里约束，避免散落到各个模块。
target_compile_definitions(ark_renderer
    PUBLIC
        GLFW_INCLUDE_NONE
        VK_NO_PROTOTYPES
        SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

target_compile_definitions(ark_renderer
    PRIVATE
        "ARK_SHADER_OUTPUT_DIR=\"${ARK_SHADER_OUTPUT_DIR_NORMALIZED}\"")

# shader 编译必须早于 renderer 构建完成，确保 TrianglePass 启动时能找到 SPIR-V。
add_dependencies(ark_renderer ark_shaders)

if(ARK_DXC_SUPPORTED)
    target_compile_definitions(ark_renderer PUBLIC ARK_HAS_DXC=1)
else()
    target_compile_definitions(ark_renderer PUBLIC ARK_HAS_DXC=0)
endif()

target_link_libraries(ark_renderer PUBLIC ${ARK_RENDERER_THIRD_PARTY_LIBS})

if(MSVC)
    target_compile_options(ark_renderer PUBLIC /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(ark_renderer PUBLIC -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------------------
# 示例程序
# ------------------------------------------------------------------------------

add_executable(ark_sandbox apps/sandbox/main.cpp)
target_link_libraries(ark_sandbox PRIVATE ARKRenderer::ark_renderer)
set_target_properties(ark_sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

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

    if(ARK_DXC_SUPPORTED)
        ark_add_smoke_test(ark_shader_assets_smoke tests/shader_assets_smoke.cpp)
        target_compile_definitions(ark_shader_assets_smoke
            PRIVATE
                "ARK_SHADER_OUTPUT_DIR=\"${ARK_SHADER_OUTPUT_DIR_NORMALIZED}\"")
        add_dependencies(ark_shader_assets_smoke ark_shaders)
    endif()
endif()
