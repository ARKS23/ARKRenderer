#pragma once

#include "core/Memory.h"
#include "rhi/Buffer.h"

#include <utility>
#include <vector>

namespace ark::rhi::vulkan {
    class VulkanDeletionQueue {
    public:
        void deferReleaseBuffer(Scope<Buffer> buffer) {
            if (!buffer) {
                return;
            }

            m_Buffers.push_back(std::move(buffer));
        }

        void flush() {
            // frame fence signal 后再清空，确保 queued buffer 不会早于 GPU 使用结束析构。
            m_Buffers.clear();
        }

        bool empty() const {
            return m_Buffers.empty();
        }

    private:
        std::vector<Scope<Buffer>> m_Buffers;
    };
} // namespace ark::rhi::vulkan
