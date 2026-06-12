# Phase 0.33 Cubemap Debug Skybox and Face Orientation Validation

## 阶段判断

Phase 0.32 已经完成 equirectangular HDR 到 cubemap 的 GPU conversion foundation：

```text
EnvironmentResource
    -> EnvironmentCubeConverter
    -> EnvironmentCubeResource
        -> cube sampled view
        -> 6 face render target views
```

这意味着 renderer 已经能在默认 HDR environment path 中生成 cubemap，但当前 cubemap 还没有进入可见渲染结果。`ForwardPass` 仍然直接采样 equirectangular 2D environment texture，`SkyboxPass` 也仍只是占位头文件。

下一阶段最稳的方向不是马上进入 irradiance / prefilter / BRDF LUT，而是先把 Phase 0.32 生成的 cubemap 画出来，用 debug skybox 验证：

- cubemap 是否真的可被 shader 采样。
- face order 是否符合 `+X -X +Y -Y +Z -Z`。
- 上下、左右、前后是否存在翻转。
- equirectangular -> cubemap conversion 是否存在明显 seam / orientation 问题。
- 默认 HDR environment path 在真实 Vulkan runtime 下是否形成可见背景。

这一步仍然不是完整 IBL。它是后续 diffuse irradiance、prefiltered specular、BRDF LUT、skybox productization 和 cubemap-based ForwardPass IBL 的可视化验证层。

## 目标

Phase 0.33 目标：

- 新增可执行的 `SkyboxPass`，替换当前占位实现。
- 新增 cubemap skybox shaders。
- 将 Phase 0.32 生成的 `EnvironmentCubeResource` 传入 frame rendering path。
- 在 scene color pass 中绘制 debug skybox background。
- 使用 `TextureCube<float4>` 采样 cubemap。
- 使用 `RenderView` 的 camera rotation / projection 重建 skybox sample direction。
- 保持 skybox 在 scene pass 内、tone mapping 前输出 linear HDR color。
- 明确 skybox pass 顺序、depth policy 和 fallback 行为。
- 增加 smoke tests 覆盖 descriptor layout、pipeline desc、cube texture binding、uniform update 和 fullscreen draw。
- 用 runtime smoke 验证默认场景、DamagedHelmet 和本地 HDR environment 路径不启动即退出。
- 明确 face orientation 仍需要人工视觉确认或后续 readback / pixel test 才能严格自动化。

## 非目标

Phase 0.33 暂不做：

- 不做 diffuse irradiance map。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 specular IBL。
- 不做 roughness-based mip sampling。
- 不把 `ForwardPass` 的 ambient lighting 改成 cubemap IBL。
- 不做 physically correct sky atmosphere。
- 不做 skybox exposure UI。
- 不做 skybox tint / rotation / intensity editor。
- 不做 HDR cubemap mip chain。
- 不做 cubemap seam fixup / edge filtering。
- 不做 GPU readback pixel test，除非作为额外验证补充。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不做 KTX / DDS / EXR loader。
- 不提交大型真实 HDRI。

## 当前基线

### EnvironmentCubeResource

当前 `EnvironmentCubeResource` 已具备：

```text
TextureType::Cube
TextureUsage::RenderTarget | TextureUsage::ShaderResource
TextureViewType::Cube sampled view
6 x TextureViewType::Texture2D face render target view
Sampler
```

其中 cube sampled view 是 skybox pass 最直接需要的资源；6 个 face render target views 只服务于 conversion 和后续 cubemap bake pass。

### EnvironmentCubeConverter

当前 `EnvironmentCubeConverter` 已完成：

```text
source EnvironmentResource must be uploaded
target EnvironmentCubeResource must be valid
target cubemap -> RenderTarget
for face in 0..5:
    beginRendering(faceRenderTargetView(face))
    bind equirectangular source
    draw fullscreen triangle
target cubemap -> ShaderResource
```

这条路径现在只在默认 renderer 的默认 HDR environment 中触发一次。它尚未暴露给任意 `RenderScene` 或 asset system。

### ForwardPass

当前 `ForwardPass` 仍然使用：

```hlsl
Texture2D<float4> g_EnvironmentTexture;
SamplerState g_EnvironmentSampler;
```

并通过 `directionToEquirectUv()` 采样 equirectangular 2D environment。Phase 0.33 不改这条 lighting path。

### SkyboxPass

当前 `SkyboxPass` 是占位头文件：

```cpp
class SkyboxPass : public RenderPass {
public:
    bool execute(FrameContext& frameContext) override;
};
```

需要新增 `.cpp`、descriptor / shader / pipeline resources、per-frame uniform buffer 和 smoke test。

## 建议设计

### FrameContext 数据入口

建议在 `FrameContext` 中新增非拥有指针：

