# Shadow / CSM 阴影异常移动修复方案

## 背景

在 Sponza + DamagedHelmet + probe spheres 的 sandbox 场景中，开启 CSM 后可以通过调整 shadow distance / cascade extent 覆盖场景，但移动相机时会明显感觉阴影跟着相机移动，并且局部出现不合理的大块阴影、阴影缺失或阴影不贴合几何体的问题。

这个问题不应只通过调大 shadow distance、cascade extent 或 bias 解决。当前表现更像是 ShadowPass 数据提交、CSM 投影拟合和 alpha mask shadow caster 三类问题叠加。

## 现象

- 移动相机时，部分阴影边界像跟着相机滑动。
- 部分区域有明显块状 / 矩形阴影。
- 一些应当受阴影影响的区域没有稳定阴影。
- 调整 CSM 覆盖距离后，覆盖范围改善，但阴影稳定性和形状仍不正常。

## 初步根因

### 1. ShadowPass per-draw uniform 生命周期错误

`ShadowPass::renderShadowLayer` 当前在同一个 frame slot 里复用一个 `m_ShadowBuffers[frameSlot]`。每个 draw 前写入新的 `ShadowUniform`，然后立即提交 draw。

但 Vulkan 后端的 `updateBuffer` 是直接写 CPU-visible uniform buffer，不是把 uniform 内容录进 command buffer。命令录制期间多次写同一个 buffer 地址，GPU 真正执行 draw 时可能看到的是最后一次写入的 uniform 数据。

风险：

- 同一层 cascade 内多个 draw 可能读到同一个 model matrix。
- 多层 cascade 连续渲染时，前面 cascade 的 draw 也可能读到后面 cascade 写入的 uniform。
- shadow map 中的 caster 位置会错乱，导致阴影不贴世界、不贴模型。

优先级：P0。

### 2. CSM 只拟合 receiver slice，没有纳入 caster bounds

当前 `ShadowCascadeBuilder` 以相机 frustum slice 的 world bounds 作为每级 cascade 的拟合对象。这个 bounds 更接近“当前 cascade 要覆盖的 receiver 区域”，但 directional shadow 还需要包含会投影到 receiver 上的 caster。

如果 caster 在 light-space depth 上位于 receiver 前方，但没有落进当前 cascade 的 near/far 范围，它就不会写入 shadow map。随着相机移动，receiver slice 跟随相机移动，caster 是否被包含也会变化，从而出现阴影缺失、跳变或看起来跟着相机移动。

优先级：P0 / P1。

### 3. Alpha mask 材质在 ShadowPass 中被当成全不透明

当前 shadow fragment shader 是空 shader，ShadowPass 只跳过 `AlphaMode::Blend`，但没有处理 `AlphaMode::Mask` 的 baseColor alpha cutoff。

因此 Sponza 中的植物、镂空贴图或其他 alpha cutout 几何体可能在 shadow map 中变成完整多边形，产生不合理的大块矩形阴影。

优先级：P1。

### 4. 当前 CSM 稳定策略仍偏基础

当前 cascade light projection 会随相机 frustum slice 重建，这是 CSM 的基本行为。Texel snapping 能降低亚像素抖动，但当前拟合仍使用 slice AABB，投影范围会随相机方向和 slice bounds 变化。

后续如果需要更稳定的 CSM，应考虑：

- 每级使用 bounding sphere / stable radius 固定 ortho extent。
- cascade 间增加 overlap / fade，减轻 split seam。
- 区分 debug coverage、receiver coverage 和 caster coverage。

优先级：P2。

## 修复顺序

### Step 1：修复 ShadowPass per-draw uniform

目标：保证 shadow map 中每个 draw 使用正确的 `lightViewProjection + model`。

推荐方案：

