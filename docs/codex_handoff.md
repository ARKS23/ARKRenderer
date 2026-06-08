# Codex Handoff Summary

更新时间：2026-06-08

## 1. 当前状态

ARKRenderer 当前已经推进到 Phase 0.8.6。默认 sandbox 渲染路径已经从阶段性的 `CubePass` 迁移到 `ForwardPass`，并优先加载 glTF 2.0 fixture：

```text
Vulkan Dynamic Rendering
    -> FrameRenderer
        -> prepare() upload stage
            -> MeshResource GPU-only vertex/index upload
            -> MaterialResource RGBA8 texture upload
        -> beginRendering(color + depth)
        -> ClearPass
        -> ForwardPass
            -> camera uniform buffer
            -> sampled image + sampler descriptor
            -> mesh pipeline
            -> indexed textured mesh draw
        -> endRendering()
        -> Present
```

当前默认资源路径：

```text
assets/models/forward_fixture.gltf
assets/textures/xiaowei.png
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

`CubePass` 仍保留为阶段回归/对照代码，但不再作为默认主线扩展点。

## 2. 最近阶段进展

最近已推送基线：

```text
1dae07f 启动 Phase08 mesh 资源闭环
d9b80af 实现 DescriptorManager 可增长 pool
5d7cea8 接入 Phase07 真实纹理资源
08a13e4 实现 Phase07 GPU buffer upload 与 deferred deletion
591f6e3 补齐 Phase07 纹理上传描述
```

本交接点已完成并准备提交 Phase 0.8.3 到 0.8.6：

```text
0.8.3 最小 textured material resource
0.8.4 ForwardPass 最小落地
0.8.5 glTF 2.0 单 mesh + 单材质加载
0.8.6 默认 sandbox 迁移
```

## 3. Phase 0.7 已完成能力

Phase 0.7 已完成真实纹理与上传生命周期的阶段性收口：

- `asset::ImageData` 与 `TextureLoader`，使用 `stb_image` 加载 LDR RGBA8。
- HDR 输入会显式失败，不会静默量化到 RGBA8。
- `TextureUploadDesc` 已补齐 row pitch、slice pitch、offset、mip、array layer 等最小通用字段。
- `BufferUploadDesc` 与 `DeviceContext::uploadBufferData()` 已落地。
- GPU-only vertex/index buffer 可以通过 staging buffer 上传。
- texture upload 使用 `Undefined -> CopyDst -> ShaderResource`。
- staging buffer 通过 frame-local deferred deletion 延迟释放。
- `VulkanDescriptorManager` 已支持可增长 descriptor pool。
- CMake 已复制 `assets/` 到 sandbox 输出目录，支持从仓库根目录和 build 输出目录运行。

## 4. Phase 0.8 已完成能力

### 0.8.1 MeshData / ModelData

实现文件：

```text
src/asset/MeshData.h
tests/mesh_data_smoke.cpp
```

当前 CPU asset 数据结构：

- `asset::MeshVertex`：固定 position / normal / uv0。
- `asset::MeshPrimitiveData`：vertices、`u32` indices、material index、debug name。
- `asset::MaterialData`：当前只表达 base color texture path。
- `asset::ModelData`：聚合 meshes / materials，不表达完整 glTF scene graph。

### 0.8.2 MeshResource

实现文件：

```text
src/renderer/MeshResource.h
src/renderer/MeshResource.cpp
```

当前职责：

- 从 `asset::MeshPrimitiveData` 创建 GPU-only vertex/index buffer。
- 创建 CpuToGpu staging buffer。
- 在 `prepare()` 阶段通过 `DeviceContext::uploadBufferData()` 记录 copy。
- 上传命令记录后通过 `deferReleaseBuffer()` 交出 staging 生命周期。
- `bind()` 统一绑定 vertex buffer 和 `UInt32` index buffer。

### 0.8.3 MaterialResource

实现文件：

```text
src/renderer/material/MaterialResource.h
src/renderer/material/MaterialResource.cpp
```

当前职责：

- 消费 `asset::MaterialData`，不解析 glTF。
- 使用 `asset::loadImageRgba8()` 加载 base color texture。
- 创建 GPU texture、texture view、sampler 和 texture staging buffer。
- 在 render scope 外通过 `DeviceContext::uploadTextureData()` 上传。
- 上传命令记录后通过 `deferReleaseBuffer()` 交出 texture staging 生命周期。
- 通过 `updateDescriptorSet()` 写入 separate sampled image / sampler descriptor。

当前限制：

- 只支持 LDR RGBA8 base color texture。
- 不支持 HDR、sRGB、mipmap、压缩纹理、texture array 或 cubemap。
- 不实现 PBR material 参数。

### 0.8.4 ForwardPass

实现文件：

```text
src/renderer/passes/ForwardPass.h
src/renderer/passes/ForwardPass.cpp
src/renderer/FrameRenderer.cpp
shaders/mesh.vert.hlsl
shaders/mesh.frag.hlsl
```

当前职责：

- 默认消费一个 `MeshResource` 和一个 `MaterialResource`。
- `prepare()` 负责 mesh/material upload。
- `execute()` 更新 camera uniform、绑定 pipeline/descriptor/mesh 并执行 `drawIndexed()`。
- descriptor layout 继续使用：

```text
set 0 binding 0: UniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
```

当前默认渲染路径已经在 `FrameRenderer` 中切换为：

```text
ClearPass + ForwardPass
```

### 0.8.5 glTF 2.0 Loader

实现文件：

```text
src/asset/GltfLoader.h
src/asset/GltfLoader.cpp
assets/models/forward_fixture.gltf
tests/gltf_loader_smoke.cpp
```

当前支持范围：

- 仅支持 glTF 2.0 core profile 的最小子集。
- 支持 `.gltf` 和 `.glb` 入口。
- 只加载第一个 mesh 的第一个 primitive。
- primitive mode 必须为 `TRIANGLES`。
- 必须包含 POSITION、NORMAL、TEXCOORD_0 和 indices。
- POSITION / NORMAL 当前要求 float vec3。
- TEXCOORD_0 当前要求 float vec2。
- indices 支持 unsigned byte / unsigned short / unsigned int，并统一转换为 `u32`。
- baseColorTexture 当前要求外部 image URI，路径基于 glTF 文件所在目录解析。
- `tinygltf` 图片解码被禁用，图片像素仍统一交给 `TextureLoader`。

当前不支持：

- glTF 1.0。
- 多 mesh、多 primitive、多材质场景调度。
- node transform、scene graph、animation、skin、morph target。
- KHR / EXT 扩展、Draco、embedded image/data URI image。

### 0.8.6 sandbox 迁移

当前行为：

- `ForwardPass` 优先查找 `assets/models/forward_fixture.gltf`。
- fixture 引用 `assets/textures/xiaowei.png`。
- 如果 glTF fixture 加载失败，fallback 到 generated mesh + `xiaowei.png`，保证 sandbox 可运行。
- CMake post-build assets copy 会把 `assets/models/forward_fixture.gltf` 和 texture 一起复制到 sandbox 输出目录。

## 5. 当前关键代码阅读顺序

建议下一位接手者按下面顺序阅读：

1. `docs/phase/phase08.md`
   - 先确认 Phase 0.8 的目标、非目标和架构边界。

2. `src/asset/MeshData.h`
   - 看 CPU mesh/material/model 数据形态。

3. `src/asset/GltfLoader.h/.cpp`
   - 看 glTF 2.0 最小子集如何转换为 `ModelData`。

4. `src/renderer/MeshResource.h/.cpp`
   - 看 CPU mesh 到 GPU-only vertex/index buffer 的上传闭环。

5. `src/renderer/material/MaterialResource.h/.cpp`
   - 看 CPU material 到 texture/view/sampler/descriptor 的资源闭环。

6. `src/renderer/passes/ForwardPass.h/.cpp`
   - 看默认 pass 如何装配 model、mesh、material、descriptor、pipeline 和 draw。

7. `src/renderer/FrameRenderer.cpp`
   - 确认 `prepare()` 仍在 `beginRendering()` 前，upload 不进入 dynamic rendering scope。

8. `src/rhi/vulkan/VulkanCommandContext.cpp`
   - 复查 buffer / texture upload barrier 与 copy 命令。

9. `src/rhi/vulkan/VulkanDescriptorManager.cpp`
   - 复查可增长 descriptor pool 的分配路径。

## 6. 必须继续遵守的边界

后续开发前继续阅读：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md
```

