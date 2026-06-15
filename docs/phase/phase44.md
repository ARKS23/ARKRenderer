# Phase 0.44 Specular IBL Validation and Quality Pass

## 阶段判断

Phase 0.43 已经把 specular IBL 的运行链路接到 `ForwardPass` 和 `mesh.frag.hlsl`：

```text
HDR equirectangular environment
    -> EnvironmentResource RGBA32Float 2D texture
    -> EnvironmentCubeConverter
    -> EnvironmentCubeResource cubemap
    -> EnvironmentIrradianceGenerator
    -> diffuse irradiance cubemap
    -> EnvironmentSpecularPrefilterGenerator
    -> prefiltered specular cubemap full mip chain
    -> EnvironmentBrdfLutGenerator
    -> BRDF integration LUT
    -> FrameContext::prefilteredSpecularCube / brdfLut
    -> ForwardPass binding 18-21
    -> mesh.frag.hlsl split-sum specular IBL
```

这说明主线功能已经不是“是否能采样 specular IBL”，而是“specular IBL 是否可验证、质量策略是否清楚、后续改动是否有回归保护”。

当前仍有三个关键缺口：

1. **没有 roughness / metallic validation fixture**
   - 现有 fixture 能验证 glTF loader、材质因子、UV、alpha、texture transform 等数据链路。
   - 但没有一个固定场景能观察 roughness mip sampling、metallic diffuse 抑制和 specular highlight 变化。

2. **没有 frame color / screenshot 级验证**
   - Phase 0.38 已有最小 texture readback 和 cubemap pixel validation。
   - 但它还不是 frame screenshot system，也没有覆盖 final scene color / tone mapped output。
   - 当前 runtime smoke 只能证明 sandbox 不崩溃，不能证明画面亮度、材质差异或 specular contribution 正确。

3. **specular IBL quality policy 仍散落在 renderer 默认值里**
   - 默认 prefiltered specular cubemap size、BRDF LUT size、sample count 已能工作。
   - 但还没有被文档化为可维护策略，也没有明确哪些值是当前质量/性能权衡。

Phase 0.44 建议先做 **validation / quality pass**，不要继续扩展新效果。

## 目标

Phase 0.44 目标：

- 新增一个轻量 roughness / metallic validation fixture。
- 让 fixture 能稳定覆盖不同 roughness 和 metallic 参数组合。
- 扩展 glTF / model smoke，验证 fixture 的材质参数被正确读取和传递。
- 增加最小 specular IBL validation smoke，优先验证数据路径和可观测差异，不要求完整 golden image。
- 明确 specular IBL quality policy：
  - prefiltered specular cubemap size。
  - prefiltered specular mip count。
  - specular prefilter sample count。
  - BRDF LUT size。
  - BRDF LUT sample count。
  - fallback semantics。
- 明确 frame color validation 的当前技术边界和后续路径。
- 保持 default sandbox、debug orientation 和 DamagedHelmet + HDR runtime smoke 继续通过。

## 非目标

Phase 0.44 暂不做：

- 不重写 Phase 0.43 的 `ForwardPass` descriptor layout。
- 不新增 specular IBL shader 模型或材质扩展。
- 不做 `KHR_materials_specular`、`KHR_materials_ior`、`KHR_materials_clearcoat`、`KHR_materials_transmission`。
- 不做完整 screenshot/golden image system。
- 不引入 image diff、PNG writer、baseline asset 管理或 CI artifact 流程。
- 不把 readback 设计成 runtime async capture API。
- 不改变 `FrameRenderer` pass graph 或引入 RenderGraph。
- 不做 bloom、auto exposure、ACES、color grading 或后处理栈。
- 不做 shadow、多光源、`KHR_lights_punctual`。
- 不提交大型 HDRI 或大型模型资源。

## 当前基线

### Material 参数链路

当前 glTF loader 已读取：

```text
pbrMetallicRoughness.baseColorFactor
pbrMetallicRoughness.metallicFactor
pbrMetallicRoughness.roughnessFactor
```

对应代码路径：

```text
src/asset/GltfLoader.cpp
    -> asset::MaterialData

src/renderer/material/MaterialResource.cpp
    -> MaterialFactors

src/renderer/passes/ForwardPass.cpp
    -> MaterialUniform

shaders/mesh.frag.hlsl
    -> PbrInputs metallic / roughness
```

