# Phase 0.34 Diffuse Irradiance Cubemap Foundation

## 阶段判断

Phase 0.33 已经完成 cubemap debug skybox 接入：

```text
EnvironmentResource
    -> EnvironmentCubeConverter
    -> EnvironmentCubeResource
        -> SkyboxPass
        -> visible HDR cubemap background
```

这说明当前 renderer 已经具备三个关键前提：

- 可以加载或生成 HDR equirectangular environment。
- 可以把 equirectangular environment 转成 cubemap。
- 可以在 scene color pass 中采样 cubemap，并经过 ToneMappingPass 显示到 backbuffer。

但 `ForwardPass` 的环境光仍然不是 cubemap IBL。当前 mesh shader 仍通过：

```hlsl
Texture2D<float4> g_EnvironmentTexture;
SamplerState g_EnvironmentSampler;
```

对 equirectangular 2D HDR 做一次方向采样，再乘以 albedo 作为 ambient contribution。这条路径能提供“环境色”，但不是物理意义上的 diffuse irradiance，也没有完成半球积分。

下一阶段最稳的方向是先生成 diffuse irradiance cubemap，再在后续阶段接入 `ForwardPass`。不要直接跳到完整 specular IBL；prefiltered specular、BRDF LUT、roughness mip sampling 都依赖更多资源格式、mip 语义和 shader binding 变化，过早推进会让问题面变大。

Phase 0.34 建议只做一件事：

```text
environment cubemap
    -> diffuse irradiance convolution
        -> low resolution irradiance cubemap
```

它是后续 `ForwardPass` diffuse cubemap ambient IBL 的资源基础。

## 目标

Phase 0.34 目标：

- 新增 `EnvironmentIrradianceGenerator`，用于从 environment cubemap 生成 low-res diffuse irradiance cubemap。
- 新增 irradiance convolution shaders。
- 复用当前 `EnvironmentCubeResource`，不引入新 RHI texture 类型。
- 使用 cubemap sampled view 作为输入。
- 使用 6 个 face render target views 作为输出。
- 输出 linear HDR irradiance，不做 tone mapping / gamma。
- 接入默认 renderer 的默认 environment bake 链路。
- 生成结果暂不接入 `ForwardPass` lighting。
- 增加 smoke tests 覆盖 pass resources、descriptor layout、pipeline desc、face draw、source cubemap binding 和 target state transition。
- 更新 handoff，明确当前完成 irradiance resource foundation，但 mesh lighting 仍未消费该 irradiance。

## 非目标

Phase 0.34 暂不做：

- 不把 `ForwardPass` ambient lighting 改成 irradiance cubemap。
- 不改 `mesh.frag.hlsl` 的 environment binding 14/15。
- 不做 specular IBL。
- 不做 prefiltered specular environment map。
- 不做 BRDF LUT。
- 不做 roughness-based mip sampling。
- 不做 HDR cubemap mip chain。
- 不做 compute shader convolution。
- 不做 GPU readback pixel test。
- 不做 RenderGraph 重构。
- 不做 bindless descriptor。
- 不提交大型真实 HDRI。
- 不在本阶段实现完整摄像机交互系统。

## 当前基线

### EnvironmentCubeResource

`EnvironmentCubeResource` 当前已经支持：

```text
TextureType::Cube
TextureUsage::RenderTarget | TextureUsage::ShaderResource
TextureViewType::Cube sampled view
6 x TextureViewType::Texture2D face render target view
Sampler
format: RGBA16Float / RGBA32Float
mipLevels >= 1
```

Phase 0.34 可以直接复用它作为 irradiance 输出资源。第一版建议：

```text
faceExtent = 32x32 或 64x64
format = RGBA16Float
mipLevels = 1
```

原因：

- diffuse irradiance 是低频信息，不需要高分辨率。
- `RGBA16Float` 对当前 HDR pipeline 足够。
- 单 mip 最小闭环更稳定，后续 specular prefilter 再处理 mip 语义。

### EnvironmentCubeConverter

当前 `EnvironmentCubeConverter` 已建立一个可复用模式：

```text
setup(device)
    -> descriptor set layout
    -> per-face uniform buffer / descriptor set
    -> shaders
    -> pipeline layout

convert(context, desc)
    -> target cube RenderTarget barrier
    -> for face in 0..5:
        beginRendering(face view)
        update face uniform
        bind source descriptors
        draw fullscreen triangle
        endRendering()
    -> target cube ShaderResource barrier
```

