# Phase 0.41 BRDF LUT Foundation

## 阶段判断

Phase 0.40 已经完成 prefiltered specular environment generation foundation。当前 renderer 内部已经具备：

```text
HDR equirectangular environment
    -> EnvironmentResource
    -> EnvironmentCubeConverter
        -> EnvironmentCubeResource
        -> SkyboxPass
    -> EnvironmentIrradianceGenerator
        -> diffuse irradiance cubemap
    -> ForwardPass diffuse ambient IBL

EnvironmentCubeResource source cubemap
    -> EnvironmentSpecularPrefilterGenerator
        -> prefiltered specular cubemap mip chain
```

但是完整 split-sum specular IBL 仍缺一块固定 2D lookup texture：

```text
BRDF integration LUT
```

常见 PBR IBL 中，specular 间接光通常由两部分组合：

```text
prefilteredSpecular = prefilteredEnvironment.SampleLevel(reflection, roughnessMip)
brdf = brdfLut.Sample(float2(NdotV, roughness)).rg
specularIBL = prefilteredSpecular * (F0 * brdf.x + brdf.y)
```

Phase 0.40 已完成第一项 prefiltered environment。Phase 0.41 建议补齐 BRDF LUT resource / generator / shader / tests。这个阶段只生成 LUT，不接 `ForwardPass`，不改 mesh shader，不引入 renderer 默认 bake path。这样后续 Phase 可以在资源都存在的前提下专注接入 `FrameContext` 和 `ForwardPass` specular IBL。

## 目标

Phase 0.41 目标：

- 新增 `EnvironmentBrdfLutResource`。
- 新增 `EnvironmentBrdfLutGenerator` 和 `EnvironmentBrdfLutGenerationDesc`。
- 新增 `brdf_lut.vert.hlsl` / `brdf_lut.frag.hlsl`。
- 使用 fullscreen triangle 生成 2D BRDF integration LUT。
- 输出 LUT 建议为 `RGBA16Float` 2D texture，RG 存储 BRDF scale/bias，BA 预留。
- LUT 默认尺寸建议 `256x256`。
- generator 负责把 target texture 渲染到 `ShaderResource` 状态。
- 新增 fake smoke 覆盖 resource 创建、rendering desc、viewport/scissor、barrier、pipeline 和 draw。
- 更新 shader assets smoke 覆盖新增 shader 文件和关键 token。
- 更新 framework headers smoke 覆盖新增 public API。
- 保证 Phase 0.34 / 0.35 / 0.38 / 0.40 相关测试继续通过。

## 非目标

Phase 0.41 暂不做：

- 不接 `ForwardPass` specular IBL。
- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameContext`。
- 不改 `Renderer` 默认 environment bake path。
- 不把 prefiltered specular cubemap 接入默认资源链。
- 不改 `mesh.frag.hlsl`。
- 不做 roughness -> mip runtime sampling。
- 不做 screenshot/golden image system。
- 不做 bloom、auto exposure、ACES 或其他 post-process。
- 不引入 compute shader。
- 不引入 RenderGraph、bindless 或新的 descriptor manager 抽象。
- 不提交新 HDR 或大型外部资源。

## 当前基线

### Specular Prefilter

Phase 0.40 已新增：

```text
src/renderer/EnvironmentSpecularPrefilterGenerator.h/.cpp
shaders/specular_prefilter.vert.hlsl
shaders/specular_prefilter.frag.hlsl
tests/specular_prefilter_smoke.cpp
```

它负责从 source cubemap 生成 roughness mip chain，但当前没有任何 pass 消费这个结果。

### ForwardPass

当前 `ForwardPass` descriptor layout 已包含：

```text
binding 14: equirectangular environment sampled image
binding 15: equirectangular environment sampler
binding 16: diffuse irradiance cubemap sampled image
binding 17: diffuse irradiance cubemap sampler
```

mesh shader 当前 ambient 路径：

```text
if irradiance cube exists:
    diffuse ambient = irradiance * albedo
