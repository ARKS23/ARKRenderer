# Phase 0.40 Prefiltered Specular Environment

## 阶段判断

Phase 0.39 已经完成 cubemap mip / face-mip render target view foundation。`EnvironmentCubeResource` 现在可以：

```cpp
rhi::TextureView* faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const;
rhi::Extent2D mipExtent(u32 mipLevel) const;
```

并且旧的：

```cpp
rhi::TextureView* faceRenderTargetView(u32 faceIndex) const;
```

仍然保持 mip0 alias 语义，现有 equirectangular conversion、diffuse irradiance generation、skybox 和 pixel validation 都已经通过验证。

当前 IBL 主线仍然只有 diffuse 部分：

```text
HDR equirectangular environment
    -> EnvironmentResource
    -> EnvironmentCubeConverter
        -> EnvironmentCubeResource
        -> SkyboxPass
    -> EnvironmentIrradianceGenerator
        -> diffuse irradiance cubemap
    -> ForwardPass diffuse ambient IBL
```

要进入完整 PBR IBL，下一块缺口是 specular environment。常见 split-sum IBL 路径需要两类资源：

```text
prefiltered specular cubemap
BRDF integration LUT
```

Phase 0.40 建议先做 prefiltered specular environment。目标是把 source environment cubemap 预滤波成一个多 mip cubemap：mip0 对应低 roughness，越高 mip 对应越高 roughness。这个阶段只负责生成资源和测试生成闭环，不接 `ForwardPass` specular IBL，不做 BRDF LUT。

## 目标

Phase 0.40 目标：

- 新增 `EnvironmentSpecularPrefilterGenerator`。
- 新增 `EnvironmentSpecularPrefilterDesc`，描述 source / target / sampleCount 等参数。
- 新增 `specular_prefilter.vert.hlsl` / `specular_prefilter.frag.hlsl`。
- 输入：已生成的 `EnvironmentCubeResource` source cubemap。
- 输出：多 mip `EnvironmentCubeResource` target cubemap。
- 对 target 的每个 `mipLevel`、每个 `faceIndex` 使用 Phase 0.39 的 `faceMipRenderTargetView(face, mip)` 渲染。
- roughness 按 mip 映射：

```text
roughness = mipLevel / max(1, targetMipLevels - 1)
```

- 每个 face+mip 使用独立 uniform buffer / descriptor set，避免同一 command buffer 内重复覆盖同一 uniform 导致 GPU 读到最后一次写入。
- 生成完成后 target cubemap 转回 `ShaderResource`。
- 新增 fake smoke 覆盖 generator 资源创建、per-face-mip rendering desc、uniform 数据和 draw 次数。
- 更新 shader assets smoke 覆盖新增 shader 文件和关键 token。
- 更新 framework headers smoke 覆盖新增 public API。
- 保证 Phase 0.32 / 0.34 / 0.38 / 0.39 的 conversion、irradiance、readback 和 face-mip tests 继续通过。

## 非目标

Phase 0.40 暂不做：

- 不做 BRDF LUT。
- 不做 `ForwardPass` specular IBL 接入。
- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不改 `RenderScene` environment API。
- 不做 material roughness -> mip 运行时采样。
- 不做 runtime quality setting UI。
- 不做 compute shader prefilter。
- 不做 cubemap CPU upload。
- 不做 screenshot/golden image system。
- 不做 bloom、auto exposure、ACES 或其他 post-process。
- 不引入 RenderGraph、bindless 或新的 descriptor manager 抽象。
- 不提交新大型 HDR 资源。

## 当前基线

### EnvironmentCubeResource

Phase 0.39 后，`EnvironmentCubeResource` 可以创建：

```text
1 cube sampled view
6 * mipLevels face-mip render target views
1 sampler
```

关键 API：

```cpp
rhi::TextureView* textureView() const;
rhi::TextureView* faceMipRenderTargetView(u32 faceIndex, u32 mipLevel) const;
rhi::Extent2D mipExtent(u32 mipLevel) const;
rhi::Sampler* sampler() const;
u32 mipLevels() const;
rhi::Format format() const;
```

这正好满足 prefilter pass 的目标写入需求。

### EnvironmentIrradianceGenerator

现有 diffuse irradiance generator 已提供可复用模式：

```text
setup(device)
    -> descriptor set layout
    -> per-face uniform buffers
    -> per-face descriptor sets
    -> shaders
    -> pipeline

generate(context, desc)
    -> validate source/target
    -> target ShaderResource/Undefined -> RenderTarget
    -> for each face:
        update source image/sampler descriptors
        update per-face uniform
        beginRendering(face mip0 view)
        draw fullscreen triangle
        endRendering
    -> target -> ShaderResource
```

