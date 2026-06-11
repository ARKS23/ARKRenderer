# Phase 0.29 HDR Environment Texture / IBL Prelude

## 阶段判断

Phase 0.28 已经把 renderer 的颜色链路收口到一个稳定状态：

```text
mesh material + direct lighting
    -> linear HDR color
    -> RGBA16Float scene color
    -> ToneMappingPass
    -> swapchain backbuffer
```

这说明当前 renderer 已经有能力承载超过 LDR 范围的 lighting result，也有明确的 output mapping 位置。下一步如果继续提升真实模型观感，最自然的方向是 IBL / environment lighting。

但是当前还不能直接进入完整 IBL：

- `TextureLoader` 当前只有 LDR `loadImageRgba8()`，遇到 HDR 输入会显式失败。
- `asset::ImageFormat::Rgba32Float` 已存在语义占位，但没有对应 loader。
- `TextureResource` 当前只接受 `Rgba8Unorm`，并映射到 `RGBA8Unorm` / `RGBA8Srgb`。
- `rhi::TextureUploadDesc` 和 Vulkan upload path 当前只支持 4 bytes per pixel 的 RGBA8 tightly packed upload。
- RHI 还没有 cubemap texture/view 语义。
- `mesh.frag.hlsl` 当前仍是 direct-light-only，ambient 只是 `ambientColor * albedo`。
- 如果直接做 irradiance map、prefiltered specular、BRDF LUT，会同时牵动 asset、RHI、Vulkan upload、shader descriptor layout、ForwardPass 和测试，阶段过大。

因此 Phase 0.29 应先补齐 “HDR environment texture 作为 renderer 资源” 这条前置链路。它是 IBL 的输入地基，不是完整 IBL 本身。

建议本阶段目标表达为：

```text
HDR file
    -> asset::ImageData(Rgba32Float)
    -> renderer EnvironmentResource
    -> RHI 2D sampled texture
    -> RenderScene environment slot
    -> tests 验证数据/资源/边界
```

## 目标

Phase 0.29 目标：

- 新增显式 HDR float image loader。
- 让 asset 层可以输出 `ImageFormat::Rgba32Float`。
- 扩展 RHI format，支持最小 `RGBA32Float` sampled texture。
- 扩展 Vulkan texture upload，支持 tightly packed 16 bytes per pixel 的 float RGBA upload。
- 新增 renderer 层 environment texture resource，用于保存 equirectangular HDR environment map。
- 在 `RenderScene` 中建立最小 environment 入口，记录 environment resource 与 intensity。
- 保持 `ForwardPass` lighting shader 不变；本阶段不把 environment 采样接入 mesh shader。
- 保持 Phase 0.28 的 tone mapping / color pipeline 不回退。
- 增加 smoke tests 覆盖 HDR loader、float texture resource desc、environment scene API 和 Vulkan format/upload 约束。
- 文档明确本阶段仍不是完整 IBL。

## 非目标

Phase 0.29 暂不做：

- 不做 diffuse irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 cubemap texture/view。
- 不做 equirectangular -> cubemap conversion。
- 不做 shader 中的 environment lighting 采样。
- 不修改 `mesh.frag.hlsl` 的 BRDF 输出。
- 不修改 `ForwardPass` descriptor layout。
- 不做 skybox pass。
- 不做 background/environment 可视化。
- 不做 HDR mipmap generation。
- 不做 compressed HDR texture。
- 不做 KTX / DDS / EXR loader。
- 不做 glTF `KHR_lights_punctual`。
- 不做 glTF environment extension。
- 不做 auto exposure / histogram。
- 不做 bloom。
- 不做 ACES / filmic tone mapping。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。

## 模块边界

