# Phase 0.20 glTF Material Render States

## 阶段判断

Phase 0.19 已经完成 glTF 2.0 core sampler / textureInfo 的最小闭环：

```text
glTF material textureInfo / sampler
    -> MaterialData texture slots
    -> ModelResource / TextureCache / TextureResource
    -> RHI SamplerDesc
    -> ForwardPass descriptor binding
```

当前真实 glTF 模型已经可以走默认 ForwardPass 渲染路径，但材质可见性状态仍不完整：

- glTF `alphaMode` 尚未读取。
- glTF `alphaCutoff` 尚未读取。
- glTF `doubleSided` 尚未读取。
- shader 当前返回 baseColor alpha，但没有 alpha mask discard。
- ForwardPass 当前只有一个 pipeline，无法按 opaque / masked / blended / double-sided 做状态分流。
- RHI `BlendStateDesc` 当前只有 `enableBlend`，不足以明确表达标准 alpha blending。

因此 Phase 0.20 的重点是补齐 glTF material render states 的最小闭环，让真实模型的基础可见性更接近 glTF 2.0 语义。

## 目标

Phase 0.20 目标：

- 在 asset 层表达 `alphaMode`、`alphaCutoff`、`doubleSided`。
- 在 `GltfLoader` 中读取 glTF 2.0 material render states。
- 在 renderer 层缓存 material render state，并提供给 ForwardPass。
- 补齐 RHI 最小 color blend 描述，使 Vulkan 后端能创建标准 alpha blend pipeline。
- ForwardPass 按 material render state 选择最小 pipeline variant。
- shader 支持 alpha mask discard。
- 保持现有 texture slots、sampler、PBR factors、normal mapping 路径不回退。
- 增加小 fixture 和 smoke tests，避免依赖 DamagedHelmet 才能验证。

## 非目标

Phase 0.20 暂不做：

- 不做 order-independent transparency。
- 不做完整 transparent sorting；最多记录限制或做保守队列顺序。
- 不做多 pass depth pre-pass。
- 不做 RenderGraph 重构。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 `TEXCOORD_1`。
- 不做 `KHR_texture_transform`。
- 不做 skin / animation / morph target。
- 不做 glTF extensions，如 `KHR_materials_transmission`、`KHR_materials_clearcoat`。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase17.md
docs/phase/phase18.md
docs/phase/phase19.md
```

边界要求：

- `asset/` 只读取 glTF material render state，输出 CPU 数据，不创建 RHI 对象。
- `asset/` 不依赖 renderer/RHI/Vulkan。
- `renderer/material/` 负责把 asset material render state 缓存为 renderer 可消费数据。
- `ForwardPass` 负责根据 material render state 选择 pipeline variant。
- `rhi/` 只暴露通用 blend / raster state 描述，不暴露 Vulkan 类型。
- `rhi/vulkan/` 负责把 RHI blend / raster state 映射到 Vulkan pipeline create info。
- `RenderQueue` 暂不承担复杂透明排序；若本阶段需要记录排序限制，应写进文档和 TODO。

## glTF 语义

glTF 2.0 material 相关字段：

```json
{
  "alphaMode": "OPAQUE" | "MASK" | "BLEND",
  "alphaCutoff": 0.5,
  "doubleSided": false
}
```

推荐映射：

```cpp
namespace ark::asset {
    enum class AlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    struct MaterialData {
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };
}
```

规则：

- `alphaMode` 缺失时为 `Opaque`。
- `alphaCutoff` 只对 `Mask` 有效，缺失时为 `0.5`。
- `doubleSided` 缺失时为 `false`。
- 遇到未知 `alphaMode` 时 warning，并 fallback 到 `Opaque`。

## Pipeline 策略

### Opaque

```text
alphaMode = Opaque
blend = disabled
depthTest = true
depthWrite = true
shader alpha = ignored or treated as 1 for visibility
```

### Mask

```text
alphaMode = Mask
blend = disabled
depthTest = true
depthWrite = true
shader:
    if baseColorAlpha < alphaCutoff:
        discard