硬性边界：

- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。
- `asset/` 只解析外部文件并输出 CPU 数据，不创建 RHI/GPU 资源。
- `renderer/` 可以创建和持有 RHI 资源，但不能接触 Vulkan 类型。
- RHI 公共层不暴露 Vulkan layout、access mask、pipeline stage、descriptor/image handle。
- `RenderDevice` 负责资源创建。
- `DeviceContext` 负责 command recording、barrier、upload、draw、submit。
- upload 命令必须记录在 dynamic rendering scope 外。
- 当前继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
- 日志输出使用英文。
- 必要注释使用简洁中文。

## 7. 已知风险和 TODO

P0 / 下一阶段优先处理：

- `ForwardPass` 当前仍只支持一个 mesh primitive 和一个 material。
- glTF loader 暂不处理 node transform 和 scene graph，真实模型接入前需要扩展。
- material system 仍是最小 resource 封装，没有资源缓存、共享 texture 或 material 参数。
- texture path 当前要求外部 image URI；embedded image/data URI image 暂不支持。
- texture upload 仍只覆盖 RGBA8、mip0、array layer 0、tightly packed 这类路径。
- 默认 pass 中 `FramesInFlight = 2` 仍是局部常量，后续应统一到 frame resource 配置。

P1：

- `CubePass` 与 `ForwardPass` 仍存在部分 descriptor/pipeline/texture 逻辑重复，后续应决定保留为 debug pass 还是收敛。
- deferred deletion 当前主要覆盖 upload staging buffer，完整 GPU object 延迟销毁仍未系统化。
- `VulkanDescriptorManager` 已支持 growable pools，但还没有 free/reset 策略和容量统计。
- shader binding 与 descriptor layout 仍靠人工一致，后续可以考虑最小 reflection 或测试校验。
- `TextureLoader` 当前只做 LDR RGBA8，后续需要 sRGB/HDR/mipmap 设计。