`EnvironmentIrradianceGenerator` 应沿用这个模式，减少新抽象。

### SkyboxPass

`SkyboxPass` 当前消费 `FrameContext::environmentCube`，用于可视化 cubemap background。

Phase 0.34 不改变 skybox 行为。它仍然显示原始 environment cubemap，而不是 irradiance cubemap。这样视觉上仍保留清晰 HDR 背景；irradiance cubemap 只作为 lighting resource foundation。

### ForwardPass

`ForwardPass` 当前仍消费 `EnvironmentResource` 的 equirectangular 2D texture。

Phase 0.34 不改变它，避免 bake resource 和 material lighting 同时变化。后续 Phase 0.35 再把 diffuse irradiance cubemap 接入 `ForwardPass`，并保留 equirectangular fallback。

## 建议设计

### EnvironmentIrradianceGenerator

建议新增：

```text
src/renderer/EnvironmentIrradianceGenerator.h
src/renderer/EnvironmentIrradianceGenerator.cpp
```

核心接口：

```cpp
struct EnvironmentIrradianceGenerationDesc {
    EnvironmentCubeResource* source = nullptr;
    EnvironmentCubeResource* target = nullptr;
    std::string debugName;
};

class EnvironmentIrradianceGenerator final {
public:
    void setup(rhi::RenderDevice& device);
    void resetImmediate();
    bool generate(rhi::DeviceContext& context, const EnvironmentIrradianceGenerationDesc& desc);
};
```

语义：

- `source` 必须是已完成 conversion 且处于可采样状态的 environment cubemap。
- `target` 必须是有效 cubemap，且具备 face render target views。
- generator 不拥有 source / target，只负责记录 draw 命令。
- 每次 generate 后把 target 转为 `ShaderResource`。
- source 和 target 不允许是同一个资源。

### Descriptor Layout

建议 descriptor layout：

```text
binding 0: IrradianceUniform uniform buffer
binding 1: source environment cube sampled image
binding 2: source environment cube sampler
```

uniform：

```cpp
struct alignas(16) IrradianceUniform {
    u32 faceIndex = 0;
    float sampleDelta = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};
```

第一版可以固定 `sampleDelta = 0.025f` 或类似值。后续如果性能需要，再把采样质量变成 renderer setting。

### Shader

建议新增：

```text
shaders/irradiance_convolve.vert.hlsl
shaders/irradiance_convolve.frag.hlsl
```

vertex shader：

- fullscreen triangle。
- 输出 uv。

fragment shader：

- 根据 `faceIndex` 和 uv 重建 face direction。
- 构建 tangent space 半球采样方向。
- 对 source cubemap 做 cosine-weighted 或显式半球积分。
- 输出 diffuse irradiance：

```hlsl
irradiance = PI * accumulatedRadiance / sampleCount;
```

第一版重点是正确性和稳定性，不追求最高性能。采样循环可以在 shader 中保持固定步长，方便 smoke test 检查 token 和行为意图。

注意：

- 使用 `TextureCube<float4>`。
- 输出 linear HDR。
- 不做 tone mapping。
- 不做 gamma。
- 不依赖 camera。

### Pipeline State

建议 pipeline：

```cpp
pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
pipelineDesc.rasterState.cullMode = rhi::CullMode::None;
pipelineDesc.depthStencilState.enableDepthTest = false;
pipelineDesc.depthStencilState.enableDepthWrite = false;
pipelineDesc.colorFormat = target->format();
pipelineDesc.blendState.colorAttachment.enableBlend = false;
```

这个 pass 自己 begin/end rendering 每个 cubemap face，不依赖 `FrameRenderer` scene rendering scope。

### Renderer 默认链路

当前默认 renderer 已有：

```cpp
EnvironmentResource m_DefaultEnvironment;
EnvironmentCubeResource m_DefaultEnvironmentCube;
EnvironmentCubeConverter m_EnvironmentCubeConverter;
bool m_DefaultEnvironmentCubeConverted = false;
```

Phase 0.34 建议新增：

