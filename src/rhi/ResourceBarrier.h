#pragma once

namespace ark::rhi {
    class Texture;

    enum class ResourceState {
        Undefined,          // 初始或未知状态，通常用于首次使用前的 transition。
        Present,            // swapchain backbuffer 等待呈现或已经呈现完成的状态。
        RenderTarget,       // 作为颜色渲染目标写入。
        DepthStencilWrite,  // 作为深度/模板附件写入。
        DepthStencilRead,   // 作为只读深度/模板资源读取。
        ShaderResource,     // 作为 shader 只读资源读取，例如 sampled texture
        UnorderedAccess,    // 作为 shader 可读写资源使用，例如 storage image/buffer。
        CopySrc,            // 作为拷贝源。
        CopyDst,            // 作为拷贝目标或 transfer clear 目标。
        VertexBuffer,       // 作为顶点缓冲读取。
        IndexBuffer,        // 作为索引缓冲读取。
        ConstantBuffer,     // 作为常量/统一缓冲读取。
        IndirectArgument,   // 作为间接绘制或间接 dispatch 参数读取。
    };

    struct ResourceBarrier {
        Texture* texture = nullptr;
        ResourceState before = ResourceState::Undefined;
        ResourceState after = ResourceState::Undefined;
    };
} // namespace ark::rhi
