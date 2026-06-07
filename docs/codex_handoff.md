# Codex Handoff Summary

## 1. 本阶段任务目标

本阶段主要完成 Phase 0.5：从最小三角形推进到带 uniform buffer、descriptor set、indexed draw、default depth buffer 和 dynamic rendering depth attachment 的旋转 cube 渲染闭环。

原始目标包括：

- 补齐 RHI descriptor、pipeline layout、uniform buffer 更新接口。
- 实现 Vulkan descriptor set layout / descriptor pool / descriptor set 最小闭环。
- 实现 `DeviceContext::bindDescriptorSet()` 和 `DeviceContext::updateBuffer()`。
- 新增 cube shader、CubePass，并使用 vertex buffer + index buffer + uniform buffer 绘制 cube。
- 让 swapchain 拥有默认 depth texture / depth view。
- 将 depth attachment 接入 Vulkan dynamic rendering，并开启 cube pipeline 的 depth test / depth write。
- 同步 `docs/phase/phase05.md`，并补充代码阅读指南。

## 2. 已完成内容

- RHI 层已补齐 descriptor 基础描述：
  - `DescriptorType`
  - `ShaderStageFlags`
  - `DescriptorBindingDesc`
  - `DescriptorSetLayoutDesc`
  - `BufferDescriptor`
  - `DescriptorSet::updateUniformBuffer()`

- Vulkan descriptor 最小闭环已完成：
  - `VulkanDescriptorSetLayout`
  - `VulkanDescriptorManager`
  - `VulkanDescriptorSet`
  - `VulkanDevice::createDescriptorSetLayout()`
  - `VulkanDevice::createDescriptorSet()`

- Pipeline layout 与 descriptor set 绑定已完成：
  - `PipelineLayoutDesc` 支持 `descriptorSetLayouts`。
  - `VulkanPipelineLayout` 创建时收集 `VkDescriptorSetLayout`。
  - `VulkanCommandContext::setPipeline()` 记录当前 graphics pipeline layout。
  - `VulkanCommandContext::bindDescriptorSet()` 调用 `vkCmdBindDescriptorSets()`。

- Uniform buffer 更新路径已完成：
  - `DeviceContext::updateBuffer()` 已加入公共 RHI。
  - `VulkanCommandContext::updateBuffer()` 转发到 `VulkanBuffer`。
  - `VulkanBuffer::updateData()` 支持 `MemoryUsage::CpuToGpu` buffer 的 map / copy / flush。

- Cube shader 和 CMake 接入已完成：
  - 新增 `shaders/cube.vert.hlsl`。
  - 新增 `shaders/cube.frag.hlsl`。
  - CMake 会编译 `cube.vert.spv` / `cube.frag.spv`。
  - `shader_assets_smoke` 覆盖 triangle 和 cube shader 产物。

- CubePass 已完成并接入默认帧流程：
  - `CubePass` 创建 vertex buffer、index buffer、per-frame uniform buffer、descriptor set layout、descriptor set、pipeline layout、pipeline。
  - 默认 `FrameRenderer` 已从 `TrianglePass` 切换到 `CubePass`。
  - `TrianglePass` 保留为 Phase 0.4 最小三角形示例。

- Owned texture 与 swapchain depth 已完成：
  - `VulkanTexture` 支持 VMA owned image。
  - swapchain color image 继续保持 borrowed，不销毁外部 `VkImage`。
  - `VulkanTextureView` 支持从 `VulkanTexture` 创建 `VkImageView`。
  - `VulkanSwapChain` 创建、销毁和 resize 时管理 default depth texture / depth view。

- Dynamic Rendering depth 接入已完成：
  - `VulkanCommandContext::pipelineBarrier()` 支持 `DepthStencilWrite` / `DepthStencilRead`。
  - texture barrier aspect mask 根据 format 选择 color / depth / depth-stencil。
  - `VulkanCommandContext::beginRendering()` 支持可选 depth attachment。
  - `FrameRenderer` 每帧 transition depth 到 `DepthStencilWrite`。
  - `CubePass` pipeline 传入 depth format，并开启 depth test / depth write。

