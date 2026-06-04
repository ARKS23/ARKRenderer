#pragma once

namespace ark::rhi {
    enum class ResourceState {
        Undefined,
        Present,
        RenderTarget,
        DepthStencilWrite,
        DepthStencilRead,
        ShaderResource,
        UnorderedAccess,
        CopySrc,
        CopyDst,
        VertexBuffer,
        IndexBuffer,
        ConstantBuffer,
        IndirectArgument,
    };

    struct ResourceBarrier {
        ResourceState before = ResourceState::Undefined;
        ResourceState after = ResourceState::Undefined;
    };
} // namespace ark::rhi
