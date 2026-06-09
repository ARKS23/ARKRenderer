# Phase 0.13 Runtime Model Resource Deferred Reset 最小闭环

## 阶段判断

Phase 0.12 已经完成 GPU object deferred destruction 的第一段闭环：

```text
TextureResource
    -> TextureView / Sampler / Texture / StagingBuffer
    -> DeviceContext::deferRelease*
    -> VulkanDeletionQueue
    -> frame fence signal 后 flush
```

这解决了 texture cache 运行期 clear 时 texture/view/sampler 立即析构的问题。但当前 model 级资源还没有形成完整运行期 unload 语义：

- `MeshResource` 的 vertex/index GPU buffer 仍只依赖 `Scope<Buffer>` immediate 析构。
- `ModelResource::reset()` 当前会 immediate clear mesh/material/local texture cache。
- `TextureCache::clearDeferred()` 清空 map 后，调用方必须保证没有 material 继续持有旧 `TextureResource*`。
- 默认 renderer 仍使用内部默认 `ModelResource + RenderScene`，还没有显式 scene/model unload API。

因此 Phase 0.13 不应直接进入 mipmap、PBR 或 RenderGraph，而应先补齐 model 级 runtime reset 的最小安全路径。目标是：当运行期需要卸载或替换一个 model 时，mesh buffer、local texture cache 和 material 引用能够按明确顺序释放，并继续与 frame fence 对齐。

## 目标

Phase 0.13 的目标是在不引入完整 ResourceManager 的前提下完成以下能力：

- `MeshResource` 支持运行期 deferred release，覆盖 vertex/index GPU buffer 和可能残留的 staging buffer。
- `ModelResource` 新增 `resetDeferred(context)`，用于运行期 model unload / replacement。
- `ModelResource::reset()` 保留为 shutdown / GPU idle immediate path。
- `ModelResource::resetDeferred(context)` 明确释放顺序，避免 material 继续引用即将清空的 local texture cache。
- 内部 local `TextureCache` 可以通过 `clearDeferred(context)` 安全释放。
- 外部共享 `TextureCache` 的释放责任继续由调用方持有，本阶段不实现引用计数或全局 ResourceManager。
- smoke tests 覆盖 model create -> upload -> resetDeferred 的 ownership 转移。
- 文档同步记录 Phase 0.12 之后的真实边界，避免误以为完整资源卸载已完成。

## 非目标

Phase 0.13 暂不做以下内容：

- 不做完整 ResourceManager handle 系统。
- 不做引用计数、asset database、hot reload 或异步 streaming。
- 不做 texture cache LRU、内存预算、统计 UI 或自动 unload 策略。
- 不做 mipmap generation。
- 不做 PBR material。
- 不做 normal / metallicRoughness / occlusion / emissive texture。
- 不做 descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction。
- 不做 RenderGraph、bindless、descriptor indexing 或 global texture array。
- 不做默认 sandbox scene 加载 API 重构；只补资源对象的 reset 语义。

这些内容应等 model 级 deferred reset 稳定后再进入后续阶段。

## 模块边界

Phase 0.13 继续遵守：

```text
docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md
docs/phase/phase08.md
docs/phase/phase09.md
docs/phase/phase10.md
docs/phase/phase11.md
docs/phase/phase12.md
```

硬性边界：

- `asset/` 不参与 GPU resource release。
- `renderer/` 可以请求 deferred release，但不能接触 Vulkan 类型。
- `rhi/` 提供 API 无关的 buffer/texture deferred release 接口。
- `rhi/vulkan/` 持有实际 deletion queue 和 frame fence flush 语义。
- deferred release 仍必须在 command recording 阶段、dynamic rendering scope 外发生。
- shutdown 仍可以在 `device.waitIdle()` 后走 immediate destruction。
- `ForwardPass` 不接管 model/resource lifetime，只消费 `RenderQueue`。

## 推荐数据流

运行期 model unload 的目标数据流：

```text
Frame N
    -> ForwardPass::prepare()
        -> ModelResource::upload()
            -> MeshResource::upload()
                -> deferReleaseBuffer(mesh staging)
            -> MaterialResource::upload()
                -> TextureResource::upload()
                    -> deferReleaseBuffer(texture staging)
    -> ForwardPass::execute()
        -> descriptor samples texture
        -> draw uses mesh vertex/index buffers
    -> submit frame N

Runtime unload during Frame N+1 command recording
    -> ModelResource::resetDeferred(context)
        -> clear draw-visible primitive/instance metadata
        -> clear MaterialResource references
        -> MeshResource::releaseDeferred(context)
            -> deferReleaseBuffer(vertex buffer)
            -> deferReleaseBuffer(index buffer)
            -> deferReleaseBuffer(staging buffers if still alive)
        -> local TextureCache::clearDeferred(context)
            -> TextureResource::releaseDeferred(context)
    -> objects stored in current frame deletion queue

Later frame begin after fence signal
    -> VulkanDeletionQueue::flush()
        -> queued Scope<T> clear()
```

注意：