Phase 0.40 建议复用这个风格，但把 per-face 扩展为 per-face-mip。

### Shader

现有 irradiance shader 已有：

```hlsl
faceUvToDirection(faceIndex, uv)
TextureCube<float4> source
SamplerState sourceSampler
fullscreen triangle
```

Phase 0.40 可以复用相同 face orientation contract。关键差异是 fragment shader 使用 GGX importance sampling，并根据 roughness 输出 prefiltered specular radiance。

## 建议设计

### Public API

新增文件：

```text
src/renderer/EnvironmentSpecularPrefilterGenerator.h
src/renderer/EnvironmentSpecularPrefilterGenerator.cpp
shaders/specular_prefilter.vert.hlsl
shaders/specular_prefilter.frag.hlsl
tests/specular_prefilter_smoke.cpp
```

建议 desc：

```cpp
struct EnvironmentSpecularPrefilterDesc {
    EnvironmentCubeResource* source = nullptr;
    EnvironmentCubeResource* target = nullptr;
    u32 sampleCount = 128;
    std::string debugName;
};
```

建议 class：

```cpp
class EnvironmentSpecularPrefilterGenerator final {
public:
    EnvironmentSpecularPrefilterGenerator() = default;
    ~EnvironmentSpecularPrefilterGenerator();

    void setup(rhi::RenderDevice& device);
    void resetImmediate();
    bool generate(rhi::DeviceContext& context, const EnvironmentSpecularPrefilterDesc& desc);

private:
    bool createDescriptorResources(u32 faceMipCount);
    bool createShaderResources();
    bool createPipelineResources();
    rhi::PipelineState* getOrCreatePipeline(rhi::Format colorFormat);
};
```

资源数量依赖 target mip count，因此建议 generator 内部持有动态数组：

```cpp
std::vector<Scope<rhi::Buffer>> m_UniformBuffers;
std::vector<Scope<rhi::DescriptorSet>> m_DescriptorSets;
u32 m_FaceMipResourceCount = 0;
```

在 `generate()` 开始时根据：

```cpp
faceMipCount = EnvironmentCubeResource::FaceCount * desc.target->mipLevels();
```

调用 `ensureFaceMipResources(faceMipCount)`。如果当前数量不足或为 0，则创建对应数量的 uniform buffers 和 descriptor sets。

### Uniform

建议 uniform 对齐到 16 bytes：

```cpp
struct alignas(16) SpecularPrefilterUniform {
    u32 faceIndex = 0;
    u32 mipLevel = 0;
    u32 sampleCount = 128;
    float roughness = 0.0f;
};
```

也可以额外加入 `sourceMipLevels` / `targetMipLevels`，但第一版暂不需要。第一版 shader 可以总是从 source cubemap mip0 采样，先不做 source LOD PDF 修正。

### Rendering Loop

建议 loop 顺序：

```cpp
for (u32 mipLevel = 0; mipLevel < target->mipLevels(); ++mipLevel) {
    const rhi::Extent2D mipExtent = target->mipExtent(mipLevel);
    const float roughness = mipLevel / max(1, target->mipLevels() - 1);

    for (u32 faceIndex = 0; faceIndex < FaceCount; ++faceIndex) {
        rhi::TextureView* faceMipView = target->faceMipRenderTargetView(faceIndex, mipLevel);
        update uniform(faceIndex, mipLevel, roughness, sampleCount)
        beginRendering(faceMipView, mipExtent)
        draw fullscreen triangle
        endRendering
    }
}
```

每个 face+mip 绑定自己的 descriptor set 和 uniform buffer。这样 command buffer 中的每个 draw 都有独立 uniform storage，不依赖 CPU-side 更新顺序。

### Resource Barriers

保持与现有 generator 一致：

```text
target current state -> RenderTarget
render all face+mip views
target RenderTarget -> ShaderResource
```

source cubemap 必须已经是 `ShaderResource`，第一版不在 generator 内隐式修改 source state。若 source 无效或与 target 是同一资源，直接失败。

### Shader Algorithm

第一版 fragment shader建议使用常见 GGX importance sampling：

```hlsl
float2 Hammersley(uint i, uint count);
float3 ImportanceSampleGGX(float2 xi, float roughness, float3 normal);
```

采样流程：

```text
N = faceUvToDirection(faceIndex, uv)
V = N
for i in sampleCount:
    H = ImportanceSampleGGX(...)
    L = normalize(2 * dot(V, H) * H - V)
    NoL = saturate(dot(N, L))
    if NoL > 0:
        color += source.SampleLevel(sourceSampler, L, 0).rgb * NoL
        weight += NoL
result = color / max(weight, epsilon)
```

注意：