```cpp
class EnvironmentCubeResource;

struct FrameContext {
    EnvironmentCubeResource* environmentCube = nullptr;
};
```

语义：

- `FrameContext` 不拥有 cubemap。
- `Renderer` 负责判断默认 cubemap 是否转换完成。
- 如果没有可用 cubemap，`environmentCube == nullptr`，`SkyboxPass` 直接 no-op。
- 先只支持默认 renderer 内部 cubemap；不扩展 `RenderScene` environment API。

`DefaultRenderer::render()` 建议：

```cpp
prepareDefaultEnvironmentCube(context, renderScene);
frameContext.environmentCube = m_DefaultEnvironmentCubeConverted ? &m_DefaultEnvironmentCube : nullptr;
```

### Pass 顺序

建议 scene pass 顺序改为：

```text
ClearPass
SkyboxPass
ForwardPass
```

原因：

- skybox 先写入 HDR scene color，作为背景。
- `ForwardPass` 后绘制 opaque / mask / blend geometry，正常覆盖 skybox。
- skybox 不写 depth，避免污染后续 geometry depth。
- 仍在 tone mapping 前输出 linear HDR。

`FrameRenderer` 当前 scene rendering scope 已包含 color + depth attachments。`SkyboxPass` 不需要自己 begin/end rendering，只需要在现有 scene rendering scope 内执行 draw。

### SkyboxPass Resource Layout

建议 `SkyboxPass` 使用 per-frame descriptor set：

```text
binding 0: SkyboxUniform uniform buffer
binding 1: cubemap sampled image
binding 2: cubemap sampler
```

uniform 建议：

```cpp
struct alignas(16) SkyboxUniform {
    glm::mat4 inverseProjection;
    glm::mat4 inverseViewRotation;
    glm::vec4 settings; // x = intensity, yzw reserved
};
```

说明：

- `inverseProjection` 用于把 fullscreen clip / NDC 坐标还原到 view direction。
- `inverseViewRotation` 只包含 camera rotation，不包含 translation，保证 skybox 不随相机位置平移。
- `settings.x` 初期固定为 1.0。
- 后续可以把 intensity 接入 `SceneEnvironment::intensity`，但 Phase 0.33 可以先保持最小。

### Shader

建议新增：

```text
shaders/skybox.vert.hlsl
shaders/skybox.frag.hlsl
```

vertex shader：

- fullscreen triangle。
- 输出 uv 或 ndc。

fragment shader：

```hlsl
[[vk::binding(0, 0)]]
ConstantBuffer<SkyboxUniform> g_Skybox;

[[vk::binding(1, 0)]]
TextureCube<float4> g_SkyboxCube;

[[vk::binding(2, 0)]]
SamplerState g_SkyboxSampler;
```

方向重建建议：

```hlsl
float2 ndc = input.uv * 2.0f - 1.0f;
float4 clip = float4(ndc, 1.0f, 1.0f);
float4 view = mul(g_Skybox.inverseProjection, clip);
float3 viewDirection = normalize(view.xyz / view.w);
float3 worldDirection = normalize(mul((float3x3)g_Skybox.inverseViewRotation, viewDirection));
float3 color = g_SkyboxCube.Sample(g_SkyboxSampler, worldDirection).rgb * g_Skybox.settings.x;
return float4(color, 1.0f);
```

注意：

- HLSL 现有代码使用 `mul(matrix, vector)` 风格，skybox shader 应保持一致。
- 由于 renderer projection 已处理 Vulkan Y 翻转，skybox shader 不应额外偷偷翻转 Y，除非视觉验证发现方向错误。
- 输出必须保持 linear HDR，不做 tone mapping / gamma。

### Pipeline State

建议 pipeline：

```cpp
pipelineDesc.topology = TriangleList;
pipelineDesc.rasterState.cullMode = CullMode::None;
pipelineDesc.depthStencilState.enableDepthTest = false;
pipelineDesc.depthStencilState.enableDepthWrite = false;
pipelineDesc.colorFormat = frameContext.colorFormat; // RGBA16Float
pipelineDesc.depthFormat = frameContext.depthFormat; // D32Float, 因 scene rendering scope 绑定了 depth attachment
pipelineDesc.blendState.colorAttachment.enableBlend = false;
```

虽然 skybox 不启用 depth test/write，但当前 scene rendering scope 有 depth attachment。为保持 Vulkan dynamic rendering pipeline format 对齐，建议 pipeline desc 仍填入当前 depth format。

### Fallback 行为

`SkyboxPass::execute()` 建议：

```text
if no DeviceContext:
    error false

if no environmentCube:
    return true

if environmentCube invalid:
    return true or warning + true

bind pipeline
bind descriptor set
update uniform
draw fullscreen triangle
```

这里建议缺失 cubemap 时 no-op，而不是报错。原因是默认 scene 仍允许没有 HDR environment。