- material 本身不拥有 GPU object，但保存 `TextureResource*`，所以 model reset 时应先让 material 不再可被 draw queue 使用。
- mesh buffer 被 draw command 直接引用，运行期 reset 必须延迟释放。
- local texture cache 由 `ModelResource` 拥有，可以在 `resetDeferred()` 中清理。
- 外部 texture cache 不应由 `ModelResource` 自动清理，否则会破坏共享 cache 的 ownership 边界。

## 设计建议

### renderer::MeshResource

当前接口：

```cpp
bool create(rhi::RenderDevice& device, const asset::MeshPrimitiveData& mesh);
bool upload(rhi::DeviceContext& context);
void bind(rhi::DeviceContext& context) const;
```

建议新增：

```cpp
bool releaseDeferred(rhi::DeviceContext& context);
void resetImmediate();
```

`releaseDeferred()` 语义：

- 用于运行期 unload/reload。
- 依次 deferred release vertex buffer、index buffer、vertex staging buffer、index staging buffer。
- 成功后 `isReady()` 返回 false。
- 如果 staging buffer 已经在 upload 后被移走，跳过即可。
- 不接触 Vulkan 类型。

`resetImmediate()` 语义：

- 用于 shutdown / GPU idle / create 失败路径。
- 清空所有 buffer scope、index count 和 upload 状态。

建议 release 顺序：

```text
vertex staging buffer
index staging buffer
vertex buffer
index buffer
```

说明：staging buffer 与 draw 没有直接依赖，但同样可能已有 copy 命令引用；只要进入同一 frame deletion queue 并在 fence 后 flush，顺序不是主要风险。vertex/index buffer 都必须延迟。

### renderer::ModelResource

当前接口：

```cpp
void reset();
```

建议新增：

```cpp
bool resetDeferred(rhi::DeviceContext& context);
```

`resetDeferred()` 语义：

- 用于运行期 model unload/replacement。
- 先清空 `m_Instances` / `m_Primitives`，使 model 不再被后续 queue build 展开。
- 清空 `m_Materials`，移除对 `TextureResource*` 的引用。
- 遍历 `m_Meshes`，调用 `MeshResource::releaseDeferred(context)`。
- 清空 `m_Meshes`。
- 清理 `m_LocalTextureCache.clearDeferred(context)`。
- 如果使用外部 `TextureCache` 创建，该 cache 不由 `ModelResource` 清理。

需要注意当前 `ModelResource` 没有记录自己创建时使用的是 local cache 还是 external cache。Phase 0.13 建议增加一个最小字段：

```cpp
bool m_UsesExternalTextureCache = false;
```

创建路径：

- `create(device, model)`：使用 `m_LocalTextureCache`，`m_UsesExternalTextureCache = false`。
- `create(device, textureCache, model)`：使用外部 cache，`m_UsesExternalTextureCache = true`。

reset 路径：

- local cache：`resetDeferred()` 中可以清 `m_LocalTextureCache.clearDeferred(context)`。
- external cache：只清 material 引用，不清外部 cache。

### renderer::TextureCache

Phase 0.13 不扩大 texture cache 策略，只沿用 Phase 0.12：

```cpp
bool clearDeferred(rhi::DeviceContext& context);
void clear();
```

需要在文档和注释中继续强调：

- `clearDeferred()` 清空 map 后，调用方不能再 draw 旧 material。
- 外部共享 cache 的 clear 时机必须由拥有者控制。
- 本阶段不做引用计数。

### renderer::RenderScene / RenderQueue

本阶段建议不扩展 scene API，但需要记录运行期 reset 约束：

- reset model 前应先确保后续不再把该 model 放进 `RenderScene`。
- `RenderQueue` 是 transient draw list，每帧重建，不拥有资源。
- 如果未来要做 scene remove API，应在 remove 后再调用 model deferred reset。

## 实施顺序

### 0.13.0 文档与范围确认

目标：

- 新增 `docs/phase/phase13.md`。
- 明确主线是 runtime model resource deferred reset。
- 明确不进入 mipmap、PBR、RenderGraph 或完整 ResourceManager。

当前实现状态：

- 文档已创建，等待审核。

### 0.13.1 MeshResource deferred release

目标：

- `MeshResource` 新增 `releaseDeferred(context)`。
- `MeshResource` 新增 `resetImmediate()`。
- release 后 `isReady()` 返回 false。

审核检查点：

- vertex/index GPU buffer 必须通过 RHI deferred release。
- staging buffer 已释放时不报错。
- 失败路径不假装释放成功。
- 不接触 Vulkan 类型。

当前实现状态：

- 已完成。
- `MeshResource` 已新增 `releaseDeferred(context)` 和 `resetImmediate()`。
- `releaseDeferred()` 会延迟释放 vertex/index GPU buffer，并兼容尚未移走的 vertex/index staging buffer。
- release 后 `isReady()` 返回 false，`indexCount()` 返回 0。

### 0.13.2 ModelResource resetDeferred

目标：

- `ModelResource` 新增 `resetDeferred(context)`。
- 区分 local texture cache 和 external texture cache。
- `reset()` 保留为 immediate path。

审核检查点：

