# Phase 0.8 Mesh、Material 与 ForwardPass 最小资产闭环

## 阶段判断

Phase 0.7 已经完成真实纹理和上传生命周期的阶段性收口：

```text
asset::ImageData
    -> staging buffer
    -> GPU texture / texture view / sampler
    -> descriptor set
    -> textured cube draw
```

同时，GPU-only vertex/index buffer 上传、staging buffer deferred deletion、growable descriptor pool 都已经具备最小可用路径。也就是说，Phase 0.8 不应该继续在 `CubePass` 中堆更多硬编码资源，而应该把当前 textured cube 验证路径迁移成更通用的 mesh / material / forward pass 资产闭环。

当前主要缺口：

- `RenderScene`、`RenderQueue`、`MaterialSystem` 和 `GltfLoader` 仍是占位。
- `CubePass` 同时承担 CPU mesh 数据、texture 加载、GPU resource、descriptor、pipeline 和 draw，已经不适合继续扩展。
- 真实资产路径仍是阶段性硬编码，glTF 外部纹理需要基于 glTF 文件所在目录解析。
- deferred deletion 当前只覆盖 upload staging buffer，完整 GPU 对象延迟销毁仍未覆盖。
- texture upload 仍只支持 tightly packed RGBA8、mip0、array layer 0。

因此 Phase 0.8 的主线应是：先建立 CPU mesh / material 数据模型，再建立 renderer 层 GPU mesh / material resource，最后用最小 `ForwardPass` 消费这些资源，并以单 mesh + 单材质 glTF 或 fixture 验证真实资产路径。

## 目标

Phase 0.8 目标是在不引入完整 ResourceManager、RenderGraph 或 PBR 的前提下，完成以下能力：

- 新增纯 CPU 侧 `MeshData` / `MaterialData`，由 `asset/` 输出，不依赖 RHI。
- 建立 renderer 层最小 `GpuMesh` 上传封装，复用 0.7 的 `uploadBufferData()` 和 deferred deletion。
- 建立最小 textured material resource，复用 sampled image / sampler descriptor 路径。
- 新增或落地 `ForwardPass`，让它消费 mesh / material，而不是继续依赖硬编码 `CubePass`。
- 补齐 glTF 2.0 单 mesh、单材质加载的最小路径，作为真实资产验证入口。
- 默认 sandbox 渲染路径逐步从阶段性的 `CubePass` 迁移到 `ForwardPass`。

## 非目标

Phase 0.8 暂不做以下内容：

- 不做完整 glTF 2.0 scene graph、animation、skin、morph target。
- 不支持 glTF 1.0，也不实现 KHR / EXT 扩展特性。
- 不做多材质、多 mesh 场景调度和复杂 node transform 层级。
- 不做完整 PBR、IBL、HDR texture、tone mapping。
- 不做 bindless texture 或 global descriptor array。
- 不做完整 ResourceManager handle 系统。
- 不做完整 RenderGraph、自动 barrier、transient resource aliasing。
- 不做 async transfer queue、多 queue ownership transfer 或 timeline semaphore upload。
- 不做 mipmap 生成、block compressed texture、cubemap、texture array。
- 不做 shader hot reload 或虚拟文件系统。

这些内容需要等 mesh / material / forward pass 的最小闭环稳定后再进入后续阶段。

## 模块边界

Phase 0.8 必须继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase07.md
```

### asset/

`asset/` 只负责读取外部文件并输出 CPU 中间数据。

推荐新增：

```text
src/asset/MeshData.h
src/asset/GltfLoader.h/.cpp
```

建议数据形态：

```cpp
namespace ark::asset {
    struct MeshVertex {
        float position[3];
        float normal[3];
        float uv0[2];
    };

    struct MeshPrimitiveData {
        std::vector<MeshVertex> vertices;
        std::vector<u32> indices;
        u32 materialIndex = 0;
        std::string debugName;
    };

    struct MaterialData {
        Path baseColorTexturePath;
        std::string debugName;
    };

