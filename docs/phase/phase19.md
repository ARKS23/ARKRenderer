# Phase 0.19 glTF Sampler 与 TextureInfo 语义补齐

## 阶段判断

Phase 0.18 已经完成真实 glTF 模型的显式 sandbox 入口、缺失 tangent 的 CPU generation、object normal matrix，以及 DamagedHelmet optional smoke。当前真实模型已经可以进入默认 ForwardPass 渲染路径：

```text
glTF 2.0 model
    -> GltfLoader
    -> ModelData / MaterialData / MeshPrimitiveData
    -> ModelResource / TextureCache / TextureResource
    -> RenderScene / RenderQueue
    -> ForwardPass
    -> mesh.vert.hlsl / mesh.frag.hlsl
```

下一步不应该直接进入完整 RenderGraph、bindless 或 IBL。当前真实模型视觉正确性的主要缺口仍在 glTF core texture sampling 语义：

- glTF `samplers` 尚未读取，所有 texture 当前统一使用 renderer 侧默认 sampler。
- `MaterialData` 的 texture slot 当前只保存 `Path`，没有表达 `texCoord`、sampler、texture info 等语义。
- `TextureResource` 当前把 texture/view/sampler 绑定在一个资源对象里，尚未拆分 sampler cache。
- shader 当前只支持 `uv0`，尚未支持 `TEXCOORD_1`。
- 暂未支持 `KHR_texture_transform`。

Phase 0.19 的重点是补齐 glTF 2.0 core sampler / texture info 的最小闭环，同时保持当前 renderer/RHI 架构边界稳定。

## 目标

Phase 0.19 目标：

- 在 asset 层表达 glTF texture slot 的最小语义：path、texCoord、sampler。
- 在 asset 层读取 glTF 2.0 `samplers`，并映射到 engine 自有 sampler 描述。
- 让 `ModelResource` / `TextureCache` / `TextureResource` 能消费 sampler 描述。
- 保持默认行为兼容：缺失 glTF sampler 时继续使用当前默认 linear/repeat 语义。
- 明确 `texCoord != 0` 的当前限制，先 warning / TODO，不扩展 vertex layout。
- 不拆分 `TextureResource` 与 sampler cache，先采用最小实现。
- 补充 tests 和 sandbox smoke，验证默认 fixture 与 DamagedHelmet 不回退。

## 非目标

Phase 0.19 暂不做：

- 不做完整 RenderGraph。
- 不做 bindless descriptor。
- 不做独立全局 `SamplerCache`。
- 不做 `TEXCOORD_1` vertex layout 扩展。
- 不做 `KHR_texture_transform`。
- 不做 anisotropy。
- 不做 compare sampler / shadow sampler。
- 不做 alpha blending / alpha mask pipeline 分流。
- 不做 double-sided pipeline 分流。
- 不做 skin / animation / morph target。
- 不做 IBL / HDR / tone mapping。
- 不把 DamagedHelmet 改成默认 sandbox 资源。

## 模块边界

继续遵守既有设计文档：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase16.md
docs/phase/phase17.md
docs/phase/phase18.md
```

边界要求：

- `asset/` 只解析 glTF sampler / texture info，输出 CPU 数据，不创建 RHI 对象。
- `asset/` 不直接包含 RHI 类型；需要定义 asset 层自己的 filter/wrap 枚举或轻量描述。
- `renderer/` 负责把 asset sampler 描述转换为 RHI `SamplerDesc`。
- `TextureCache` 可以在当前阶段把 sampler 描述纳入 key，避免拆大架构。
- `rhi/` 只暴露通用 sampler 能力，不暴露 Vulkan 类型。
- `rhi/vulkan/` 负责把通用 sampler 描述映射到 `VkSamplerCreateInfo`。
- `ForwardPass` 继续只消费已准备好的 material / texture resource，不负责 glTF 解析。

## 数据结构建议

### Asset 层

建议新增 asset 层 sampler 描述，避免 `asset/` 依赖 RHI：

```cpp
namespace ark::asset {
    enum class TextureFilter {
        Nearest,
        Linear,
    };

    enum class TextureMipFilter {
        Nearest,
        Linear,
    };

