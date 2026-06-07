# 文件系统与 Shader 资源加载设计

本文档用于约束 ARKRenderer 中通用文件系统能力和 shader 资源加载能力的边界。重构前 `TrianglePass` 内部直接实现了 `findShaderFile()` 和 `readSpirvFile()`，随着 `ForwardPass`、`SkyboxPass`、`BloomPass` 等 pass 增加，这类逻辑如果继续分散在 pass 中，会造成重复代码和资源路径策略不一致。

本设计的目标是把“文件怎么找、怎么读”和“shader bytecode 怎么解释”拆开，让 renderer/pass 只表达渲染意图，不负责底层文件读取细节。

## 当前问题

重构前 `TrianglePass` 内部包含两类 helper：

```text
findShaderFile()
    -> 根据 ARK_SHADER_OUTPUT_DIR、shaders/、build/msvc-vcpkg/shaders 查找 shader 文件

readSpirvFile()
    -> 打开二进制文件
    -> 检查文件大小是否能按 u32 对齐
    -> 读取为 std::vector<u32>
```

这些逻辑在三角形阶段可以接受，但后续会带来几个问题：

- 每个 pass 都可能重复写 shader 文件查找逻辑。
- shader 输出目录策略分散，后续修改资源目录时容易漏改。
- pass 同时承担了渲染逻辑和文件 IO 逻辑，职责不够干净。
- SPIR-V 的 `u32` 对齐检查属于 shader 资源语义，不应该散落在渲染 pass 中。
- 后续做 shader hot reload、资源目录配置或 pak/虚拟文件系统时，没有统一入口。

## 设计原则

核心原则：

- `core/FileSystem` 只提供通用文件能力，不理解 shader、texture、gltf 或 Vulkan。
- `asset/ShaderLoader` 负责 shader 文件路径策略和 SPIR-V bytecode 读取。
- `renderer/passes` 只向 asset 层请求 shader bytecode，然后通过 `RenderDevice` 创建 `Shader`。
- RHI 和 Vulkan 后端不读取项目资源路径，不硬编码 shader 目录。
- 日志输出文本继续使用英文，避免控制台编码问题。
- 代码注释只解释非显然设计，不写教程式长注释。

依赖方向：

```text
renderer/pass
    -> asset/ShaderLoader
        -> core/FileSystem

rhi
    -> core

rhi/vulkan
    -> rhi
    -> core
```

禁止方向：

```text
core -> asset
core -> renderer
core -> rhi
asset -> renderer
asset -> rhi
rhi/vulkan -> asset
```

## core/FileSystem 职责

`core/FileSystem` 应该是一个轻量工具层，只处理通用文件系统问题。

建议文件：

```text
src/core/
|-- FileSystem.h
`-- FileSystem.cpp
```

建议接口：

```cpp
namespace ark {
    using Path = std::filesystem::path;

    bool fileExists(const Path& path);
    std::vector<u8> readBinaryFile(const Path& path);
    Path findFirstExistingPath(std::span<const Path> candidates);
}
```

职责说明：

- `fileExists()`：封装 `std::filesystem::exists()` 和普通文件判断。
- `readBinaryFile()`：把任意文件读取为 `std::vector<u8>`。
- `findFirstExistingPath()`：从候选路径中返回第一个存在的路径；如果都不存在，返回空路径。

不建议放入 `core/FileSystem` 的内容：

- `readSpirvFile()`。
- `loadTexture()`。
- `loadGltf()`。
- shader 输出目录策略。
- Vulkan 或 RHI 类型。

原因是 `core` 应该保持最底层、最稳定。它可以被任何模块使用，但不应该知道任何上层资源格式。

## asset/ShaderLoader 职责

shader 加载属于资产层。它知道 shader 是如何组织的，也知道 SPIR-V bytecode 的基本格式要求，但不负责创建 RHI `Shader` 对象。

建议文件：

```text
src/asset/
|-- ShaderLoader.h
`-- ShaderLoader.cpp
```

建议接口：

