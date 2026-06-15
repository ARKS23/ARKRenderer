# Phase 0.42 Renderer Default Specular Bake Path

## 阶段判断

Phase 0.41 已经完成 BRDF LUT resource / generator foundation。当前 renderer 已具备完整 split-sum specular IBL 所需的两个离线/启动时生成部件：

```text
EnvironmentSpecularPrefilterGenerator
    source cubemap -> prefiltered specular cubemap mip chain

EnvironmentBrdfLutGenerator
    fullscreen triangle -> 2D BRDF integration LUT
```

但是默认 runtime 仍没有持有或生成这两类资源。当前默认 renderer 的 environment bake 链路停在 diffuse IBL：

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

所以 Phase 0.42 最合适的目标不是直接改 shader，而是先把默认 renderer 的 specular bake path 接起来，让 runtime 每次默认启动都能生成：

```text
prefiltered specular cubemap
BRDF integration LUT
```

这样 Phase 0.43 再接 `ForwardPass` descriptor layout 和 mesh shader specular IBL 时，资源生命周期、生成顺序和 fallback 策略已经稳定。

## 目标

Phase 0.42 目标：

- 默认 renderer 创建并持有 prefiltered specular cubemap。
- 默认 renderer 创建并持有 BRDF LUT resource。
- 默认 renderer 初始化 `EnvironmentSpecularPrefilterGenerator`。
- 默认 renderer 初始化 `EnvironmentBrdfLutGenerator`。
- 在 default environment cubemap conversion 成功后，生成 prefiltered specular cubemap mip chain。
- 在 default environment 准备阶段生成 BRDF LUT。
- 给 `FrameContext` 增加 prefiltered specular cubemap / BRDF LUT 指针。
- `FrameRenderer` / pass 执行路径能把这些资源随 `FrameContext` 传递，但本阶段不消费它们。
- bake 失败不导致 sandbox 启动失败，只记录 warning 并继续 diffuse IBL / skybox 路径。
- 更新 framework headers smoke 覆盖新增 `FrameContext` 字段。
- 扩展 runtime smoke，确认 default sandbox 和真实模型 + HDR environment 不会启动即退出。

## 非目标

Phase 0.42 暂不做：

- 不改 `ForwardPass` descriptor layout。
- 不新增 mesh shader specular IBL bindings。
- 不改 `mesh.frag.hlsl` 光照公式。
- 不做 roughness -> mip runtime sampling。
- 不把 BRDF LUT 或 prefiltered cubemap 绑定到 `ForwardPass`。
- 不新增 screenshot/golden image system。
- 不做 material preview、UI slider 或 debug overlay。
- 不做 glTF camera、bloom、auto exposure、ACES 或其他 post-process。
- 不新增 compute shader 路径。
- 不引入 RenderGraph、bindless 或新的 descriptor manager 抽象。
- 不提交新的大型 HDRI 或模型资源。

## 当前基线

### Renderer Default Environment

当前 `DefaultRenderer` 已经有：

```text
m_DefaultEnvironment
m_DefaultEnvironmentCube
m_DefaultIrradianceCube
m_EnvironmentCubeConverter
m_EnvironmentIrradianceGenerator

m_DefaultEnvironmentCubeConverted
m_DefaultIrradianceCubeGenerated
```

默认生成顺序是：

```text
createDefaultEnvironment()
    create EnvironmentResource
    create EnvironmentCubeResource
    create irradiance EnvironmentCubeResource

prepareDefaultEnvironmentCube()
    upload EnvironmentResource
    convert equirectangular -> cubemap

prepareDefaultIrradianceCube()
    convolve environment cubemap -> irradiance cubemap

resolveFrameEnvironmentCube()
resolveFrameIrradianceCube()
```

Phase 0.42 应沿用这个风格，不引入新的调度系统。

### Existing Specular Prefilter API

Phase 0.40 已有：

```cpp
struct EnvironmentSpecularPrefilterDesc {
    EnvironmentCubeResource* source = nullptr;
    EnvironmentCubeResource* target = nullptr;
    u32 sampleCount = 128;
    std::string debugName;
};
```

generator 要求：

- source cubemap 已经处于 `ShaderResource`。
- target cubemap 有完整 face-mip render target views。
- source 和 target 不能是同一个 resource。
- 完成后 target texture 转为 `ShaderResource`。

