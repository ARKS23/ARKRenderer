# Phase 0.32 Equirectangular to Cubemap Conversion

## 阶段判断

Phase 0.31 已经完成 cubemap foundation：

```text
assets/HDR/
    -> HDR asset baseline

rhi::TextureType::Cube
rhi::TextureViewType::Cube
    -> Vulkan cube-compatible image
    -> Vulkan cube texture view

EnvironmentCubeResource
    -> cubemap texture
    -> cube texture view
    -> sampler
```

这意味着 renderer 已经能创建 cubemap 资源，但 cubemap 仍然是空容器。Phase 0.30 的 forward lighting 仍然直接采样 equirectangular 2D HDR texture，Phase 0.31 也没有改变画面结果。

下一阶段最稳的方向是打通 equirectangular HDR 到 cubemap 的 GPU conversion：

```text
EnvironmentResource (2D equirectangular HDR)
    -> EquirectangularToCubePass
    -> EnvironmentCubeResource (6 faces)
```

这一步仍不是完整 IBL。它的价值是把后续 irradiance、prefiltered specular、BRDF LUT 和 skybox 所需的 cubemap 输入先做成可靠基础。

## 目标

Phase 0.32 目标：

- 为 `EnvironmentCubeResource` 增加 per-face render target view 能力。
- 让 cubemap texture 支持 render target usage。
- 新增 equirectangular -> cubemap conversion shader。
- 新增 renderer 层 conversion pass / converter。
- 从现有 `EnvironmentResource` 读取 equirectangular HDR sampled image / sampler。
- 渲染 6 次，将不同 face 写入 cubemap 的 6 个 array layer。
- 明确 cubemap face orientation 和坐标系约定。
- 增加 smoke tests 覆盖 face view desc、conversion descriptor layout、pipeline desc 和 draw count。
- 保持 Phase 0.30 equirectangular ambient lighting 路径不回退。
- 不把本阶段扩大成完整 IBL。

## 非目标

Phase 0.32 暂不做：

- 不做 diffuse irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 specular IBL。
- 不做 roughness-based mip sampling。
- 不做 skybox / environment background pass，除非只作为临时 debug preview。
- 不做 HDR cubemap mipmap generation。
- 不做 compute shader conversion。
- 不做 KTX / DDS / EXR loader。
- 不做 compressed HDR texture。
- 不做 glTF environment extension。
- 不做 `KHR_lights_punctual`。
- 不做 bloom。
- 不做 auto exposure / histogram。
- 不做 ACES / filmic tone mapping。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不替换 `ForwardPass` 的现有 equirectangular environment ambient 采样路径。

## 当前基线

### EnvironmentResource

当前 `EnvironmentResource` 表示 equirectangular 2D HDR environment：

```text
asset::ImageData(Rgba32Float)
    -> RGBA32Float 2D texture
    -> 2D texture view
    -> sampler
    -> upload mip0/layer0
```

限制：

- 只支持 2D texture。
- `mipLevels = 1`。
- upload 只支持 tightly packed mip0/layer0。
- 默认 sampler 为 `U=Repeat`、`V/W=ClampToEdge`。

### EnvironmentCubeResource

当前 `EnvironmentCubeResource` 表示 cubemap resource foundation：

```text
EnvironmentCubeResourceDesc
    faceExtent
    format = RGBA16Float / RGBA32Float
    mipLevels

create()
    -> TextureType::Cube
    -> arrayLayers = 6
    -> TextureViewType::Cube
    -> sampler
```

限制：

- 当前 usage 只有 `ShaderResource`。
- 当前只有 cube sampled view。
- 没有 per-face render target view。
- 不负责填充像素。
- 不接入 `RenderScene`。

### RHI / Vulkan

当前 RHI 已能表达：

```cpp
TextureDesc::type = TextureType::Cube;
TextureViewDesc::type = TextureViewType::Cube;
```

Vulkan 后端已能映射：

```text
TextureType::Cube
    -> VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT

TextureViewType::Cube
    -> VK_IMAGE_VIEW_TYPE_CUBE
```

还需要确认 Vulkan 是否允许同一个 cube-compatible image 创建 2D array layer view 或 2D view，用于 per-face render target。Phase 0.32 推荐新增 RHI view type 时优先保守：

```cpp
enum class TextureViewType {
    Texture2D,
    Texture2DArray,
    Cube,
};
```

如果只需要单 face render target，也可以继续使用 `Texture2D` view type，并通过 `baseArrayLayer` / `arrayLayerCount = 1` 表达单 layer view；但 Vulkan 后端必须明确校验该组合可以绑定为 color attachment。