这意味着 Phase 0.44 不需要新增 material factor 数据结构，只需要新增验证 fixture 和测试覆盖。

### 当前可提交模型 fixture

当前 `assets/models/` 已有小型 glTF fixtures：

```text
forward_fixture.gltf
forward_multinode_fixture.gltf
forward_multidraw_fixture.gltf
alpha_modes_fixture.gltf
texture_transform_fixture.gltf
...
```

Phase 0.44 建议新增：

```text
assets/models/specular_ibl_validation_fixture.gltf
```

第一版可以使用一组简单 quad / low-poly primitives，而不是立即引入复杂球体网格。材质球视觉更接近最终 PBR 验证，但手写 glTF fixture 复杂度高；如果要控制阶段风险，可以先用平面/圆盘网格验证 material data 和 shader branch，后续再升级为球体 grid。

### Frame color readback 边界

Phase 0.38 已有：

```cpp
struct TextureReadbackDesc {
    Texture* texture = nullptr;
    Buffer* destinationBuffer = nullptr;
    Extent2D extent{};
    u32 bytesPerPixel = 0;
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
    u64 destinationOffset = 0;
    u64 rowPitch = 0;
};

bool DeviceContext::copyTextureToBuffer(const TextureReadbackDesc&);
bool Buffer::readData(void* destination, u64 size, u64 offset = 0) const;
```

但当前 `VulkanCommandContext::copyTextureToBuffer()` 的 bytes-per-pixel helper 只覆盖：

```text
RGBA8Unorm  -> 4 bytes
RGBA8Srgb   -> 4 bytes
RGBA32Float -> 16 bytes
```

而 `FrameRenderer` scene color 是：

```text
RGBA16Float scene color
usage = RenderTarget | ShaderResource
```

因此 Phase 0.44 如果要做 frame color readback，有两个选择：

1. **先不读 HDR scene color**
   - 只做 fixture + fake/unit smoke + runtime smoke。
   - 把 frame color readback 留给后续 Phase。

2. **增加最小 validation readback path**
   - 给 validation target 使用 readback 支持的格式，或扩展 readback 支持 `RGBA16Float`。
   - 如果扩展 `RGBA16Float`，还需要明确 half-float 读取和统计逻辑，不建议在本阶段扩大成 screenshot system。

建议 Phase 0.44 采用保守路线：文档化 readback 约束，先做材质 fixture、质量策略和最小统计 validation 设计；如果实现量可控，再加一个小范围 frame validation smoke。

## Validation Fixture 方案

### Fixture 目标

新增 `specular_ibl_validation_fixture.gltf`，用于固定 roughness / metallic 组合：

```text
columns: roughness
    0.05, 0.25, 0.50, 0.75, 1.00

rows: metallic
    0.00, 0.50, 1.00
```

建议总计 15 个 primitives 或 nodes：

```text
Metallic 0.00: Roughness 0.05 / 0.25 / 0.50 / 0.75 / 1.00
Metallic 0.50: Roughness 0.05 / 0.25 / 0.50 / 0.75 / 1.00
Metallic 1.00: Roughness 0.05 / 0.25 / 0.50 / 0.75 / 1.00
```

每个材质使用不同 name，便于测试和人工排查：

```text
M00_R005
M00_R025
M00_R050
M00_R075
M00_R100
M50_R005
...
M100_R100
```

材质建议：

```json
{
  "pbrMetallicRoughness": {
    "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
    "metallicFactor": 0.0,
    "roughnessFactor": 0.05
  }
}
```

当前 `GltfLoader` 基线仍要求 material 有外部 `baseColorTexture`，因此第一版 fixture 复用既有小型 `assets/textures/xiaowei.png` 作为 baseColor texture。roughness / metallic 验证仍只依赖 material factors，不使用 metallicRoughness texture，避免材质参数被贴图覆盖。

### 几何选择

优先级：

1. **低风险：grid quads**
   - 手写 glTF 最简单。
   - 可以稳定验证 material factor、draw item、descriptor 和 shader branch。
   - 缺点是 specular highlight 不如球体直观。

2. **更好视觉：low-poly spheres**
   - 更适合观察 roughness mip 和 environment reflection。
   - 需要生成更多 vertex / normal / index 数据。
   - 如果手写 JSON 过大，后续可用一个小工具生成 fixture，但生成物仍应是可提交 glTF。