## Debug HDR Fixture 策略

Phase 0.33 最好补一个可提交的小型 debug HDR fixture：

```text
assets/HDR/debug_latlong_32x16.hdr
```

用途：

- 明确 face orientation。
- 明确 top / bottom。
- 明确 horizon。
- 避免依赖 98MB 本地真实 HDRI。

建议图案：

```text
+X: red
-X: cyan
+Y: green / bright top marker
-Y: magenta / dark bottom marker
+Z: blue
-Z: yellow
horizon: thin neutral line
```

如果本阶段暂不生成 fixture，也必须继续支持手工验证：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

真实 HDRI 仍不提交。`assets/HDR/*.hdr` 默认受 `.gitignore` 保护，只有 `debug_*.hdr` 允许作为小 fixture 入库。

## 测试策略

### SkyboxPass Fake RHI Smoke

建议新增：

```text
tests/skybox_pass_smoke.cpp
```

覆盖：

- `SkyboxPass::setup()` 创建 descriptor set layout。
- descriptor layout 包含：
  - binding 0 uniform buffer fragment
  - binding 1 sampled image fragment
  - binding 2 sampler fragment
- 创建 per-frame uniform buffers / descriptor sets。
- shader modules 加载 `skybox.vert.spv` / `skybox.frag.spv`。
- pipeline 使用 frame context color/depth format。
- cull mode none。
- depth test/write disabled。
- no blend。
- `environmentCube == nullptr` 时 no-op 且不 draw。
- `environmentCube` valid 时：
  - update uniform。
  - bind cube sampled view。
  - bind cubemap sampler。
  - draw fullscreen triangle once。

### Shader Asset Smoke

扩展 `tests/shader_assets_smoke.cpp`：

- 编译并加载 `skybox.vert.spv`。
- 编译并加载 `skybox.frag.spv`。
- source token 覆盖：
  - `TextureCube<float4>`
  - `g_SkyboxCube`
  - `inverseProjection`
  - `inverseViewRotation`
  - `Sample`
  - no `pow`
  - no `linearToOutput`
  - no `applyToneMapping`

### Framework Headers Smoke

扩展 `tests/framework_headers_smoke.cpp`：

- include `renderer/passes/SkyboxPass.h` 已有，可触碰新 public API。
- touch `FrameContext::environmentCube`。
- touch `EnvironmentCubeResource::textureView()` / `sampler()` 作为 skybox sampled input。

### Runtime Smoke

建议验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_skybox_pass_smoke ark_shader_assets_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

