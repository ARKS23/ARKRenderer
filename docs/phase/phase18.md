# Phase 0.18 Tangent Generation 与真实模型验证入口

## 阶段判断

Phase 0.17 已经完成最小 direct-light-only PBR 输入解释：

```text
glTF material texture slots
    -> MaterialData / TextureCache / MaterialResource
    -> ForwardPass descriptors
    -> mesh.frag.hlsl PbrInputs
    -> TBN normal map
    -> fixed directional lighting
```

当前真实模型验证的主要问题不再是 descriptor 或 texture slot 是否可见，而是：

- glTF 模型可能没有 `TANGENT` attribute。
- 当前缺失 tangent 时只使用固定 fallback `(1, 0, 0, 1)`。
- DamagedHelmet 本地资产可以被 loader smoke 加载，但没有默认 sandbox 模型切换入口。
- 默认 sandbox 仍固定加载 `assets/models/forward_multinode_fixture.gltf`。
- shader 侧 normal / tangent 当前使用 model matrix 变换，非等比缩放下不严格正确。

因此 Phase 0.18 的重点是：让真实 glTF 模型可以通过显式 sandbox 入口渲染，并在缺失 tangent 时获得可用的 CPU tangent generation 结果。

## 目标

Phase 0.18 目标：

- 在 asset 层实现缺失 `TANGENT` 时的 CPU tangent generation。
- 保留 glTF 显式 `TANGENT` 的优先级，生成逻辑只补缺失路径。
- 对退化 UV / 退化三角形提供稳定 fallback，不让 NaN 进入 shader。
- 为 sandbox 增加显式模型路径 override，便于验证 DamagedHelmet。
- 保持默认 sandbox fixture 不变，避免真实资产成为强依赖。
- 补充 tests 与 smoke，覆盖 tangent generation、显式 tangent、fallback、DamagedHelmet 可选验证。
- 记录当前仍不是完整 PBR，不引入 IBL / HDR / tone mapping。

## 非目标

Phase 0.18 暂不做：

- 不做 IBL / environment map。
- 不做 HDR framebuffer / tone mapping / exposure。
- 不做完整 Cook-Torrance + image based lighting。
- 不做 shadow map。
- 不做 glTF sampler 参数。
- 不做 glTF texture transform。
- 不做 KTX / BasisU / DDS / compressed texture。
- 不做 skin / animation / morph target。
- 不做 KHR materials 扩展。
- 不提交 `assets/models/DamagedHelmet/` 大模型资源，除非用户明确要求。
- 不把 DamagedHelmet 改成默认 sandbox 资源。
- 不引入 RenderGraph 重构。

## 模块边界

继续遵守既有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase15.md
docs/phase/phase16.md
docs/phase/phase17.md
```

边界要求：

- `asset/` 只处理 CPU glTF 数据，不创建 RHI / GPU 资源。
- tangent generation 属于 `asset::MeshPrimitiveData` 的 CPU 后处理，不进入 renderer。
- `renderer/` 只消费已经准备好的 vertex data、material data 和 model path。
- `RendererDesc` / `ApplicationDesc` 可以携带模型路径，但不暴露 Vulkan 类型。
- `ForwardPass` 不负责模型选择，不负责 glTF 加载。
- `rhi/` 与 `rhi/vulkan/` 不参与 tangent generation。

## 数据流

目标数据流：

```text
glTF primitive
    -> POSITION / NORMAL / TEXCOORD_0
    -> optional TANGENT

GltfLoader
    -> if TANGENT exists:
        read FLOAT VEC4 tangent
    -> else:
        generate tangent from indexed triangles
    -> MeshPrimitiveData

MeshResource
    -> GPU-only vertex buffer

ForwardPass
    -> vertex layout location 3 tangent
    -> mesh.vert.hlsl world tangent
    -> mesh.frag.hlsl TBN normal map
```

Sandbox 模型选择：

```text
ark_sandbox.exe [optional_model_path]
    -> ApplicationDesc / RendererDesc
    -> Renderer default scene model path
    -> ModelResource
    -> RenderScene / RenderQueue / ForwardPass
```

## Tangent Generation 设计

### 输入

每个 `MeshPrimitiveData` 已有：

- `vertices[i].position`
- `vertices[i].normal`
- `vertices[i].uv0`
- `indices`

### 输出

补齐：

```cpp
vertex.tangent[0..2] = normalized tangent.xyz
vertex.tangent[3] = handedness
```

建议默认：

```cpp
(1, 0, 0, 1)
```

### 算法建议

按 indexed triangle 累加 tangent / bitangent：

```text
for each triangle (i0, i1, i2):
    p0,p1,p2 = positions
    uv0,uv1,uv2 = texcoords
    edge1 = p1 - p0
    edge2 = p2 - p0
    duv1 = uv1 - uv0
    duv2 = uv2 - uv0
    denom = duv1.x * duv2.y - duv2.x * duv1.y
    if abs(denom) too small:
        mark degenerate and skip
    tangent = (edge1 * duv2.y - edge2 * duv1.y) / denom
    bitangent = (edge2 * duv1.x - edge1 * duv2.x) / denom
    accumulate per vertex