- 文档与 smoke 收尾已完成：
  - `docs/phase/phase05.md` 已记录 Phase 0.5 当前实现状态。
  - 已新增 Phase 0.5 完成摘要和代码阅读指南。
  - `framework_headers_smoke.cpp` 覆盖 depth texture、depth barrier、depth pipeline state 和 CubePass 头文件。

## 3. 关键设计决策

- `RenderDevice` 继续只负责资源创建，不参与每帧 draw。
  - 原因：保持 Diligent Engine 风格中的“创建设备对象”和“使用命令上下文”职责分离。
  - 涉及模块：`src/rhi/RenderDevice.h`、`src/rhi/vulkan/VulkanDevice.cpp`。
  - 后续影响：ResourceManager / PipelineCache 可以继续挂在 device 侧，不污染每帧命令录制。

- `DeviceContext` 负责资源使用、命令录制、buffer 更新和 draw。
  - 原因：`updateBuffer()` 是“使用资源”的行为，放在 command context 侧比放在 RenderDevice 更贴合当前架构。
  - 涉及模块：`src/rhi/DeviceContext.h`、`src/rhi/vulkan/VulkanCommandContext.cpp`。
  - 后续影响：后续 upload system 可以替换 `updateBuffer()` 的内部实现，但上层调用语义可以保持稳定。

- `CubePass` 自持阶段验证资源。
  - 原因：Phase 0.5 还没有 ResourceManager / RenderGraph，不引入过重资源系统，先用 pass-local 资源完成闭环。
  - 涉及模块：`src/renderer/passes/CubePass.h`、`src/renderer/passes/CubePass.cpp`。
  - 后续影响：进入材质、GLTF、场景系统后，mesh / material / texture 资源应从 pass-local 逐步迁移到 ResourceManager 或资产系统。

- per-frame uniform buffer / descriptor set 使用双缓冲。
  - 原因：避免 CPU 更新当前帧 uniform 时覆盖 GPU 仍在读取的上一帧 uniform 数据。
  - 涉及模块：`CubePass`、`VulkanCommandContext`。
  - 后续影响：如果后续 frames-in-flight 数量可配置，需要把 `CubePass::FramesInFlight` 与 `DeviceContext` 的 frame slot 数统一。

- swapchain color image 是 borrowed，default depth image 是 owned。
  - 原因：swapchain image 由 `VkSwapchainKHR` 拥有，不能由 `VulkanTexture` 销毁；depth image 是项目自己创建的，需要 VMA 管理。
  - 涉及模块：`VulkanTexture`、`VulkanTextureView`、`VulkanSwapChain`。
  - 后续影响：后续 texture loader 创建的 GPU-only texture 也应走 owned texture 路径。

- 继续使用 Vulkan Dynamic Rendering，不引入传统 `VkRenderPass` / `VkFramebuffer`。
  - 原因：项目阶段设计已选择 modern Vulkan dynamic rendering，FrameRenderer 直接传 color/depth attachment。
  - 涉及模块：`VulkanCommandContext::beginRendering()`、`VulkanPipelineState.cpp`、`FrameRenderer.cpp`。
  - 后续影响：RenderGraph 后续应生成 dynamic rendering 的 attachment 和 barrier，而不是回退到固定 render pass。

- `ClearPass` 暂时保留为空语义 pass。
  - 原因：真实 clear 由 `beginRendering(loadOp=Clear)` 表达；保留 ClearPass 是为了后续 RenderGraph / pass 顺序演进。
  - 涉及模块：`src/renderer/passes/ClearPass.cpp`、`FrameRenderer.cpp`。
  - 后续影响：后续可以把 clear 表达转成 RenderGraph pass，或者删除空 pass 并在文档中说明。

## 4. 修改文件清单

### 文档