    struct ModelData {
        std::vector<MeshPrimitiveData> meshes;
        std::vector<MaterialData> materials;
        std::string debugName;
    };
}
```

约束：

- `asset/` 不创建 `rhi::Buffer`、`rhi::Texture`、descriptor 或 pipeline。
- Phase 0.8 明确面向 glTF 2.0 core profile 的最小子集；不兼容 glTF 1.0，也不要求实现扩展。
- glTF index component type 在 asset 层统一转换为 `u32`，避免 renderer 处理多种 glTF index 格式。
- glTF 外部 texture path 以 glTF 文件所在目录为 base 解析。
- 初期只支持 POSITION / NORMAL / TEXCOORD_0；缺失字段必须明确失败或文档记录 fallback。
- 日志输出使用英文。

### renderer/

`renderer/` 负责把 CPU asset 数据转换为 RHI 资源，并组织 draw。

推荐新增：

```text
src/renderer/MeshResource.h/.cpp
src/renderer/material/MaterialResource.h/.cpp
src/renderer/passes/ForwardPass.cpp
```

建议职责：

- `MeshResource`：持有 GPU-only vertex/index buffer 和 upload staging buffer。
- `MaterialResource`：持有 base color texture、texture view、sampler 和 descriptor 相关资源。
- `ForwardPass`：创建 pipeline，绑定 camera/object/material descriptor，执行 indexed draw。

`CubePass` 可以暂时保留为阶段回归 pass，但不再继续扩展成 asset / material 系统。

### rhi/

RHI 公共层当前已有 Phase 0.8 最小所需能力：

- `BufferUploadDesc`
- `TextureUploadDesc`
- vertex/index buffer binding
- `drawIndexed`
- descriptor set layout / descriptor set
- sampled image / sampler descriptor
- graphics pipeline desc

Phase 0.8 默认不新增 Vulkan 语义到公共 RHI。如果确实需要扩展，应先判断它是否是 API 无关语义，而不是 Vulkan 实现细节。

### rhi/vulkan/

Vulkan 后端继续负责：

- GPU-only buffer upload 的 `vkCmdCopyBuffer` 和 buffer barrier。
- texture upload 的 `vkCmdCopyBufferToImage` 和 image barrier。
- descriptor set allocation / write。
- pipeline 创建和 dynamic rendering 附件格式。
- staging buffer deferred deletion。

Phase 0.8 不引入 transfer queue 或 timeline semaphore。

## 实施顺序

### 0.8.0 文档与范围确认

工作内容：

- 新增 `docs/phase/phase08.md`。
- 明确 Phase 0.8 从真实资产最小闭环开始，而不是完整 glTF / PBR。
- 记录 Phase 0.7 已具备的 upload、texture、descriptor 和 lifetime 基础。

验收：

- 文档和设计边界一致。
- 没有把 Phase 0.8 扩大到 ResourceManager / RenderGraph / PBR。

当前实现状态：

- 已新增 `docs/phase/phase08.md`，明确 Phase 0.8 面向 mesh / material / ForwardPass 的最小资产闭环。
- 已明确 glTF 范围为 glTF 2.0 core profile 的最小子集，不支持 glTF 1.0 或 KHR / EXT 扩展特性。
- 当前阶段仍不进入完整 ResourceManager、RenderGraph、PBR、bindless 或完整 glTF scene graph。

### 0.8.1 MeshData 与 ModelData

工作内容：

- 新增 `asset::MeshVertex`、`MeshPrimitiveData`、`MaterialData`、`ModelData`。
- 先用固定 vertex layout：position、normal、uv0。
- index 统一为 `u32`。
- 增加轻量 smoke test，验证数据结构可用。

验收：

- `asset/` 不依赖 renderer / RHI。
- `framework_headers_smoke` 能触碰新增类型。
- 后续 `CubePass` 的硬编码 cube 数据可以迁移成 `MeshPrimitiveData`。

当前实现状态：

- 已新增 `src/asset/MeshData.h`，定义 `MeshVertex`、`MeshPrimitiveData`、`MaterialData` 和 `ModelData`。
- `MeshVertex` 当前固定为 position、normal、uv0，作为 Phase 0.8 最小 CPU 顶点格式。
- `MeshPrimitiveData` 使用 `std::vector<MeshVertex>` 和 `std::vector<u32>`，index 在 asset 层统一表达为 `u32`。
- `MaterialData` 当前只保存 base color texture path，不创建 GPU texture。
- `ModelData` 当前只聚合 meshes / materials，不表达完整 glTF scene graph。
- 已新增 `tests/mesh_data_smoke.cpp`，覆盖 primitive empty 判断、vertex/index byte size、material texture path 和 model 聚合。
- 已在 `CMakeLists.txt` 接入 `ark_mesh_data_smoke`，并在 `framework_headers_smoke` 中触碰新增类型。

### 0.8.2 GpuMesh 上传封装

工作内容：

- 在 renderer 层新增最小 `MeshResource` 或 `GpuMesh`。
- 从 `asset::MeshPrimitiveData` 创建 GPU-only vertex/index buffer。
- 创建 `CpuToGpu | TransferSrc` staging buffer。
- 在 render scope 外通过 `DeviceContext::uploadBufferData()` 上传。
- 上传成功后通过 `deferReleaseBuffer()` 交给 frame deferred deletion。

验收：

- mesh 上传逻辑不再只存在于 `CubePass`。
- GPU mesh 默认使用 GPU-only buffer。
- staging buffer 不会在 GPU copy 完成前释放。
- RHI 公共层不暴露 Vulkan 类型。

当前实现状态：

- 已新增 `src/renderer/MeshResource.h/.cpp`，作为 renderer 层最小 GPU mesh 封装。
- `MeshResource::create()` 从 `asset::MeshPrimitiveData` 创建 GPU-only vertex/index buffer，以及 CpuToGpu staging buffer。
- `MeshResource::upload()` 在 render scope 外通过 `DeviceContext::uploadBufferData()` 记录 vertex/index copy，并在上传命令记录后调用 `deferReleaseBuffer()` 交出 staging 生命周期。
- `MeshResource::bind()` 负责绑定 vertex/index buffer，当前 index type 统一为 `rhi::IndexType::UInt32`。
- `CubePass` 已迁移为使用 `asset::MeshPrimitiveData` 和 `MeshResource`，不再直接持有 vertex/index GPU buffer 或 mesh staging buffer。
- `CubePass` 的 cube CPU 数据已升级到 `asset::MeshVertex`，包含 position、normal、uv0；当前 textured cube shader 仍只读取 position 和 uv0。

### 0.8.3 最小 textured material resource

工作内容：

- 新增 renderer 层 material resource，先只支持 base color RGBA8 texture。
- 复用 `asset::loadImageRgba8()` 加载纹理。
- 创建 texture、texture view、sampler 和 texture staging buffer。
- 复用 `TextureUploadDesc` 上传。
- 继续使用 separate sampled image / sampler descriptor。

验收：

- material resource 不解析 glTF，只消费 `asset::MaterialData`。
- HDR 输入仍不会静默进入 RGBA8 路径。
- descriptor layout / descriptor write / shader binding 一致。

### 0.8.4 ForwardPass 最小落地

工作内容：

- 落地 `ForwardPass`，先绘制一个 mesh primitive。
- 新增 `mesh.vert.hlsl` / `mesh.frag.hlsl` 或重命名 textured cube shader。
- shader 输入为 position / normal / uv0。
- 先沿用最小 descriptor：

```text
set 0 binding 0: UniformBuffer
set 0 binding 1: SampledImage
set 0 binding 2: Sampler
```

- `FrameRenderer` 暂时继续手动调度，不引入完整 RenderGraph。

验收：

- `ForwardPass` 能渲染 textured mesh。
- upload 仍在 `prepare()` 阶段发生，dynamic rendering scope 内只做 draw。
- 默认 sandbox 可运行。

### 0.8.5 glTF 2.0 单 mesh + 单材质加载

工作内容：

- 使用 `tinygltf` 实现最小 `asset::loadGltfModel()`。
- 支持 glTF 2.0 core profile 中一个文件的第一个 mesh primitive。
- 支持 POSITION / NORMAL / TEXCOORD_0。
- 支持 indices，并统一转换为 `u32`。
- 支持 baseColorTexture 外部图片路径解析。
- 对 unsupported feature 输出英文日志并失败或跳过。

验收：

- asset 层输出 `ModelData`，不创建 GPU 资源。
- 外部 texture 相对路径基于 glTF 2.0 文件所在目录解析。
- 缺失关键 attribute 时行为明确，不假装加载成功。

### 0.8.6 默认 sandbox 迁移

工作内容：

- 默认渲染路径从 `CubePass` 迁移到 `ForwardPass`。
- 提供一个最小测试模型或 generated mesh fallback。
- `assets/` 继续通过 CMake copy 到 sandbox 输出目录。
- `CubePass` 可保留为验证/回退 pass，但不再作为主线扩展点。

验收：

- 从仓库根目录运行和从 build output 运行 sandbox 都能找到资源。
- sandbox smoke 通过。
- validation layer 无明显 descriptor、barrier、lifetime 错误。

## 代码阅读建议

Phase 0.8 审核建议按下面顺序阅读：

1. `src/asset/MeshData.h` 与 `src/asset/GltfLoader.*`

   确认 CPU 数据结构不依赖 renderer / RHI，glTF 路径解析和 unsupported feature 处理是否清晰。

2. `src/renderer/MeshResource.*`

   确认 GPU-only buffer、staging buffer、upload 时机和 deferred deletion 是否正确。

3. `src/renderer/material/MaterialResource.*`

   确认 texture upload、texture view、sampler 和 descriptor 更新是否复用 0.7 路径。

4. `src/renderer/passes/ForwardPass.*`

   确认 pass 只组织 draw，不解析文件格式，不暴露 Vulkan 类型。

5. `src/renderer/FrameRenderer.cpp`

   确认 `prepare()` 仍在 `beginRendering()` 前，`execute()` 只在 render scope 内绘制。

6. `src/rhi/vulkan/VulkanCommandContext.cpp`

   复查 buffer / texture upload barrier 是否满足 mesh 和 material 资源使用。

7. shaders

   核对 vertex input layout、descriptor set binding 和 HLSL register 是否一致。

## 审核检查点

- `asset/` 不依赖 renderer、RHI 或 Vulkan。
- `renderer/` 不包含 Vulkan 头文件，不出现 `Vk*` 类型。
- glTF loader 不创建 GPU 资源。
- index / vertex 数据转换在 asset 层完成，renderer 只消费统一格式。
- mesh / texture upload 只在 render scope 外记录。
- staging buffer 进入 deferred deletion，不能提前析构。
- descriptor layout、descriptor write、shader binding 一致。
- `ForwardPass` 不扩展成完整 material system。
- `CubePass` 不继续承担真实资产系统职责。
- 日志使用英文。
- 不支持的 glTF feature 明确失败、跳过或写 TODO。

## 验证计划

每个子阶段完成后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径变化后运行 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

建议至少做一次 validation / RenderDoc 检查：

- vertex/index buffer 是否为 GPU-only，数据是否正确。
- texture image 最终是否处于 shader read 状态。
- descriptor set 中 uniform buffer、sampled image、sampler 是否有效。
- dynamic rendering scope 内是否只执行 render commands。
- glTF 外部 texture 是否按预期加载。

## 完成标准

Phase 0.8 完成时应满足：

- asset 层能输出最小 mesh / material CPU 数据。
- renderer 层能将 mesh / material 数据创建为 GPU resource。
- `ForwardPass` 能绘制 textured mesh。
- 默认 sandbox 不再依赖硬编码 cube 作为唯一渲染路径。
- glTF 2.0 单 mesh + 单材质或等价 fixture 能进入渲染闭环。
- build、CTest 和 sandbox smoke 通过。

## 后续 Phase 建议

Phase 0.9 可以考虑以下方向：

- 更完整的 RenderScene / RenderQueue。
- 多 mesh / 多材质 glTF 2.0。
- 最小 material system。
- PBR 参数与基础光照。
- mipmap / sRGB / color space 处理。
- ResourceManager handle 系统。
- RenderGraph 第一版资源声明和 pass 调度。

在 Phase 0.8 完成前，不建议直接进入完整 PBR 或多节点 glTF scene，因为当前最需要验证的是资产数据到 GPU draw 的职责拆分和生命周期闭环。