继续遵守现有设计文档和最近 phase：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase26.md
docs/phase/phase27.md
docs/phase/phase28.md
```

边界要求：

- `asset/` 只负责读取 HDR 文件并输出 CPU `ImageData`，不创建 RHI/GPU 资源。
- `renderer/` 负责把 HDR CPU image 转成 environment GPU resource。
- `rhi/` 可以新增 format，但不要在本阶段引入 cubemap API。
- `rhi/vulkan/` 只补齐 `RGBA32Float` format mapping 和 16 bytes per pixel upload 路径。
- `RenderScene` 保存 scene 级 environment 语义，不负责创建资源。
- `FrameRenderer` 的两段渲染顺序不变。
- `ForwardPass` 本阶段不消费 environment。
- `ToneMappingPass` 不参与 environment resource。
- tests 必须证明新资源链路可用，但不要求画面变化。

## 当前基线

### TextureLoader

当前 loader 只有 LDR RGBA8 路径：

```cpp
ImageData loadImageRgba8(const Path& path);
```

并且遇到 HDR 会失败：

```text
HDR image is not supported by loadImageRgba8
```

这是正确的保护：HDR 不能静默量化到 8-bit。Phase 0.29 应新增独立入口，而不是改变 `loadImageRgba8()` 语义。

建议新增：

```cpp
ImageData loadImageHdrRgba32F(const Path& path);
```

### ImageData

当前已有格式枚举：

```cpp
enum class ImageFormat {
    Unknown,
    Rgba8Unorm,
    Rgba32Float,
};
```

`Rgba32Float` 应在 Phase 0.29 变成真实可用路径：

```text
width * height * 4 channels * sizeof(float)
bytesPerPixel = 16
pixels 保存 float bytes
```

### TextureResource

当前 `TextureResource` 只接受 RGBA8：

```text
ImageFormat::Rgba8Unorm
bytesPerPixel = 4
TextureColorSpace::Srgb -> RGBA8Srgb
TextureColorSpace::Linear -> RGBA8Unorm
```

这条路径主要服务 glTF material textures。Phase 0.29 不建议强行让 material `TextureResource` 同时承载 HDR environment 的所有语义，否则会把 material texture color space 和 environment map 混在一起。

推荐新增独立 renderer 资源：

```text
EnvironmentResource
```

### Vulkan Upload

当前 Vulkan upload 约束：

```text
format: RGBA8Unorm / RGBA8Srgb
bytesPerPixel: 4
rowPitch: tightly packed
mipLevel: 0
arrayLayer: 0
```

Phase 0.29 最小扩展：

```text
format: RGBA32Float
bytesPerPixel: 16
rowPitch: tightly packed
mipLevel: 0
arrayLayer: 0
```

仍不支持 HDR mip generation。

## 建议设计

### HDR Loader

新增 loader 入口：

```cpp
ImageData loadImageHdrRgba32F(const Path& path);
```

建议实现约束：

- 使用 `stbi_is_hdr_from_memory()` 识别 HDR。
- 非 HDR 文件传入 HDR loader 应失败并输出英文日志。
- 使用 `stbi_loadf_from_memory()` 强制输出 4 channels。
- `ImageData::format = ImageFormat::Rgba32Float`。
- `ImageData::bytesPerPixel = 16`。
- `ImageData::pixels` 保存 float 原始 bytes。
- 检查 width / height / byte size overflow。
- 保持 `loadImageRgba8()` 遇到 HDR 失败的现有行为。

测试可以使用极小 `.hdr` fixture，或者在 test 中通过本地 fixture 文件验证。不要依赖 DamagedHelmet 或外部下载资产。

### RHI Format

建议新增：

```cpp
enum class Format {
    ...
    RGBA16Float,
    RGBA32Float,
    ...
};
```

Vulkan mapping：

```text
rhi::Format::RGBA32Float <-> VK_FORMAT_R32G32B32A32_SFLOAT
```

同时更新：

- format name helper。
- Vulkan format mapping。
- reverse format mapping 如已有。
- tests / header smoke。

### Texture Upload

建议在 `VulkanCommandContext::uploadTextureData()` 中把 “RGBA8 only” 改成按 format 判断：

```text
RGBA8Unorm / RGBA8Srgb:
    bytesPerPixel = 4

RGBA32Float:
    bytesPerPixel = 16
