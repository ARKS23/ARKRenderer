# Phase 0.17 最小基础光照 / PBR 输入解释

## 阶段判断

Phase 0.16 已经完成 glTF 2.0 常见 PBR texture slots 的最小数据链路：

```text
glTF material
    -> asset::MaterialData
        -> baseColor / normal / metallicRoughness / occlusion / emissive texture paths
        -> baseColorFactor / emissiveFactor / metallicFactor / roughnessFactor
        -> normalScale / occlusionStrength
    -> ModelResource
        -> TextureCache
            -> file texture + fallback texture
            -> sRGB / linear color-space key
    -> MaterialResource
        -> MaterialTextureSet
        -> MaterialFactors
    -> ForwardPass
        -> descriptor binding 1/2/5-12
        -> material uniform binding 4
    -> mesh.frag.hlsl
        -> samples all material texture slots
        -> outputs baseColor * occlusion + emissive
```

当前链路已经可以保证 shader-visible descriptor 有效，也能避免 color texture 和 data texture 的错误复用。但 shader 仍没有真正解释 PBR 输入：

- `normalTexture` 已采样，但没有正确 tangent-space normal mapping。
- `metallicRoughnessTexture` 已采样，但没有参与 lighting。
- `metallicFactor` / `roughnessFactor` 已进入 uniform，但没有进入 BRDF。
- `occlusionTexture` 只做最小输出调制。
- 当前没有 scene light / camera position / world position 的明确 shader 数据流。
- `MeshVertex` 只有 position / normal / uv0，没有 tangent。

因此 Phase 0.17 的重点不是继续增加 texture 数量，也不是直接进入完整 PBR/IBL，而是完成一个可审、可验证的最小基础光照路径，让 Phase 0.16 已接入的 material 输入开始被合理使用。

## 目标

Phase 0.17 的目标是在不引入 IBL、RenderGraph、bindless 或复杂 glTF 扩展的前提下完成：

- `asset::MeshVertex` 支持 tangent 数据。
- `GltfLoader` 读取 glTF 2.0 `TANGENT` attribute。
- `mesh.vert.hlsl` 输出 fragment shader 需要的 world-space 数据：
  - world position
  - world normal
  - world tangent
  - tangent handedness
  - uv0
- `ForwardPass` 增加最小 lighting uniform。
- `mesh.frag.hlsl` 使用：
  - baseColor texture / factor
  - normal texture / normalScale
  - metallicRoughness texture / metallicFactor / roughnessFactor
  - occlusion texture / occlusionStrength
  - emissive texture / emissiveFactor
- shader 实现最小 direct lighting，优先使用一个 fixed directional light。
- `assets/models/DamagedHelmet/` 作为真实资产验证对象，但不默认承诺完整 PBR 观感。
- smoke tests 覆盖 tangent 读取、fallback tangent 行为、shader asset 编译和默认 sandbox 不回退。

## 非目标

Phase 0.17 暂不做：

- 不做 IBL / environment map。
- 不做 HDR pipeline / tone mapping / exposure。
- 不做 image based specular prefilter。
- 不做 BRDF LUT。
- 不做 shadow map。
- 不做 glTF sampler 参数。
- 不做 glTF texture transform。
- 不做 tangent generation，除非文档另行确认范围。
- 不支持 skin / animation / morph target。
- 不支持 KHR materials 扩展。
- 不支持 embedded image / data URI image。
- 不支持 KTX / BasisU / DDS / compressed texture。
- 不引入 bindless / descriptor indexing。
- 不引入完整 RenderGraph。
- 不替换默认 sandbox 资产，除非真实资产 smoke 明确稳定。

## 模块边界

继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase11.md
docs/phase/phase12.md
docs/phase/phase13.md
docs/phase/phase14.md
docs/phase/phase15.md
docs/phase/phase16.md
```

硬性边界：

- `asset/` 只解析 glTF 并输出 CPU mesh/material 数据，不创建 RHI/GPU 资源。
- tangent 是 mesh vertex attribute，不属于 material resource。
- `renderer/` 可以把 tangent 映射到 RHI vertex layout，但不能接触 Vulkan 类型。
- `ForwardPass` 可以管理 lighting uniform，但不负责 glTF 加载或 texture cache。
- `RenderScene` / `RenderQueue` 不拥有 GPU 资源，不解析 asset。
- `rhi/` 只表达 API 无关的 buffer、descriptor、pipeline、vertex layout。
- `rhi/vulkan/` 只处理 Vulkan 映射。
- upload / mip generation / deferred release 继续发生在 dynamic rendering scope 外。

## 推荐数据流

目标数据流：

```text
glTF mesh primitive attributes
    -> POSITION / NORMAL / TEXCOORD_0 / TANGENT
    -> asset::MeshVertex
        -> position
        -> normal
        -> uv0
        -> tangent.xyz
        -> tangent.w