- Phase 0.40 第一版可以不做 source mip PDF LOD 修正。
- sampleCount 建议 clamp 到合理范围，例如 `[16, 1024]`，默认 `128`。
- roughness 为 0 时可以使用一个很小的下限参与 GGX 计算，避免除零。
- face orientation 必须与 `CubemapOrientation` / equirect-to-cube / irradiance shader 保持一致。

### Target Resource Policy

建议 Phase 0.40 的默认 target desc：

```cpp
EnvironmentCubeResourceDesc specularDesc{};
specularDesc.faceExtent = {128, 128};
specularDesc.format = rhi::Format::RGBA16Float;
specularDesc.mipLevels = rhi::calculateMipLevelCount(specularDesc.faceExtent);
```

第一版 generator 不负责创建 target resource；调用方传入 target。原因：

- 与 `EnvironmentIrradianceGenerator` 风格一致。
- 测试可以明确验证 target desc。
- 后续 renderer default bake path 可以自行决定尺寸、format、readback flag。

## 实施顺序

### 0.40.0 文档与范围确认

目标：

- 新增 `docs/phase/phase40.md`。
- 明确本阶段只做 prefiltered specular environment resource generation。
- 明确不做 BRDF LUT、ForwardPass specular IBL、RenderGraph 或 compute。

审核点：

- 不改 `ForwardPass` descriptor layout。
- 不改 `FrameRenderer` pass graph。
- 不把 prefilter 和最终 shading 混在一个阶段。

### 0.40.1 Generator Skeleton and API

目标：

- 新增 `EnvironmentSpecularPrefilterGenerator.h/.cpp`。
- 新增 desc / setup / reset / generate。
- 接入 CMake renderer source。
- 更新 `framework_headers_smoke` 覆盖 public API。

审核点：

- API 风格与 `EnvironmentIrradianceGenerator` 一致。
- source / target 必须非空、有效、不同资源。
- target 必须有有效 mip levels 和 face-mip views。

### 0.40.2 Shader and Pipeline Resources

目标：

- 新增 `specular_prefilter.vert.hlsl` / `specular_prefilter.frag.hlsl`。
- 使用 fullscreen triangle。
- descriptor layout：

```text
binding 0: uniform buffer
binding 1: source TextureCube sampled image
binding 2: source sampler
```

- pipeline color format 按 target format 缓存，和现有 generator 风格一致。

审核点：

- shader 使用 `TextureCube`。
- shader 使用 `faceUvToDirection()`，face order 与 contract 一致。
- shader 包含 GGX importance sampling / roughness / sampleCount 关键路径。

### 0.40.3 Face-Mip Render Path

目标：

- generator 为每个 face+mip 创建/持有独立 uniform buffer 和 descriptor set。
- `generate()` 遍历全部 target mips 和 faces。
- 每个 draw 使用 `target->faceMipRenderTargetView(face, mip)`。
- 每个 draw 使用 `target->mipExtent(mip)` 设置 rendering desc / viewport / scissor。
- 生成完成后 target 转到 `ShaderResource`。

审核点：

- draw count 应为 `FaceCount * targetMipLevels`。
- mip0 roughness 为 0，最后一个 mip roughness 为 1。
- 所有 rendering extent 必须与 mip extent 一致。
- uniform buffer 不可在多个 pending draw 中复用同一 storage。

### 0.40.4 Tests

目标：

- 新增 `ark_specular_prefilter_smoke`：
  - fake device/context 下 setup 创建 descriptor layout、shader、pipeline resources。
  - target `mipLevels = 4` 时 draw count 为 `6 * 4`。
  - rendering desc 使用对应 face-mip view。
  - viewport/scissor 使用 `mipExtent()`。
  - uniform roughness 覆盖 0 到 1。
  - invalid desc 被拒绝：source null、target null、source == target、invalid source/target。
- 更新 `ark_shader_assets_smoke`：
  - 检查新 shader 文件存在。
  - 检查 `TextureCube`、`faceUvToDirection`、`Hammersley`、`ImportanceSampleGGX`、`roughness`、`sampleCount` 等关键 token。
