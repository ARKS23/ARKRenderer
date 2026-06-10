# Phase 0.21 glTF TEXCOORD_1 与 Texture Slot UV Selection

## 阶段判断

Phase 0.19 已经把 glTF textureInfo / sampler 语义读入 asset 层：

```text
glTF textureInfo.texCoord
    -> MaterialTextureSlotData::texCoord
    -> MaterialResource / ForwardPass descriptor binding
```

Phase 0.20 已经补齐 alphaMode / alphaCutoff / doubleSided 与 ForwardPass pipeline variant。

当前剩余的 glTF core material 正确性缺口是：`texCoord` 已经被读取和保存，但实际渲染仍只使用 `TEXCOORD_0`。如果模型的 baseColor、normal、metallicRoughness、occlusion 或 emissive texture 指定 `texCoord=1`，当前 shader 仍会用 `uv0` 采样，视觉结果会错误。

因此 Phase 0.21 的重点是补齐 `TEXCOORD_1` 的最小闭环，并让每个 material texture slot 能选择 `uv0` 或 `uv1`。

## 目标

Phase 0.21 目标：

- `MeshVertex` 支持 `uv1`。
- `GltfLoader` 读取 `TEXCOORD_1`。
- `MaterialTextureSlotData::texCoord` 能进入 renderer material uniform。
- `ForwardPass` vertex layout 增加 `uv1`。
- `mesh.vert.hlsl` / `mesh.frag.hlsl` 传递并选择 `uv0` / `uv1`。
- baseColor、normal、metallicRoughness、occlusion、emissive texture slot 各自使用自己的 `texCoord`。
- 保持 Phase 0.20 alpha、blend、normal mapping、sampler、mipmap 路径不回退。
- 新增小 glTF fixture 覆盖不同 texture slot 使用 `TEXCOORD_1`。

## 非目标

Phase 0.21 暂不做：

- 不做 `TEXCOORD_2+`。
- 不做 `KHR_texture_transform`。
- 不做 texture coordinate set 的动态数组或 bindless。
- 不做 vertex format runtime reflection。
- 不做透明排序。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 skin / animation / morph target。
- 不做压缩纹理、KTX、BasisU。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase19.md
docs/phase/phase20.md
```

边界要求：

- `asset/` 只读取 glTF vertex attribute 和 material textureInfo，输出 CPU 数据。
- `asset/` 不依赖 renderer/RHI/Vulkan。
- `renderer/material/` 负责把每个 texture slot 的 `texCoord` 缓存为 shader uniform 可消费的数据。
- `ForwardPass` 负责 vertex layout、material uniform 和 shader descriptor 绑定。
- RHI / Vulkan 不需要知道 texture slot 使用哪个 UV set。
- `RenderQueue` 不参与 UV 选择，不拥有 GPU resource。

## glTF 语义

glTF textureInfo:

```json
{
  "index": 0,
  "texCoord": 0
}
```

glTF mesh primitive attributes:

```json
{
  "TEXCOORD_0": accessorIndex,
  "TEXCOORD_1": accessorIndex
}
```

Phase 0.21 支持策略：

- `texCoord == 0`：使用 `uv0`。
- `texCoord == 1`：使用 `uv1`。
- `texCoord > 1`：warning，并 fallback 到 `uv0`。
- primitive 缺失 `TEXCOORD_1` 且有 slot 使用 `texCoord=1`：warning，并 fallback 到 `uv0` 或复制 `uv0` 到 `uv1`。

建议采用复制 `uv0` 到 `uv1` 的策略：

- loader 不需要知道后续哪些 material slot 会使用 `texCoord=1`。
- shader 可以始终读取 `uv1`，vertex layout 稳定。
- 真实模型不会因为单个 UV set 缺失而直接加载失败。
- warning 明确记录语义降级。

## CPU 数据结构

### MeshVertex

当前：

```cpp
struct MeshVertex {
    float position[3]{};
    float normal[3]{};
    float uv0[2]{};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};
