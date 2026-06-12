# Phase 0.31 Cubemap Foundation and HDR Asset Baseline

## 阶段判断

Phase 0.30 已经把 HDR environment resource 接入 forward lighting：

```text
HDR equirectangular texture
    -> EnvironmentResource
    -> RenderScene::SceneEnvironment
    -> ForwardPass binding 14/15
    -> mesh.frag.hlsl equirectangular ambient
    -> RGBA16Float scene color
    -> ToneMappingPass
```

这让 environment 首次真正影响画面，但它仍不是完整 IBL。当前 shader 直接用 world normal 去采样 2D equirectangular texture，只能作为最小 ambient contribution；没有 cubemap、没有 irradiance、没有 prefiltered specular、没有 BRDF LUT，也没有 skybox。

下一阶段不建议直接进入完整 IBL。最稳的方向是先补齐 cubemap foundation，并建立一个可复用的 HDR 资源基线：

```text
small/debug HDR asset
    -> existing equirectangular environment path
    -> manual visual validation

RHI cubemap semantics
    -> Vulkan cube-compatible image/view mapping
    -> renderer cubemap resource foundation
    -> tests 验证 resource/view/lifetime
```

Phase 0.31 的重点是资源形态和验证地基，不要求立刻让 cubemap 改变画面。

## 目标

Phase 0.31 目标：

- 新增 `assets/HDR/` 作为 HDR environment 资源目录。
- 明确可提交的小型 debug HDR 资源规范。
- 明确真实 HDRI 资源的本地验证路径和许可证要求。
- 为 RHI 增加 cubemap texture / texture view 的最小语义。
- 在 Vulkan 后端支持 cube-compatible texture 创建和 cube texture view 映射。
- 新增或预留 renderer 层 cubemap resource foundation，用于后续 equirectangular -> cubemap conversion / IBL。
- 保持 Phase 0.30 的 equirectangular ambient lighting 路径不回退。
- 增加 smoke tests 覆盖 cubemap desc、view desc、Vulkan mapping、renderer resource lifetime 和 public headers。
- 文档明确本阶段仍不做完整 IBL。

## 非目标

Phase 0.31 暂不做：

- 不做 equirectangular -> cubemap conversion pass。
- 不做 compute shader conversion。
- 不做 render-to-cubemap pipeline。
- 不做 diffuse irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 specular IBL。
- 不做 roughness-based mip sampling。
- 不做 skybox / environment background pass。
- 不做 HDR mipmap generation。
- 不做 compressed HDR texture。
- 不做 KTX / DDS / EXR loader。
- 不做 glTF environment extension。
- 不做 `KHR_lights_punctual`。
- 不做 bloom。
- 不做 auto exposure / histogram。
- 不做 ACES / filmic tone mapping。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不提交大型 HDRI 作为默认强依赖。

## HDR 资源策略

### 目录约定

建议新增：

```text
assets/HDR/
```

用途：

- `assets/HDR/`：HDR environment 资源目录。
- `assets/HDR/debug_*.hdr`：允许提交的小型 debug / fixture HDR 资源。
- `assets/HDR/*.hdr`：默认忽略真实 HDRI，避免大型外部资源误提交。

如果后续决定提交真实 HDRI，必须同时记录：

- 来源 URL。
- 下载日期。
- 分辨率。
- 文件大小。
- 许可证。
- 是否允许再分发和商业使用。

### 可提交小资源

推荐补一个非常小的 debug HDR：

```text
assets/HDR/debug_gradient_16x8.hdr
```

建议用途：

- 手工验证 `ark_sandbox.exe [model] [environment.hdr]` 路径。
- 验证 equirectangular UV 方向是否左右/上下颠倒。
- 作为后续 conversion / cubemap face orientation 的对照。

建议内容：

```text
width = 16
height = 8
format = Radiance RGBE .hdr
pattern:
    +X / -X / +Y / -Y / +Z / -Z 方向用不同颜色或亮度编码
    alpha implicit 1.0
```

不要为了 debug fixture 引入大体积真实照片。小资源的目标是方向和路径验证，不是画质。

### 本地真实 HDRI

真实 HDRI 建议先放：