- 更新 `ark_framework_headers_smoke` 覆盖新 generator/desc。
- 保持现有 cubemap / irradiance / pixel smoke 通过。

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_specular_prefilter_smoke ark_shader_assets_smoke ark_framework_headers_smoke ark_environment_cube_resource_smoke ark_equirectangular_to_cube_smoke ark_environment_irradiance_smoke ark_cubemap_orientation_pixel_smoke
build/msvc-vcpkg/Debug/ark_specular_prefilter_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
build/msvc-vcpkg/Debug/ark_framework_headers_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_cube_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_equirectangular_to_cube_smoke.exe
build/msvc-vcpkg/Debug/ark_environment_irradiance_smoke.exe
build/msvc-vcpkg/Debug/ark_cubemap_orientation_pixel_smoke.exe
```

### 0.40.5 验证与收尾

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

### Uniform Buffer Reuse

不能只创建一个 uniform buffer 然后在同一个 command buffer 内多次更新并绘制。GPU 执行时可能所有 draw 都读到最后一次 CPU 写入的数据。第一版应给每个 face+mip 独立 uniform buffer，或者使用有明确 offset 的 ring/dynamic uniform。为了保持简单，Phase 0.40 推荐独立 uniform buffer。

### Source / Target State

source cubemap 应处于 `ShaderResource`。target cubemap 在生成期间处于 `RenderTarget`，生成完成后转回 `ShaderResource`。不要在 generator 里隐式把 source 也改成别的状态。

### Roughness Mapping

粗糙度映射应稳定可测试：

```text
mip0 -> roughness 0
last mip -> roughness 1
```

后续如果要使用 perceptual roughness 或更复杂分布，可以在接入 `ForwardPass` specular IBL 前重新评估，但 Phase 0.40 不要混入材质采样策略。

### Shader Cost

prefilter shader 是离线/启动时 bake 路径，不是每帧 pass。默认 sampleCount 不宜太高。建议第一版默认 128，测试可用较小值，例如 16 或 32。

### Physical Accuracy

第一版可以不做 source mip PDF LOD 修正，也不做 seam fixup。目标是建立 renderer 内部 prefiltered specular cubemap generation foundation。更高精度的 prefilter 可在 specular IBL 验证阶段继续改进。

### Format

target 建议继续使用 `RGBA16Float`。`RGBA32Float` 可用于验证但不是默认 runtime 资源。不要在 Phase 0.40 扩展格式矩阵。

## 完成标准

Phase 0.40 完成时应满足：

- 新增 `EnvironmentSpecularPrefilterGenerator` public API。
- 新增 specular prefilter shaders。
- generator 能对 target cubemap 的全部 face+mip 执行 render path。
- 每个 face+mip 有独立 uniform storage。
- roughness / sampleCount / faceIndex / mipLevel uniform 可被测试覆盖。
- target 在生成后进入 `ShaderResource`。
- fake smoke 覆盖 draw count、rendering desc、viewport/scissor、uniform 数据和 invalid desc。
- shader assets smoke 覆盖新增 shader 和关键 token。
- framework headers smoke 覆盖新增 API。
- 现有 cubemap conversion、irradiance generation、pixel readback 和 sandbox runtime smoke 继续通过。
- handoff 明确后续仍需 BRDF LUT 和 `ForwardPass` specular IBL。

## 实施结果

Phase 0.40 已完成 0.40.0 ~ 0.40.5：

- 新增 `EnvironmentSpecularPrefilterGenerator` 和 `EnvironmentSpecularPrefilterDesc`。
- 新增 `specular_prefilter.vert.hlsl` / `specular_prefilter.frag.hlsl`。
- generator 会校验 source / target 非空、有效、不同资源，并要求 source cubemap 已处于 `ShaderResource`。
- generator 会按 `targetMipLevels * FaceCount` 创建独立 uniform buffer / descriptor set，避免同一 command buffer 内覆盖 pending draw uniform。
- generator 会遍历所有 face+mip，使用 `faceMipRenderTargetView(face, mip)` 和 `mipExtent(mip)` 渲染，并在完成后把 target cubemap 转回 `ShaderResource`。
- shader 使用 GGX importance sampling、Hammersley 序列、`TextureCube.SampleLevel()` 和稳定 face orientation contract，输出 linear HDR prefiltered radiance。
- 新增 `ark_specular_prefilter_smoke`，覆盖 invalid desc、descriptor layout、shader/pipeline resource、barrier、face-mip rendering desc、viewport/scissor、uniform 数据、draw count 和 per-face-mip descriptor 更新。
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
CTest: 20/20 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

当前边界：Phase 0.40 只完成 prefiltered specular cubemap generation foundation；默认 renderer bake path、BRDF LUT、`ForwardPass` specular IBL 和 roughness mip runtime sampling 仍留给后续阶段。

## 后续 Phase 建议

Phase 0.40 后建议：

1. **BRDF LUT**
   - 新增 split-sum BRDF integration LUT resource。
2. **Renderer Default Specular Bake Path**
   - 在 renderer 默认 environment bake 中创建并生成 prefiltered specular cubemap。
3. **ForwardPass Specular IBL**
   - 接入 prefiltered environment、BRDF LUT 和 roughness mip sampling。
4. **Specular IBL Validation**
   - 使用金属/粗糙度测试球、HDR environment 和 Phase 0.38 readback 基础做验证。
5. **Screenshot / Pixel Test Infrastructure**
   - 在 readback 基础上扩展 frame color screenshot、golden image 或统计型 pixel smoke。