```cpp
EnvironmentCubeResource m_DefaultIrradianceCube;
EnvironmentIrradianceGenerator m_EnvironmentIrradianceGenerator;
bool m_DefaultIrradianceCubeGenerationAttempted = false;
bool m_DefaultIrradianceCubeGenerated = false;
```

默认链路：

```text
createDefaultEnvironment()
    -> create m_DefaultEnvironmentCube
    -> create m_DefaultIrradianceCube

render()
    -> prepareDefaultEnvironmentCube()
    -> prepareDefaultIrradianceCube()
    -> FrameRenderer render()
```

但 `FrameContext` 暂时不需要新增 `irradianceCube`。如果本阶段不接入 `ForwardPass`，就不要把资源传到 frame pass，避免暴露未消费的 API。

### Failure Policy

建议失败策略：

- source cubemap 不存在：no-op，返回 true 或 warn 后返回 true。
- target 创建失败：renderer 继续使用现有 skybox 和 equirectangular ForwardPass。
- generate 失败：warn，不影响主渲染。
- smoke test 中非法 desc 应返回 false。

原因：irradiance 当前只是后续资源基础，不应该让默认 sandbox 因 irradiance bake 失败而无法启动。

## 摄像机交互判断

现在确实需要考虑摄像机交互，但不建议把它塞进 Phase 0.34。

当前 `Application` 每帧使用一个固定 `RenderView`：

```text
RenderView view;
view.setDefaultPerspective(currentExtent);
renderer->render(scene, view);
```

这导致：

- 无法绕模型检查 normal、roughness、metallic 的视角响应。
- 无法方便验证 skybox/cubemap orientation。
- DamagedHelmet 或后续真实场景只能从默认角度观察。
- 以后调试 IBL、tone mapping、bloom、shadow 时反馈很慢。

所以摄像机交互是需要的，尤其是从“能渲染”走向“能调试画面”时。但它不属于 renderer core IBL 的硬前置。更好的拆法是单独做一个小阶段：

```text
Phase 0.35 或 Phase 0.34A: Sandbox Camera Controller
```

推荐设计：

- `RenderView` 继续保持纯 view/projection 数据载体。
- 不急着做完整 ECS camera component。
- 在 app/sandbox 层新增轻量 `CameraController` 或 `SandboxCameraController`。
- controller 持有 position / yaw / pitch / distance / fov / near / far。
- controller 根据窗口输入更新 `RenderView`。
- `renderer/` 不依赖 GLFW。
- `Window` 或 app 层提供最小输入查询接口。

第一版交互建议：

```text
Orbit mode:
    right mouse drag: rotate around target
    mouse wheel: dolly / zoom
    middle mouse drag: pan

Free-look optional:
    WASD: move
    right mouse drag: look
    Shift: faster
```

优先做 orbit mode。它更适合查看默认模型、DamagedHelmet 和材质/IBL 效果。

不建议现在做：

- 不做完整 editor camera framework。
- 不做 scene graph camera entity。
- 不做多 camera 管理。
- 不做 input action mapping 系统。
- 不让 renderer 直接读取 keyboard / mouse。

结论：

- 如果下一步目标是推进 PBR/IBL，Phase 0.34 仍应先做 irradiance cubemap foundation。
- 如果下一步目标是提升调试效率和视觉验证，摄像机交互可以插队成为 Phase 0.34A。
- 摄像机交互迟早要做，而且越到 IBL/阴影/后处理阶段越有价值；但第一版应控制在 app/sandbox 层，不要污染 renderer 分层。

## 测试策略

### Environment Irradiance Smoke

建议新增：

```text
tests/environment_irradiance_smoke.cpp
```

覆盖：

- `EnvironmentIrradianceGenerator::setup()` 创建 descriptor layout。
- descriptor layout 包含：
  - binding 0 uniform buffer fragment
  - binding 1 sampled image fragment
  - binding 2 sampler fragment
- 每个 face 有独立 uniform buffer / descriptor set。
- shader modules 加载 `irradiance_convolve.vert.spv` / `irradiance_convolve.frag.spv`。
- pipeline color format 使用 target cubemap format。
- depth test/write disabled。
- cull mode none。
- source cubemap descriptor 正确绑定。
- target cubemap 进入 RenderTarget 后渲染 6 个 face。
- target cubemap 最终进入 ShaderResource。
- 每个 face draw fullscreen triangle 一次。

### Shader Asset Smoke