```

### Blend

```text
alphaMode = Blend
blend = enabled
depthTest = true
depthWrite = false
shader alpha = output alpha
```

标准 alpha blend：

```text
srcColor = SrcAlpha
dstColor = OneMinusSrcAlpha
colorOp = Add
srcAlpha = One
dstAlpha = OneMinusSrcAlpha
alphaOp = Add
```

说明：

- Phase 0.20 不保证 blended draw 的全局排序正确性。
- 如果本阶段不做透明排序，则应在文档和日志/TODO 中明确该限制。
- `Mask` 不需要 blend，适合 foliage / cutout / hard alpha。

## doubleSided 策略

glTF `doubleSided=true` 表示材质两面可见。

建议映射：

```text
doubleSided = true:
    cullMode = None

doubleSided = false:
    cullMode = Back
```

当前 ForwardPass 使用 `CullMode::None`。Phase 0.20 可以切换为：

- 默认 opaque/mask/blend pipeline 使用 `Back` culling。
- double-sided variant 使用 `None`。

审核点：

- 需要确认当前 glTF winding、projection、frontFace 与 Vulkan culling 方向一致。
- 如果切换到 `Back` culling 导致现有 fixture 消失，应优先修正 frontFace / coordinate convention，而不是长期保持全局 `None`。
- 如果风险较高，可以第一步先保存 `doubleSided` 并创建 variant，但默认仍保持 `None`；文档必须说明这是过渡策略。

## RHI BlendState 补齐

当前：

```cpp
struct ColorBlendAttachmentDesc {
    bool enableBlend = false;
};
```

建议补齐为最小通用描述：

```cpp
enum class BlendFactor {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
};

enum class BlendOp {
    Add,
};

struct ColorBlendAttachmentDesc {
    bool enableBlend = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
};
```

Phase 0.20 只需要映射上述最小枚举到 Vulkan：

```text
Zero -> VK_BLEND_FACTOR_ZERO
One -> VK_BLEND_FACTOR_ONE
SrcAlpha -> VK_BLEND_FACTOR_SRC_ALPHA
OneMinusSrcAlpha -> VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
Add -> VK_BLEND_OP_ADD
```

## Shader 策略

当前 `mesh.frag.hlsl` 读取 baseColor alpha 并返回：

```hlsl
return float4(evaluateDirectLighting(inputs, input.worldPosition), inputs.baseColor.a);
```

Phase 0.20 建议扩展 material uniform：

```hlsl
struct MaterialUniform {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint alphaMode;
};
```

或采用对齐更明确的 float 字段：

```hlsl
float alphaCutoff;
float alphaMode;
float2 padding;
```

Mask 逻辑：

```hlsl
if (g_Material.alphaMode == AlphaModeMask && inputs.baseColor.a < g_Material.alphaCutoff) {
    discard;
}
```

注意：

- C++ `MaterialUniform` 与 HLSL cbuffer 大小必须保持 16-byte 对齐。
- `Opaque` 可以输出 alpha=1，避免 swapchain alpha 造成混淆。
- `Blend` 输出真实 alpha。

## ForwardPass Pipeline Variant

当前 ForwardPass 只缓存一个 pipeline：

```cpp
Scope<rhi::PipelineState> m_Pipeline;
```

Phase 0.20 建议引入最小 pipeline key：

```cpp
enum class ForwardAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct ForwardPipelineKey {
    rhi::Format colorFormat;
    rhi::Format depthFormat;
    ForwardAlphaMode alphaMode;
    bool doubleSided;
};
```

缓存方式：

```cpp
std::map<ForwardPipelineKey, Scope<rhi::PipelineState>> m_Pipelines;
```

绘制时：

```text
for draw item:
    key = makePipelineKey(material.renderState(), swapchain formats)
    pipeline = getOrCreatePipeline(key)
    bind pipeline
    bind descriptor set
    draw