1. 优先引入 shadow pass push constants。
   - `ShadowUniform` 当前是两个 `mat4`，大小 128 bytes，符合 Vulkan 最小 push constant 保证。
   - 每个 draw 调用 push constants，再 draw。
   - 这是最适合 depth-only shadow pass 的方案，避免 per-draw uniform buffer / descriptor set 管理膨胀。

2. 如果暂不扩 RHI push constants，则使用 per-cascade per-draw uniform 资源。
   - 每个 frame slot 为 `cascadeCount * drawCount` 准备独立 uniform buffer。
   - 每个 draw 绑定对应 descriptor set。
   - 不复用同一个 buffer region。

不推荐：

- 继续复用同一个 uniform buffer 并在 draw 前反复写入。
- 只改成多个 buffer 但复用同一个 descriptor set，并在录制期间反复 update descriptor set。

### Step 2：扩展 CSM caster coverage

目标：receiver slice 内的像素能看到稳定的遮挡物。

第一版建议：

1. `buildCascadeShadowFrameData` 增加 scene bounds / caster bounds 输入。
2. 仍用 camera frustum slice 计算 receiver 的 light-space X/Y 范围。
3. 使用 scene bounds 或 caster bounds 扩展 light-space depth range。
4. 保留 texel snapping，先不引入复杂裁剪。

后续增强：

- 按 light-space X/Y 和 scene object bounds 做 caster 过滤。
- 为每级 cascade 记录 receiver bounds 和 caster bounds，方便 debug UI 显示。
- 增加 shadow caster count / clipped caster count 诊断信息。

### Step 3：支持 alpha mask shadow caster

目标：alpha cutout 几何体在 shadow map 中按 alpha cutoff 正确 discard。

建议方案：

1. ShadowPass 为 alpha mask 材质准备单独 pipeline / shader variant。
2. Shadow shader 增加必要的 UV、baseColor texture、sampler、alphaCutoff。
3. `AlphaMode::Mask` 在 shadow fragment 中采样 alpha 并 discard。
4. Opaque 材质继续走轻量 depth-only 路径。

注意：

- `AlphaMode::Blend` 初期仍可不投射阴影。
- 双面 alpha mask 材质需要结合 `doubleSided` 决定 culling。

### Step 4：CSM 质量增强

目标：降低相机移动 / 旋转时的 cascade popping 和 seam。

建议方案：

1. 每级 cascade 使用 stable sphere radius 计算 ortho half extent。
2. texel snapping 对齐 light-space projection center。
3. 增加 cascade overlap / fade band。
4. UI 中显示每级 split、extent、texel world size 和 caster depth range。

## 验证方案

### 自动测试

- 保持现有 shader asset smoke 测试通过。
- 增加 ShadowPass uniform 策略测试：
  - 验证不再只有单个 shadow uniform buffer 被所有 draw 复用。
  - 如果接入 push constants，则测试 pipeline layout 和 shader contract 存在 push constant range。
- 增加 CSM builder 测试：
  - split 距离递增。
  - receiver bounds 有效。
  - caster-expanded depth range 覆盖 scene bounds 的 light-space depth。

### Sandbox 视觉验证

场景：

- Sponza 放大场景。
- DamagedHelmet 放在中庭。
- 多个 probe spheres 用于观察受影、Bloom、ToneMapping、IBL。
- 保留 Shadow Debug UI。

验证点：

- 移动相机时，地面和墙面上的阴影应贴住世界位置，不应出现明显滑动。
- CSM split 切换处不应出现大块阴影跳变。
- 植物 / 镂空材质不应投射完整矩形阴影。
- 调整 cascade extent、shadow distance、bias、PCF 后不应触发 device lost。

## 完成标准

- ShadowPass 每个 draw 使用正确的 per-draw 数据。
- CSM receiver coverage 与 caster coverage 分离，阴影不再因相机移动明显丢失。
- Alpha mask 材质不再产生整块错误阴影。
- sandbox 中关闭 debug overlay 后，阴影视觉结果稳定、可解释。