    enum class TextureAddressMode {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    struct TextureSamplerData {
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureMipFilter mipFilter = TextureMipFilter::Linear;
        TextureAddressMode addressU = TextureAddressMode::Repeat;
        TextureAddressMode addressV = TextureAddressMode::Repeat;
    };

    struct MaterialTextureSlotData {
        Path path;
        u32 texCoord = 0;
        TextureSamplerData sampler;
    };
}
```

`MaterialData` 由当前：

```cpp
Path baseColorTexturePath;
Path normalTexturePath;
Path metallicRoughnessTexturePath;
Path occlusionTexturePath;
Path emissiveTexturePath;
```

逐步迁移为：

```cpp
MaterialTextureSlotData baseColorTexture;
MaterialTextureSlotData normalTexture;
MaterialTextureSlotData metallicRoughnessTexture;
MaterialTextureSlotData occlusionTexture;
MaterialTextureSlotData emissiveTexture;
```

兼容策略：

- 为了减少一次性改动，可以先保留现有 `hasBaseColorTexture()` 等 accessor，但内部改为检查 `slot.path`。
- 如果现有 tests 大量使用 `baseColorTexturePath`，可以先保留 path 字段并增加 slot 字段，Phase 0.19 收尾再决定是否清理旧字段。
- 不建议长期同时保留两套数据源；否则容易出现 path 不一致。

### Renderer 层

建议扩展 `TextureResourceDesc`：

```cpp
struct TextureResourceDesc {
    Path path;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    bool generateMips = true;
    rhi::SamplerDesc sampler;
    bool hasSamplerOverride = false;
    std::string debugName;
};
```

最小实现：

- 如果 `hasSamplerOverride == false`，使用当前默认 sampler。
- 如果 `hasSamplerOverride == true`，使用传入 sampler。
- `TextureCacheKey` 暂时包含 sampler 描述，保证同一路径不同 sampler 时不会错误复用。

长期优化：

- 后续可拆出 image/view cache 与 sampler cache，避免同一 image 因 sampler 不同重复创建 texture。

## glTF Sampler 映射

glTF 2.0 sampler 字段：

```json
{
  "magFilter": 9728 | 9729,
  "minFilter": 9728 | 9729 | 9984 | 9985 | 9986 | 9987,
  "wrapS": 33071 | 33648 | 10497,
  "wrapT": 33071 | 33648 | 10497
}
```

建议映射：

```text
magFilter:
    9728 NEAREST -> Nearest
    9729 LINEAR  -> Linear
    missing      -> Linear

minFilter:
    9728 NEAREST                -> min Nearest, mip Nearest
    9729 LINEAR                 -> min Linear,  mip Nearest
    9984 NEAREST_MIPMAP_NEAREST -> min Nearest, mip Nearest
    9985 LINEAR_MIPMAP_NEAREST  -> min Linear,  mip Nearest
    9986 NEAREST_MIPMAP_LINEAR  -> min Nearest, mip Linear
    9987 LINEAR_MIPMAP_LINEAR   -> min Linear,  mip Linear
    missing                     -> min Linear,  mip Linear

wrapS / wrapT:
    10497 REPEAT          -> Repeat
    33071 CLAMP_TO_EDGE   -> ClampToEdge
    33648 MIRRORED_REPEAT -> MirroredRepeat
    missing               -> Repeat
```

当前 RHI `AddressMode` 只有：

```cpp
Repeat,
ClampToEdge,
```

Phase 0.19 有两个选择：

方案 A：RHI 增加 `MirroredRepeat`

- 优点：更贴近 glTF core。
- 代价：需要补 Vulkan sampler 映射和测试。

方案 B：遇到 `MIRRORED_REPEAT` 时 warning 并 fallback 到 `Repeat`

- 优点：改动更小。
- 代价：真实 glTF sampler 语义不完整。

建议 Phase 0.19 采用方案 A，因为 `MirroredRepeat` 是 glTF core sampler wrap mode，映射到 Vulkan 成本较低。

## texCoord 策略

glTF textureInfo 有 `texCoord` 字段。当前 `MeshVertex` 只有 `uv0`，shader 也只使用 `uv0`。

Phase 0.19 建议：

- 读取并保存 `texCoord`。
- 如果 `texCoord == 0`，正常使用。
- 如果 `texCoord != 0`，输出 warning，并暂时仍使用 `uv0`。
- 文档记录 `TEXCOORD_1` 是后续阶段内容。

原因：

- 扩展 `uv1` 会影响 `MeshVertex`、glTF accessor 读取、vertex layout、shader 输入和所有 mesh fixtures。
- 当前阶段主要目标是 sampler 语义闭环，不宜把 vertex layout 变更混入。

## TextureCache Key 策略

当前 key：

```text
source + canonicalPath + colorSpace + fallbackKind
```

Phase 0.19 最小扩展：

```text
source + canonicalPath + colorSpace + fallbackKind + samplerDesc
```

审核点：

- 同一路径、同 colorSpace、同 sampler 应复用同一个 `TextureResource`。
- 同一路径、同 colorSpace、不同 sampler 可以创建不同 `TextureResource`。
- fallback texture 也应拥有稳定 sampler key。

注意：

- 这会因为 sampler 不同重复创建 texture/view。当前阶段可接受。
- 如果后续模型规模增大，应拆分 `TextureResource` 中的 sampler ownership，或者引入 `SamplerCache`。

## 实施顺序

### 0.19.0 文档与范围确认

目标：

- 新增 `docs/phase/phase19.md`。
- 明确主线是 glTF sampler / texture info 语义。
- 明确不做 texture transform、TEXCOORD_1、RenderGraph、IBL。

审核点：

- 不重复 Phase 0.16 多 texture slot 和 Phase 0.18 tangent 工作。
- 不提前做大架构拆分。

### 0.19.1 Asset texture slot / sampler 描述

目标：

- 新增 asset 层 `TextureSamplerData` / `MaterialTextureSlotData`。
- `MaterialData` 能表达每个 texture slot 的 path、texCoord、sampler。
- 保持 asset 层不依赖 RHI。

审核点：

- accessor 命名清晰。
- 默认值符合 glTF：linear / repeat / texCoord 0。
- 旧 tests 的 path 检查要同步更新。

当前状态：

- `TextureFilter`、`TextureAddressMode`、`TextureSamplerData`、`MaterialTextureSlotData` 已加入 asset 层。
- `MaterialData` 已新增 baseColor / normal / metallicRoughness / occlusion / emissive texture slot。
- 旧 path 字段暂时保留，并与新 slot path 同步，避免当前 renderer/测试一次性迁移。
- `has*Texture()` 已兼容新 slot 和旧 path 字段。

说明：

- 当前是兼容迁移阶段，不建议长期保留两套 texture path 数据源。
- 0.19.4 接入 renderer 后，应评估是否清理旧 path 字段。

### 0.19.2 GltfLoader sampler / textureInfo 读取

目标：

- 读取 glTF `samplers`。
- 从 texture index 解析 image path + sampler index。
- 读取 textureInfo `texCoord`。
- 映射 glTF filter / wrap 到 asset 层描述。

审核点：

- 缺失 sampler 使用默认值。
- 非法 sampler index 明确失败或 warning，不静默使用随机数据。
- `texCoord != 0` 记录 warning / TODO。

当前状态：

- `GltfLoader` 已读取 textureInfo 的 `index` / `texCoord`。
- glTF sampler filter/wrap 已映射到 asset 层 sampler 描述。
- 缺失 sampler 使用默认 linear / repeat。
- 非法 sampler index 和 unsupported `texCoord != 0` 会输出 warning。
- `assets/models/sampler_fixture.gltf` 已覆盖默认 sampler、explicit sampler 和 `texCoord=1`。
- `gltf_loader_smoke` 已验证新 slot path、texCoord 和 sampler 字段。

说明：

- `texCoord=1` 当前只记录到 asset 数据并 warning；shader 仍使用 `uv0`。
- `KHR_texture_transform` 未接入。

### 0.19.3 RHI SamplerDesc 补齐

目标：

- `rhi::AddressMode` 增加 `MirroredRepeat`。
- Vulkan sampler 映射补齐 `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT`。
- sampler desc equality / key helper 如果需要，放在 renderer 层或局部 helper，不污染 RHI。

审核点：

- RHI 仍不暴露 Vulkan 类型。
- Vulkan mapping 有 default/error 保护。
- 现有 sampler 默认行为不变。

当前状态：

- `rhi::AddressMode::MirroredRepeat` 已加入公共 RHI sampler 描述。
- Vulkan 后端已映射到 `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT`。
- `framework_headers_smoke` 已覆盖新增 RHI enum。

说明：

- 0.19.3 只补齐 RHI/Vulkan sampler address mode 能力。
- glTF sampler 到 `TextureResource` 创建的实际传递仍属于 0.19.4，当前 renderer 仍使用既有默认 sampler。

### 0.19.4 TextureResourceDesc / TextureCache 接入 sampler

目标：

- `TextureResourceDesc` 支持 sampler override。
- `TextureResource` 使用 override 创建 sampler。
- `TextureCacheKey` 纳入 sampler 描述。
- `ModelResource` 把 asset sampler 描述转换为 RHI sampler 描述。

审核点：

- `TextureResource` 仍负责 texture/view/sampler 生命周期。
- fallback texture 的 sampler 行为稳定。
- 同 path 不同 sampler 不错误复用。
- 没有 Vulkan 类型泄漏。

### 0.19.5 Tests

目标：

- `gltf_loader_smoke` 覆盖 sampler filter / wrap / texCoord。
- `model_resource_smoke` 覆盖 sampler desc 传递到 fake device。
- `texture cache` smoke 覆盖 sampler key。
- `framework_headers_smoke` 覆盖新增 enum / desc。

建议新增小 fixture：

```text
assets/models/sampler_fixture.gltf
```

fixture 内容：

- 至少两个 material 指向同一 image。
- material A 使用默认 sampler。
- material B 使用 explicit sampler，例如 nearest + clamp + mirrored repeat。
- 两者共享 image path，但 sampler 不同，用于验证 cache key。

审核点：

- fixture 小而可读。
- 不依赖 DamagedHelmet 才能通过。

### 0.19.6 Sandbox smoke

必须运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

默认 sandbox smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe
```

DamagedHelmet optional smoke：

```powershell
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

### 0.19.7 Phase 0.19 收尾

目标：

- 更新本文档实现状态。
- 记录 sampler / texture info 当前支持范围。
- 记录 `texCoord != 0` 和 `KHR_texture_transform` 后续计划。
- 需要用户明确要求时再同步 `docs/codex_handoff.md`。

## 审核检查点

- `asset/` 不依赖 renderer/RHI/Vulkan。
- glTF sampler 默认值符合规范。
- glTF explicit sampler 可以传递到 `TextureResource`。
- `MirroredRepeat` 若支持，应在 RHI 和 Vulkan 后端都有映射。
- `TextureCache` 不因 sampler 不同而错误复用资源。
- 当前不扩展 `MeshVertex` 和 shader 输入到 `uv1`。
- `texCoord != 0` 有明确 warning / TODO。
- default sandbox 行为不变。
- DamagedHelmet 仍是 optional local smoke。

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

Phase 0.19 完成时应满足：

- glTF `samplers` 可以被 asset 层读取并保留。
- texture slot 能表达 path、texCoord、sampler。
- sampler 描述能从 glTF 传递到 RHI sampler 创建。
- default sampler 行为兼容旧 fixture。
- `MirroredRepeat` 支持或有明确 fallback 策略。
- `TextureCache` key 能区分不同 sampler。
- build、CTest、default sandbox、DamagedHelmet optional smoke 通过。
- 文档明确仍未支持 `TEXCOORD_1`、`KHR_texture_transform`、IBL/HDR/tone mapping。

## 当前实现状态

### 0.19.4 TextureResourceDesc / TextureCache 接入 sampler

- `MaterialTextureSlotData` 已补充 `hasSampler`，用于区分 glTF 缺省 sampler 和 explicit sampler。
- `TextureResourceDesc` 已支持 `sampler` / `hasSamplerOverride`。
- `TextureResource` 在 `hasSamplerOverride=true` 时使用传入的 `rhi::SamplerDesc` 创建 sampler，否则保持既有默认 linear/repeat 策略。
- `TextureCacheKey` 已纳入 sampler override 描述，避免同一路径、同 colorSpace 但 sampler 不同的纹理资源被错误复用。
- `ModelResource` 已把 asset 层 sampler 转换为 RHI `SamplerDesc`，转换逻辑保留在 renderer 层，asset 层不依赖 RHI/Vulkan。

说明：
- 当前 `TextureResource` 仍同时拥有 texture/view/sampler。同 image 不同 sampler 会重复创建 texture/view/sampler，这是 Phase 0.19 的最小实现；后续可拆成 image/view cache + sampler cache。
- `TextureCacheKey` 保留 `hasSamplerOverride`，因为默认 sampler 的 mip filter 会受实际 mip count 影响，而 key 在图片解码前就需要确定。

### 0.19.5 Tests

- `gltf_loader_smoke` 覆盖 default sampler、explicit sampler、filter/wrap、`texCoord=1` 和 legacy path 同步。
- `model_resource_smoke` 覆盖 TextureCache sampler key，验证同 path/colorSpace 不同 sampler 会得到不同 `TextureResource`。
- `model_resource_smoke` 覆盖 ModelResource sampler override，验证 asset sampler 能传到 fake RHI device 的 sampler desc。
- `framework_headers_smoke` 覆盖 `hasSampler` 和 `MirroredRepeat` 的头文件集成。

### 0.19.6 Sandbox smoke

已通过：
```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
build/msvc-vcpkg/Debug/ark_sandbox.exe
build/msvc-vcpkg/Debug/ark_sandbox.exe assets/models/DamagedHelmet/DamagedHelmet.gltf
```

结果：
- CTest: 8/8 passed。
- default sandbox 成功启动并渲染 `assets/models/forward_multinode_fixture.gltf`。
- DamagedHelmet optional sandbox 成功启动并加载 `assets/models/DamagedHelmet/DamagedHelmet.gltf`。

### 0.19.7 Phase 0.19 收尾

Phase 0.19 已完成 glTF 2.0 core sampler / textureInfo 的最小闭环：

- asset 层已能表达 texture slot 的 path、`texCoord`、sampler 和 sampler 是否显式存在。
- glTF loader 已读取 textureInfo `index` / `texCoord`，并读取 glTF `samplers` 的 filter / wrap。
- RHI 已补齐 `MirroredRepeat` address mode，Vulkan 后端已映射到对应 `VkSamplerAddressMode`。
- renderer 层已把 asset sampler 转换成 RHI `SamplerDesc`，并传入 `TextureResource` 创建 sampler。
- `TextureCache` 已把 sampler override 纳入 key，避免不同 sampler 的同图资源错误复用。
- `sampler_fixture.gltf` 覆盖 default sampler、explicit sampler、同 image 不同 sampler 和 `texCoord=1` warning 路径。

当前保留限制：

- `texCoord != 0` 只记录 warning，shader 仍使用 `uv0`。
- 暂不支持 `TEXCOORD_1`、`KHR_texture_transform`、anisotropy、compare sampler。
- 暂不拆独立 `SamplerCache`，同 image 不同 sampler 会重复创建 texture/view/sampler。
- 暂不处理 alphaMode、doubleSided、IBL、HDR/tone mapping。

提交前验证状态：

- `cmake --build --preset msvc-vcpkg-local-debug` passed。
- `ctest --preset msvc-vcpkg-local-debug` passed，8/8 tests passed。
- default sandbox smoke passed。
- DamagedHelmet optional sandbox smoke passed。

## 后续 Phase 建议

Phase 0.19 后建议进入：

1. `TEXCOORD_1` / 多 UV 通道支持。
2. `KHR_texture_transform` 最小支持。
3. alphaMode / alphaCutoff / doubleSided pipeline 分流。
4. 可配置 scene light / camera。
5. HDR framebuffer / tone mapping。
6. IBL / environment map / BRDF LUT。
7. renderer 级资源/场景加载入口，替代内部默认 scene 过渡方案。