P2：

- Vulkan debug object names。
- RenderDoc capture / screenshot / pixel smoke。
- ResourceManager handle 系统。
- RenderScene / RenderQueue。
- PBR、IBL、tone mapping。

## 8. 最近验证记录

本交接点在同步文档前已完成：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：

```text
build passed
CTest: 6/6 passed
```

同时完成 sandbox smoke：

```text
从仓库根目录运行 ark_sandbox：通过
从 build 输出目录运行 ark_sandbox：通过
```

日志确认 `assets/models/forward_fixture.gltf` 被加载，未观察到启动期资源路径错误。

## 9. 推荐下一步

建议下一阶段从 Phase 0.8 收尾或 Phase 0.9 开始：

1. 将 `ForwardPass` 从单 mesh / 单 material 扩展到最小 `RenderScene` / `RenderQueue`。
2. 扩展 glTF 2.0 loader 支持多 primitive、node transform 和多材质。
3. 建立最小 material/resource cache，避免重复加载同一 texture。
4. 明确 texture color space：RGBA8 Unorm 与 sRGB 的边界。
5. 扩展 texture upload 到 mipmap 或至少记录 mipmap 设计。
6. 系统化 GPU object deferred destruction。

在完成这些之前，不建议直接进入完整 PBR、RenderGraph、bindless 或复杂 glTF 扩展。

## 10. 下一次 Codex 启动提示

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.8.6 完成状态、默认 ForwardPass 渲染路径、glTF 2.0 最小加载范围、已知风险和后续建议。

然后阅读：

docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
docs/phase/phase08.md

当前默认渲染路径已经是 Vulkan Dynamic Rendering + ClearPass + ForwardPass + MeshResource + MaterialResource + uniform buffer + sampled image + sampler + indexed textured mesh + depth attachment。

不要重复 Phase 0.5 / 0.6 / 0.7 / 0.8.1-0.8.6 已完成工作。

下一步优先考虑 Phase 0.8 收尾或 Phase 0.9：RenderScene / RenderQueue、glTF 多 primitive/node transform、多材质、resource cache、texture color space 与更完整 deferred destruction。

不要提前引入完整 RenderGraph、bindless、复杂 PBR 或 glTF 扩展。如果实现方向与既有设计文档冲突，先更新设计文档，再改代码。

新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档。

先执行并查看：

git status -sb
git diff --stat
git log --oneline -n 5
```