- `docs/phase/phase05.md`
  - 修改内容：记录 Phase 0.5 每个小阶段的实现状态、完成摘要、代码阅读指南、验证命令和后续建议。
  - 原因：方便阶段审核和后续接力开发。
  - 后续注意：Phase 0.6 开始后应新增 `docs/phase/phase06.md`，不要继续把所有后续内容堆进 Phase05。

- `docs/codex_handoff.md`
  - 修改内容：当前交接总结文档。
  - 原因：方便切换到 API 方式或新 Codex 会话后继续开发。
  - 后续注意：本文档是生成后新增文件，尚未提交。

### Renderer 层

- `src/renderer/FrameRenderer.cpp`
  - 修改内容：默认帧流程切换到 `CubePass`；接入 backbuffer/depth barrier；组装 color + depth `RenderingDesc`。
  - 原因：完成 color + depth dynamic rendering 的默认帧调度。
  - 后续注意：这是 RenderGraph 落地前的手动调度器，后续不要在这里堆复杂 pass 依赖逻辑。

- `src/renderer/passes/CubePass.h`
  - 修改内容：新增 CubePass 类，声明 pass-local GPU 资源和 pipeline format 缓存。
  - 原因：Phase 0.5 用于验证 descriptor、uniform、indexed draw 和 depth test。
  - 后续注意：`FramesInFlight = 2` 是当前阶段硬编码，后续应与 DeviceContext frame slot 配置统一。

- `src/renderer/passes/CubePass.cpp`
  - 修改内容：创建 cube vertex/index/uniform/descriptor/pipeline 资源；每帧更新 camera uniform；执行 `drawIndexed(36)`。
  - 原因：默认渲染内容从 triangle 演进到旋转 cube。
  - 后续注意：矩阵行列主序和 Vulkan Y 翻转已按当前 shader 写法处理，但仍建议用 RenderDoc 或截图确认。

- `src/renderer/passes/TrianglePass.*`
  - 修改内容：未在最终 0.5.8/0.5.9 改动中修改，但保留为 Phase 0.4 示例。
  - 原因：保留最小三角形路径便于后续调试 pipeline / shader。
  - 后续注意：默认帧流程现在不执行 TrianglePass。

### RHI 公共层

- `src/rhi/DeviceContext.h`
  - 修改内容：新增 `updateBuffer()`；已有 `bindDescriptorSet()`、`drawIndexed()` 等命令接口用于 CubePass。
  - 原因：提供 CPU 写 uniform buffer 的公共入口。
  - 后续注意：当前只支持直接更新 CPU-visible buffer；GPU-only 上传需要单独 upload system。

- `src/rhi/Texture.h`
  - 修改内容：补齐 `TextureUsage` 位运算和 `hasTextureUsage()`。
  - 原因：支持组合 `RenderTarget` / `DepthStencil` / `ShaderResource` 等 usage。
  - 后续注意：后续 sampled texture 会继续扩展 usage 映射。

- `src/rhi/DescriptorSetLayout.h`
  - 修改内容：定义 descriptor 类型和 shader stage flags。
  - 原因：Phase 0.5 descriptor 最小闭环。
  - 后续注意：当前只支持 `UniformBuffer`，sampled image / sampler 留到 Phase 0.6。

- `src/rhi/DescriptorSet.h`
  - 修改内容：定义 `BufferDescriptor` 和 `updateUniformBuffer()`。
  - 原因：让 pass 可以把 uniform buffer 写入 descriptor set。
  - 后续注意：descriptor 类型增多后应考虑统一 `WriteDescriptorSetDesc`。

- `src/rhi/PipelineLayout.h`
  - 修改内容：`PipelineLayoutDesc` 支持 descriptor set layout 列表。
  - 原因：Vulkan pipeline layout 创建需要 descriptor set layout 信息。
  - 后续注意：公共层不拥有 descriptor set layout，生命周期由调用方保证。

### Vulkan 后端