```text
assets/HDR/studio_1k.hdr
assets/HDR/outdoor_1k.hdr
```

并用 sandbox 参数验证：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/studio_1k.hdr
```

建议优先使用 CC0 来源，例如 Poly Haven / ambientCG 等，但在提交任何真实 HDRI 前必须再确认具体文件的许可证和再分发条款。

`.gitignore` 建议忽略：

```text
assets/HDR/*.hdr
!assets/HDR/debug_*.hdr
```

是否忽略所有 `.hdr` 不建议一刀切；小 debug fixture 应允许提交。

## 模块边界

继续遵守现有设计边界：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase29.md
docs/phase/phase30.md
```

边界要求：

- `asset/` 仍只负责读取 HDR 文件并输出 CPU `ImageData`。
- `EnvironmentResource` 继续表示 equirectangular 2D HDR environment。
- 新的 cubemap resource 不应复用 material `TextureResource` 的 sRGB/linear policy。
- `RenderScene` 仍只保存 scene 语义，不创建或拥有 GPU resource。
- `ForwardPass` 当前继续消费 equirectangular environment ambient，不要求改为 cubemap。
- `FrameRenderer` 的两段 dynamic rendering 顺序不变。
- `ToneMappingPass` 不参与 environment / cubemap resource。
- RHI 可以新增 cubemap 语义，但不能暴露 Vulkan 类型。
- 只有 `src/rhi/vulkan/` 可以包含 Vulkan 头文件和 `Vk*` 类型。

## 当前基线

### Environment 2D 路径

当前已支持：

```text
HDR .hdr file
    -> asset::loadImageHdrRgba32F()
    -> asset::ImageData(Rgba32Float)
    -> EnvironmentResource
    -> rhi::Format::RGBA32Float 2D texture
    -> ForwardPass binding 14/15
    -> mesh.frag.hlsl equirectangular ambient
```

限制：

- `EnvironmentResource` 是 2D texture，不是 cubemap。
- `mipLevels = 1`。
- upload 只支持 tightly packed mip0/layer0。
- shader 只用 normal 方向做 ambient sampling。
- 没有 specular IBL。

### RHI Texture 基线

Phase 0.31 前 `TextureDesc` 只有：

```cpp
struct TextureDesc {
    Extent2D extent;
    Format format = Format::Unknown;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    TextureUsage usage = TextureUsage::None;
};
```

Phase 0.31 推荐最小增加 texture/view 维度语义，例如：

```cpp
enum class TextureDimension {
    Texture2D,
    Cube,
};

enum class TextureViewDimension {
    Texture2D,
    Cube,
};
```

或采用更贴近现有代码风格的命名，但必须明确：

- cube texture 本质是 2D texture array，`arrayLayers = 6`。
- cube view 必须覆盖 6 layers。
- 后端负责映射 Vulkan cube-compatible image 和 cube view。

## 建议设计

### RHI Cubemap Semantics

建议扩展：

```cpp
enum class TextureType {
    Texture2D,
    Cube,
};

enum class TextureViewType {
    Texture2D,
    Cube,
};
```

`TextureDesc`：

```cpp
struct TextureDesc {
    Extent2D extent;
    Format format = Format::Unknown;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    TextureUsage usage = TextureUsage::None;
    TextureType type = TextureType::Texture2D;
};
```

`TextureViewDesc`：

```cpp
struct TextureViewDesc {
    Format format = Format::Unknown;
    u32 baseMipLevel = 0;
    u32 mipLevelCount = 1;
    u32 baseArrayLayer = 0;
    u32 arrayLayerCount = 1;
    TextureViewType type = TextureViewType::Texture2D;
};
```

约束：

- `TextureType::Cube` 要求 `extent.width == extent.height`。
- `TextureType::Cube` 要求 `arrayLayers == 6`。
- `TextureViewType::Cube` 要求 `arrayLayerCount == 6`。
- cube texture 必须是 sampled image 可用，后续 conversion 才会扩展 render target / transfer usage。

如果当前 `TextureViewDesc` 字段与上面不完全一致，应以现有字段为准增量扩展，不做大改名。

### Vulkan Mapping

Vulkan image 创建：

```text
TextureType::Texture2D:
    imageType = VK_IMAGE_TYPE_2D
    flags = 0

TextureType::Cube:
    imageType = VK_IMAGE_TYPE_2D
    flags includes VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    arrayLayers = 6
```

Vulkan image view：

```text
TextureViewType::Texture2D:
    viewType = VK_IMAGE_VIEW_TYPE_2D

TextureViewType::Cube:
    viewType = VK_IMAGE_VIEW_TYPE_CUBE
```

后端校验：

- cube texture 非 square 直接失败。
- cube texture arrayLayers 非 6 直接失败。
- cube view layer count 非 6 直接失败。
- 失败日志使用英文。

### Renderer Cubemap Resource Foundation

建议新增资源类之一：

```text
src/renderer/EnvironmentCubeResource.h
src/renderer/EnvironmentCubeResource.cpp
```

最小职责：

- 创建 `rhi::Format::RGBA16Float` 或 `RGBA32Float` cubemap texture。
- 创建 cube texture view。
- 创建 sampler。
- 支持 `releaseDeferred()` / `resetImmediate()`。
- 暂不负责填充像素或 conversion。

建议 desc：

```cpp
struct EnvironmentCubeResourceDesc {
    rhi::Extent2D faceExtent;
    rhi::Format format = rhi::Format::RGBA16Float;
    u32 mipLevels = 1;
    rhi::SamplerDesc sampler;
    bool hasSamplerOverride = false;
    std::string debugName;
};
```

本阶段不强制接入 `RenderScene`，也不替换 `EnvironmentResource`。它只是后续 IBL 资源的基础。

### HDR Asset Smoke

如果用户补充了小 HDR：

```text
assets/HDR/debug_gradient_16x8.hdr
```

建议补充：

- `texture_loader_smoke`：读取该 fixture，验证 `Rgba32Float`、尺寸、bpp。
- sandbox manual smoke：使用该 HDR 路径短启动。

如果该 HDR 由外部下载，不建议直接加入自动测试，除非许可证和体积都明确适合提交。

### .gitignore

建议增加：

```gitignore
assets/HDR/*.hdr
!assets/HDR/debug_*.hdr
```

不建议忽略：

```gitignore
assets/HDR/*.hdr
```

原因：小型 debug fixture 应允许入库。

## 实施顺序

### 0.31.0 文档与范围确认

目标：

- 新增 `docs/phase/phase31.md`。
- 明确本阶段是 cubemap foundation + HDR asset baseline。
- 明确本阶段不做 equirectangular -> cubemap conversion 和完整 IBL。
- 明确 HDR 资源目录和提交策略。

审核点：

- 不把阶段扩大成 irradiance / prefilter / BRDF LUT。
- 不修改 ForwardPass environment ambient 路径。
- 不引入 RenderGraph / bindless。

### 0.31.1 HDR Asset Baseline

目标：

- 新增 `assets/HDR/`。
- 预留或接入小型 debug HDR fixture。
- 更新 `.gitignore`，默认忽略真实 `.hdr`，但允许 `debug_*.hdr` fixture。
- 文档记录用户补充 HDR 的建议命名和验证命令。

审核点：

- 小 fixture 体积可控。
- 真实 HDRI 不作为默认强依赖。
- 外部真实 HDRI 提交前必须有许可证记录。

### 0.31.2 RHI Cubemap API

目标：

- 扩展 RHI texture / texture view desc，表达 cube texture/view。
- 保持现有 2D texture 默认行为不变。
- 补充 public header smoke。

审核点：

- 现有 `TextureResource`、`EnvironmentResource`、FrameRenderer scene color 不受影响。
- 默认 desc 仍是 2D。
- 不暴露 Vulkan 类型。

### 0.31.3 Vulkan Cubemap Mapping

目标：

- Vulkan texture 创建支持 cube-compatible image。
- Vulkan texture view 支持 cube view。
- 加入必要校验和英文日志。

审核点：

- cube texture 必须 square。
- cube texture 必须 6 layers。
- cube view 必须覆盖 6 layers。
- 不影响现有 RGBA8 / RGBA16Float / RGBA32Float 2D path。

### 0.31.4 Renderer Cubemap Resource Foundation

目标：

- 新增 renderer cubemap resource。
- 创建 cube texture / cube view / sampler。
- 支持 deferred release / immediate reset。
- 暂不接入 shader。

审核点：

- resource owner 在 renderer 层。
- 不进入 material texture cache。
- 不要求像素 upload 或 conversion。

### 0.31.5 Tests

目标：

- `framework_headers_smoke` 覆盖 cubemap public API。
- 新增或扩展 fake RHI smoke 覆盖 cubemap resource desc / view desc / sampler。
- 如 HDR fixture 已补充，`texture_loader_smoke` 覆盖该 fixture。
- `forward_pass_pipeline_smoke` 确认 Phase 0.30 environment ambient 不回退。

审核点：

- tests 不依赖大型 HDRI。
- fake RHI tests 不需要真实 Vulkan。
- CTest 仍覆盖已有 smoke。

### 0.31.6 验证与收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - equirectangular -> cubemap conversion
  - irradiance map
  - prefiltered specular
  - BRDF LUT
  - skybox
  - specular IBL

建议验证：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

如果已补充小型 HDR fixture：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/debug_gradient_16x8.hdr
```

## 审核检查点

- `assets/HDR/` 资源策略明确。
- 本地真实 HDRI 不被误提交为默认强依赖。
- RHI cubemap API 默认不影响 2D texture。
- Vulkan cubemap 创建和 view mapping 有明确校验。
- renderer cubemap resource 不接触 Vulkan 类型。
- `EnvironmentResource` 继续表示 equirectangular 2D HDR environment。
- `ForwardPass` Phase 0.30 environment ambient 不回退。
- `ToneMappingPass` 不变。
- build、CTest、default sandbox、DamagedHelmet smoke 通过。
- 文档明确仍未进入完整 IBL。

## 当前实现状态

当前已完成 Phase 0.31 代码实现与验证：

- `assets/HDR/` 目录已落地，`assets/HDR/.gitkeep` 进入版本管理。
- 用户已补充 `assets/HDR/warm_restaurant_8k.hdr` 作为本地真实 HDRI，文件约 98MB，默认不提交。
- `.gitignore` 默认忽略 `assets/HDR/*.hdr`，但允许 `assets/HDR/debug_*.hdr` 小型 fixture 入库。
- RHI 新增 `rhi::TextureType::Cube` 和 `rhi::TextureViewType::Cube`。
- Vulkan texture 创建支持 cube-compatible image，texture view 支持 cube view，并校验 square、6 layers、mip/layer range。
- renderer 新增 `EnvironmentCubeResource`，负责创建 cubemap texture / cube view / sampler，并支持 deferred release / immediate reset。
- `ark_environment_cube_resource_smoke` 覆盖 cubemap desc / view desc / sampler / lifecycle。
- `ark_framework_headers_smoke` 覆盖 cubemap public API。
- `cmake --preset msvc-vcpkg`、full build、CTest 12/12、default sandbox、DamagedHelmet sandbox 和 DamagedHelmet + local HDR environment smoke 已通过。

## 完成标准

Phase 0.31 完成时应满足：

- 项目有明确 HDR environment 资源目录和提交策略。
- 小型 debug HDR fixture 可用于手工验证，或文档明确等待用户补充。
- RHI 能表达 cube texture / cube view。
- Vulkan 后端能创建 cube-compatible image 和 cube view。
- renderer 层有 cubemap resource foundation，并覆盖生命周期测试。
- Phase 0.30 equirectangular ambient 路径不回退。
- tests 覆盖 cubemap API / renderer resource / existing environment path。
- 文档和 handoff 记录当前已支持 cubemap foundation，但仍未支持 conversion、irradiance、prefilter、BRDF LUT、skybox 和 specular IBL。

## 后续 Phase 建议

Phase 0.31 后建议进入：

1. Equirectangular -> cubemap conversion pass。
2. Diffuse irradiance map。
3. Prefiltered specular environment map。
4. BRDF LUT。
5. Skybox / environment background pass。
6. bloom / auto exposure / ACES tone mapping。