else if equirectangular environment exists:
    fallback ambient = environment(normal) * albedo
else:
    fallback ambient = scene ambient * albedo
```

当前没有：

```text
prefiltered specular cubemap binding
BRDF LUT binding
specular IBL term
```

Phase 0.41 不改这部分，只准备 BRDF LUT 资源。

### RHI Texture Capability

当前 RHI 已支持普通 2D texture、render target view、sampled image view、sampler、dynamic rendering 和 `RGBA16Float` format。BRDF LUT 不需要 cubemap、不需要 readback，也不需要新增 RHI texture type。

因为当前 format 集合中已经有 `RGBA16Float`，第一版建议使用：

```cpp
rhi::Format::RGBA16Float
```

而不是新增 `RG16Float`。后续如果要节省显存或贴近常见实现，可以单独扩展 `RG16Float` format。

## 建议设计

### Public API

新增文件：

```text
src/renderer/EnvironmentBrdfLutResource.h
src/renderer/EnvironmentBrdfLutResource.cpp
src/renderer/EnvironmentBrdfLutGenerator.h
src/renderer/EnvironmentBrdfLutGenerator.cpp
shaders/brdf_lut.vert.hlsl
shaders/brdf_lut.frag.hlsl
tests/brdf_lut_smoke.cpp
```

建议 resource desc：

```cpp
struct EnvironmentBrdfLutResourceDesc {
    rhi::Extent2D extent{256, 256};
    rhi::Format format = rhi::Format::RGBA16Float;
    rhi::SamplerDesc sampler;
    bool hasSamplerOverride = false;
    std::string debugName;
};
```

建议 resource API：

```cpp
class EnvironmentBrdfLutResource final {
public:
    EnvironmentBrdfLutResource() = default;
    ~EnvironmentBrdfLutResource();

    bool create(rhi::RenderDevice& device, const EnvironmentBrdfLutResourceDesc& desc);
    bool isValid() const;
    void releaseDeferred(rhi::DeviceContext& context);
    void resetImmediate();

    rhi::Texture* texture() const;
    rhi::TextureView* textureView() const;
    rhi::TextureView* renderTargetView() const;
    rhi::Sampler* sampler() const;
    rhi::Extent2D extent() const;
    rhi::Format format() const;
};
```

创建策略：

```text
TextureDesc:
    type = Texture2D
    extent = desc.extent
    format = desc.format
    mipLevels = 1
    arrayLayers = 1
    usage = RenderTarget | ShaderResource

TextureView:
    sampled view: Texture2D, mip0, layer0
    render target view: Texture2D, mip0, layer0

Sampler:
    min/mag = Linear
    mip = Linear or Nearest
    U/V/W = ClampToEdge
```

BRDF LUT 是屏幕空间查表 texture，不需要 repeat。默认 sampler 应该 clamp。

### Generator API

建议 desc：

```cpp
struct EnvironmentBrdfLutGenerationDesc {
    EnvironmentBrdfLutResource* target = nullptr;
    u32 sampleCount = 1024;
    std::string debugName;
};
```

建议 class：

```cpp
class EnvironmentBrdfLutGenerator final {
public:
    EnvironmentBrdfLutGenerator() = default;
    ~EnvironmentBrdfLutGenerator();