```

最终 per vertex：

```text
N = normal
T = accumulated tangent
T = normalize(T - N * dot(N, T))
B = accumulated bitangent
handedness = dot(cross(N, T), B) < 0 ? -1 : 1
```

如果 accumulated tangent 退化：

- 使用 fallback tangent。
- fallback tangent 应尽量与 normal 正交。
- 如果 normal 也退化，则使用 `(1, 0, 0, 1)`。

### 关键策略

- glTF 显式 `TANGENT` 存在时不重新生成。
- 只在 primitive 完整读取后生成缺失 tangent。
- 生成逻辑不应依赖 `glm`，除非 asset 层已经接受该依赖；建议先用小型本地 float helper，保持 asset 层轻量。
- 不因单个退化 triangle 失败整个模型。
- 如果整个 primitive 无法生成有效 tangent，使用 fallback 并记录 warning。

## Sandbox 模型路径 Override

建议扩展：

```cpp
struct ApplicationDesc {
    WindowDesc window;
    Path defaultModelPath;
};

struct RendererDesc {
    rhi::NativeWindowHandle nativeWindow;
    rhi::Extent2D extent{1280, 720};
    bool enableValidation = false;
    Path defaultModelPath;
};
```

`apps/sandbox/main.cpp` 支持：

```powershell
ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

策略：

- 不传参数时继续使用 `assets/models/forward_multinode_fixture.gltf`。
- 传参时使用用户指定 path。
- path 查找可沿用当前 `findFirstExistingPath` 风格。
- 加载失败时输出清晰英文日志，避免静默 fallback。

## Normal Matrix 最小修复

当前 vertex shader 用 model matrix 变换 normal / tangent：

```hlsl
mul((float3x3)g_Object.model, input.normal)
```

Phase 0.18 可以做最小修复：

方案 A：C++ ObjectUniform 增加 normal matrix。

```cpp
struct ObjectUniform {
    glm::mat4 model;
    glm::mat4 normalMatrix;
};
```

优点：

- shader 简洁。
- 计算集中在 CPU，便于后续扩展。

代价：

- per-draw object uniform 从 64 bytes 增加到 128 bytes。
- HLSL cbuffer 和 C++ 结构要同步。

方案 B：shader 内计算 inverse transpose。

优点：

- C++ 改动少。

代价：

- shader 成本更高。
- HLSL 侧 inverse 支持与编译输出要验证。

建议 Phase 0.18 采用方案 A。

## 实施顺序

### 0.18.0 文档与范围确认

目标：

- 新增 `docs/phase/phase18.md`。
- 明确主线是 tangent generation + sandbox model override。
- 明确不做 IBL/HDR/tone mapping。

审核点：

- 不重复 Phase 0.17 已完成的 descriptor / texture slot 工作。
- 不默认提交或绑定 DamagedHelmet 大模型资源。

### 0.18.1 Tangent generation CPU helper

目标：

- 增加 CPU tangent generation helper。
- 缺失 `TANGENT` 时自动生成。
- 保持 asset 层无 renderer/RHI 依赖。

审核点：

- 显式 `TANGENT` 不被覆盖。
- 退化 UV / 退化 triangle 不产生 NaN。
- fallback 明确。

### 0.18.2 Tangent tests

目标：

- `mesh_data_smoke` 或 `gltf_loader_smoke` 覆盖生成 tangent。
- 覆盖显式 tangent 优先。
- 覆盖缺失 tangent fallback / generation。

审核点：

- 测试 fixture 小而可读。
- 不依赖 DamagedHelmet 大资源才能通过。

### 0.18.3 Sandbox model path override

目标：

- `apps/sandbox/main.cpp` 支持可选 model path 参数。
- `ApplicationDesc` / `RendererDesc` 传递 path。
- Renderer 默认模型加载路径可配置。

审核点：

- 不传参行为不变。
- path 失败时日志清晰。
- 不暴露 Vulkan 类型。

### 0.18.4 Object normal matrix

目标：

- Object uniform 增加 normal matrix。
- `mesh.vert.hlsl` 使用 normal matrix 变换 normal / tangent。

审核点：

- C++ / HLSL cbuffer 对齐一致。
- descriptor binding 不变。
- existing draw path 不回退。

### 0.18.5 DamagedHelmet 可选渲染 smoke

目标：

- 本地存在 `assets/models/DamagedHelmet/DamagedHelmet.gltf` 时，通过 sandbox 参数 smoke。
- 不把 DamagedHelmet 设为默认模型。
- 记录当前视觉限制。

审核点：

- 测试不依赖未提交大资源。
- 失败日志能区分资源缺失、加载失败、渲染启动失败。

### 0.18.6 Tests 与 sandbox smoke

建议运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

