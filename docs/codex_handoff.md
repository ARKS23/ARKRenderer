# Codex Handoff Summary

更新时间：2026-06-08

## 1. 当前状态

ARKRenderer 当前默认渲染路径已经完成 Phase 0.6：

```text
Vulkan Dynamic Rendering
    -> FrameRenderer
        -> prepare() upload stage
        -> beginRendering(color + depth)
        -> CubePass
            -> uniform buffer
            -> descriptor set
            -> sampled image + sampler
            -> indexed textured cube draw
        -> endRendering()
        -> Present
```

已提交推送的最近阶段：

```text
2bf1468 实现 Phase06 纹理采样闭环
```

Phase 0.7 已开始推进，当前完成到：

```text
0.7.1 TextureLoader 与 ImageData
```

Phase 0.7 主线仍是资源上传与生命周期收口，不是 glTF / PBR / RenderGraph。

## 2. 当前工作区

最近一次状态：

```text
## main...origin/main
 M .gitignore
 M CMakeLists.txt
 M docs/phase/phase07.md
 M src/asset/TextureLoader.h
 M tests/framework_headers_smoke.cpp
?? docs/codex_handoff.md
?? docs/phase/phase07.md
?? src/asset/TextureLoader.cpp
?? tests/texture_loader_smoke.cpp
```

说明：

- `.gitignore` 新增 `.idea/`，用于忽略 JetBrains IDE 生成文件。
- `docs/phase/phase07.md` 是新 Phase 0.7 文档，并已同步 0.7.1 完成状态。
- `docs/codex_handoff.md` 是当前交接文档，仍未跟踪。
- `src/asset/TextureLoader.cpp` 是新实现文件，仍未跟踪。
- `tests/texture_loader_smoke.cpp` 是新 smoke test，仍未跟踪。

最近 5 个提交：

```text
2bf1468 实现 Phase06 纹理采样闭环
2df8f7a 删除 Codex 交接总结
18dd048 添加 Codex 交接总结
1a790c9 完成 Phase05 深度渲染收尾
6b8b16c 实现 CubePass 与 SwapChain 深度资源
```

## 3. Phase 0.6 已完成内容

- RHI:
  - `SamplerDesc`
  - sampled image descriptor
  - sampler descriptor
  - `TextureUploadDesc`
  - `DeviceContext::uploadTextureData()`

- Vulkan:
  - `VulkanSampler` RAII。
  - descriptor layout / pool / write 支持 sampled image 与 sampler。
  - `uploadTextureData()` 使用 staging buffer、image barrier 和 `vkCmdCopyBufferToImage`。
  - texture upload 走 `Undefined -> CopyDst -> ShaderResource`。

- Renderer:
  - `CubePass` 已从 vertex color cube 演进为 textured cube。
  - 使用 24 顶点支持每个面独立 UV。
  - 程序生成 checkerboard texture。
  - `prepare()` 在 `beginRendering()` 前执行 upload，避免 copy 命令进入 dynamic rendering scope。

- Shader:
  - `shaders/textured_cube.vert.hlsl`
  - `shaders/textured_cube.frag.hlsl`
  - set 0 binding 0: camera uniform
  - set 0 binding 1: sampled image
  - set 0 binding 2: sampler

Phase 0.6 提交前已通过 build、CTest 和 hidden-window sandbox smoke。

## 4. Phase 0.7.1 已完成内容

实现文件：

```text
src/asset/TextureLoader.h
src/asset/TextureLoader.cpp
```

新增能力：

- `ark::asset::ImageFormat`
- `ark::asset::ImageData`
- `ark::asset::TextureLoader::loadRgba8()`
- `ark::asset::loadImageRgba8()`

实现策略：

- `TextureLoader` 只输出 CPU 侧数据，不依赖 renderer / RHI / Vulkan。
- 使用 `core::readBinaryFile()` 读取文件 bytes。
- `loadImageRgba8()` 只支持 LDR 8-bit 图片。
- 使用 `stbi_is_hdr_from_memory()` 检测 HDR。
- HDR 输入会显式失败，不会静默量化到 RGBA8。
- LDR 图片通过 `stbi_load_from_memory(..., 4)` 强制输出 RGBA8。
- `ImageData` 当前保存 `width`、`height`、`format`、`bytesPerPixel`、`pixels` 和 `debugName`。

测试：

```text
tests/texture_loader_smoke.cpp
```

覆盖内容：

- 临时生成 2x2 PPM，验证 LDR RGBA8 成功路径。
- 临时生成 HDR header，验证 HDR 被拒绝。
- 验证缺失文件失败路径。
- 验证 `TextureLoader::loadRgba8()` class wrapper。
- `framework_headers_smoke` 已触碰 `ImageData` 类型。

已接入：

```text
CMakeLists.txt
ark_texture_loader_smoke
```

验证结果：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

CTest 当前 4/4 通过：