MeshResource
    -> GPU-only vertex buffer
    -> ForwardPass vertex layout
        -> location 0: position
        -> location 1: normal
        -> location 2: uv0
        -> location 3: tangent

ForwardPass
    -> CameraUniform
    -> ObjectUniform
    -> MaterialUniform
    -> LightingUniform

mesh.vert.hlsl
    -> world position
    -> world normal
    -> world tangent
    -> tangent handedness
    -> uv0

mesh.frag.hlsl
    -> baseColor
    -> tangent-space normal -> world-space normal
    -> metallic / roughness
    -> occlusion
    -> emissive
    -> fixed directional light
    -> final color
```

## 设计建议

### MeshVertex Tangent

建议扩展：

```cpp
struct MeshVertex {
    float position[3]{};
    float normal[3]{};
    float uv0[2]{};
    float tangent[4]{}; // xyz = tangent, w = bitangent handedness
};
```

审核点：

- asset 层仍不引入 `glm`。
- tangent 默认值应明确，不要让 shader 使用未定义数据。
- 如果 glTF 缺少 tangent：
  - 本阶段建议 fallback 为 `(1, 0, 0, 1)`，并在文档记录 normal mapping 质量受限。
  - 不建议本阶段实现 tangent generation。

### GltfLoader Tangent Parsing

需要读取：

```text
primitive.attributes["TANGENT"]
accessor type: VEC4
component type: FLOAT
```

审核点：

- TANGENT 缺失不导致模型加载失败。
- TANGENT 存在但格式不支持时应失败或 warning，需要在实现前明确策略。
- POSITION / NORMAL / TEXCOORD_0 现有路径不回退。
- 多 primitive / 多 node / material remap 不受影响。

### Vertex Layout

`ForwardPass` mesh pipeline vertex layout 建议新增：

```text
location 3: tangent, Format::R32G32B32A32Float
```

如果当前 RHI `Format` 还没有 `R32G32B32A32Float`，建议在 Phase 0.17 内补齐：

- `rhi::Format::R32G32B32A32Float`
- Vulkan `VK_FORMAT_R32G32B32A32_SFLOAT`
- format name / mapping smoke

不要用两个 attribute 临时拆 tangent，除非 RHI format 扩展受阻。

### LightingUniform

建议新增：

```cpp
struct alignas(16) LightingUniform {
    glm::vec4 lightDirection; // xyz, world space; w unused
    glm::vec4 lightColor;     // rgb intensity; a unused
    glm::vec4 ambientColor;   // rgb ambient; a unused
    glm::vec4 cameraPosition; // xyz, world space; w unused
};
```

最小策略：

- fixed directional light。
- fixed camera position 可以先从 `RenderView` 推导；如果当前 `RenderView` 不提供，先使用 view matrix inverse 或固定值，并记录 TODO。
- Lighting uniform 可以放在 set 0 新 binding，例如 binding 13。

审核点：

- C++ / HLSL cbuffer 对齐一致。
- binding 与 descriptor layout 一致。
- 每 frame 更新 lighting uniform，不需要 per draw。
- 不把 lighting 数据放进 material uniform。

### Shader 最小 Lighting

建议先做 direct lighting 的简化模型：

```text
N = normal map through TBN
V = normalize(cameraPosition - worldPosition)
L = normalize(-lightDirection)
baseColor = baseColorTexture * baseColorFactor
metallic = mr.b * metallicFactor
roughness = mr.g * roughnessFactor
ao = lerp(1, occlusion.r, occlusionStrength)
emissive = emissiveTexture * emissiveFactor
```

输出可以二选一：

1. Lambert + 简化 specular：
   - diffuse = baseColor.rgb * max(dot(N, L), 0)
   - specular 使用 roughness 做简单 power 或强度调制
2. 最小 Cook-Torrance direct light：
   - GGX NDF
   - Smith geometry
   - Schlick Fresnel

建议 Phase 0.17 使用方案 1 或严格限制的方案 2。若实现方案 2，必须在文档明确这是 direct-light-only PBR，不含 IBL、tone mapping、HDR。

### DamagedHelmet 验证

`assets/models/DamagedHelmet/` 可以用于真实 glTF 2.0 验证：

- 先作为可选 fixture 或手动 sandbox path。
- 不直接切默认 sandbox，除非：
  - tangent 读取稳定。
  - material texture slots 全部加载。
  - shader 不崩溃。
  - sandbox smoke 通过。

注意：

- 当前 `.gitignore` 有用户侧未提交改动涉及 `assets/models/DamagedHelmet/`。
- 是否提交真实资产或保持本地验证，需要用户明确决定。

## 实施顺序

### 0.17.0 文档与范围确认

目标：

- 新增 `docs/phase/phase17.md`。
- 明确主线是最小基础光照 / PBR 输入解释。
- 明确不进入 IBL / RenderGraph / bindless / sampler / texture transform。

审核检查点：

- 不重复 Phase 0.16 texture slot 接入。
- 明确 normal mapping 依赖 tangent。
- 明确 DamagedHelmet 只作为真实资产验证对象。

### 0.17.1 MeshVertex tangent 与 glTF TANGENT 读取

目标：

- `MeshVertex` 新增 tangent。
- `GltfLoader` 读取 `TANGENT` attribute。
- TANGENT 缺失时使用明确 fallback。
- smoke tests 覆盖 tangent 读取和缺失路径。

审核检查点：

- asset 层不依赖 renderer/RHI。
- glTF VEC4/FLOAT 检查明确。
- 旧 fixture 不含 tangent 时不回退。

### 0.17.2 RHI vertex format 与 ForwardPass vertex layout

目标：

- 如需要，新增 `R32G32B32A32Float` format。
- Vulkan format 映射补齐。
- ForwardPass vertex layout 增加 location 3 tangent。
- `mesh.vert.hlsl` 输入 tangent。

审核检查点：

- Vertex attribute offset 使用 `offsetof`。
- shader location 与 vertex layout 一致。
- pipeline 创建不回退。

### 0.17.3 Shader varying 扩展

目标：

- vertex shader 输出 world position、world normal、world tangent、tangent handedness、uv0。
- fragment shader 接收这些数据。

审核检查点：

- normal / tangent 使用 model matrix 转换。
- 非 uniform scale 的 normal matrix 可以先记录 TODO；如果实现 inverse-transpose，要保持代码清晰。
- 不引入 Vulkan 类型。

### 0.17.4 ForwardPass LightingUniform

目标：

- 新增 lighting uniform buffer。
- descriptor layout 增加 lighting binding。
- prepare/update descriptor 写入 lighting uniform。
- execute 或 prepare 阶段更新每帧 lighting 数据。

审核检查点：

- C++ / HLSL 对齐一致。
- binding 与 shader 一致。
- lighting uniform 是 per-frame，不是 per-draw。

### 0.17.5 Shader 最小光照 / PBR 输入解释

目标：

- 使用 normal map 构造 world-space normal。
- 使用 metallicRoughness G/B 和 scalar factors。
- 使用 occlusionStrength。
- 使用 emissiveFactor。
- 输出 direct-light-only 可解释结果。

审核检查点：

- 不声称完整 PBR。
- 没有 IBL 时 metal 观感有限，需要文档记录。
- roughness / metallic 输入链路可审。
- fallback texture 默认值不改变旧 fixture 的稳定性。

### 0.17.6 DamagedHelmet 可选验证

目标：

- 验证 `assets/models/DamagedHelmet/` 能通过当前 loader/material/shader 路径。
- 可选增加非默认 sandbox path 或手动 smoke 说明。

审核检查点：

- 不未经确认提交大型真实资产。
- 不未经确认替换默认 fixture。
- 如果 glTF 使用未支持特性，应记录限制，不假装完成。

### 0.17.7 Tests 与 sandbox smoke

目标：

- `gltf_loader_smoke` 覆盖 tangent。
- `model_resource_smoke` 覆盖 vertex byte size / pipeline layout 相关最小行为。
- `shader_assets_smoke` 覆盖 mesh shader 编译。
- 默认 sandbox smoke 通过。

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及默认渲染路径时继续运行 sandbox smoke。

### 0.17.8 Phase 0.17 收尾

目标：

- 更新本文档当前实现状态。
- 按需同步 `docs/codex_handoff.md`。
- 记录后续 TODO：tangent generation、IBL、tone mapping、glTF sampler、texture transform、RenderGraph。

## 审核检查点

- `asset/` 只输出 CPU mesh/material 数据。
- `renderer/` 不接触 Vulkan 类型。
- tangent attribute 与 shader location 一致。
- lighting uniform 与 HLSL cbuffer 对齐一致。
- descriptor layout 与 shader binding 一致。
- upload / mip generation / deferred release 不进入 dynamic rendering scope。
- normal mapping 不在缺少 tangent 时假装完全正确。
- metallic/roughness 进入 direct lighting，但不声明完整 PBR。
- DamagedHelmet 验证不等于完整 glTF 支持。

## 验证计划

实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

涉及 shader / descriptor / 默认 sandbox 渲染路径时运行 sandbox smoke。

## 当前实现状态

### 已完成到 0.17.4

- 0.17.1 已完成：`MeshVertex` 增加 `tangent[4]`，默认值为 `(1, 0, 0, 1)`；`GltfLoader` 支持读取 glTF 2.0 `TANGENT` attribute。
- 0.17.1 已补测试：`forward_fixture.gltf` 覆盖 tangent 缺失 fallback；`forward_tangent_fixture.gltf` 覆盖显式 `FLOAT VEC4` tangent 读取。
- 0.17.2 已完成：ForwardPass vertex layout 增加 location 3，格式使用既有 `rhi::Format::R32G32B32A32Float`。
- 0.17.3 已完成：`mesh.vert.hlsl` 输出 world position / world normal / world tangent / uv0，`mesh.frag.hlsl` 接收相同 location。
- 0.17.4 已完成：ForwardPass 增加 per-frame `LightingUniform`，descriptor binding 为 13，fragment shader 同步使用 binding 13。

### 当前限制

- `TANGENT` 缺失时仅使用固定 fallback，不生成 mesh tangent；缺失 tangent 的真实模型 normal mapping 质量仍受限。
- vertex shader 暂用 model matrix 变换 normal/tangent，非等比缩放下 inverse-transpose normal matrix 仍留给后续阶段。
- shader 已接入最小 direct lighting 与 TBN normal map，但这仍不是完整 PBR；IBL / HDR / tone mapping 不在当前完成范围。

### 已运行验证

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：build 通过，CTest 8/8 通过；sandbox smoke 通过，默认 `forward_multinode_fixture.gltf` 启动正常。

建议新增或扩展测试：

- `mesh_data_smoke`
  - tangent 默认值和 vertex byte size。
- `gltf_loader_smoke`
  - TANGENT attribute 读取。
  - TANGENT 缺失 fallback。
- `model_resource_smoke`
  - vertex buffer size 随 tangent 扩展。
  - material texture/factor 行为不回退。
- `shader_assets_smoke`
  - `mesh.vert.spv` / `mesh.frag.spv` 正常生成并可加载。

## 完成标准

Phase 0.17 完成时应满足：

- `MeshVertex` 表达 tangent。
- `GltfLoader` 读取 glTF 2.0 tangent attribute。
- ForwardPass vertex layout 与 shader location 支持 tangent。
- shader 能构造 TBN 并使用 normal texture。
- shader 使用 metallicRoughness / occlusion / emissive 输入。
- 有最小 directional light 数据链路。
- build、ctest 和必要 sandbox smoke 通过。
- 文档明确当前是 direct-light-only 最小光照，不包含 IBL / HDR / tone mapping / 完整 PBR。

## 后续 Phase 建议

Phase 0.17 完成后，建议继续：

1. Tangent generation，支持无 tangent glTF 的正确 normal mapping。
2. glTF sampler 参数和 texture transform。
3. IBL / environment map / BRDF LUT。
4. HDR framebuffer、tone mapping、exposure。
5. renderer 级资源 / scene 加载入口，替代内部默认 scene 过渡方案。
6. pipeline / shader / descriptor layout deferred destruction。
7. RenderGraph 第一版 pass/resource 声明。