```cpp
namespace ark::asset {
    std::vector<u32> readSpirvFile(const Path& path);
    Path findCompiledShaderFile(std::string_view fileName);
    std::vector<u32> loadCompiledShader(std::string_view fileName);
}
```

职责说明：

- `findCompiledShaderFile()`：统一处理 shader 查找路径。
- `readSpirvFile()`：读取 SPIR-V 文件，并检查大小是否按 `u32` 对齐。
- `loadCompiledShader()`：组合查找和读取，供 pass 直接使用。

当前第一版查找顺序建议：

```text
1. ARK_SHADER_OUTPUT_DIR/fileName
2. shaders/fileName
3. build/msvc-vcpkg/shaders/fileName
```

说明：

- `ARK_SHADER_OUTPUT_DIR` 由 CMake 注入，是正常构建运行路径。
- `shaders/` 用于开发时从仓库根目录直接运行时的 fallback。
- `build/msvc-vcpkg/shaders/` 用于当前本地 preset 的调试 fallback。

后续如果引入统一资源目录，可以把查找顺序调整为：

```text
1. Application/Renderer 传入的 asset root
2. 构建输出目录
3. 仓库开发目录 fallback
```

## renderer/pass 使用方式

重构后，`TrianglePass` 不再包含本地文件读取函数。

目标代码形态：

```cpp
#include "asset/ShaderLoader.h"

rhi::ShaderDesc vertexShaderDesc{};
vertexShaderDesc.debugName = "TriangleVertexShader";
vertexShaderDesc.stage = rhi::ShaderStage::Vertex;
vertexShaderDesc.bytecode = asset::loadCompiledShader("triangle.vert.spv");

if (!vertexShaderDesc.bytecode.empty()) {
    m_VertexShader = device.createShader(vertexShaderDesc);
}
```

pass 的职责变为：

- 说明自己需要哪个 shader 文件。
- 说明 shader stage。
- 把 bytecode 交给 `RenderDevice` 创建 RHI `Shader`。

pass 不再负责：

- 打开文件。
- 判断文件大小。
- 处理 fallback 路径。
- 直接使用 `std::ifstream`。

## 和 ShaderCompiler 的关系

当前已有 `asset/ShaderCompiler.h` 占位。建议区分两个概念：

```text
ShaderCompiler
    -> 负责编译 HLSL/GLSL 到 SPIR-V
    -> 可以是离线工具、CMake 构建步骤或后续运行时编译入口

ShaderLoader
    -> 负责加载已经编译好的 shader bytecode
    -> 当前主要读取 .spv 文件
```

Phase 0.4 现状是 CMake 使用 DXC 编译 HLSL 到 SPIR-V，因此运行时暂时只需要 `ShaderLoader`。

后续如果要做 shader hot reload，可以形成如下流程：

```text
ShaderCompiler
    compile source -> .spv

ShaderLoader
    read .spv -> bytecode

RenderDevice
    create Shader from bytecode
```

不要让 `ShaderLoader` 直接创建 RHI `Shader`，否则 asset 层会依赖 rhi 层，打破当前模块边界。

## 错误处理与日志

建议第一版继续使用空 vector 表达加载失败：

```cpp
std::vector<u32> bytecode = asset::loadCompiledShader("triangle.vert.spv");
if (bytecode.empty()) {
    ARK_ERROR("...");
}
```

日志要求：

- 日志文本使用英文。
- 日志包含文件路径。
- `readSpirvFile()` 应检查文件大小是否大于 0。
- `readSpirvFile()` 应检查文件大小是否能被 `sizeof(u32)` 整除。

示例日志：

```text
Failed to find compiled shader: triangle.vert.spv
Failed to read binary file: build/msvc-vcpkg/shaders/triangle.vert.spv
Invalid SPIR-V shader size: build/msvc-vcpkg/shaders/triangle.vert.spv
```

后续如果引入 `Result<T>`，可以把错误信息从日志返回给调用者，但第一版不必复杂化。

## 测试建议