- reset 后 `empty()` 为 true。
- local cache 会 deferred clear。
- external cache 不由 model 自动清。
- material 引用在 texture cache 清空前被移除。

当前实现状态：

- 已完成。
- `ModelResource` 已新增 `resetDeferred(context)`。
- `ModelResource` 记录当前创建路径是否使用 external texture cache。
- local cache 路径会在 deferred reset 中调用 `m_LocalTextureCache.clearDeferred(context)`。
- external cache 路径只清除 model/material/mesh 引用，不清外部 cache。

### 0.13.3 Runtime reset smoke tests

目标：

- 扩展 fake RHI context，统计 deferred buffer 数量。
- 测试 `MeshResource::releaseDeferred()`。
- 测试 local-cache `ModelResource::resetDeferred()` 会释放 mesh buffer 和 local texture。
- 测试 external-cache `ModelResource::resetDeferred()` 不清外部 cache。

建议测试：

- mesh create -> upload -> releaseDeferred。
- model create(local cache) -> upload -> resetDeferred。
- model create(external cache) -> upload -> resetDeferred，确认外部 cache size 不变。
- 原有 model resource / render queue smoke 不回退。

当前实现状态：

- 已完成。
- `ark_model_resource_smoke` 已新增 mesh deferred release、local model deferred reset、external cache model deferred reset 覆盖。
- 已确认原有 texture resource / texture cache / model resource / render queue smoke 不回退。

### 0.13.4 文档与 handoff 同步

目标：

- 更新 `docs/phase/phase13.md` 当前实现状态。
- 同步 `docs/codex_handoff.md` 到 Phase 0.12/0.13 最新状态。
- 记录剩余 TODO：mipmap、PBR material、glTF 多 texture、pipeline/shader/layout deferred destruction、真正资源/场景加载入口。

当前实现状态：

- 已完成。
- `docs/phase/phase13.md` 已记录 0.13.1 ~ 0.13.4 完成状态和验证记录。
- `docs/codex_handoff.md` 已同步到 Phase 0.13 状态。
- `assets/models/DamagedHelmet/` 仍作为未跟踪真实资产保留，未纳入本阶段提交范围。

## 审核检查点

- `asset/` 没有参与 GPU release。
- `renderer/` 只通过 RHI 请求 deferred release。
- `MeshResource` release 后不会继续被 draw。
- `ModelResource` reset 后不会继续生成 draw item。
- local texture cache 和 external texture cache ownership 清楚。
- `ForwardPass` 不接管 resource lifetime。
- deferred release 不进入 dynamic rendering scope。
- 当前默认 sandbox 多 node textured draw 不回退。

## 验证计划

代码实现后运行：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

如涉及默认 sandbox 渲染路径，继续运行 sandbox smoke：

```powershell
$stdout = "build\msvc-vcpkg\ark_sandbox_stdout.log"
$stderr = "build\msvc-vcpkg\ark_sandbox_stderr.log"
Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath "build\msvc-vcpkg\Debug\ark_sandbox.exe" -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Start-Sleep -Seconds 3
if ($process.HasExited) {
    Write-Output "ark_sandbox exited early with code $($process.ExitCode)"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
    exit $process.ExitCode
} else {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Write-Output "ark_sandbox started successfully and was stopped after smoke check"
    Get-Content -Path $stdout,$stderr -ErrorAction SilentlyContinue
}
```

## 完成标准

Phase 0.13 完成时应满足：

- `MeshResource` 支持 vertex/index/staging buffer deferred release。
- `ModelResource` 支持运行期 `resetDeferred(context)`。
- local texture cache 在 model deferred reset 中安全释放。
- external texture cache 不被 model 错误清理。
- `reset()` 和 `resetDeferred()` 的用途明确。
- 现有 Phase 0.12 texture deferred release 行为不回退。
- build、ctest 和必要 sandbox smoke 通过。

## 当前验证记录

截至 2026-06-09，0.13.1 ~ 0.13.4 已完成并验证：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

结果：build 通过，8/8 ctest 通过。

sandbox smoke：

```text
ark_sandbox started successfully and was stopped after smoke check
```

剩余边界：

- external texture cache 的真实 unload 时机仍由外部拥有者控制，本阶段不做引用计数。
- descriptor set / descriptor layout / pipeline / shader module 的 deferred destruction 仍未纳入本阶段。
- mipmap、PBR material、glTF 多 texture 和真正资源/场景加载入口留给后续 phase。

## 后续 Phase 建议

Phase 0.13 完成后，建议按以下顺序继续：

1. mipmap upload / generation 策略，包含 `TransferSrc` usage、per-mip barrier 和 blit 支持。
2. glTF material 数据扩展：baseColorFactor、metallicFactor、roughnessFactor。
3. glTF normal / metallicRoughness / emissive texture 支持，并明确 color / non-color texture 语义。
4. 最小基础光照或 PBR shader。
5. 真正的 renderer 资源/场景加载入口，替代内部默认 scene 过渡方案。
6. pipeline / shader / descriptor layout 的 deferred destruction。
7. RenderGraph 第一版 pass/resource 声明。