- `src/rhi/vulkan/VulkanCommandContext.cpp`
  - 修改内容：实现 descriptor set 绑定、buffer 更新转发、depth barrier 映射、dynamic rendering depth attachment。
  - 原因：把 RHI 命令语义落到 Vulkan command buffer。
  - 后续注意：当前 `pipelineBarrier()` 主要处理 texture image barrier，buffer barrier 后续仍需补齐。

- `src/rhi/vulkan/VulkanBuffer.cpp`
  - 修改内容：实现 `updateData()`，支持 `CpuToGpu` buffer map/copy/flush。
  - 原因：CubePass 每帧更新 camera uniform。
  - 后续注意：`GpuOnly initialData upload is not implemented yet` 仍是明确未完成项。

- `src/rhi/vulkan/VulkanDescriptorSetLayout.cpp`
  - 修改内容：创建并销毁 `VkDescriptorSetLayout`，映射 uniform buffer 和 shader stage。
  - 原因：descriptor layout 最小闭环。
  - 后续注意：descriptor type 增加时需要扩展映射和校验。

- `src/rhi/vulkan/VulkanDescriptorManager.cpp`
  - 修改内容：持有固定容量 `VkDescriptorPool`，分配 descriptor set。
  - 原因：Phase 0.5 暂时不做复杂 pool 增长。
  - 后续注意：固定 256 容量是阶段实现，后续需支持 pool 增长、reset 或 frame-local pool。

- `src/rhi/vulkan/VulkanDescriptorSet.cpp`
  - 修改内容：包装 `VkDescriptorSet`，实现 uniform buffer descriptor 写入。
  - 原因：让 CubePass 能绑定 camera uniform。
  - 后续注意：当前 descriptor set 不单独 free，随 descriptor pool 销毁。

- `src/rhi/vulkan/VulkanPipelineLayout.cpp`
  - 修改内容：从 `PipelineLayoutDesc.descriptorSetLayouts` 收集 `VkDescriptorSetLayout` 创建 `VkPipelineLayout`。
  - 原因：descriptor set 绑定需要 pipeline layout。
  - 后续注意：descriptor set layout 生命周期必须长于使用它创建的 pipeline layout / pipeline。

- `src/rhi/vulkan/VulkanPipelineState.cpp`
  - 修改内容：已支持 vertex input、dynamic rendering color/depth format、depth stencil state。
  - 原因：CubePass 需要 indexed cube + depth test。
  - 后续注意：pipeline cache 还没有接入，每次 format 变化会重建 pipeline。

- `src/rhi/vulkan/VulkanTexture.cpp`
  - 修改内容：支持 VMA owned image；borrowed image 用于 swapchain color image。
  - 原因：default depth texture 需要由项目自己创建和释放。
  - 后续注意：owned image 必须走 VMA 构造函数，borrowed image 不能传 `Owned`。

- `src/rhi/vulkan/VulkanTextureView.cpp`
  - 修改内容：支持根据 texture format 创建 `VkImageView`，并选择 color/depth/depth-stencil aspect。
  - 原因：depth view 需要 depth aspect。
  - 后续注意：D24S8 stencil attachment 尚未完整接入 dynamic rendering。

- `src/rhi/vulkan/VulkanSwapChain.cpp`
  - 修改内容：创建、销毁和 resize default depth texture / depth view。
  - 原因：SwapChain 语义负责窗口相关 backbuffer 和默认 depth buffer。
  - 后续注意：resize 后 pass 不应缓存旧 depth view。

### Shader / CMake / Tests

- `shaders/cube.vert.hlsl`
  - 修改内容：新增 cube vertex shader，读取 set 0 / binding 0 的 camera uniform。
  - 原因：CubePass 需要 MVP 矩阵变换。
  - 后续注意：矩阵主序需要用真实画面或 RenderDoc 继续确认。

- `shaders/cube.frag.hlsl`
  - 修改内容：新增 cube fragment shader，输出 vertex color。
  - 原因：阶段验证不引入 texture sampling。
  - 后续注意：Phase 0.6 texture sampling 后可替换为 textured shader。

