# 第三方依赖

ARKRenderer 使用 **CMake + vcpkg manifest mode** 管理第三方依赖。依赖清单统一维护在项目根目录的 `vcpkg.json`，CMake 入口为 `CMakeLists.txt`。

## 环境
- C++ 标准：C++20
- 构建系统：CMake 3.25 或更高版本
- 包管理器：vcpkg

## 构建与测试

项目提供了 CMake Preset，Windows + MSVC 下可以直接使用：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```

`cmake --preset msvc-vcpkg` 会触发 vcpkg manifest mode，根据 `vcpkg.json` 自动安装缺失依赖。

当前依赖 smoke test 为：

```text
tests/dependency_smoke.cpp
```

该测试用于验证核心第三方库可以被正确包含、链接并运行。

## 依赖清单

| 依赖 | vcpkg 包名 | CMake 使用方式 | 用途 |
| --- | --- | --- | --- |
| Vulkan loader / headers | `vulkan` | `find_package(Vulkan REQUIRED)` / `Vulkan::Vulkan` | Vulkan API 入口、头文件和 loader 链接 |
| Vulkan Memory Allocator | `vulkan-memory-allocator` | `GPUOpen::VulkanMemoryAllocator` | Vulkan 显存分配管理 |
| volk | `volk` | `volk::volk`, `volk::volk_headers` | Vulkan 函数指针加载 |
| vk-bootstrap | `vk-bootstrap` | `vk-bootstrap::vk-bootstrap` | 简化 Vulkan instance、device、swapchain 初始化 |
| SPIR-V Headers | `spirv-headers` | `SPIRV-Headers::SPIRV-Headers` | SPIR-V 公共头文件 |
| SPIRV-Reflect | `spirv-reflect` | `unofficial::spirv-reflect` | 反射 SPIR-V shader 资源绑定信息 |
| GLFW | `glfw3` | `glfw` | 窗口、输入和 Vulkan surface 创建 |
| glm | `glm` | `glm::glm` | 数学库，向量、矩阵和变换 |
| spdlog | `spdlog` | `spdlog::spdlog` | 日志输出 |
| fmt | `fmt` | `fmt::fmt` | 字符串格式化 |
| Dear ImGui docking | `imgui[docking-experimental,glfw-binding,vulkan-binding]` | `imgui::imgui` | 调试 UI，启用 docking、GLFW 和 Vulkan backend |
| stb | `stb` | `${Stb_INCLUDE_DIR}` | 图片加载等单头文件工具 |
| DirectX Shader Compiler | `directx-dxc` | `Microsoft::DirectXShaderCompiler` | HLSL / shader 编译工具链 |
| tinygltf | `tinygltf` | `find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")` | glTF 模型加载 |

## 重要宏定义

`CMakeLists.txt` 中为 `ARKRenderer::ark_renderer` 配置了以下公共宏：

| 宏 | 作用 |
| --- | --- |
| `GLFW_INCLUDE_NONE` | 禁止 GLFW 自动包含 OpenGL 头文件，避免和 Vulkan/volk 头文件顺序冲突 |
| `VK_NO_PROTOTYPES` | 禁用 Vulkan 静态函数原型，配合 volk 动态加载 Vulkan 函数 |
| `SPIRV_REFLECT_USE_SYSTEM_SPIRV_H` | 让 SPIRV-Reflect 使用 vcpkg 提供的系统 SPIR-V headers |
| `VMA_STATIC_VULKAN_FUNCTIONS=0` | 禁用 VMA 静态 Vulkan 函数绑定 |
| `VMA_DYNAMIC_VULKAN_FUNCTIONS=0` | 禁用 VMA 内部动态 Vulkan 函数绑定，改由 `vmaImportVulkanFunctionsFromVolk()` 从 volk 导入函数表 |
| `ARK_HAS_DXC` | 当前平台支持 DXC 时为 `1`，否则为 `0` |

## KTX / KTX2

Phase 0.60 起新增 KTX-Software / libktx 依赖：

```text
vcpkg package: ktx
CMake package: find_package(Ktx CONFIG REQUIRED)
Expected target: KTX::ktx
```

该依赖用于读取 KTX/KTX2 纹理容器，优先服务于 `assets/models/sponza/` 中的 `.ktx` 贴图恢复。项目仍通过自身 RHI / `TextureResource` 上传纹理，不直接使用 libktx 的 Vulkan upload helper 创建后端资源。

当前第一版只把非压缩 RGBA8 / SRGB8_ALPHA8 类 KTX 纹理解码为 CPU-side RGBA8 base mip，后续 mip 仍由 GPU 生成；BasisU、压缩格式、supercompression 转码和原始 mip chain 上传留到后续阶段。