    void setup(rhi::RenderDevice& device);
    void resetImmediate();
    bool generate(rhi::DeviceContext& context, const EnvironmentBrdfLutGenerationDesc& desc);

private:
    bool createDescriptorResources();
    bool createShaderResources();
    bool createPipelineResources();
    rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);
};
```

BRDF LUT generation 不需要输入 texture，只需要一个 uniform buffer：

```cpp
struct alignas(16) BrdfLutUniform {
    u32 sampleCount = 1024;
    u32 padding0 = 0;
    u32 padding1 = 0;
    u32 padding2 = 0;
};
```

Descriptor layout：

```text
binding 0: uniform buffer
```

也可以第一版不使用 uniform，直接 shader 常量固定 sample count。但为了测试可控和后续质量配置，建议保留 uniform。

### Rendering Path

`generate()` 建议流程：

```text
validate target
clamp sampleCount
target current state -> RenderTarget
update uniform
beginRendering(target.renderTargetView(), target.extent())
set viewport/scissor to target extent
bind pipeline
bind descriptor set
draw fullscreen triangle
endRendering()
target RenderTarget -> ShaderResource
```

BRDF LUT 只渲染一次，不需要 per-face 或 per-mip resources。

### Shader Algorithm

vertex shader 与其他 fullscreen pass 一致：

```hlsl
SV_VertexID fullscreen triangle
uv = positions[vertexId] * 0.5 + 0.5
```

fragment shader 输入：

```text
uv.x = NdotV
uv.y = roughness
```

建议实现：

```hlsl
float RadicalInverseVdc(uint bits);
float2 Hammersley(uint i, uint count);
float3 ImportanceSampleGGX(float2 xi, float roughness, float3 normal);
float2 IntegrateBRDF(float nDotV, float roughness, uint sampleCount);
```

核心积分：

```text
V = float3(sqrt(1 - NdotV^2), 0, NdotV)
N = float3(0, 0, 1)

for each sample:
    H = ImportanceSampleGGX(xi, roughness, N)
    L = normalize(2 * dot(V, H) * H - V)
    NoL = saturate(L.z)
    NoH = saturate(H.z)
    VoH = saturate(dot(V, H))
    if NoL > 0:
        G = GeometrySmith(...)
        G_Vis = G * VoH / max(NoH * NoV, epsilon)
        Fc = pow(1 - VoH, 5)
        A += (1 - Fc) * G_Vis
        B += Fc * G_Vis