### Existing BRDF LUT API

Phase 0.41 已有：

```cpp
struct EnvironmentBrdfLutResourceDesc {
    rhi::Extent2D extent{256, 256};
    rhi::Format format = rhi::Format::RGBA16Float;
    rhi::SamplerDesc sampler;
    bool hasSamplerOverride = false;
    std::string debugName;
};

struct EnvironmentBrdfLutGenerationDesc {
    EnvironmentBrdfLutResource* target = nullptr;
    u32 sampleCount = 1024;
    std::string debugName;
};
```

generator 要求：

- target LUT 已创建 sampled view、render target view 和 sampler。
- 完成后 target texture 转为 `ShaderResource`。

### FrameContext

当前 `FrameContext` 只传递：

```cpp
EnvironmentCubeResource* environmentCube = nullptr;
EnvironmentCubeResource* irradianceCube = nullptr;
```

Phase 0.42 建议新增：

```cpp
EnvironmentCubeResource* prefilteredSpecularCube = nullptr;
EnvironmentBrdfLutResource* brdfLut = nullptr;
```

这一步只是 resource plumbing。`ForwardPass` 可以暂时忽略这些字段。

## 资源策略

### Default Specular Cubemap

建议默认配置：

```text
debugName: DefaultSandboxSpecularCube
faceExtent: 256x256
format: RGBA16Float
mipLevels: full chain
sampleCount: 128
```

`256x256` 是当前阶段比较平衡的默认值：

- 比 `128x128` 更适合观察金属高光。
- 比 `512x512` 更适合当前无异步 bake / 无缓存的启动路径。
- full mip chain 能覆盖 roughness 从 0 到 1 的采样需求。

如果实现时需要 helper，建议添加局部函数：

```cpp
u32 calculateMipLevels(rhi::Extent2D extent);
```

对于 `256x256`，结果应为 `9`。

### Default BRDF LUT

建议默认配置：

```text
debugName: DefaultSandboxBrdfLut
extent: 256x256
format: RGBA16Float
sampleCount: 1024
```

BRDF LUT 是 2D 数据 texture，不是颜色输出：

- 不做 tone mapping。
- 不做 gamma。
- 不受 exposure 影响。
- sampler 保持 clamp。

### Failure Policy

specular bake 是增强路径，不能阻塞默认 sandbox：

```text
environment cubemap conversion failed
    -> no skybox/specular/irradiance cube
    -> keep equirectangular ForwardPass fallback

irradiance generation failed
    -> keep equirectangular ambient fallback

specular prefilter failed
    -> keep diffuse IBL path

BRDF LUT generation failed
    -> keep diffuse IBL path
```

本阶段失败只应影响后续 specular IBL 是否可用，不应影响模型、skybox、diffuse IBL、tone mapping 或 present。

## 设计方案

### Renderer Resource Lifetime

在 `DefaultRenderer` 内新增成员：

```cpp
EnvironmentSpecularPrefilterGenerator m_EnvironmentSpecularPrefilterGenerator;
EnvironmentBrdfLutGenerator m_EnvironmentBrdfLutGenerator;

EnvironmentCubeResource m_DefaultSpecularCube;
EnvironmentBrdfLutResource m_DefaultBrdfLut;

bool m_DefaultSpecularCubeGenerationAttempted = false;
bool m_DefaultSpecularCubeGenerated = false;
bool m_DefaultBrdfLutGenerationAttempted = false;
bool m_DefaultBrdfLutGenerated = false;
```

构造阶段：

```text
m_EnvironmentSpecularPrefilterGenerator.setup(device)
m_EnvironmentBrdfLutGenerator.setup(device)
createDefaultEnvironment()
```

析构阶段应遵循现有资源释放顺序：

```text
waitIdle()
FrameRenderer reset
scene/model reset
generators resetImmediate()
generated resources resetImmediate()
backend reset
```

### Resource Creation

`createDefaultEnvironment()` 中扩展：

```text
create m_DefaultSpecularCube
create m_DefaultBrdfLut
```

如果创建失败：

- 记录 warning。
- reset 对应 resource。
- 不让 `createDefaultEnvironment()` 返回 false。