```text
ark_dependency_smoke
ark_framework_headers_smoke
ark_texture_loader_smoke
ark_shader_assets_smoke
```

本次没有运行 sandbox smoke，因为 0.7.1 只新增 asset CPU loader，没有改变默认渲染路径。

## 5. 必须继续遵守的边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase06.md
docs/phase/phase07.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `renderer/` 和 `app/` 不允许直接接触 Vulkan 类型。
- RHI 公共层不暴露 Vulkan layout、access mask、pipeline stage、descriptor/image handle。
- `RenderDevice` 负责资源创建，不负责每帧命令录制。
- `DeviceContext` 负责 command recording、barrier、upload、draw、submit。
- `SwapChain` 负责 window backbuffer、default depth、acquire/present/resize。
- `asset/` 只解析文件并输出 CPU 侧数据，不创建 GPU/RHI 资源。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。

## 6. 当前关键入口

建议按顺序阅读：

1. `src/renderer/FrameRenderer.cpp`
   - 看 `prepare()` 与 `beginRendering()` 的边界。

2. `src/renderer/passes/CubePass.cpp`
   - 看 Phase 0.6 texture upload、staging buffer、descriptor 和 draw。

3. `src/asset/TextureLoader.h/.cpp`
   - 看 0.7.1 CPU image loader、HDR 拒绝和 `ImageData` 数据结构。

4. `src/rhi/DeviceContext.h`
   - 看 `TextureUploadDesc` 当前仍偏最小实现，后续要补 row pitch / bytesPerPixel。

5. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 看 texture upload 和 barrier 的 Vulkan 落地。

6. `src/rhi/vulkan/VulkanBuffer.cpp`
   - `GpuOnly initialData` 仍未实现，后续应通过 buffer upload path 解决。

7. `src/rhi/vulkan/VulkanDescriptorManager.cpp`
   - 当前仍是固定容量 descriptor pool，后续应升级为 growable pool。

## 7. 剩余风险和 TODO

Phase 0.7 后续 P0：

- `TextureUploadDesc` 缺少 row pitch / bytesPerPixel 等显式约束。
- `DeviceContext` 还没有 GPU-only buffer upload API。
- `VulkanBuffer` 对 `GpuOnly initialData` 仍报 `GpuOnly initialData upload is not implemented yet`。
- staging buffer 当前仍由 `CubePass` 保活，未迁移到 upload allocator / deferred deletion。
- `VulkanDeletionQueue` 仍是空壳。
- `VulkanDescriptorManager` 仍是固定容量 pool。

P1：

- HDR texture loader 尚未实现；当前 `loadImageRgba8()` 会显式拒绝 HDR。
- `CubePass::FramesInFlight = 2` 仍与底层 frame slot 数量分离。
- `VulkanCommandContext::pipelineBarrier()` 仍需补强 buffer barrier。
- 仍未用 RenderDoc 系统检查 textured cube descriptor、layout 和 fragment sampling。

P2：

- Vulkan debug object names。
- shader reflection 校验 descriptor layout 与 HLSL binding。
- screenshot / pixel smoke 或 RenderDoc capture 脚本。

## 8. 推荐下一步

1. 提交当前 0.7.1 相关变更：

```text
.gitignore
CMakeLists.txt
docs/codex_handoff.md
docs/phase/phase07.md
src/asset/TextureLoader.h
src/asset/TextureLoader.cpp
tests/framework_headers_smoke.cpp
tests/texture_loader_smoke.cpp
```

2. 继续 Phase 0.7.2 / 0.7.3：

```text
TextureUploadDesc rowPitch / bytesPerPixel
DeviceContext::uploadBufferData()
Vulkan vkCmdCopyBuffer
GPU-only buffer upload smoke path
```

3. 再做 Phase 0.7.4：

```text
VulkanDeletionQueue
frame-local deferred deletion
staging resource safe release
```

4. 最后迁移 `CubePass` 并处理 descriptor pool：

```text
CubePass uses reusable upload path
VulkanDescriptorManager growable pools
```

每个实现小阶段后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径变化后再运行：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

## 9. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.6 完成状态、Phase 0.7.1 已完成内容、剩余风险和当前工作区状态。

然后阅读：

docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase06.md
docs/phase/phase07.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + CubePass + uniform buffer + descriptor set + sampled image + sampler + indexed textured cube + depth attachment。

不要重复 Phase 0.5 / Phase 0.6 / Phase 0.7.1 已完成工作。

接下来从 Phase 0.7.2 / 0.7.3 开始，优先做通用 upload 描述、GPU-only buffer upload、staging 生命周期和 descriptor pool 可维护性。

不要提前引入 ResourceManager / RenderGraph / bindless / glTF / PBR。如果实现方向与既有设计文档冲突，先更新设计文档，再改代码。

新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档。

先执行并查看：

git status -sb
git diff --stat
git log --oneline -n 5
```