扩展 `tests/shader_assets_smoke.cpp`：

- 验证 compiled SPIR-V：
  - `irradiance_convolve.vert.spv`
  - `irradiance_convolve.frag.spv`
- 验证 source token：
  - `TextureCube<float4>`
  - `g_SourceEnvironmentCube`
  - `faceUvToDirection`
  - `sampleDelta`
  - `cos`
  - `sin`
  - `PI`
  - no `linearToOutput`
  - no `applyToneMapping`

### Framework Headers Smoke

扩展 `tests/framework_headers_smoke.cpp`：

- include `renderer/EnvironmentIrradianceGenerator.h`。
- touch `EnvironmentIrradianceGenerationDesc`。
- touch source / target `EnvironmentCubeResource` 指针。

### Runtime Smoke

建议验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_environment_irradiance_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --preset msvc-vcpkg-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

runtime smoke 仍用于确认：

- 默认 procedural environment path 不启动即退出。
- 默认 environment cube conversion 不回退。
- irradiance generation 不破坏 skybox / ForwardPass / ToneMappingPass。
- DamagedHelmet 路径仍可启动。

## 实施顺序

### 0.34.0 文档与范围确认

目标：

- 新增 `docs/phase/phase34.md`。
- 明确本阶段只做 diffuse irradiance cubemap resource foundation。
- 明确暂不改 `ForwardPass` lighting。
- 明确摄像机交互需要做，但不放入本阶段主线。

审核点：

- 不扩大到完整 IBL。
- 不提前引入 prefilter / BRDF LUT。
- 不把 app camera controller 和 renderer IBL bake 混在一起。

### 0.34.1 Irradiance Shader

目标：

- 新增 `irradiance_convolve.vert.hlsl`。
- 新增 `irradiance_convolve.frag.hlsl`。
- CMake 接入 shader 编译。
- shader asset smoke 覆盖 source token 和 SPIR-V。

审核点：

- 使用 `TextureCube<float4>`。
- 输出 linear HDR。
- 不做 tone mapping / gamma。
- face direction 与 equirect-to-cube / skybox 约定保持一致。

### 0.34.2 EnvironmentIrradianceGenerator

目标：

- 新增 `EnvironmentIrradianceGenerator.h/.cpp`。
- 创建 descriptor set layout。
- 创建 per-face uniform buffers / descriptor sets。
- 创建 shader modules。
- 创建 pipeline layout / pipeline。
- 实现 `generate()`。

审核点：

- source 必须是 valid cubemap。
- target 必须是 valid cubemap。
- source != target。
- 每个 face render scope 独立 begin/end。
- target 最终转为 ShaderResource。

### 0.34.3 Renderer Default Bake Path

目标：

- 默认 renderer 创建 low-res `m_DefaultIrradianceCube`。
- 默认 renderer setup `m_EnvironmentIrradianceGenerator`。
- 在 default environment cube conversion 成功后生成 irradiance cubemap。
- 记录 generated / attempted 状态。

审核点：

- irradiance generation 失败不阻断渲染。
- 不把 irradiance cube 暴露到 `FrameContext`。
- 不改变 skybox 显示 source cubemap。
- 不改变 `ForwardPass` equirectangular ambient。

### 0.34.4 Tests

目标：

- 新增 `ark_environment_irradiance_smoke`。
- 更新 `shader_assets_smoke`。
- 更新 `framework_headers_smoke`。
- 保持 existing skybox / equirectangular-to-cube / forward pass tests 通过。

审核点：

- fake RHI test 不依赖真实 Vulkan。
- 不依赖大型 HDRI。
- 覆盖 no-op / invalid desc / valid generate path。

### 0.34.5 验证与收尾

目标：

- 完整 build。
- CTest 全量通过。
- runtime smoke 通过。
- 更新 `docs/codex_handoff.md`。
- 文档记录 Phase 0.34 完成后仍未支持：
  - ForwardPass diffuse cubemap IBL
  - specular IBL
  - prefiltered specular
  - BRDF LUT
  - roughness mip sampling
  - camera interaction

审核点：

- 不把 irradiance generation 写成已经影响最终 mesh lighting。
- 不把 runtime smoke 写成视觉正确性自动验证。
- 清楚记录下一阶段建议。

## 当前实现状态

Phase 0.34 已完成 `0.34.0 文档与范围确认` ~ `0.34.5 验证与收尾`：