原因：默认 environment 本身已经可以 fallback 到 procedural image；specular 资源失败不应破坏基本渲染。

### Bake Order

默认每帧 prepare 阶段建议顺序：

```text
prepareDefaultEnvironmentCube(context, renderScene)
prepareDefaultIrradianceCube(context, renderScene)
prepareDefaultSpecularCube(context, renderScene)
prepareDefaultBrdfLut(context, renderScene)
```

其中：

- `prepareDefaultSpecularCube()` 依赖 `m_DefaultEnvironmentCubeConverted`。
- `prepareDefaultBrdfLut()` 不依赖 environment cubemap，但为了日志和启动成本可放在 specular bake 同一段。
- 如果 scene environment 不是 renderer 默认 environment，则不生成默认 specular cube。
- BRDF LUT 可以与 scene environment 无关，但本阶段仍建议只在 default renderer path 内生成。

### FrameContext Wiring

`DefaultRenderer::render()` 创建 `FrameContext` 时新增：

```cpp
frameContext.prefilteredSpecularCube = resolveFramePrefilteredSpecularCube(renderScene);
frameContext.brdfLut = resolveFrameBrdfLut(renderScene);
```

resolver 逻辑：

```text
resolveFramePrefilteredSpecularCube()
    if generated && default environment && resource valid
        return &m_DefaultSpecularCube
    return nullptr

resolveFrameBrdfLut()
    if generated && resource valid
        return &m_DefaultBrdfLut
    return nullptr
```

本阶段 `ForwardPass` 不使用这两个字段，但 tests 应保证字段可以被 public header 消费。

## Tests

### Unit / Smoke

建议更新：

```text
tests/framework_headers_smoke.cpp
```

覆盖：

- `FrameContext::prefilteredSpecularCube`
- `FrameContext::brdfLut`
- `EnvironmentBrdfLutResource` forward declaration / include path 可用

现有 tests 应继续通过：

```text
ark_specular_prefilter_smoke
ark_brdf_lut_smoke
ark_environment_irradiance_smoke
ark_equirectangular_to_cube_smoke
ark_cubemap_orientation_pixel_smoke
ark_forward_pass_pipeline_smoke
ark_shader_assets_smoke
```

### Runtime Smoke

建议继续运行：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

预期：

- 默认 sandbox 不启动即退出。
- debug orientation path 不启动即退出。
- DamagedHelmet + HDR path 不启动即退出。
- 日志中能看到 specular cubemap / BRDF LUT generation 成功或可解释的 warning。

runtime smoke 仍不等同于视觉正确性测试。真正的高光正确性留到 Phase 0.43 / 0.44 做 shader 接入和可视化验证。

## 实施计划

### 0.42.0 文档与范围确认

- 确认本阶段只做 renderer default specular bake path。
- 明确 `ForwardPass` specular IBL 不在本阶段。
- 明确资源默认尺寸、mip 策略、采样数和失败策略。

### 0.42.1 FrameContext Specular Resource Plumbing

- 在 `FrameContext.h` forward declare `EnvironmentBrdfLutResource`。
- 新增 `prefilteredSpecularCube` 字段。
- 新增 `brdfLut` 字段。
- 更新 `framework_headers_smoke`。

### 0.42.2 Renderer Specular Resource Lifetime

- `Renderer.cpp` include specular prefilter / BRDF LUT headers。
- `DefaultRenderer` 新增 generator/resource/flag 成员。
- 构造阶段 setup generators。
- 析构阶段 reset generators 和 generated resources。
- `createDefaultEnvironment()` 创建 default specular cubemap 和 default BRDF LUT。

### 0.42.3 Renderer Default Specular Bake

- 新增 `prepareDefaultSpecularCube()`。
- 新增 `prepareDefaultBrdfLut()`。
- 在 render prepare 阶段调用。
- 新增 `resolveFramePrefilteredSpecularCube()`。
- 新增 `resolveFrameBrdfLut()`。
- `FrameContext` 填入 resolved resources。
- bake 失败时保持非 fatal warning。

### 0.42.4 Tests

- build debug preset。
- 跑 CTest。
- 跑 default sandbox smoke。
- 跑 debug orientation sandbox smoke。
- 跑 DamagedHelmet + HDR runtime smoke。
- 关注 specular bake logs，不要求视觉差异。