- `CMakeLists.txt`
  - 修改内容：加入 cube shader 编译目标。
  - 原因：运行时只加载编译后的 SPIR-V。
  - 后续注意：shader 列表增长后可以考虑拆出 helper cmake 文件。

- `tests/shader_assets_smoke.cpp`
  - 修改内容：覆盖 triangle 和 cube 四个 SPIR-V 产物。
  - 原因：确保 CMake shader 编译输出可加载。
  - 后续注意：新增 shader 后同步扩展此 smoke。

- `tests/framework_headers_smoke.cpp`
  - 修改内容：覆盖 CubePass 头文件、depth texture desc、depth barrier、depth pipeline state。
  - 原因：保证公共头文件和轻量描述结构能编译。
  - 后续注意：此测试不创建真实 Vulkan 对象，不替代 sandbox 或 RenderDoc 验证。

## 5. 当前项目状态

- 当前分支：`main`
- 远端状态：`main` 与 `origin/main` 对齐。
- 生成本文档前 `git status -sb`：

```text
## main...origin/main
```

- 生成本文档前 `git diff --stat`：为空。
- 生成本文档前 `git diff --name-status`：为空。
- 当前已修改 / 新增 / 删除文件：
  - 生成本文档前：无已修改、新增或删除文件。
  - 生成本文档后：新增 `docs/codex_handoff.md`，尚未提交。

最近相关 commit：

```text
1a790c9 完成 Phase05 深度渲染收尾
6b8b16c 实现 CubePass 与 SwapChain 深度资源
4a0e41f 实现 Vulkan Descriptor 与绑定链路
bd2100d 补齐 Phase05 RHI 描述结构
a125226 整理文件系统与 Shader 加载流程
```

已实际运行并通过的验证命令：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

CTest 结果：

```text
3/3 tests passed:
- ark_dependency_smoke
- ark_framework_headers_smoke
- ark_shader_assets_smoke
```

已实际运行 sandbox smoke：

```powershell
build\msvc-vcpkg\Debug\ark_sandbox.exe
```

运行方式：隐藏窗口启动 3 秒后停止，并捕获日志。

结果：通过。日志显示 Vulkan instance / device / command context / depth buffer / swapchain / renderer 均成功初始化。

未验证内容：

- 未使用 RenderDoc 验证 draw call、descriptor binding、depth attachment 内容。
- 未做像素级截图比对。
- 未手动长时间运行观察 resize、最小化、连续帧稳定性。
- 未验证不同 GPU / driver / validation layer 版本。

## 6. 未完成事项 / TODO

### P0：必须继续处理

- 进入 Phase 0.6 前，设计并实现 texture sampling 最小闭环：
  - `Sampler` RHI 描述与 Vulkan 实现。
  - sampled image descriptor 类型。
  - texture shader 绑定路径。
  - textured cube 验证。

- 实现 GPU-only texture / buffer 上传路径：
  - 当前 `VulkanBuffer` 对 `GpuOnly initialData` 会报 `not implemented yet`。
  - 需要 staging buffer、copy command、resource transition、flush/submit 策略。

- 用 RenderDoc 或 validation 日志检查 Phase 0.5 默认帧：
  - descriptor set layout 与 shader binding 是否完全匹配。
  - depth attachment layout 是否正确。
  - depth test 是否真正生效。

### P1：建议后续处理

- 将 `CubePass::FramesInFlight = 2` 与 `VulkanCommandContext` 的 frame slot 配置统一。
- 将固定容量 `VulkanDescriptorManager` 升级为可增长 pool 或 frame-local descriptor pool。
- 梳理 `VulkanCommandContext::pipelineBarrier()`，补充 buffer barrier，并考虑后续迁移到 synchronization2。
- 将 `ClearPass` 的空语义与 FrameRenderer 的 clear 逻辑整理清楚，避免后续误解。
- 为 `VulkanDevice::createSampler()`、`createFence()` 等仍未实现的工厂方法补齐真实实现或更新错误信息。

### P2：可选优化

