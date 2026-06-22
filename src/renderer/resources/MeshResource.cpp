#include "renderer/resources/MeshResource.h"

#include "core/Log.h"
#include "rhi/RenderDevice.h"

#include <limits>

namespace ark {
    namespace {
        // Mesh primitive 的 local bounds 只依赖 CPU 顶点 position，不需要等 GPU buffer 上传完成。
        Bounds3 computeLocalBounds(const asset::MeshPrimitiveData& mesh) {
            Bounds3 bounds = makeInvalidBounds();
            for (const asset::MeshVertex& vertex : mesh.vertices) {
                expandBounds(bounds, glm::vec3{vertex.position[0], vertex.position[1], vertex.position[2]});
            }

            return bounds;
        }
    } // namespace

    bool MeshResource::create(rhi::RenderDevice& device, const asset::MeshPrimitiveData& mesh) {
        if (mesh.empty()) {
            ARK_ERROR("MeshResource requires non-empty mesh data");
            return false;
        }

        if (mesh.indices.size() > std::numeric_limits<u32>::max()) {
            ARK_ERROR("MeshResource index count exceeds u32 range");
            return false;
        }

        const u64 vertexByteSize = mesh.vertexByteSize();
        const u64 indexByteSize = mesh.indexByteSize();
        if (vertexByteSize == 0 || indexByteSize == 0) {
            ARK_ERROR("MeshResource requires non-empty vertex and index data");
            return false;
        }

        m_Uploaded = false;
        m_LocalBounds = computeLocalBounds(mesh);
        m_IndexCount = static_cast<u32>(mesh.indices.size());

        rhi::BufferDesc vertexBufferDesc{};
        vertexBufferDesc.debugName = mesh.debugName.empty() ? "MeshVertexBuffer" : mesh.debugName + ".VertexBuffer";
        vertexBufferDesc.size = vertexByteSize;
        vertexBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
        vertexBufferDesc.memoryUsage = rhi::MemoryUsage::GpuOnly;
        m_VertexBuffer = device.createBuffer(vertexBufferDesc);

        rhi::BufferDesc indexBufferDesc{};
        indexBufferDesc.debugName = mesh.debugName.empty() ? "MeshIndexBuffer" : mesh.debugName + ".IndexBuffer";
        indexBufferDesc.size = indexByteSize;
        indexBufferDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
        indexBufferDesc.memoryUsage = rhi::MemoryUsage::GpuOnly;
        m_IndexBuffer = device.createBuffer(indexBufferDesc);

        // CPU mesh 数据先进入 staging，prepare() 阶段再 copy 到 GPU-only buffer。
        rhi::BufferDesc vertexStagingDesc{};
        vertexStagingDesc.debugName =
            mesh.debugName.empty() ? "MeshVertexStagingBuffer" : mesh.debugName + ".VertexStagingBuffer";
        vertexStagingDesc.size = vertexByteSize;
        vertexStagingDesc.usage = rhi::BufferUsage::TransferSrc;
        vertexStagingDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        vertexStagingDesc.initialData = mesh.vertices.data();
        m_VertexStagingBuffer = device.createBuffer(vertexStagingDesc);

        rhi::BufferDesc indexStagingDesc{};
        indexStagingDesc.debugName =
            mesh.debugName.empty() ? "MeshIndexStagingBuffer" : mesh.debugName + ".IndexStagingBuffer";
        indexStagingDesc.size = indexByteSize;
        indexStagingDesc.usage = rhi::BufferUsage::TransferSrc;
        indexStagingDesc.memoryUsage = rhi::MemoryUsage::CpuToGpu;
        indexStagingDesc.initialData = mesh.indices.data();
        m_IndexStagingBuffer = device.createBuffer(indexStagingDesc);

        if (!m_VertexBuffer || !m_IndexBuffer || !m_VertexStagingBuffer || !m_IndexStagingBuffer) {
            ARK_ERROR("MeshResource failed to create mesh buffers");
            return false;
        }

        return true;
    }

    bool MeshResource::upload(rhi::DeviceContext& context) {
        if (m_Uploaded) {
            return true;
        }

        if (!m_VertexBuffer || !m_IndexBuffer || !m_VertexStagingBuffer || !m_IndexStagingBuffer) {
            ARK_ERROR("MeshResource requires buffers before upload");
            return false;
        }

        rhi::BufferUploadDesc vertexUploadDesc{};
        vertexUploadDesc.sourceBuffer = m_VertexStagingBuffer.get();
        vertexUploadDesc.destinationBuffer = m_VertexBuffer.get();
        vertexUploadDesc.size = m_VertexBuffer->getDesc().size;

        rhi::BufferUploadDesc indexUploadDesc{};
        indexUploadDesc.sourceBuffer = m_IndexStagingBuffer.get();
        indexUploadDesc.destinationBuffer = m_IndexBuffer.get();
        indexUploadDesc.size = m_IndexBuffer->getDesc().size;

        const bool vertexUploaded = context.uploadBufferData(vertexUploadDesc);
        const bool indexUploaded = context.uploadBufferData(indexUploadDesc);
        if (!vertexUploaded || !indexUploaded) {
            return false;
        }

        // upload 命令已记录，staging buffer 交给当前 frame fence 对齐的 deferred deletion。
        if (!context.deferReleaseBuffer(m_VertexStagingBuffer) || !context.deferReleaseBuffer(m_IndexStagingBuffer)) {
            ARK_ERROR("MeshResource failed to defer mesh staging buffers");
            return false;
        }

        m_Uploaded = true;
        return true;
    }

    bool MeshResource::releaseDeferred(rhi::DeviceContext& context) {
        // 运行期卸载时，copy staging 和 draw buffer 都必须延迟到 frame fence 后析构。
        if (m_VertexStagingBuffer && !context.deferReleaseBuffer(m_VertexStagingBuffer)) {
            ARK_ERROR("MeshResource failed to defer vertex staging buffer");
            return false;
        }

        if (m_IndexStagingBuffer && !context.deferReleaseBuffer(m_IndexStagingBuffer)) {
            ARK_ERROR("MeshResource failed to defer index staging buffer");
            return false;
        }

        if (m_VertexBuffer && !context.deferReleaseBuffer(m_VertexBuffer)) {
            ARK_ERROR("MeshResource failed to defer vertex buffer");
            return false;
        }

        if (m_IndexBuffer && !context.deferReleaseBuffer(m_IndexBuffer)) {
            ARK_ERROR("MeshResource failed to defer index buffer");
            return false;
        }

        m_IndexCount = 0;
        m_Uploaded = false;
        m_LocalBounds = makeInvalidBounds();
        return true;
    }

    void MeshResource::resetImmediate() {
        // 只用于 shutdown / GPU idle 或尚未提交使用的路径。
        m_VertexStagingBuffer.reset();
        m_IndexStagingBuffer.reset();
        m_VertexBuffer.reset();
        m_IndexBuffer.reset();
        m_IndexCount = 0;
        m_Uploaded = false;
        m_LocalBounds = makeInvalidBounds();
    }

    void MeshResource::bind(rhi::DeviceContext& context) const {
        if (!isReady()) {
            ARK_ERROR("MeshResource must be uploaded before binding");
            return;
        }

        context.setVertexBuffer(0, *m_VertexBuffer);
        context.setIndexBuffer(*m_IndexBuffer, rhi::IndexType::UInt32);
    }

    rhi::DrawIndexedDesc MeshResource::makeDrawIndexedDesc() const {
        rhi::DrawIndexedDesc desc{};
        desc.indexCount = m_IndexCount;
        return desc;
    }
} // namespace ark