```

审核点：

- pipeline variant 创建仍在 `ForwardPass` 内，不泄露到 asset 层。
- descriptor layout 不因 alpha mode 改变。
- shader module 可复用。
- pipeline layout 可复用。
- resize / swapchain format 改变时，key 中 format 改变即可创建新 pipeline；后续可清理旧 pipeline。

## RenderQueue 与透明排序

Phase 0.20 不建议实现完整 transparent sorting。

最低策略：

- `RenderQueue` 维持当前顺序。
- `ForwardPass` 按 draw item 顺序绘制。
- `Blend` pipeline depth write off。
- 文档明确当前 blended material 可能排序不正确。

可选小优化：

- `RenderQueue` 将 draw items 粗分为 opaque/mask 在前、blend 在后。
- 不做 back-to-front depth sort。

建议本阶段优先选择最低策略，除非测试 fixture 明确需要 blended draw 在 opaque 后面。

## 实施顺序

### 0.20.0 文档与范围确认

目标：
- 新增 `docs/phase/phase20.md`。
- 明确本阶段主线是 glTF material render states。
- 明确不做 HDR/IBL/RenderGraph/透明排序。

审核点：
- 不重复 Phase 0.19 sampler 工作。
- 不提前进入大规模 renderer 重构。

### 0.20.1 MaterialData render state

目标：
- asset 层新增 `AlphaMode`。
- `MaterialData` 新增 `alphaMode`、`alphaCutoff`、`doubleSided`。
- 默认值符合 glTF 2.0。

审核点：
- asset 层不依赖 RHI。
- legacy material tests 不需要大量改动。

### 0.20.2 GltfLoader alpha / doubleSided 读取

目标：
- 读取 glTF `alphaMode`。
- 读取 `alphaCutoff`。
- 读取 `doubleSided`。
- 未知 alphaMode warning 并 fallback 到 Opaque。

审核点：
- 缺省值符合 glTF。
- fixture 小而可读。
- DamagedHelmet optional smoke 不回退。

### 0.20.3 RHI blend state 补齐

目标：
- 增加最小 `BlendFactor` / `BlendOp`。
- 扩展 `ColorBlendAttachmentDesc`。
- Vulkan 后端映射到 `VkPipelineColorBlendAttachmentState`。

审核点：
- RHI 不暴露 Vulkan 类型。
- 默认 blend state 行为保持关闭。
- 旧 pipeline 创建路径不需要显式填新字段。

### 0.20.4 MaterialResource render state

目标：
- `MaterialResource` 缓存 alpha mode、alpha cutoff、double sided。
- 提供 renderer 查询接口。
- `MaterialUniform` 包含 alpha cutoff / alpha mode。

审核点：
- texture resource 生命周期不变。
- MaterialResource 不负责创建 pipeline。

### 0.20.5 ForwardPass pipeline variants

目标：
- 按 alpha mode / double sided / swapchain format 创建 pipeline variant。
- Opaque / Mask depth write on。
- Blend depth write off。
- Blend pipeline 启用标准 alpha blend。

审核点：
- descriptor layout 不变。
- pipeline layout 不变。
- pipeline variant key 清晰、可测试。
- 不引入 RenderGraph。

### 0.20.6 Shader alpha mask / alpha output

目标：
- `mesh.frag.hlsl` 支持 alpha mask discard。
- Opaque 输出 alpha 1。
- Blend 输出 baseColor alpha。

审核点：
- HLSL / C++ uniform 对齐一致。
- shader source smoke 覆盖关键字段。

### 0.20.7 Tests 与 sandbox smoke

目标：
- 新增小 glTF fixture 覆盖 Opaque / Mask / Blend / doubleSided。
- `gltf_loader_smoke` 覆盖 alphaMode / alphaCutoff / doubleSided。
- `model_resource_smoke` 覆盖 MaterialResource render state。
- pipeline desc smoke 覆盖 blend state 和 cull mode。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

### 0.20.8 Phase 0.20 收尾

目标：
- 更新本文档实现状态。
- 记录当前 blended sorting 限制。
- 记录后续 transparent sorting / HDR / IBL 计划。
- 只在用户明确要求时同步 `docs/codex_handoff.md`。

## 审核检查点

- `asset/` 不依赖 renderer/RHI/Vulkan。
- glTF `alphaMode` 默认值和 fallback 正确。
- `alphaCutoff` 只影响 Mask。
- `doubleSided` 能影响 cull mode 或被明确记录为过渡限制。
- RHI blend state 不泄露 Vulkan 类型。
- Vulkan blend mapping 明确。
- ForwardPass pipeline variant 不破坏 descriptor layout。
- Mask shader 会 discard。
- Blend pipeline depth write off。
- 当前不声明完整透明排序正确性。
- default sandbox 行为不回退。
- DamagedHelmet optional smoke 不回退。

## 验证计划

必须通过：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 完成标准

Phase 0.20 完成时应满足：

- glTF `alphaMode` / `alphaCutoff` / `doubleSided` 被 asset 层读取并保存。
- MaterialResource 能暴露 render state 给 ForwardPass。
- RHI / Vulkan 能表达并创建标准 alpha blend pipeline。
- ForwardPass 能根据 material state 创建并选择 pipeline variant。
- Mask material 能执行 alpha cutoff discard。
- Blend material 能启用 alpha blending，且 depth write 关闭。
- double-sided material 能关闭背面剔除，或有清晰过渡记录。
- tests 覆盖 loader、material resource、pipeline desc、shader source。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持完整透明排序、OIT、HDR、IBL、texture transform、多 UV。

## 当前实现状态

已完成 0.20.0 ~ 0.20.8：

- `MaterialData` 已新增 `AlphaMode`、`alphaCutoff`、`doubleSided`，默认值对齐 glTF 2.0。
- `GltfLoader` 已读取 `alphaMode`、`alphaCutoff`、`doubleSided`，未知 alphaMode 会 warning 并 fallback 到 Opaque。
- RHI 已补齐最小 `BlendFactor` / `BlendOp`，Vulkan pipeline creation 已映射到 `VkPipelineColorBlendAttachmentState`。
- `MaterialResource` 已缓存 material render state，`ForwardPass` 按 color/depth format、alpha mode、doubleSided 建立 pipeline variant。
- Blend material 使用标准 alpha blending，并关闭 depth write；Opaque / Mask 保持 depth write。
- `mesh.frag.hlsl` 已支持 Mask discard，并让 Blend 输出 base color alpha。
- 新增 `assets/models/alpha_modes_fixture.gltf`，覆盖 Opaque / Mask / Blend / doubleSided 的 loader 与 material resource 路径。
- `framework_headers_smoke`、`gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke` 已覆盖 Phase 0.20 的关键字段和路径。

当前保守限制：

- `doubleSided` 已进入 asset、material resource 和 pipeline variant key，但当前 ForwardPass 仍保持 `CullMode::None`，避免在确认 glTF winding / projection 前引入剔除回归；后续确认 front face 后再收紧到 doubleSided 控制 cull mode。
- Blend draw 仍未做 back-to-front sorting；当前只保证 pipeline state 和 shader output 正确，不声明透明排序完全正确。
- 本轮不修改 `docs/codex_handoff.md`，除非后续明确要求同步。

本轮验证结果：

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过，8/8 tests passed。
- default sandbox smoke 通过。
- DamagedHelmet sandbox smoke 通过；日志中存在已知 tangent generation skipped degenerate triangles warning，不影响启动。
- Phase 0.20 收尾完成；未同步 `docs/codex_handoff.md`。

## 后续 Phase 建议

Phase 0.20 后建议进入：

1. `TEXCOORD_1` / 多 UV 通道支持。
2. `KHR_texture_transform` 最小支持。
3. transparent sorting / render queue 分桶。
4. 可配置 scene light / camera。
5. HDR framebuffer / tone mapping。
6. IBL / environment map / BRDF LUT。
7. renderer 级资源 / 场景加载入口整理。
