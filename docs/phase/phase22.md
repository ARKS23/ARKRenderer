# Phase 0.22 KHR_texture_transform 最小支持

## 阶段判断

Phase 0.19 ~ 0.21 已经完成 glTF core texture sampling 的主要闭环：

```text
textureInfo.index / texCoord / sampler
    -> MaterialTextureSlotData
    -> TextureCache / TextureResource / SamplerDesc
    -> MaterialResource
    -> ForwardPass material uniform
    -> mesh shader uv0 / uv1 selection
```

当前真实 glTF 材质仍缺一个常见扩展：`KHR_texture_transform`。很多 DCC 导出的资产会通过该扩展表达贴图平移、缩放、旋转，甚至覆盖 `texCoord`。如果忽略它，即使 `TEXCOORD_0/1` 和 sampler 已经正确，贴图仍可能错位。

因此 Phase 0.22 的重点是补齐 `KHR_texture_transform` 的最小闭环，让每个 material texture slot 在选择 `uv0/uv1` 后，可以独立应用 offset / scale / rotation。

## 目标

Phase 0.22 目标：

- 在 asset 层表达 per-slot texture transform。
- `GltfLoader` 读取 `KHR_texture_transform` 的 `offset`、`scale`、`rotation`、`texCoord`。
- `MaterialResource` 缓存每个 texture slot 的 transform。
- `ForwardPass` material uniform 携带 per-slot transform。
- `mesh.frag.hlsl` 在每个 texture slot 采样前应用 transform。
- 保持 Phase 0.21 的 `TEXCOORD_0/1`、per-slot UV selection、sampler、alpha、normal mapping 路径不回退。
- 新增小 glTF fixture 和 smoke tests 覆盖 transform 数据流。

## 非目标

Phase 0.22 暂不做：

- 不做 `TEXCOORD_2+`。
- 不做任意 UV set 数组。
- 不做 texture transform animation。
- 不做非 glTF 扩展的自定义 UV 节点系统。
- 不做 bindless / material table 重构。
- 不做透明排序。
- 不做 doubleSided culling 精确化。
- 不做 HDR framebuffer / tone mapping。
- 不做 IBL / environment map / BRDF LUT。
- 不做 RenderGraph 重构。
- 不做完整 glTF extensions 系统；只解析本阶段需要的 `KHR_texture_transform`。

## 模块边界