### 0.42.5 验证与收尾

- 自审资源生命周期和 fallback 行为。
- 检查 `FrameContext` 新字段未被 `ForwardPass` 误用。
- 更新 handoff。
- 记录测试结果。
- 提交和推送。

## 完成标准

Phase 0.42 完成时应满足：

- 默认 renderer 创建 prefiltered specular cubemap resource。
- 默认 renderer 创建 BRDF LUT resource。
- 默认 renderer 能生成 prefiltered specular cubemap mip chain。
- 默认 renderer 能生成 BRDF LUT。
- `FrameContext` 能传递 prefiltered specular cubemap / BRDF LUT 指针。
- `ForwardPass` 行为保持不变。
- `mesh.frag.hlsl` 行为保持不变。
- specular bake 失败不会让 sandbox 启动失败。
- CTest 继续通过。
- default sandbox / debug orientation / DamagedHelmet + HDR runtime smoke 继续通过。
- handoff 明确后续仍需 `ForwardPass` specular IBL。

## 实施结果

Phase 0.42 已完成 0.42.0 ~ 0.42.5：

- 新增 `FrameContext::prefilteredSpecularCube` 和 `FrameContext::brdfLut`。
- `framework_headers_smoke` 已覆盖新增 `FrameContext` 字段。
- `DefaultRenderer` 新增 `EnvironmentSpecularPrefilterGenerator` 和 `EnvironmentBrdfLutGenerator`。
- `DefaultRenderer` 新增 `m_DefaultSpecularCube` 和 `m_DefaultBrdfLut`。
- 默认 specular cubemap 使用 `256x256 RGBA16Float` full mip chain。
- 默认 BRDF LUT 使用 `256x256 RGBA16Float`。
- 默认 render prepare 阶段在 environment cubemap conversion 成功后生成 prefiltered specular cubemap。
- 默认 render prepare 阶段生成 BRDF integration LUT。
- `FrameContext` 会在资源生成成功后收到 resolved prefiltered specular cubemap / BRDF LUT 指针。
- specular bake / BRDF LUT bake 失败保持非 fatal warning，不影响 sandbox 基础启动。
- 本阶段没有改 `ForwardPass` descriptor layout。
- 本阶段没有改 `mesh.frag.hlsl` 光照公式。

最终验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_renderer ark_framework_headers_smoke
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted renderer/framework headers build passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
DamagedHelmet + local HDR environment smoke passed
```

当前边界：Phase 0.42 只完成 renderer default specular bake path 和 `FrameContext` resource plumbing；`ForwardPass` specular descriptors、fallback specular resources、mesh shader split-sum specular IBL 和 roughness mip runtime sampling 仍留给后续阶段。

## 风险与注意事项

- 默认启动会增加一次 specular cubemap mip chain bake 和 BRDF LUT bake，可能增加启动前几帧 GPU 成本。
- 当前没有异步 bake / persistent cache，所以资源尺寸不宜过大。
- `EnvironmentSpecularPrefilterGenerator` source 必须是 shader-readable cubemap；调用顺序必须在 environment cubemap conversion 成功之后。
- `EnvironmentCubeResource` mipLevels 必须与 face-mip render target views 匹配。
- BRDF LUT 是 data texture，后续 shader 使用时不能做 gamma / tone mapping。
- 本阶段新增 `FrameContext` 字段后，fake tests 应避免误判为 ForwardPass 已消费 specular resources。

## 后续 Phase 建议

Phase 0.43 建议做 `ForwardPass Specular IBL`：

- 给 `ForwardPass` descriptor layout 增加 prefiltered specular cubemap / BRDF LUT bindings。
- 增加 fallback specular resources。
- mesh shader 使用 reflection vector、roughness mip 和 BRDF LUT 计算 specular IBL。
- 将 diffuse irradiance IBL 与 specular IBL 组合为更完整的 indirect lighting。
- 增加 roughness / metallic fixture 或材质球 grid，验证 mip 选择和金属高光。

Phase 0.44 可考虑做视觉验证与质量策略：

- 截图或 readback 驱动的最小 golden validation。
- specular intensity / environment intensity policy。
- 可配置 specular cube size、BRDF LUT size 和 sampleCount。
- 更合适的默认真实验证资产。