## 建议设计

### EnvironmentCubeResource 扩展

建议扩展 `EnvironmentCubeResource`：

```cpp
class EnvironmentCubeResource final {
public:
    rhi::TextureView* textureView() const;
    rhi::TextureView* faceRenderTargetView(u32 faceIndex) const;
};
```

内部新增：

```cpp
std::array<Scope<rhi::TextureView>, 6> m_FaceViews;
```

texture usage 建议：

```cpp
textureDesc.usage =
    rhi::TextureUsage::RenderTarget |
    rhi::TextureUsage::ShaderResource;
```

face view desc 建议：

```cpp
rhi::TextureViewDesc faceViewDesc{};
faceViewDesc.format = textureDesc.format;
faceViewDesc.baseMipLevel = 0;
faceViewDesc.mipLevelCount = 1;
faceViewDesc.baseArrayLayer = faceIndex;
faceViewDesc.arrayLayerCount = 1;
faceViewDesc.type = rhi::TextureViewType::Texture2D;
```

审核点：

- cube sampled view 继续覆盖 6 layers。
- per-face render target view 只覆盖单 layer。
- releaseDeferred 必须释放 face views、cube view、sampler、texture。
- `resetImmediate()` 同样清理 face views。

### Conversion Pass

建议新增：

```text
src/renderer/passes/EquirectangularToCubePass.h
src/renderer/passes/EquirectangularToCubePass.cpp
```

或者如果暂时不想把它放入 frame pass list，可命名为：

```text
src/renderer/EnvironmentCubeConverter.h
src/renderer/EnvironmentCubeConverter.cpp
```

推荐先使用 `EnvironmentCubeConverter`。原因是 conversion 属于资源准备阶段，不是每帧 scene draw pass。后续如果 RenderGraph 引入 resource build pass，再移动也不迟。

最小 API：

```cpp
struct EnvironmentCubeConversionDesc {
    EnvironmentResource* source = nullptr;
    EnvironmentCubeResource* target = nullptr;
    std::string debugName;
};

class EnvironmentCubeConverter final {
public:
    void setup(rhi::RenderDevice& device);
    bool convert(rhi::DeviceContext& context, const EnvironmentCubeConversionDesc& desc);
};
```

如果当前 renderer lifecycle 不方便直接调用 `convert()`，可先只完成 converter 内部资源创建和 fake RHI smoke，不强制接入 `Renderer` 主线。

### Shader

建议新增：

```text
shaders/equirect_to_cube.vert.hlsl
shaders/equirect_to_cube.frag.hlsl
```

vertex shader：

- 使用 fullscreen triangle。
- 不需要 mesh vertex buffer。

fragment shader：

- 输入当前 face index。
- 从 fullscreen uv 构造 face local xy。
- 根据 face index 映射到 world direction。
- 将 direction 转为 equirectangular uv。
- 采样 `Texture2D<float4> sourceEnvironment`。
- 输出 linear HDR color。

descriptor binding 建议：

```text
binding 0: conversion uniform buffer
binding 1: source equirectangular sampled image
binding 2: source sampler
```

uniform 建议：

```hlsl
cbuffer EquirectToCubeUniform : register(b0)
{
    uint faceIndex;
    float outputResolution;
    float2 padding;
};
```

### Face Orientation

必须在文档和 shader tests 中明确 face 顺序。建议采用 Vulkan 常见 cubemap face 顺序：

```text
0 = +X
1 = -X
2 = +Y
3 = -Y
4 = +Z
5 = -Z
```

建议映射先保持右手世界方向语义，再根据当前 renderer camera / mesh 坐标系做视觉校验。如果发现上下或左右反转，优先修 shader mapping，而不是在 asset loader 或 `EnvironmentResource` 中做隐式修正。

建议写入 shader 注释或文档：

```text
face 0 +X: direction = normalize(float3( 1, -y, -x))
face 1 -X: direction = normalize(float3(-1, -y,  x))
face 2 +Y: direction = normalize(float3( x,  1,  y))
face 3 -Y: direction = normalize(float3( x, -1, -y))
face 4 +Z: direction = normalize(float3( x, -y,  1))
face 5 -Z: direction = normalize(float3(-x, -y, -1))
```

该映射需要结合实际渲染结果验证，不能只靠纸面确认。

## HDR 资源补充策略

### 必须补充的小型 debug HDR

建议补充一个可提交的极小 fixture：

```text
assets/HDR/debug_latlong_32x16.hdr
```

用途：