建议 Phase 0.44 第一版采用 grid quads 或极简低面数球体，不引入外部模型资源。

### fixture 放置

建议文件：

```text
assets/models/specular_ibl_validation_fixture.gltf
```

如果需要二进制 buffer，优先用 glTF JSON 内 `data:` URI 还是外部 `.bin` 需要按当前 repo fixture 风格决定。现有小 fixture 多为单文件 `.gltf`，建议保持单文件以降低资产管理成本。

## 测试方案

### glTF loader smoke

扩展 `tests/gltf_loader_smoke.cpp`：

- 加载 `assets/models/specular_ibl_validation_fixture.gltf`。
- 验证 material count。
- 验证每个 material name。
- 验证 metallic / roughness factor。
- 验证 baseColorFactor。
- 验证 primitive / node instance count。

最小验收：

```text
15 materials loaded
roughness factors match expected sequence
metallic factors match expected rows
all materials opaque
all baseColor alpha = 1
```

### model resource smoke

扩展 `tests/model_resource_smoke.cpp`：

- 用 fixture 创建 `ModelResource`。
- 验证 material resources 数量。
- 验证 `MaterialResource::factors()` 中 metallic / roughness 保持不变。
- 验证 render queue / draw item 能覆盖所有 primitives。

这一步证明 fixture 不只是 asset 层能读，也能进入 renderer material resource。

### ForwardPass validation smoke

优先做 fake RHI 层 smoke，不直接做真实 screenshot：

- 构造多个 material factors。
- 捕获 `MaterialUniform`。
- 验证 roughness / metallic 按 draw item 写入。
- 验证 specular IBL resources 绑定时 `environment.w = 1`。
- 验证缺少 specular resource 时 `environment.w = 0`，fallback descriptor 仍绑定。

当前 `forward_pass_pipeline_smoke` 已覆盖单个 material 的 specular resource binding。Phase 0.44 可以扩展成多 material / multi draw path，或新增更专门的 validation test。

### Frame color validation smoke

建议分两档：

#### 0.44 最小档

- 不引入完整 screenshot。
- 不保存 PNG。
- 不做 golden image。
- 只设计和记录 frame validation constraints。
- runtime smoke 仍覆盖：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf assets/HDR/warm_restaurant_8k.hdr
```

#### 可选增强档

如果实现量可控，可新增一个统计型 smoke：

```text
ark_frame_color_validation_smoke
```

只做以下断言：

- frame readback 成功。
- 平均亮度大于 0。
- 画面不是单色。
- specular enabled 和 disabled 的统计结果有差异。
- roughness/metallic fixture 不同区域有可测差异。

注意：当前 `RGBA16Float` scene color 不能直接走现有 `copyTextureToBuffer()`，所以这个 smoke 需要先解决读取目标：

- 读取 tone-mapped RGBA8 validation target；或
- 扩展 readback 支持 `RGBA16Float`；或
- 让 `FrameRenderer` 暴露一个专用 validation copy path。

这些都不应扩大成完整 screenshot system。

## Specular IBL Quality Policy

Phase 0.44 建议把当前默认值集中记录为策略，避免后续阶段随意改：

```text
Default prefiltered specular cubemap size: 256x256
Default prefiltered specular format: RGBA16Float
Default prefiltered specular mip levels: full mip chain, 9 mips for 256x256
Default specular prefilter sample count: 128