```

本阶段仍保留以下限制：

- only mip 0。
- only array layer 0。
- only tightly packed rows。
- source offset 必须按 pixel size 对齐。
- texture 必须有 `TransferDst` usage。

`generateTextureMips()` 本阶段不需要支持 `RGBA32Float`，environment resource 默认不生成 mips。

### EnvironmentResource

建议新增：

```text
src/renderer/EnvironmentResource.h
src/renderer/EnvironmentResource.cpp
```

最小职责：

- 持有 HDR environment texture / view / sampler / staging buffer。
- 从 `asset::ImageData(Rgba32Float)` 创建 GPU texture。
- format 固定为 `rhi::Format::RGBA32Float`。
- usage 固定为 `ShaderResource | TransferDst`。
- mipLevels 固定为 `1`。
- sampler 默认：
  - min/mag: Linear
  - mip: Nearest
  - addressU: Repeat
  - addressV: ClampToEdge
  - addressW: ClampToEdge
- upload 必须发生在 dynamic rendering scope 外。
- 支持 `releaseDeferred()` / `resetImmediate()`，对齐 `TextureResource` 的生命周期风格。

建议先不要把 `EnvironmentResource` 合并进 `TextureCache`。material texture cache 的 key 是 path + color space + sampler override；environment map 后续还会涉及 intensity、rotation、mip chain、cubemap/prefilter 等语义，独立资源更干净。

### RenderScene Environment Slot

建议在 `RenderScene` 中新增最小 scene environment 语义：

```cpp
struct SceneEnvironment {
    EnvironmentResource* environment = nullptr;
    float intensity = 1.0f;
};
```

API：

```cpp
const SceneEnvironment& environment() const;
void setEnvironment(const SceneEnvironment& environment);
void clearEnvironment();
```

约束：

- `RenderScene` 不拥有 `EnvironmentResource`。
- `RenderScene::clear()` 是否清 environment 需要明确。建议 `clear()` 清空 objects/models，但保留 lighting/environment policy 也可以；如果选择清空，必须测试约束。当前 `clear()` 不重置 lighting，因此建议 environment 也作为 scene policy 保留，另用 `clearEnvironment()` 显式清空。
- intensity 小于 0 时建议在 setter clamp 到 0，或者在后续 shader consumer 中 clamp。本阶段若只存数据，setter clamp 更直接。

### Sandbox / Runtime

本阶段不要求默认 sandbox 加载 HDR environment。

可选最小入口：

```text
assets/environments/
```

但如果没有确定要提交的轻量 HDR fixture，不要引入大体积 HDR 资产。测试 fixture 应尽量小。

## 实施顺序

### 0.29.0 文档与范围确认

目标：

- 新增 `docs/phase/phase29.md`。
- 明确本阶段是 IBL 前置，不是完整 IBL。
- 明确本阶段只做 HDR loader、float texture upload、environment resource 和 scene environment slot。

审核点：

- 不把本阶段扩大成 cubemap / irradiance / prefilter / BRDF LUT。
- 不修改 ForwardPass shader 绑定。
- 不引入 RenderGraph 或 bindless。

### 0.29.1 HDR Image Loader

目标：

- 新增 `loadImageHdrRgba32F()`。
- 使用 stb float path 读取 HDR。
- 输出 `ImageFormat::Rgba32Float`。
- 保持 `loadImageRgba8()` 对 HDR 输入显式失败。

审核点：

- HDR 与 LDR loader 语义分离。
- 不静默量化 HDR。
- byte size overflow 检查完整。

### 0.29.2 RHI RGBA32Float Texture Upload

目标：

- 新增 `rhi::Format::RGBA32Float`。
- Vulkan format mapping 到 `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `TextureUploadDesc` 支持 `bytesPerPixel = 16`。
- Vulkan upload 支持 `RGBA32Float` tightly packed mip0/layer0。

审核点：

- 不扩大到 arbitrary rowPitch。
- 不扩大到 array/cubemap。
- 不要求 `generateTextureMips()` 支持 float texture。

### 0.29.3 EnvironmentResource

目标：

- 新增 `EnvironmentResource`。
- 从 `ImageData(Rgba32Float)` 创建 `RGBA32Float` GPU sampled texture。
- 默认 `mipLevels = 1`。
- 默认 sampler 对齐 equirectangular environment map。
- 支持 upload / deferred release / immediate reset。

审核点：

- 不复用 material `TextureResource` 的 sRGB/linear color space policy。
- 不进入 `TextureCache`。
- 不拥有 scene policy。

### 0.29.4 RenderScene Environment API

目标：

- 新增 `SceneEnvironment`。
- `RenderScene` 保存 environment resource pointer 和 intensity。
- 提供 getter / setter / clear API。

审核点：

- `RenderScene` 不拥有 GPU resource。
- `ForwardPass` 暂不消费该 API。
- API 只表达 scene semantic，不创建资源。

### 0.29.5 Tests

目标：

- `texture_loader_smoke` 覆盖 HDR loader。
- `framework_headers_smoke` 覆盖 `RGBA32Float`、`EnvironmentResource`、`SceneEnvironment` public API。
- 新增或扩展 fake RHI smoke，覆盖 `EnvironmentResource` 创建：
  - texture format = `RGBA32Float`
  - usage = `ShaderResource | TransferDst`
  - bytesPerPixel = 16
  - mipLevels = 1
  - sampler addressU = Repeat
  - sampler addressV/W = ClampToEdge
- shader source smoke 不需要改，因为本阶段不改 shader。

审核点：

- tests 不依赖大体积 HDR asset。
- tests 不需要真实 Vulkan。
- CTest 仍覆盖已有 smoke。

### 0.29.6 验证与收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - environment shader sampling
  - irradiance map
  - prefiltered specular
  - BRDF LUT
  - cubemap
  - skybox
  - bloom / auto exposure

建议验证：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