继续遵守现有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase19.md
docs/phase/phase20.md
docs/phase/phase21.md
```

边界要求：

- `asset/` 只读取 glTF extension，输出 CPU 数据。
- `asset/` 不依赖 renderer/RHI/Vulkan。
- `renderer/material/` 负责缓存 material texture transform。
- `ForwardPass` 负责把 transform 写入 material uniform。
- `TextureResource` 不保存 transform。
- `TextureCache` key 不包含 transform。
- RHI / Vulkan 不需要知道 texture transform。
- `RenderQueue` 不参与 UV transform。

## glTF 语义

`KHR_texture_transform` 挂在 textureInfo 的 extensions 下：

```json
{
  "baseColorTexture": {
    "index": 0,
    "texCoord": 0,
    "extensions": {
      "KHR_texture_transform": {
        "offset": [0.1, 0.2],
        "rotation": 0.5,
        "scale": [2.0, 2.0],
        "texCoord": 1
      }
    }
  }
}
```

字段默认值：

- `offset`: `[0, 0]`
- `rotation`: `0`
- `scale`: `[1, 1]`
- extension 内 `texCoord` 缺失时使用 textureInfo 外层 `texCoord`

Phase 0.22 支持策略：

- 支持 `offset`、`rotation`、`scale`。
- 支持 extension 内 `texCoord` override。
- 只支持最终 `texCoord` 为 0 或 1。
- `texCoord > 1` warning 并 fallback 到 0。
- 如果 textureInfo 没有 extension，则 transform 为 identity。

## 数据结构建议

### asset 层

建议新增：

```cpp
struct TextureTransformData {
    float offset[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotation = 0.0f;
    bool hasTransform = false;
};
```

挂到：

```cpp
struct MaterialTextureSlotData {
    Path path;
    u32 texCoord = 0;
    TextureSamplerData sampler;
    TextureTransformData transform;
    bool hasSampler = false;
};
```

注意：

- `hasTransform` 只表达是否从 glTF extension 显式读取到 transform。
- 即使 `hasTransform=false`，shader uniform 也可以写 identity transform，减少 shader 分支。
- `transform.texCoord` 不单独存储；extension 的 texCoord override 直接写回 slot.texCoord。

### renderer 层

建议新增：

```cpp
struct MaterialTextureTransform {
    float offset[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotation = 0.0f;
};

struct MaterialTextureTransformSet {
    MaterialTextureTransform baseColor;
    MaterialTextureTransform normal;
    MaterialTextureTransform metallicRoughness;
    MaterialTextureTransform occlusion;
    MaterialTextureTransform emissive;
};
```

放在 `MaterialResource` 中，与 `MaterialTextureCoordinateSet` 并列。

不要放在：

- `TextureResource`：同一 texture 可在不同材质或不同 slot 中有不同 transform。
- `TextureCache`：transform 不影响 GPU texture 内容和 sampler。
- RHI descriptor：transform 是 shader uniform，不是 descriptor 状态。

## Shader 策略

Phase 0.21 当前流程：

```hlsl
uv = selectUv(slotTexCoord, input.uv0, input.uv1)
sample(texture, uv)
```

Phase 0.22 改为：

```hlsl
uv = selectUv(slotTexCoord, input.uv0, input.uv1)
uv = transformUv(uv, slotOffset, slotScale, slotRotation)
sample(texture, uv)
```

建议 helper：

```hlsl
float2 transformUv(float2 uv, float2 offset, float2 scale, float rotation) {
    const float s = sin(rotation);
    const float c = cos(rotation);
    const float2 scaled = uv * scale;
    return float2(
        c * scaled.x - s * scaled.y,
        s * scaled.x + c * scaled.y
    ) + offset;
}
```

审核点：

- 每个 texture slot 独立 transform。
- baseColor alpha mask 应使用 transform 后的 baseColor UV。
- normal / MR / AO / emissive 不应复用 baseColor transform。
- transform 应在 shader 中应用，避免把 UV 预烘焙到 mesh 数据；同一 mesh 可被多个 material 以不同 transform 使用。

## Uniform 布局策略

当前 `MaterialUniform` 已包含 factors、alpha、per-slot texCoord selector。

Phase 0.22 建议以 16-byte 对齐的方式新增每个 slot 的 transform：

```cpp
glm::vec4 baseColorUvTransform0;        // offset.xy, scale.xy
glm::vec4 baseColorUvTransform1;        // rotation, padding...
```

也可以合并为：

```cpp
struct TextureTransformUniform {
    glm::vec4 offsetScale; // offset.xy, scale.xy
    glm::vec4 rotation;    // rotation.x, padding
};
```

建议优先选择显式 `glm::vec4` 字段，理由：

- HLSL / C++ constant buffer 对齐更直观。
- tests 可以用 `sizeof(MaterialUniform)` 约束。
- 后续如需加入 transform matrix，也容易替换。

## GltfLoader 策略

tinygltf 的 textureInfo extension 数据可从 `tinygltf::Value` 中读取。

建议新增 helper：

```cpp
TextureTransformData readTextureTransform(const tinygltf::Value& extensions, int& texCoord);
```

读取规则：

- 如果不存在 `KHR_texture_transform`，返回 identity。
- `offset` 必须是长度 2 number array，否则 warning 并保留默认值。
- `scale` 必须是长度 2 number array，否则 warning 并保留默认值。
- `rotation` 必须是 number，否则 warning 并保留默认值。
- extension 内 `texCoord` 如果存在，覆盖外层 `texCoord`。
- 最终 texCoord 仍走 Phase 0.21 的 0/1/fallback 规则。

日志要求：

- unsupported / malformed transform 用英文 warning。
- 不因为单个 malformed optional field 让整个 glTF 加载失败。

## Fixture 策略

新增：

```text
assets/models/texture_transform_fixture.gltf
```

建议内容：

- 一个 quad primitive。
- 同时包含 `TEXCOORD_0` / `TEXCOORD_1`。
- baseColorTexture 使用：
  - 外层 `texCoord = 0`
  - extension 内 `texCoord = 1`
  - offset / scale / rotation 非默认值
- normal 或 occlusion 使用另一个非默认 transform。
- 继续复用 `assets/textures/xiaowei.png`。

测试应覆盖：

- loader 能读取 offset / scale / rotation。
- extension 内 texCoord override 生效。
- MaterialResource 能保留 transform。
- shader source 包含 `transformUv` 和 per-slot transform 字段。

## 实施顺序

### 0.22.0 文档与范围确认

目标：

- 新增 `docs/phase/phase22.md`。
- 明确主线是 `KHR_texture_transform` 最小闭环。
- 明确不做透明排序、HDR、IBL、RenderGraph。

审核点：

- 不重复 Phase 0.21 UV set 工作。
- 不改变 descriptor layout。
- 不把 transform 放进 TextureResource / TextureCache。

### 0.22.1 Asset texture transform 数据结构

目标：

- 新增 `TextureTransformData`。
- `MaterialTextureSlotData` 持有 transform。
- 默认值为 identity。

审核点：

- asset 层不依赖 renderer/RHI。
- `hasTransform` 仅用于调试/测试，不影响 shader 是否能收到 identity transform。

### 0.22.2 GltfLoader 读取 KHR_texture_transform

目标：

- 读取 `offset`、`scale`、`rotation`。
- 读取 extension 内 `texCoord` override。
- malformed optional field warning + fallback。

审核点：

- 外层 textureInfo `texCoord` 与 extension `texCoord` 优先级正确。
- 最终 texCoord 仍只支持 0/1。
- 不支持的 field 不导致 crash。

### 0.22.3 MaterialResource transform set

目标：

- `MaterialResource` 缓存每个 texture slot 的 transform。
- 提供 `textureTransforms()` 查询接口。

审核点：

- `TextureResource` / `TextureCache` 不接入 transform。
- 同一 texture resource 可被不同 transform 复用。

### 0.22.4 ForwardPass material uniform 扩展

目标：

- `MaterialUniform` 增加 per-slot transform。
- `makeMaterialUniform()` 写入 identity 或读取到的 transform。

审核点：

- C++ / HLSL constant buffer 对齐一致。
- descriptor layout 不变。
- pipeline layout 不变。

### 0.22.5 Shader transform 应用

目标：

- `mesh.frag.hlsl` 增加 `transformUv()`。
- 每个 slot 先 select UV，再 apply transform。
- alpha mask 使用 transformed baseColor UV。

审核点：

- 每个 texture slot 独立 transform。
- normal / MR / AO / emissive 不错误复用 baseColor transform。

### 0.22.6 Fixtures 与 tests

目标：

- 新增 `texture_transform_fixture.gltf`。
- `gltf_loader_smoke` 覆盖 transform 数据和 texCoord override。
- `model_resource_smoke` 覆盖 MaterialResource transform set。
- `shader_assets_smoke` 覆盖 transform shader path。
- `framework_headers_smoke` 覆盖新 public structs。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

### 0.22.7 Phase 0.22 收尾

目标：

- 更新本文档实现状态。
- 记录当前限制：
  - 只支持 textureInfo 上的 `KHR_texture_transform`。
  - 只支持最终 `texCoord` 0/1。
  - 不做 texture transform animation。
  - tangent generation 仍基于 uv0。
- 只在用户明确要求时同步 `docs/codex_handoff.md`。

## 审核检查点

- `asset/` 不依赖 renderer/RHI/Vulkan。
- `TextureResource` 不保存 UV transform。
- `TextureCache` key 不包含 UV transform。
- `MaterialResource` 保存 per-slot transform。
- extension 内 `texCoord` override 正确。
- malformed optional transform field 有 warning/fallback。
- HLSL / C++ material uniform 对齐一致。
- baseColor、normal、metallicRoughness、occlusion、emissive 分别应用自己的 transform。
- alpha mask 使用 transformed baseColor UV。
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

Phase 0.22 完成时应满足：

- asset 层能表达 texture transform。
- GltfLoader 能读取 `KHR_texture_transform` 的 offset / scale / rotation / texCoord。
- extension texCoord override 生效。
- MaterialResource 能暴露 per-slot transform。
- ForwardPass material uniform 能传递 transform。
- shader 能按每个 texture slot 应用 transform。
- tests 覆盖 loader、material resource、shader source 和 header smoke。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 texture transform animation、`TEXCOORD_2+`、IBL、HDR、透明排序。

## 后续 Phase 建议

Phase 0.22 后建议进入：

1. transparent sorting / RenderQueue 分桶。
2. doubleSided culling 精确化。
3. 可配置 scene light / camera。
4. 更完整的 direct lighting BRDF。
5. HDR framebuffer / tone mapping。
6. IBL / environment map / BRDF LUT。
7. renderer 级资源 / 场景加载入口整理。