默认 sandbox smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
```

本地真实模型 smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

## 当前实现状态

截至 0.18.6：
- `asset::generateTangents(MeshPrimitiveData&)` 已实现 indexed triangle CPU tangent generation。
- glTF 显式 `TANGENT` 保持优先；缺失 `TANGENT` 时在 primitive 索引读取完成后自动生成。
- 退化 UV / 退化 triangle 会跳过并使用 per-vertex fallback tangent，避免 NaN 进入 shader。
- `ApplicationDesc` / `RendererDesc` 已支持 `defaultModelPath`，`ark_sandbox.exe [optional_model_path]` 可覆盖默认模型。
- 默认 sandbox 仍加载 `assets/models/forward_multinode_fixture.gltf`，不绑定 DamagedHelmet。
- Object uniform 已增加 `normalMatrix`；vertex shader 使用 normal matrix 变换 normal，并对 world tangent 做正交化。
- `mesh_data_smoke`、`gltf_loader_smoke`、`shader_assets_smoke` 已覆盖 Phase 0.18 关键路径。

本地验证记录：
```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

DamagedHelmet smoke 可启动并加载成功；当前本地模型会报告少量退化 triangle 被跳过，这是 tangent generation fallback 路径的预期输入质量提示。

### 0.18.7 Phase 0.18 收尾

目标：

- 更新本文档实现状态。
- 记录 tangent generation 质量限制。
- 记录 DamagedHelmet 是否能通过本地渲染 smoke。
- 只在用户明确要求时同步 `docs/codex_handoff.md`。

收尾结论：

- Phase 0.18 已完成 0.18.1 ~ 0.18.6 的实现与验证。
- 本阶段没有同步 `docs/codex_handoff.md`，保持用户指定的 handoff 同步边界。
- 本阶段没有把 DamagedHelmet 设为默认 sandbox 资源，也没有要求提交该大模型资产。
- 默认 sandbox fixture 仍是 `assets/models/forward_multinode_fixture.gltf`。
- DamagedHelmet 可通过显式参数路径启动 smoke：`ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf`。

实现落点：

- `src/asset/MeshData.cpp` / `MeshData.h`：新增 `asset::generateTangents(MeshPrimitiveData&)`。
- `src/asset/GltfLoader.cpp`：缺失 glTF `TANGENT` 时生成 tangent，显式 `TANGENT` 不覆盖。
- `src/app/Application.*`、`src/renderer/Renderer.*`、`apps/sandbox/main.cpp`：新增 sandbox model path override。
- `src/renderer/passes/ForwardPass.cpp`、`shaders/mesh.vert.hlsl`：Object uniform 增加 `normalMatrix`，shader 修正 normal/tangent world-space 变换。
- `tests/mesh_data_smoke.cpp`、`tests/gltf_loader_smoke.cpp`、`tests/shader_assets_smoke.cpp`：补充生成 tangent、显式 tangent、真实模型可选验证和 shader source smoke。

质量限制：

- 当前 tangent generation 是 Phase 0.18 最小 CPU helper，不是 MikkTSpace；结果可能与 DCC/baker 的 tangent basis 不完全一致。
- 算法依赖 glTF 已经按 UV seam / normal seam 拆分顶点，不在本阶段主动重建 vertex split。
- 退化 UV 或退化 triangle 会被跳过；如果 vertex 没有有效累计 tangent，会使用与 normal 正交的 fallback tangent。
- DamagedHelmet 本地 smoke 中会出现少量退化 triangle warning，当前作为输入质量提示处理，不阻断渲染。
- sandbox smoke 只验证启动、加载和短时间渲染循环，不做自动截图或像素级正确性判断。

最终验证结果：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：
- build 通过。
- CTest 8/8 通过。
- 默认 sandbox smoke 通过，加载 `assets/models/forward_multinode_fixture.gltf`。
- DamagedHelmet optional smoke 通过，加载 `assets/models/DamagedHelmet/DamagedHelmet.gltf`，并报告 66 个退化 triangle 被跳过。

## 审核检查点

- `asset/` 不依赖 renderer/RHI/Vulkan。
- tangent generation 不覆盖 glTF 显式 tangent。
- 缺失 tangent 的真实模型不再全量使用固定 fallback。
- shader 不接收 NaN tangent。
- default sandbox 行为不变。
- model override 不引入全局状态。
- ObjectUniform C++ / HLSL 对齐一致。
- DamagedHelmet 是可选本地验证，不是提交依赖。

## 验证计划

必须通过：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

默认 sandbox smoke 必须通过。

如果本地存在 DamagedHelmet：

- glTF loader smoke 应加载通过。
- sandbox path override smoke 应启动通过。

## 完成标准

Phase 0.18 完成时应满足：

- 无 `TANGENT` glTF 可以生成稳定 tangent。
- 显式 `TANGENT` glTF 行为不变。
- DamagedHelmet 可通过显式 sandbox path 启动渲染。
- 默认 sandbox fixture 不变。
- normal matrix 路径比 Phase 0.17 更正确。
- build、CTest、sandbox smoke 通过。
- 文档明确仍未进入 IBL/HDR/tone mapping/完整 PBR。

## 后续 Phase 建议

Phase 0.18 后建议进入：

1. glTF sampler 参数与 texture transform。
2. 更完整的 direct lighting BRDF。
3. HDR framebuffer 与 tone mapping。
4. IBL / environment map / BRDF LUT。
5. 可配置 scene light / camera。
6. RenderGraph 第一版 pass/resource 声明。