- 验证 HDR loader。
- 验证 sandbox environment path。
- 验证 equirectangular UV 方向。
- 验证 cubemap face orientation。
- 作为未来 conversion / skybox / irradiance 的稳定测试资源。

建议内容：

```text
width = 32
height = 16
format = Radiance RGBE .hdr
pattern:
    +X / -X / +Y / -Y / +Z / -Z 方向区域使用明显颜色
    top/bottom 使用不同亮度
    horizon 有水平参考线
```

该 fixture 应由项目生成或手工构造，避免外部许可证问题。体积应非常小，适合进入自动测试。

### 本地真实 HDRI

当前已有：

```text
assets/HDR/warm_restaurant_8k.hdr
```

用途：

- 本地视觉验证。
- 检查高动态范围、tone mapping 和环境光亮度。
- 后续 conversion 完成后验证真实 HDRI 的 face seams 和方向。

但它约 98MB，不应提交。`.gitignore` 已默认忽略 `assets/HDR/*.hdr`，除 `debug_*.hdr` 外不会误入库。

建议再准备一个较小的本地真实 HDRI：

```text
assets/HDR/studio_1k.hdr
assets/HDR/outdoor_sun_1k.hdr
```

要求：

- 优先 CC0。
- 1K 或 2K 足够。
- outdoor HDRI 最好有明显太阳和地平线，便于验证方向。
- 提交真实 HDRI 前必须记录来源、下载日期、分辨率、文件大小和许可证。

## 测试策略

### Fake RHI Tests

建议新增或扩展：

```text
tests/environment_cube_resource_smoke.cpp
tests/framework_headers_smoke.cpp
tests/equirectangular_to_cube_smoke.cpp
```

覆盖点：

- `EnvironmentCubeResource` 创建 6 个 face views。
- face view desc 为单 layer render target view。
- cube sampled view 仍覆盖 6 layers。
- texture usage 包含 `RenderTarget | ShaderResource`。
- converter descriptor layout 包含 uniform、sampled image、sampler。
- pipeline color format 使用 target cubemap format。
- conversion draw count 为 6。
- 每个 face 都绑定对应 face render target view。
- conversion 不依赖真实 Vulkan device。

### Shader Asset Smoke

扩展 `shader_assets_smoke`：

- 确认新增 shader 文件存在。
- 确认包含 equirectangular sampling token。
- 确认包含 face index / face direction mapping token。
- 确认没有使用 sRGB/gamma 编码。

### Runtime Smoke

建议手工验证：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