Default BRDF LUT size: 256x256
Default BRDF LUT format: RGBA16Float
Default BRDF LUT sample count: 1024
```

建议落点：

1. **文档策略**
   - 先在 `phase44.md` 和 `codex_handoff.md` 记录默认策略。
   - 适合作为 0.44 最小闭环。

2. **代码常量命名**
   - 如果当前常量在 `Renderer.cpp` 中已经集中，则只需要补注释或 smoke。
   - 如果分散，建议收口到同一局部配置段。

3. **公开配置 API**
   - 暂不建议 Phase 0.44 做 public API。
   - 后续 renderer config / scene loading API 明确后再暴露。

### fallback semantics

必须明确：

```text
fallback specular cube + fallback BRDF LUT 只保证 descriptor 完整。
当真实 specular resources 不 ready 时，LightingUniform.environment.w 必须为 0。
shader 不应该在 specular disabled 时使用 fallback specular contribution。
```

这条必须被测试继续覆盖。

## 实施拆分

### 0.44.0 文档与范围确认

- 新增 `docs/phase/phase44.md`。
- 明确本阶段只做 validation / quality pass。
- 明确不做完整 screenshot/golden image、后处理和新材质扩展。
- 明确 frame color readback 的 `RGBA16Float` 限制。

### 0.44.1 Specular IBL Validation Fixture

- 新增 `assets/models/specular_ibl_validation_fixture.gltf`。
- 包含 roughness / metallic grid。
- 使用 material factors 驱动 roughness / metallic。
- 复用既有小型 baseColor texture，不新增大型贴图。
- 保持文件小、可提交、可人工打开。

验收：

- glTF loader 能读取 fixture。
- material factors 与预期一致。
- model 至少包含 15 个可绘制 primitives 或 instances。

### 0.44.2 Fixture Asset / Model Tests

- 扩展 `gltf_loader_smoke`。
- 扩展 `model_resource_smoke` 或 `render_scene_queue_smoke`。
- 验证 material factor 数据从 asset 进入 renderer resource。

验收：

- fixture material count / names / metallic / roughness 全部被测试覆盖。
- draw item 数量符合预期。

### 0.44.3 Specular IBL Validation Smoke

- 扩展 `forward_pass_pipeline_smoke` 或新增专用 smoke。
- 覆盖 multi material roughness / metallic uniform 写入。
- 覆盖 specular enabled / disabled 语义。
- 覆盖 fallback descriptor 不等于 fallback contribution。

验收：

- `environment.w == 1` only when scene environment + prefiltered specular cube + BRDF LUT ready。
- `environmentSpecular.x == mipLevels - 1`。
- material roughness / metallic per draw 正确。

### 0.44.4 Quality Policy 收口

- 记录当前 specular cube / BRDF LUT 默认 size、format、sample count。
- 如果常量分散，则在 renderer 内做小范围整理。
- 不新增复杂 public API。

验收：

- 默认策略在文档中可查。
- 代码中默认值位置清楚。
- tests 或 header smoke 能覆盖关键默认参数，若实现量合理。

### 0.44.5 Tests

建议命令：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_gltf_loader_smoke ark_model_resource_smoke ark_forward_pass_pipeline_smoke ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_gltf_loader_smoke.exe
build/msvc-vcpkg/Debug/ark_model_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
```