如果加入 debug fixture，再验证：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/debug_latlong_32x16.hdr
```

## 实施顺序

### 0.33.0 文档与范围确认

目标：

- 新增 `docs/phase/phase33.md`。
- 明确本阶段只做 debug skybox / orientation validation。
- 明确不进入 irradiance / prefilter / BRDF LUT / ForwardPass IBL。

审核点：

- 不扩大到完整 IBL。
- 不改 `ForwardPass` lighting path。
- 不引入 RenderGraph / bindless。

### 0.33.1 FrameContext Cubemap Input

目标：

- `FrameContext` 增加 `EnvironmentCubeResource* environmentCube`。
- `DefaultRenderer` 在 conversion 成功后传入默认 cubemap。
- 缺失 cubemap 时保持 nullptr。

审核点：

- `FrameContext` 不拥有资源。
- 不把 renderer 私有默认 cubemap 暴露成全局状态。
- 不要求所有 scene 都必须有 cubemap。

### 0.33.2 Skybox Shader

目标：

- 新增 `skybox.vert.hlsl`。
- 新增 `skybox.frag.hlsl`。
- CMake 接入 shader 编译。
- shader asset smoke 覆盖关键 token。

审核点：

- 使用 `TextureCube<float4>`。
- 输出 linear HDR。
- 不做 tone mapping / gamma。
- 不额外翻转方向，除非视觉验证明确要求。

### 0.33.3 SkyboxPass

目标：

- 实现 `SkyboxPass.h/.cpp`。
- 创建 descriptor resources。
- 创建 shader resources。
- 创建 pipeline layout / pipeline。
- 每帧更新 skybox uniform。
- 绑定 cubemap sampled image / sampler。
- fullscreen draw。

审核点：

- no cubemap 时 no-op。
- pass 在 scene rendering scope 中执行，不自己 begin/end rendering。
- pipeline color/depth format 与当前 scene render scope 对齐。
- 不写 depth。

### 0.33.4 FrameRenderer 接入

目标：

- `DefaultFrameRenderer` 新增 `SkyboxPass`。
- scene pass 顺序调整为 `ClearPass -> SkyboxPass -> ForwardPass`。
- setup / prepare / execute 顺序保持现有 pass 框架。

审核点：

- `ForwardPass` 仍然在 skybox 后执行。
- scene color barrier / depth barrier 不变。
- tone mapping path 不变。

### 0.33.5 Tests

目标：

- 新增 `ark_skybox_pass_smoke`。
- 更新 `shader_assets_smoke`。
- 更新 `framework_headers_smoke`。
- 保持 `ark_equirectangular_to_cube_smoke` 和 `ark_forward_pass_pipeline_smoke` 通过。

审核点：

- fake RHI test 不依赖真实 Vulkan。
- shader smoke 依赖 `ark_shaders`。
- no-cubemap path 和 valid-cubemap path 都覆盖。

### 0.33.6 验证与收尾

目标：

- 更新本文档实现状态和验证记录。
- 更新 `docs/codex_handoff.md`。
- 记录仍未支持：
  - diffuse irradiance
  - prefiltered specular
  - BRDF LUT
  - specular IBL
  - cubemap roughness mip sampling
  - automated face orientation pixel test

审核点：

- build 通过。
- CTest 通过。
- runtime smoke 通过。
- 如果补 debug HDR fixture，记录视觉方向验证结果。
- 文档明确 Phase 0.33 仍不是完整 IBL。

## 审核检查点

- `SkyboxPass` 能 no-op 处理缺失 cubemap。
- skybox shader 使用 `TextureCube<float4>`。
- skybox 输出 linear HDR，仍由 `ToneMappingPass` 统一 tone map。
- skybox pass 在 `ForwardPass` 前绘制。
- skybox 不写 depth。
- `ForwardPass` equirectangular ambient path 不回退。
- `EnvironmentCubeConverter` 仍只负责 conversion，不承担 skybox 绘制。
- tests 不依赖大型 HDRI。
- runtime smoke 不启动即退出。

## 当前实现状态

Phase 0.33 已完成 `0.33.0 文档与范围确认` ~ `0.33.6 验证与收尾`：

- `FrameContext` 已新增非拥有 `EnvironmentCubeResource* environmentCube`。
- `DefaultRenderer` 已在默认 environment cubemap conversion 成功后把 cubemap 传入 frame context。
- 默认 sandbox environment 策略已调整：默认无参数时优先加载 `assets/HDR/2k.hdr`，找不到则使用程序化 64x32 RGBA32F HDR sky gradient，避免默认打开只有空背景。
- 新增 `shaders/skybox.vert.hlsl` / `shaders/skybox.frag.hlsl`。
- `SkyboxPass` 已从占位头文件实现为真实 pass，支持 no-cubemap no-op、per-frame uniform、cube sampled image / sampler binding、pipeline cache 和 fullscreen draw。
- `FrameRenderer` scene pass 顺序已调整为 `ClearPass -> SkyboxPass -> ForwardPass`。
- 新增 `ark_skybox_pass_smoke`，覆盖 no-cubemap path、cubemap draw path、descriptor layout、pipeline desc、uniform update、cube binding 和 draw count。
- `shader_assets_smoke` 已覆盖 skybox SPIR-V 和 shader source token。
- `framework_headers_smoke` 已覆盖 `FrameContext::environmentCube`。
- `ForwardPass` 仍然使用 equirectangular environment ambient path，未切换到 cubemap IBL。

### 验证记录

本轮在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_skybox_pass_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_forward_pass_pipeline_smoke
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
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
ark_skybox_pass_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_equirectangular_to_cube_smoke passed
full build passed
CTest: 14/14 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

runtime smoke 使用隐藏窗口启动后自动停止，用于确认默认模型、默认/procedural environment、cubemap conversion、SkyboxPass、ForwardPass 和 ToneMappingPass 串联后不会启动即退出。方向正确性仍需后续视觉检查或 readback/pixel test 严格验证。

## 完成标准

Phase 0.33 完成时应满足：

- 默认 HDR environment conversion 成功后，skybox 能采样 generated cubemap。
- 缺失 environment / cubemap 时 renderer 正常渲染，不报错退出。
- skybox shader / pass / pipeline / descriptor layout 有 smoke test 覆盖。
- Phase 0.32 的 equirectangular -> cubemap conversion path 不回退。
- Phase 0.30 的 ForwardPass equirectangular ambient path 不回退。
- 文档和 handoff 记录 skybox 已可用于 cubemap orientation visual validation，但仍未支持 irradiance、prefilter、BRDF LUT、specular IBL 和 roughness mip sampling。

## 后续 Phase 建议

Phase 0.33 后建议进入：

1. Diffuse irradiance map generation。
2. ForwardPass diffuse cubemap ambient IBL。
3. Prefiltered specular environment map。
4. BRDF LUT。
5. ForwardPass specular IBL / roughness mip sampling。
6. bloom / auto exposure / ACES tone mapping。
