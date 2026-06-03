# ARKRenderer Dependencies

This project uses CMake with vcpkg manifest mode. The dependency manifest lives in `vcpkg.json`.

## Libraries

1. Vulkan SDK / loader: `vulkan`
2. Vulkan Memory Allocator: `vulkan-memory-allocator`
3. volk: `volk`
4. vk-bootstrap: `vk-bootstrap`
5. SPIR-V headers: `spirv-headers`
6. SPIRV-Reflect: `spirv-reflect`
7. GLFW: `glfw3`
8. glm: `glm`
9. spdlog: `spdlog`
10. fmt: `fmt`
11. ImGui docking branch with GLFW and Vulkan bindings: `imgui[docking-experimental,glfw-binding,vulkan-binding]`
12. stb: `stb`
13. DirectX Shader Compiler: `directx-dxc`
14. tinygltf: `tinygltf`

## Build

Install vcpkg and set `VCPKG_ROOT` to the vcpkg checkout directory, then configure and build:

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
```