如果新增 frame validation smoke，则加入：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_frame_color_validation_smoke
build/msvc-vcpkg/Debug/ark_frame_color_validation_smoke.exe
```

### 0.44.6 验证与收尾

Runtime smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf assets/HDR/warm_restaurant_8k.hdr
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

收尾：

- 更新 `docs/codex_handoff.md`。
- 记录 fixture、tests、quality policy 和仍未完成的 screenshot/golden boundary。
- `git diff --check`。
- commit / push。

## 完成标准

Phase 0.44 完成时应满足：

- 有一个 committed roughness / metallic validation fixture。
- fixture 不依赖大型外部资源。
- glTF loader smoke 覆盖 fixture material factors。
- renderer/model smoke 覆盖 material factors 进入 renderer resource。
- ForwardPass smoke 覆盖 specular IBL enable flag、max mip 和 per-material roughness / metallic uniform。
- specular IBL default quality policy 已记录。
- fallback specular descriptor semantics 继续明确：descriptor 完整不等于 contribution enabled。
- CTest 通过。
- default sandbox / debug orientation / fixture / DamagedHelmet + HDR runtime smoke 通过。
- handoff 明确：仍没有完整 screenshot/golden image system，frame validation 如未实现必须记录为后续任务。

## 实施结果

Phase 0.44 已完成 0.44.0 ~ 0.44.6 的最小 validation / quality pass：

- 新增 `assets/models/specular_ibl_validation_fixture.gltf`。
- fixture 包含 15 个 material / mesh / node，覆盖 3 档 metallic 和 5 档 roughness：
  - metallic: `0.0`, `0.5`, `1.0`
  - roughness: `0.05`, `0.25`, `0.50`, `0.75`, `1.0`
- fixture 复用既有小型 `assets/textures/xiaowei.png` 作为 baseColor texture，以符合当前 `GltfLoader` material baseline；roughness / metallic 仍由 material factors 驱动，不使用 metallicRoughness texture。
- `gltf_loader_smoke` 已覆盖 fixture shape、material names、baseColor texture、metallic/roughness factors、opaque render state 和 node transform。
- `model_resource_smoke` 已覆盖 fixture 进入 `ModelResource` 后的 primitive/material/instance counts、fallback texture reuse、material factors、render state 和 `RenderQueue` 15 draw item 展开。
- `forward_pass_pipeline_smoke` 新增 material grid uniform capture，覆盖 15 个 draw 的 per-material roughness / metallic uniform、specular IBL enable flag 和 max prefiltered mip。
- 默认 specular IBL quality policy 已记录：
  - prefiltered specular cubemap: `256x256 RGBA16Float`
  - prefiltered specular mip levels: full mip chain, 256x256 下为 9 mips
  - specular prefilter sample count: `128`
  - BRDF LUT: `256x256 RGBA16Float`
  - BRDF LUT sample count: `1024`
- 本阶段没有新增 frame color readback / screenshot / golden image smoke，因为当前 scene color 是 `RGBA16Float`，而现有 `copyTextureToBuffer()` readback helper 仍只支持 `RGBA8Unorm` / `RGBA8Srgb` / `RGBA32Float`。

最终验证：

```powershell
cmake --build --preset msvc-vcpkg-debug --target ark_gltf_loader_smoke ark_model_resource_smoke ark_forward_pass_pipeline_smoke ark_shader_assets_smoke
build/msvc-vcpkg/Debug/ark_gltf_loader_smoke.exe
build/msvc-vcpkg/Debug/ark_model_resource_smoke.exe
build/msvc-vcpkg/Debug/ark_forward_pass_pipeline_smoke.exe
build/msvc-vcpkg/Debug/ark_shader_assets_smoke.exe
cmake --build --preset msvc-vcpkg-debug
ctest --test-dir build/msvc-vcpkg -C Debug --output-on-failure
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe --debug-orientation
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/specular_ibl_validation_fixture.gltf assets/HDR/warm_restaurant_8k.hdr
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf assets/HDR/warm_restaurant_8k.hdr
```

结果：

```text
targeted glTF/model/ForwardPass/shader smoke build passed
ark_gltf_loader_smoke passed
ark_model_resource_smoke passed
ark_forward_pass_pipeline_smoke passed
ark_shader_assets_smoke passed
full build passed
CTest: 21/21 passed
default sandbox smoke passed
debug orientation sandbox smoke passed
specular validation fixture smoke passed
specular validation fixture + local HDR smoke passed
DamagedHelmet + local HDR environment smoke passed
```

当前边界：Phase 0.44 已提供 roughness / metallic quad-grid fixture、asset/model/ForwardPass 数据链路验证和默认 specular quality policy 文档化，但仍没有完整 screenshot/golden image system、frame color readback validation、球体材质视觉 fixture、public quality config API、glTF camera、bloom、auto exposure、ACES、shadow 或多光源。

## 风险与注意事项

- 如果 fixture 使用 quads，specular highlight 视觉不如球体明显；但测试稳定、实现成本低。
- 如果 fixture 使用球体，手写 glTF 可能过大；应避免提交不必要的大型资产。
- `RGBA16Float` scene color 当前不能直接用现有 `copyTextureToBuffer()` readback；不要在文档或测试中假设完整 frame screenshot 已存在。
- 统计型 frame validation 只能证明趋势，不等价于 perceptual golden image。
- 本地 `assets/HDR/warm_restaurant_8k.hdr` 可用于人工验证，但受 `.gitignore` 保护，不应提交。
- 直接用 default procedural HDR 可以减少外部依赖，但 specular highlight 可能不如真实 HDRI 明显。
- 修改 renderer 默认 quality constants 时要确认 runtime smoke 时间，不要让默认启动明显变慢。

## 后续 Phase 建议

Phase 0.45 可以根据 0.44 结果选择：

1. **Frame Screenshot / Golden Infrastructure**
   - 支持 tone-mapped target readback。
   - 支持统计型 smoke 或小型 golden baseline。
   - 明确 CI / local-only 策略。

2. **glTF Camera / Scene Camera Selection**
   - 让 validation fixture 和真实模型拥有稳定相机入口。
   - 减少 sandbox camera 默认位置对验证的影响。

3. **Post-process Quality**
   - bloom、auto exposure、ACES / filmic tone mapping。
   - 前提是已有 frame validation 或明确人工验证路径。

4. **Renderer Resource / Scene Loading API**
   - 把当前 sandbox/default scene 过渡策略收口成正式入口。