```

建议改为：

```cpp
struct MeshVertex {
    float position[3]{};
    float normal[3]{};
    float uv0[2]{};
    float uv1[2]{};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};
```

注意：

- `MeshVertex` layout 改变会影响 vertex buffer upload、ForwardPass vertex layout、tests 和 fixture。
- tangent generation 仍使用 `uv0`。Phase 0.21 不改变 tangent 生成策略。
- 如果后续 normal texture 指定 `texCoord=1`，严格 tangent basis 应基于对应 UV set 生成；本阶段先记录限制。

### MaterialTextureSlotData

`MaterialTextureSlotData::texCoord` 已存在。Phase 0.21 不需要在 asset 层新增字段。

需要新增 renderer 层 uniform 表达：

```cpp
struct MaterialUniform {
    ...
    float baseColorTexCoord;
    float normalTexCoord;
    float metallicRoughnessTexCoord;
    float occlusionTexCoord;
    float emissiveTexCoord;
};
```

也可以用 `uint` / `int`，但当前 HLSL constant buffer 已大量使用 float，继续使用 float 可以减少对齐和跨编译器布局风险。

## Shader 策略

### Vertex Shader

`mesh.vert.hlsl` 增加：

```hlsl
[[vk::location(3)]] float2 uv1 : TEXCOORD1;
```

由于当前 tangent 使用 location 3，建议新 layout：

```text
location 0: position
location 1: normal
location 2: uv0
location 3: uv1
location 4: tangent
```

对应 C++ `ForwardPass` vertex layout 必须同步。

### Fragment Shader

新增 helper：

```hlsl
float2 selectUv(float selector, float2 uv0, float2 uv1) {
    return selector >= 0.5f ? uv1 : uv0;
}
```

每个 slot 使用自己的 UV：

```hlsl
float2 baseColorUv = selectUv(g_Material.baseColorTexCoord, input.uv0, input.uv1);
float2 normalUv = selectUv(g_Material.normalTexCoord, input.uv0, input.uv1);
```

审核点：

- baseColor / normal / metallicRoughness / occlusion / emissive 不能共用一个全局 UV selector。
- alpha mask 使用 baseColor slot 采样结果，因此随 baseColorTexCoord 生效。
- normal map 如果使用 `uv1`，当前 tangent basis 仍基于 `uv0` 生成，需记录为后续限制。

## ForwardPass 策略

Phase 0.21 不改变 descriptor layout，不新增 texture binding。

需要修改：

- vertex input layout 增加 `uv1` attribute。
- tangent location 从 3 移到 4。
- `MaterialUniform` 增加每个 texture slot 的 texCoord selector。
- `makeMaterialUniform()` 从 `MaterialResource` 读取 texture slot texCoord。

建议 `MaterialResource` 缓存：

```cpp
struct MaterialTextureCoordinateSet {
    u32 baseColor = 0;
    u32 normal = 0;
    u32 metallicRoughness = 0;
    u32 occlusion = 0;
    u32 emissive = 0;
};
```

或者在现有 `MaterialTextureSet` 旁新增 `MaterialTextureCoordinateSet`。不要把 `texCoord` 塞进 `TextureResource`，因为同一 texture resource 可以被不同 material slot 用不同 UV set 采样。

## GltfLoader 策略

需要新增：

- 常量 `Texcoord1AttributeName = "TEXCOORD_1"`。
- 尝试读取 `TEXCOORD_1` accessor。
- 如果存在，要求 count 与 POSITION 一致，类型为 float vec2。
- 如果不存在，把 `uv0` 复制到 `uv1`。
- `resolveTextureSlot()` 对 `texCoord > 1` warning，并将 slot texCoord fallback 到 0 或 clamp 到 0。

建议 fallback 规则：

```text
slot.texCoord == 0 -> 0
slot.texCoord == 1 -> 1
slot.texCoord > 1  -> warning + 0
```

理由：

- Phase 0.21 只声明支持两个 UV sets。
- 使用 unsupported texCoord 时继续用 uv0，比直接采样未定义数据更稳。

## Fixture 策略

新增或扩展小 glTF fixture：

```text
assets/models/texcoord1_fixture.gltf
```

建议内容：

- 一个 quad primitive。
- 同时包含 `TEXCOORD_0` 和 `TEXCOORD_1`。
- 至少两个 material texture slot 使用不同 texCoord：
  - baseColorTexture.texCoord = 0
  - normalTexture.texCoord = 1
  - occlusionTexture.texCoord = 1
- 继续复用 `assets/textures/xiaowei.png`。

测试应覆盖：

- loader 读取 `uv1` 数据。
- material slot texCoord 保留。
- ModelResource / MaterialResource 保留 texture coordinate set。
- shader source 包含 `uv1` 和 per-slot selector。

## 实施顺序

### 0.21.0 文档与范围确认

目标：

- 新增 `docs/phase/phase21.md`。
- 明确 Phase 0.21 主线是 `TEXCOORD_1` 与 per-slot UV selection。
- 明确不做 `KHR_texture_transform`、透明排序、IBL、HDR。

审核点：

- 不重复 Phase 0.19 sampler 工作。
- 不改变 descriptor layout。
- 不引入 RenderGraph 或 bindless。

### 0.21.1 MeshVertex uv1

目标：

- `MeshVertex` 新增 `uv1`。
- 更新 mesh data smoke 中的 size/layout 预期。
- 保持 vertex/index upload 路径不变。

审核点：

- `offsetof()` 相关 vertex layout 后续同步。
- 不改 RHI buffer 接口。

### 0.21.2 GltfLoader TEXCOORD_1 读取

目标：

- 读取 `TEXCOORD_1`。
- 缺失时复制 `uv0` 到 `uv1`。
- unsupported `texCoord > 1` warning + fallback。

审核点：

- POSITION/NORMAL/TEXCOORD_0 仍是最小必需输入。
- TEXCOORD_1 是可选输入。
- tangent generation 仍基于 uv0，并在文档记录限制。

### 0.21.3 MaterialResource texture coordinate set

目标：

- `MaterialResource` 缓存每个 texture slot 的 texCoord。
- `ForwardPass` 能从 material 查询这些 texCoord。

审核点：

- `TextureResource` 不保存 texCoord。
- `TextureCache` key 不包含 texCoord。
- 同一 texture resource 可被不同 UV set 重用。

### 0.21.4 ForwardPass vertex layout 与 material uniform

目标：

- vertex layout 增加 `uv1`。
- tangent location 改为 4。
- `MaterialUniform` 增加 per-slot texCoord selector。

审核点：

- C++ uniform size 与 HLSL constant buffer 对齐。
- descriptor set layout 不变。
- pipeline layout 不变。

### 0.21.5 Shader UV selection

目标：

- `mesh.vert.hlsl` 传递 `uv1`。
- `mesh.frag.hlsl` 每个 texture slot 按自己的 texCoord selector 选择 uv。
- alpha mask 继续基于 baseColor texture 结果。

审核点：

- normal / MR / AO / emissive 不能错误复用 baseColor UV。
- `uv1` 命名和 location 与 C++ vertex layout 一致。

### 0.21.6 Fixtures 与 tests

目标：

- 新增 `texcoord1_fixture.gltf`。
- `gltf_loader_smoke` 覆盖 uv1 数据和 texture slot texCoord。
- `model_resource_smoke` 覆盖 MaterialResource texture coordinate set。
- `shader_assets_smoke` 覆盖 shader uv1 / selector path。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

### 0.21.7 Phase 0.21 收尾

目标：

- 更新本文档实现状态。
- 记录当前限制：
  - 只支持 `TEXCOORD_0/1`。
  - `texCoord > 1` fallback。
  - tangent generation 仍基于 uv0。
  - 不支持 `KHR_texture_transform`。
- 只在用户明确要求时同步 `docs/codex_handoff.md`。

## 审核检查点

- `asset/` 不依赖 renderer/RHI/Vulkan。
- `TextureCache` 不因为 texCoord 产生重复 texture resource。
- `TextureResource` 不保存 material slot 的 UV 选择。
- `MaterialResource` 保存 per-slot texCoord。
- `ForwardPass` vertex layout 与 shader location 完全一致。
- HLSL / C++ material uniform 对齐一致。
- baseColor、normal、metallicRoughness、occlusion、emissive 分别使用自己的 UV selector。
- `TEXCOORD_1` 缺失时行为明确，不采样未定义数据。
- `texCoord > 1` 有 warning / fallback。
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

Phase 0.21 完成时应满足：

- `MeshVertex` 包含 `uv1`。
- `GltfLoader` 能读取 glTF `TEXCOORD_1`。
- 缺失 `TEXCOORD_1` 时 `uv1` 有明确 fallback。
- `texCoord > 1` 不会静默错误采样。
- MaterialResource 能暴露 per-slot texture coordinate set。
- ForwardPass vertex layout 和 shader location 同步。
- shader 能按每个 texture slot 选择 `uv0` / `uv1`。
- tests 覆盖 loader、material resource、shader source 和 header smoke。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 `KHR_texture_transform`、`TEXCOORD_2+`、IBL、HDR、透明排序。

## 当前实现状态

已完成 0.21.0 ~ 0.21.7：

- `MeshVertex` 已新增 `uv1`。
- `GltfLoader` 已读取可选 `TEXCOORD_1`，缺失时复制 `uv0` 到 `uv1`。
- `MaterialResource` 已缓存 baseColor、normal、metallicRoughness、occlusion、emissive 的 per-slot texCoord。
- `ForwardPass` vertex layout 已增加 `uv1`，tangent location 调整到 4。
- `MaterialUniform` 已增加 per-slot texCoord selector。
- `mesh.vert.hlsl` 已传递 `uv1`。
- `mesh.frag.hlsl` 已按每个 texture slot 选择 `uv0` / `uv1`。
- 新增 `assets/models/texcoord1_fixture.gltf`，覆盖 `TEXCOORD_1` 和多 texture slot texCoord。
- `gltf_loader_smoke`、`model_resource_smoke`、`shader_assets_smoke`、`framework_headers_smoke`、`mesh_data_smoke` 已覆盖 Phase 0.21 关键路径。

当前保守限制：

- 只支持 `TEXCOORD_0` / `TEXCOORD_1`。
- `texCoord > 1` 会 warning 并 fallback 到 0。
- 缺失 `TEXCOORD_1` 时 `uv1` 复制 `uv0`，避免 shader 读取未定义数据。
- tangent generation 仍基于 `uv0`；如果 normal texture 使用 `texCoord=1`，严格 tangent basis 仍是后续改进项。
- 仍不支持 `KHR_texture_transform`。
- 本轮不修改 `docs/codex_handoff.md`，除非后续明确要求同步。

本轮验证结果：

- `cmake --build --preset msvc-vcpkg-local-debug` 通过。
- `ctest --preset msvc-vcpkg-local-debug` 通过，8/8 tests passed。
- default sandbox smoke 通过。
- DamagedHelmet sandbox smoke 通过；日志中存在已知 tangent generation skipped degenerate triangles warning，不影响启动。
- Phase 0.21 收尾完成。

## 后续 Phase 建议

Phase 0.21 后建议进入：

1. `KHR_texture_transform` 最小支持。
2. transparent sorting / RenderQueue 分桶。
3. doubleSided culling 精确化。
4. 可配置 scene light / camera。
5. HDR framebuffer / tone mapping。
6. IBL / environment map / BRDF LUT。
7. renderer 级资源 / 场景加载入口整理。