- 把 CMake shader 编译逻辑拆成单独 cmake helper。
- 为 shader bytecode 增加反射检查，自动校验 descriptor set layout 与 shader binding。
- 为 Vulkan object 增加 debug name。
- 增加 screenshot / pixel smoke 或 RenderDoc capture 脚本。
- 后续引入 RenderGraph 后，把 FrameRenderer 中手写 barrier 和 pass 顺序逐步迁移到图调度。

## 7. 已知问题和风险

- `GpuOnly initialData upload is not implemented yet`。
  - 位置：`src/rhi/vulkan/VulkanBuffer.cpp`
  - 风险：后续创建 GPU-only vertex/index/texture 资源时不能直接传 initial data。

- `VulkanDevice::createSampler()`、`createFence()` 仍是未实现工厂。
  - 位置：`src/rhi/vulkan/VulkanDevice.cpp`
  - 风险：Phase 0.6 texture sampling 会立刻需要 sampler。

- `VulkanDescriptorManager` 使用固定容量 descriptor pool。
  - 风险：资源数量增长后会分配失败；目前没有 pool 增长、reset 或回收策略。

- `CubePass::FramesInFlight` 硬编码为 2。
  - 风险：如果后续 swapchain image count 或 command context frame slots 可配置，CubePass 需要同步调整。

- `VulkanCommandContext::pipelineBarrier()` 目前主要处理 texture image barrier。
  - 风险：后续 staging upload、buffer copy、compute pass 需要 buffer barrier 和更完整同步模型。

- Stencil attachment 尚未接入。
  - 当前默认 depth format 是 `D32Float`，正常。
  - 如果后续切到 `D24UnormS8UInt`，需要补 `pStencilAttachment` 或格式相关处理。

- `CubePass` 的矩阵主序和 HLSL `mul()` 写法需要视觉验证。
  - 当前构建与 smoke 通过，但没有像素级验证。
  - 建议用 RenderDoc 检查 uniform 数据和 vertex shader 输出。

- `ClearPass` 当前为空语义。
  - clear 实际由 `beginRendering(loadOp=Clear)` 完成。
  - 后续加入 RenderGraph 前应明确保留或移除该 pass。

- `VulkanDevice.cpp` 中 `glfwGetRequiredInstanceExtensions` 注释为 `暂时GLFW Hardcode`。
  - 当前项目窗口系统就是 GLFW，问题不大。
  - 如果后续支持 Win32/SDL，需要抽象 surface extension 查询。

## 8. 后续继续工作的建议步骤

1. 先阅读 `docs/codex_handoff.md` 和 `docs/phase/phase05.md` 的 Phase 0.5 完成摘要。

2. 再阅读既有设计文档，后续开发必须沿着这些文档定义的架构边界推进：

   ```text
   docs/design/framework.md
   docs/design/module_responsibility.md
   docs/design/file_system_and_shader_loading.md
   ```

   如果实现中发现设计文档与代码目标不一致，先补充或修正文档，再做代码变更。

3. 查看当前工作区：

   ```powershell
   git status -sb
   git diff --stat
   git log --oneline -n 5
   ```

4. 运行现有验证，确认本机环境仍正常：

   ```powershell
   cmake --build --preset msvc-vcpkg-local-debug
   ctest --preset msvc-vcpkg-local-debug
   build\msvc-vcpkg\Debug\ark_sandbox.exe
   ```

5. 用 RenderDoc 或 Vulkan validation 日志检查默认 cube：
   - 确认 `cube.vert.spv` 使用 set 0 / binding 0。
   - 确认 bound descriptor set 指向当前 frame slot 的 uniform buffer。
   - 确认 color attachment 和 depth attachment layout 正确。
   - 确认 depth test / depth write 生效。

6. 新建 Phase 0.6 文档：

   ```text
   docs/phase/phase06.md
   ```

   建议主题：Sampler、sampled image descriptor、texture upload、textured cube。

7. 先补 RHI 描述：
   - `SamplerDesc`
   - sampled image descriptor
   - texture descriptor / write descriptor
   - shader stage visibility