return float2(A, B) / sampleCount
```

输出：

```hlsl
return float4(A, B, 0.0f, 1.0f);
```

注意：

- LUT shader 输出 linear data，不做 tone mapping，不做 gamma encoding。
- `roughness` 应 clamp 到 `[0, 1]`。
- `NdotV` 应 clamp 到 `[0, 1]`，避免边界除零。
- 默认 sampleCount 建议 `1024`，测试可使用 `64` 或 `128`。

### Resource Ownership

Phase 0.41 中 generator 不创建 target resource。调用方显式创建 `EnvironmentBrdfLutResource`，再传给 generator。这样与现有 `EnvironmentIrradianceGenerator` / `EnvironmentSpecularPrefilterGenerator` 风格一致：

```text
Resource owns texture/view/sampler
Generator owns pipeline/shader/descriptor/uniform
Renderer or test decides when创建 resource and when generate
```

## 实施顺序

### 0.41.0 文档与范围确认

目标：

- 新增 `docs/phase/phase41.md`。
- 明确本阶段只做 BRDF LUT resource / generator / shader / tests。
- 明确不接 `ForwardPass`、不改默认 renderer bake path、不改 mesh shader。

审核点：

- 不把 BRDF LUT 和最终 specular IBL 混在一个阶段。
- 不新增大型资源。
- 不新增 RHI format，第一版使用 `RGBA16Float`。

### 0.41.1 BRDF LUT Resource

目标：

- 新增 `EnvironmentBrdfLutResource.h/.cpp`。
- 支持创建 2D `RenderTarget | ShaderResource` texture。
- 创建 sampled texture view、render target view 和 clamp sampler。
- 支持 `releaseDeferred()` / `resetImmediate()`。
- 更新 `framework_headers_smoke` 覆盖 public API。

审核点：

- extent 必须有效。
- format 必须有效，默认 `RGBA16Float`。
- sampler 默认 clamp-to-edge。
- resource 不接触 Vulkan 类型。

### 0.41.2 Generator Skeleton and Pipeline

目标：

- 新增 `EnvironmentBrdfLutGenerator.h/.cpp`。
- 新增 desc / setup / reset / generate。
- 创建 descriptor set layout、uniform buffer、descriptor set、shader、pipeline layout。
- pipeline 不启用 depth。
- pipeline color format 按 target format 缓存。

审核点：

- generator 只依赖 RHI 和 renderer resource，不依赖 Vulkan。
- target null / invalid 应失败。
- sampleCount clamp 到合理范围，例如 `[16, 4096]`。

### 0.41.3 Shader and Render Path

目标：

- 新增 `brdf_lut.vert.hlsl` / `brdf_lut.frag.hlsl`。
- generator 渲染 target 2D texture。
- target 生成前 transition 到 `RenderTarget`。
- target 生成后 transition 到 `ShaderResource`。
- viewport/scissor 使用 target extent。

审核点：

- shader 包含 `Hammersley`、`ImportanceSampleGGX`、`GeometrySmith`、`IntegrateBRDF`。
- shader 输出 `float4(brdf.x, brdf.y, 0, 1)`。
- shader 不采样 external texture。
- shader 不做 tone mapping 或 gamma encoding。

### 0.41.4 Tests

目标：

- 新增 `ark_brdf_lut_smoke`：
  - fake device/context 下创建 resource。
  - 验证 texture desc：2D、`RGBA16Float`、`RenderTarget | ShaderResource`、mip1、layer1。
  - 验证 sampled view 和 render target view。
  - 验证 sampler clamp policy。
  - generator setup 创建 descriptor layout、uniform、shader、pipeline layout。
  - `generate()` 产生两次 barrier：target -> RenderTarget，target -> ShaderResource。
  - rendering desc 使用 target render target view 和 target extent。
  - viewport/scissor 使用 target extent。
  - draw vertexCount 为 3。
  - uniform sampleCount clamp 可验证。
  - invalid desc 被拒绝。
- 更新 `ark_shader_assets_smoke`：
  - 检查新增 shader source 和 compiled SPIR-V。
  - 检查关键 token：`Hammersley`、`ImportanceSampleGGX`、`IntegrateBRDF`、`GeometrySmith`、`roughness`、`NdotV`、`sampleCount`。
  - 检查没有 tone mapping / gamma output token。
- 更新 `ark_framework_headers_smoke`。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_brdf_lut_smoke ark_shader_assets_smoke ark_framework_headers_smoke
build/msvc-vcpkg/Debug/ark_brdf_lut_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
```

### 0.41.5 验证与收尾

目标：

- full build。
- CTest 全量通过。
- default sandbox runtime smoke。
- debug orientation sandbox runtime smoke。
- DamagedHelmet + 本地 HDR runtime smoke。
- 更新 `docs/codex_handoff.md`。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

## 关键风险

### LUT Format

BRDF LUT 只需要 RG 两个通道，但当前不建议为此阶段扩展 `RG16Float`。第一版使用 `RGBA16Float` 能复用已有 format、Vulkan mapping 和测试路径。后续如果有显存压力，再独立做 format expansion。

### Shader Precision

BRDF LUT 是数据 texture，不是颜色输出。shader 不应做 tone mapping、gamma、exposure 或 Reinhard。最终 `ForwardPass` 接入时也应按 linear data 使用。

### Sample Count

BRDF integration 是离线/启动时生成路径，默认 sampleCount 可以高一些，例如 1024。但 smoke test 不需要跑高采样。generator 应 clamp sampleCount，测试用较小值验证数据流。

### Coordinate Convention

LUT 坐标约定必须稳定：

```text
u = NdotV
v = roughness
```

后续 `ForwardPass` specular IBL 必须使用同一约定：

```hlsl
float2 brdf = g_BrdfLut.Sample(g_BrdfSampler, float2(nDotV, roughness)).rg;
```