当前已有 `shader_assets_smoke` 用于检查 CMake 产出的 SPIR-V 文件存在并按 `u32` 对齐。重构后建议调整或扩展该测试：

- 调用 `asset::findCompiledShaderFile("triangle.vert.spv")`。
- 调用 `asset::loadCompiledShader("triangle.vert.spv")`。
- 检查返回 bytecode 非空。
- 检查第一个 word 是 SPIR-V magic number `0x07230203`。

这样可以覆盖：

- CMake shader 输出路径是否正确。
- `ShaderLoader` 查找路径是否正确。
- SPIR-V 二进制读取是否正确。

## 实施步骤

建议按以下顺序落地：

1. 扩展 `src/core/FileSystem.h`，新增通用二进制读取和候选路径查找接口。
2. 新增 `src/core/FileSystem.cpp`。
3. 新增 `src/asset/ShaderLoader.h` 和 `src/asset/ShaderLoader.cpp`。
4. 修改 `TrianglePass.cpp`，删除本地 `findShaderFile()` 和 `readSpirvFile()`。
5. 修改 `tests/shader_assets_smoke.cpp`，使用 `ShaderLoader` 读取三角形 shader。
6. 更新 CMake，确保新增 `.cpp` 被编译进 `ark_renderer`，测试能链接到实现。
7. 构建并运行测试。

建议验证命令：

```powershell
cmake --build --preset msvc-vcpkg-local-debug
ctest --preset msvc-vcpkg-local-debug
```

## 验收标准

完成后应满足：

- `TrianglePass.cpp` 不再直接包含 `<fstream>`。
- `TrianglePass.cpp` 不再定义 `findShaderFile()` 或 `readSpirvFile()`。
- shader 查找路径只在 `asset/ShaderLoader.cpp` 中维护。
- `core/FileSystem` 不包含 shader、SPIR-V、RHI 或 Vulkan 概念。
- `asset/ShaderLoader` 不创建 RHI 对象，不依赖 renderer 或 rhi。
- `shader_assets_smoke` 能通过 `ShaderLoader` 成功读取 `triangle.vert.spv` 和 `triangle.frag.spv`。
- 现有三角形绘制路径保持不变。

## 当前实现状态

本设计已完成第一版落地：

- `src/core/FileSystem.h/.cpp` 已新增 `fileExists()`、`readBinaryFile()` 和 `findFirstExistingPath()`。
- `src/asset/ShaderLoader.h/.cpp` 已新增 `findCompiledShaderFile()`、`readSpirvFile()` 和 `loadCompiledShader()`。
- `TrianglePass` 已改为通过 `asset::loadCompiledShader()` 加载 `triangle.vert.spv` 和 `triangle.frag.spv`。
- `TrianglePass` 不再直接包含 `<fstream>`，也不再维护 pass-local shader 查找逻辑。
- `shader_assets_smoke` 已改为通过 `ShaderLoader` 读取 SPIR-V，并检查 magic number。
- 测试侧不再注入 `ARK_SHADER_OUTPUT_DIR`，shader 路径策略集中在 `ShaderLoader.cpp`。

当前仍未实现：

- 运行时 shader 编译。
- shader hot reload。
- asset root 从 `Application` 或 `RendererDesc` 传入。
- pak / zip / 虚拟文件系统。
- 统一资源 handle 和生命周期管理。

## 后续扩展

后续可以在这个基础上继续扩展：

- `AssetRoot`：由 `Application` 或 `RendererDesc` 提供资源根目录。
- shader hot reload：监听 shader 文件变化并重新加载 bytecode。
- shader cache：记录 source hash、compile options 和输出 bytecode。
- 虚拟文件系统：支持 pak、zip 或内存资源。
- 资源管理器：由 `ResourceManager` 统一管理 shader handle 和生命周期。

第一版不建议直接做完整虚拟文件系统。当前只需要一个轻量文件读取层和一个 shader bytecode 加载层，把 pass 中的重复 IO 逻辑收回来即可。