如果新增独立 test target，例如 `ark_environment_resource_smoke`，补充：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_resource_smoke
build/msvc-vcpkg/Debug/ark_environment_resource_smoke.exe
```

## 审核检查点

- HDR loader 与 LDR loader 语义明确分离。
- HDR 输入不会走 RGBA8 静默量化。
- `ImageFormat::Rgba32Float` 成为真实可用格式。
- RHI / Vulkan format mapping 支持 `RGBA32Float`。
- Vulkan upload 支持 `RGBA32Float` 的 16 bytes per pixel tightly packed upload。
- `generateTextureMips()` 不被误用到 HDR environment texture。
- `EnvironmentResource` 与 material `TextureResource` 语义分离。
- `RenderScene` 只保存 environment semantic，不拥有 GPU resource。
- `ForwardPass`、`mesh.frag.hlsl`、`ToneMappingPass` 本阶段不变。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未进入完整 IBL。

## 当前实现状态

Phase 0.29 已完成 0.29.0 ~ 0.29.6 的实现、测试与收尾。

已完成：

- `TextureLoader::loadHdrRgba32F()` / `loadImageHdrRgba32F()` 已接入 stb float HDR path。
- `loadImageRgba8()` 继续显式拒绝 HDR 输入，避免静默量化到 8-bit。
- `asset::ImageFormat::Rgba32Float` 现在作为真实 CPU image output 使用，`bytesPerPixel = 16`。
- `rhi::Format::RGBA32Float` 已加入 RHI format 枚举，并映射到 Vulkan `VK_FORMAT_R32G32B32A32_SFLOAT`。
- `VulkanCommandContext::uploadTextureData()` 已支持 `RGBA32Float` tightly packed mip0/layer0 upload。
- `EnvironmentResource` 已新增，用于从 `ImageData(Rgba32Float)` 创建 `RGBA32Float` 2D sampled environment texture。
- `EnvironmentResource` 默认 `mipLevels = 1`，usage 为 `ShaderResource | TransferDst`，不调用 HDR mip generation。
- `EnvironmentResource` 默认 sampler 采用 equirectangular environment map 友好的 `U=Repeat`、`V/W=ClampToEdge`。
- `RenderScene` 已新增 `SceneEnvironment`、`environment()`、`setEnvironment()` 和 `clearEnvironment()`。
- `RenderScene::setEnvironment()` 会把负 intensity clamp 到 `0`；`RenderScene::clear()` 保持 lighting/environment policy，显式清空使用 `clearEnvironment()`。
- `framework_headers_smoke`、`texture_loader_smoke`、`render_scene_queue_smoke` 已覆盖新增 public API 和边界。
- 新增 `ark_environment_resource_smoke`，使用 fake RHI 验证 resource desc、upload desc、sampler policy、deferred release 和 reset 行为。
- `CMakeLists.txt` 已接入 `ark_environment_resource_smoke`。

仍未支持：

- `ForwardPass` / `mesh.frag.hlsl` 中的 environment sampling。
- cubemap texture/view。
- equirectangular -> cubemap conversion。
- diffuse irradiance map。
- prefiltered specular environment map。
- BRDF LUT。
- skybox / environment background pass。
- HDR mipmap generation。
- bloom、auto exposure、ACES / filmic tone mapping。

## 验证记录

Phase 0.29 收尾在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_environment_resource_smoke ark_texture_loader_smoke ark_render_scene_queue_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_environment_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_texture_loader_smoke.exe
build/msvc-vcpkg/Debug/ark_render_scene_queue_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：

```text
targeted smoke build passed
ark_environment_resource_smoke passed
ark_texture_loader_smoke passed
ark_render_scene_queue_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 11/11 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
```

runtime smoke 使用隐藏窗口启动 3 秒后自动停止，用于确认默认场景和 DamagedHelmet 路径不会启动即退出。

## 完成标准

Phase 0.29 完成时应满足：

- asset 层能显式读取 HDR image 为 `Rgba32Float`。
- renderer 层能创建和上传 HDR environment 2D texture。
- RHI/Vulkan 能支持 `RGBA32Float` sampled texture 的最小 upload。
- `RenderScene` 能表达 scene environment resource 和 intensity。
- tests 覆盖 HDR loader、float texture resource 和 scene environment API。
- 默认渲染行为不回退。
- 文档和 handoff 记录当前已支持 HDR environment texture 前置链路。
- 文档明确仍未支持完整 IBL、cubemap、irradiance、prefilter、BRDF LUT、skybox、bloom 和 auto exposure。

## 后续 Phase 建议

Phase 0.29 后建议进入：

1. 最小 environment lighting 接入：ForwardPass descriptor layout 增加 environment texture/sampler/intensity，mesh shader 对 equirectangular HDR 进行 diffuse-ish sampling 或先替换 ambient。
2. Cubemap resource / equirectangular -> cubemap conversion。
3. Diffuse irradiance map。
4. Prefiltered specular environment map。
5. BRDF LUT。
6. Skybox / environment background pass。
7. bloom / auto exposure / ACES tone mapping。