### Phase Boundary

这个阶段不要把 LUT 接入 `ForwardPass`。如果同时改 descriptor layout、FrameContext、Renderer 默认 bake 和 mesh shader，验证面会膨胀。更稳妥的顺序是：

```text
0.41 BRDF LUT
0.42 Renderer default specular bake path
0.43 ForwardPass specular IBL
```

### Runtime Resource Lifetime

Phase 0.41 的 resource/generator 可以先只由 tests 使用。默认 renderer 是否持有 BRDF LUT resource 留给 Phase 0.42。这样不会在本阶段改变 runtime 行为，也不会影响 sandbox 默认画面。

## 完成标准

Phase 0.41 完成时应满足：

- 新增 `EnvironmentBrdfLutResource` public API。
- 新增 `EnvironmentBrdfLutGenerator` public API。
- 新增 BRDF LUT shaders。
- generator 能把 target LUT 渲染到 `ShaderResource` 状态。
- resource 创建 sampled view、render target view 和 clamp sampler。
- fake smoke 覆盖 resource desc、rendering desc、viewport/scissor、uniform、barrier 和 draw。
- shader assets smoke 覆盖新增 shader 和关键 token。
- framework headers smoke 覆盖新增 API。
- 现有 cubemap conversion、irradiance generation、specular prefilter、pixel readback 和 sandbox runtime smoke 继续通过。
- handoff 明确后续仍需 renderer default specular bake path 和 `ForwardPass` specular IBL。

## 实施结果

Phase 0.41 已完成 0.41.0 ~ 0.41.5：

- 新增 `EnvironmentBrdfLutResource` 和 `EnvironmentBrdfLutResourceDesc`。
- 新增 `EnvironmentBrdfLutGenerator` 和 `EnvironmentBrdfLutGenerationDesc`。
- 新增 `brdf_lut.vert.hlsl` / `brdf_lut.frag.hlsl`。
- resource 创建 2D `RenderTarget | ShaderResource` texture、sampled view、render target view 和 clamp sampler。
- generator 使用 uniform 控制 sampleCount，默认 1024，并在 CPU 侧 clamp 到 `[16, 4096]`。
- generator 渲染 fullscreen triangle，完成后把 target texture 转到 `ShaderResource`。
- shader 使用 Hammersley 序列、GGX importance sampling、Smith geometry 和 split-sum BRDF integration，输出 `float4(A, B, 0, 1)` linear data。
- 新增 `ark_brdf_lut_smoke`，覆盖 invalid desc、sampler override、deferred release、descriptor layout、uniform、barrier、rendering desc、viewport/scissor、pipeline 和 draw。
- `ark_shader_assets_smoke` 已覆盖新增 shader 文件、编译产物和关键 token。
- `ark_framework_headers_smoke` 已覆盖新增 public API。

最终验证：

```powershell
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

当前边界：Phase 0.41 只完成 BRDF LUT resource / generator foundation；默认 renderer bake path、`FrameContext` specular resource plumbing、`ForwardPass` specular IBL 和 roughness mip runtime sampling 仍留给后续阶段。

## 后续 Phase 建议

Phase 0.41 后建议：

1. **Renderer Default Specular Bake Path**
   - 默认 renderer 创建 prefiltered specular cubemap 和 BRDF LUT，并在 environment bake 中生成。
2. **FrameContext Specular Resource Plumbing**
   - 给 `FrameContext` 增加 prefiltered specular cubemap / BRDF LUT 指针。
3. **ForwardPass Specular IBL**
   - 增加 descriptor bindings，mesh shader 采样 prefiltered environment 和 BRDF LUT。
4. **Specular IBL Validation**
   - 使用金属/粗糙度测试模型、HDR environment 和 readback/screenshot 基础做验证。
5. **Quality and Resource Policy**
   - 暴露 specular cubemap 分辨率、prefilter sampleCount、BRDF LUT size 等配置。