- 新增 `shaders/irradiance_convolve.vert.hlsl` / `shaders/irradiance_convolve.frag.hlsl`，使用 fullscreen triangle 和 `TextureCube<float4>` 做 diffuse irradiance convolution。
- CMake 已接入 irradiance shader 编译，输出 `irradiance_convolve.vert.spv` / `irradiance_convolve.frag.spv`。
- 新增 `EnvironmentIrradianceGenerator`，沿用 `EnvironmentCubeConverter` 的 per-face dynamic rendering 模式。
- `EnvironmentIrradianceGenerator::generate()` 支持 source cubemap -> target irradiance cubemap，覆盖 source/target 校验、source != target、per-face uniform、descriptor update、6 face draw 和 target ShaderResource barrier。
- 默认 renderer 已创建 `DefaultSandboxIrradianceCube`，并在默认 environment cubemap conversion 成功后生成 32x32 RGBA16F diffuse irradiance cubemap。
- 默认 renderer 的 irradiance generation 失败只记录 warning，不阻断 skybox / ForwardPass / ToneMappingPass。
- 默认 renderer 暂未把 irradiance cubemap 暴露到 `FrameContext`，也未接入 `ForwardPass`。
- `ForwardPass` 仍然使用 equirectangular 2D environment ambient path。
- 新增 `ark_environment_irradiance_smoke`。
- `shader_assets_smoke` 已覆盖 irradiance shader SPIR-V 和 source token。
- `framework_headers_smoke` 已覆盖 `EnvironmentIrradianceGenerator` public API。

### 验证记录

本轮在 Windows/MSVC/vcpkg/DXC debug preset 下完成验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target ark_environment_irradiance_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_equirectangular_to_cube_smoke ark_skybox_pass_smoke
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_skybox_pass_smoke.exe
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
ark_environment_irradiance_smoke passed
ark_shader_assets_smoke passed
ark_framework_headers_smoke passed
ark_equirectangular_to_cube_smoke passed
ark_skybox_pass_smoke passed
full build passed
CTest: 15/15 passed
default sandbox smoke passed
DamagedHelmet sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

`ark_environment_irradiance_smoke` 中的 `InvalidSameCubeIrradiance` error log 是测试刻意触发非法输入路径，用于验证 `source == target` 会被拒绝，进程退出码为 0。runtime smoke 仅证明默认 renderer 的 environment conversion、irradiance generation、SkyboxPass、ForwardPass 和 ToneMappingPass 串联后不会启动即退出，不代表 irradiance 视觉正确性或最终 mesh lighting 已切换到 cubemap IBL。

## 完成标准

Phase 0.34 完成时应满足：

- 默认 renderer 能在 environment cubemap conversion 后生成 low-res irradiance cubemap。
- irradiance generator 有独立 smoke test 覆盖。
- shader asset smoke 覆盖 irradiance shaders。
- skybox path 不回退。
- equirectangular-to-cube path 不回退。
- ForwardPass equirectangular ambient path 不回退。
- build / CTest / runtime smoke 通过。
- handoff 明确当前只是 diffuse irradiance resource foundation。

## 后续 Phase 建议

Phase 0.34 后建议二选一：

1. 如果继续推进 PBR/IBL：

```text
Phase 0.35 ForwardPass Diffuse Irradiance IBL
    -> FrameContext 或 RenderScene 暴露 irradiance cubemap
    -> ForwardPass 新增 TextureCube irradiance binding
    -> mesh.frag.hlsl 用 irradiance * albedo / diffuse BRDF 逻辑替代 equirectangular ambient
    -> fallback 保持旧 ambient / black cube
```

2. 如果优先提升调试体验：

```text
Phase 0.35 Sandbox Camera Controller
    -> app/window input query
    -> orbit camera controller
    -> update RenderView each frame
    -> resize 保持 projection aspect
    -> runtime smoke 确认默认无输入行为不变
```

长期顺序建议：

```text
Diffuse irradiance generation
 -> ForwardPass diffuse cubemap IBL
 -> Sandbox camera controller
 -> Cubemap orientation fixture / pixel validation
 -> Prefiltered specular environment
 -> BRDF LUT
 -> ForwardPass specular IBL
 -> Bloom / auto exposure / ACES tone mapping
```