8. 再补 Vulkan 实现：
   - `VulkanSampler`
   - descriptor set layout sampled image mapping
   - descriptor update for sampled image + sampler
   - texture upload staging path

9. 新增 textured cube shader 和 smoke：
   - `shaders/textured_cube.vert.hlsl`
   - `shaders/textured_cube.frag.hlsl`
   - CMake shader compile entry
   - shader assets smoke

10. 最后改 CubePass 或新增 TexturedCubePass：
   - 先使用程序生成 checkerboard texture，避免一开始引入图片加载风险。
   - 验证 textured cube 后，再接 `TextureLoader`。

11. 每完成一个小闭环就运行：

    ```powershell
    cmake --build --preset msvc-vcpkg-local-debug
    ctest --preset msvc-vcpkg-local-debug
    ```

    并至少做一次 sandbox smoke。

## 9. 推荐给下一个 Codex 会话的启动提示词

```text
请先阅读 docs/codex_handoff.md，理解 ARKRenderer 当前 Phase 0.5 的完成状态、已知风险和后续建议。

然后阅读既有设计文档，后续开发必须遵循这些文档中的模块职责和架构边界：

docs/design/framework.md
docs/design/module_responsibility.md
docs/design/file_system_and_shader_loading.md

你需要以资深图形工程师的标准继续推进，重点关注 Vulkan/RHI 边界、资源生命周期、同步、descriptor、pipeline 和后续可维护性。

然后执行并查看：

git status -sb
git diff --stat
git log --oneline -n 5

不要重复已经完成的 Phase 0.5 工作。当前默认渲染路径已经是 Vulkan Dynamic Rendering + CubePass + uniform buffer + descriptor set + indexed draw + depth attachment。

接下来请优先从 Phase 0.6 开始，围绕 texture sampling 继续推进：先写 docs/phase/phase06.md 设计方案，再实现 Sampler、sampled image descriptor、texture upload 和 textured cube。

不要大规模重构无关代码，不要引入过重架构。如果实现方向与既有设计文档冲突，先说明并更新设计文档，再修改代码。新增代码保持现有风格：左大括号不换行，namespace 内缩进，日志输出用英文，必要注释用简洁中文。不确定的地方写 TODO 或记录到文档，不要假装完成。
```

## 10. 重要约束

- 后续开发必须遵循既有设计文档框架，尤其是：
  - `docs/design/framework.md`
  - `docs/design/module_responsibility.md`
  - `docs/design/file_system_and_shader_loading.md`
- 如果代码实现需要偏离设计文档，必须先补充或修正文档，并说明原因。
- 继续开发时应以资深图形工程师视角审查设计，重点关注 Vulkan 资源生命周期、同步、descriptor、pipeline、barrier 和后续可维护性。
- 项目使用 Vulkan 后端。
- 项目使用 C++20。
- 依赖管理使用 CMake + vcpkg manifest mode。
- 渲染路径使用 Vulkan Dynamic Rendering。
- 不使用传统 `VkRenderPass` / `VkFramebuffer`，除非已有代码必须兼容。
- Renderer / app 层不能包含 Vulkan 头文件。
- RHI 公共层不能暴露 `Vk*` 类型。
- `RenderDevice` 负责创建对象和设备能力；`DeviceContext` 负责命令录制、绑定、更新和提交；`SwapChain` 负责窗口 backbuffer、depth buffer、acquire/present/resize。
- 保持代码结构简洁，优先完成当前阶段目标。
- 不要过早引入过重架构，ResourceManager / RenderGraph / bindless 等按阶段落地。
- 新增代码需要有必要中文注释，但不要写教程式长注释。
- 运行时日志字符串使用英文，避免中文乱码。
- 不确定的地方写 TODO 或记录到文档，不要假装完成。
- 保持当前代码风格：左大括号不换行，namespace 内多一个缩进。
- 不要随意删除或重写用户未要求修改的文件。