如果补充了 debug fixture：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/debug_latlong_32x16.hdr
```

如果 Phase 0.32 不接入可视化 skybox，那么 runtime smoke 只能验证不崩溃，方向正确性需要后续 skybox 或 readback/pixel test 才能严格验证。

## 实施顺序

### 0.32.0 文档与范围确认

目标：

- 新增 `docs/phase/phase32.md`。
- 明确本阶段只做 equirectangular -> cubemap conversion。
- 明确不做完整 IBL。
- 明确需要小型 debug HDR fixture。

审核点：

- 不提前进入 irradiance / prefilter / BRDF LUT。
- 不替换 ForwardPass 当前 equirectangular ambient。
- 不引入 RenderGraph / bindless。

### 0.32.1 Cubemap Face Render Views

目标：

- 扩展 `EnvironmentCubeResource`。
- 创建 6 个 face render target views。
- texture usage 增加 `RenderTarget`。
- tests 覆盖 desc 和生命周期。

审核点：

- cube sampled view 不回退。
- face views releaseDeferred / resetImmediate 正确释放。
- Vulkan view mapping 支持单 face view。

### 0.32.2 Conversion Shader

目标：

- 新增 fullscreen triangle vertex shader。
- 新增 equirectangular-to-cube fragment shader。
- 新增 CMake shader compile entry。
- shader asset smoke 覆盖关键 token。

审核点：

- shader 输出 linear HDR。
- 不做 tone mapping。
- 不做 gamma encode。
- face orientation 有明确注释。

### 0.32.3 EnvironmentCubeConverter

目标：

- 新增 renderer 层 converter。
- 创建 descriptor set layout / pipeline layout / pipeline。
- 创建 per-face uniform buffer 或可复用 uniform buffer。
- 将 source environment sampled image / sampler 绑定到 conversion shader。
- 对 6 个 face 依次 beginRendering -> draw fullscreen triangle -> endRendering。

审核点：

- conversion 命令在 dynamic rendering scope 中只包含 render commands。
- source environment 必须已上传。
- target cubemap 必须有效。
- 每个 face render target view 只覆盖单 layer。

### 0.32.4 Minimal Integration Path

目标：

- 选择一个最小接入口触发 conversion。
- 推荐先只在 renderer prepare/resource setup 阶段触发一次，而不是每帧转换。
- 如果接入成本过高，本阶段可以先只完成 converter + fake tests，runtime smoke 保持现状。

审核点：

- 不让 conversion 每帧重复执行。
- 不改变现有 lighting 画面路径。
- 失败时 fallback 到现有 equirectangular path。

### 0.32.5 Tests

目标：

- 更新 `environment_cube_resource_smoke`。
- 新增 `equirectangular_to_cube_smoke`。
- 更新 `shader_assets_smoke`。
- 更新 `framework_headers_smoke`。
- 保持 `forward_pass_pipeline_smoke` 通过。

审核点：

- tests 不依赖 98MB HDRI。
- fake RHI tests 不需要真实 Vulkan。
- 如使用 debug HDR fixture，必须体积小且可提交。

### 0.32.6 验证与收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - diffuse irradiance
  - prefiltered specular
  - BRDF LUT
  - skybox
  - specular IBL
  - cubemap mip chain / roughness sampling

建议验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

## 审核检查点

- `EnvironmentCubeResource` 有 6 个 face render target views。
- cubemap texture usage 包含 render target 和 shader resource。
- equirectangular -> cubemap shader 输出 linear HDR。
- face order 和 face orientation 有文档记录。
- converter 不接触 Vulkan 类型。
- conversion 不替换 ForwardPass 的 equirectangular ambient path。
- tests 不依赖大型 HDRI。
- build、CTest、sandbox smoke 通过。
- 文档明确仍未进入完整 IBL。

## 当前实现状态

Phase 0.32 已完成 `0.32.0 文档与范围确认` ~ `0.32.6 验证与收尾`：

- `EnvironmentCubeResource` 已扩展 6 个 per-face render target views。
- cubemap texture usage 已扩展为 `RenderTarget | ShaderResource`。
- Vulkan `Texture2D` view 校验已收窄为单 array layer，用于明确 cubemap 单 face render target view 语义。
- 新增 `shaders/equirect_to_cube.vert.hlsl` / `shaders/equirect_to_cube.frag.hlsl`。
- 新增 `EnvironmentCubeConverter`，通过 RHI descriptor / pipeline / dynamic rendering 将 equirectangular `EnvironmentResource` 转换到 `EnvironmentCubeResource`。
- 默认 renderer 已新增 minimal integration path：默认 HDR environment 上传后触发一次 conversion，失败时保留既有 equirectangular ForwardPass path。
- `ForwardPass` 当前仍然采样 equirectangular environment，不切换到 cubemap。
- 新增 `ark_equirectangular_to_cube_smoke`，覆盖 descriptor layout、pipeline state、6 face rendering、per-face uniform、barrier 和 draw count。
- 更新 `ark_environment_cube_resource_smoke`、`ark_shader_assets_smoke`、`ark_framework_headers_smoke`。

### 验证记录

本轮在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_equirectangular_to_cube_smoke ark_environment_cube_resource_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
configure passed
targeted smoke build passed
ark_equirectangular_to_cube_smoke passed
ark_environment_cube_resource_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
full build passed
CTest: 13/13 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

`ark_environment_cube_resource_smoke` 中出现的 error log 是测试刻意触发非法输入路径，用于验证拒绝行为，进程退出码为 0。runtime smoke 只验证真实 Vulkan 路径不会启动即退出；由于 Phase 0.32 尚未接入 skybox 或 readback，cubemap face orientation 仍需后续可视化或像素测试严格验证。

## 完成标准

Phase 0.32 完成时应满足：

- renderer 能从 `EnvironmentResource` 转换得到 `EnvironmentCubeResource`。
- conversion shader / pipeline / descriptor layout 有测试覆盖。
- 6 个 cubemap faces 都有明确 render target view 和 draw path。
- 小型 debug HDR fixture 策略明确；如果加入 fixture，则自动测试可读取它。
- Phase 0.30 equirectangular ambient lighting 不回退。
- 文档和 handoff 记录当前已支持 equirectangular -> cubemap conversion，但仍未支持 irradiance、prefilter、BRDF LUT、skybox、specular IBL 和 roughness mip sampling。

## 后续 Phase 建议

Phase 0.32 后建议进入：

1. Cubemap debug skybox / face orientation visual validation。
2. Diffuse irradiance map。
3. Prefiltered specular environment map。
4. BRDF LUT。
5. ForwardPass 切换到 cubemap-based IBL。
6. bloom / auto exposure / ACES tone mapping。
