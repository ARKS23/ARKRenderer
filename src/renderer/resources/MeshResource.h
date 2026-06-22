#pragma once

#include "asset/MeshData.h"
#include "core/Memory.h"
#include "core/Types.h"
#include "renderer/core/Bounds.h"
#include "rhi/Buffer.h"
#include "rhi/DeviceContext.h"
#include "rhi/RHICommon.h"

namespace ark::rhi {
    class RenderDevice;
} // namespace ark::rhi

namespace ark {
    // renderer 层 GPU mesh 资源：把 asset CPU mesh 转换为 RHI buffer，并管理首次上传状态。
    class MeshResource final {
    public:
        MeshResource() = default;

        bool create(rhi::RenderDevice& device, const asset::MeshPrimitiveData& mesh);
        bool upload(rhi::DeviceContext& context);
        bool releaseDeferred(rhi::DeviceContext& context);
        void resetImmediate();
        void bind(rhi::DeviceContext& context) const;
        rhi::DrawIndexedDesc makeDrawIndexedDesc() const;

        bool isReady() const {
            return m_Uploaded && m_VertexBuffer && m_IndexBuffer && m_IndexCount > 0;
        }

        u32 indexCount() const {
            return m_IndexCount;
        }

        const Bounds3& localBounds() const {
            return m_LocalBounds;
        }

    private:
        Scope<rhi::Buffer> m_VertexBuffer;
        Scope<rhi::Buffer> m_IndexBuffer;
        Scope<rhi::Buffer> m_VertexStagingBuffer;
        Scope<rhi::Buffer> m_IndexStagingBuffer;
        Bounds3 m_LocalBounds;
        u32 m_IndexCount = 0;
        bool m_Uploaded = false;
    };
} // namespace ark
