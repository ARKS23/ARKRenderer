#pragma once

#include "core/Memory.h"
#include "rhi/Buffer.h"
#include "rhi/Sampler.h"
#include "rhi/Texture.h"
#include "rhi/TextureView.h"

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

        void deferReleaseTexture(Scope<Texture> texture) {
            if (!texture) {
                return;
            }

            m_Textures.push_back(std::move(texture));
        }

        void deferReleaseTextureView(Scope<TextureView> textureView) {
            if (!textureView) {
                return;
            }

            m_TextureViews.push_back(std::move(textureView));
        }

        void deferReleaseSampler(Scope<Sampler> sampler) {
            if (!sampler) {
                return;
            }

            m_Samplers.push_back(std::move(sampler));
        }

        void flush() {
            // frame fence signal 后再清空，确保 queued GPU object 不早于 GPU 使用结束析构。
            m_Buffers.clear();
            m_TextureViews.clear();
            m_Samplers.clear();
            m_Textures.clear();
        }

        bool empty() const {
            return m_Buffers.empty() && m_TextureViews.empty() && m_Samplers.empty() && m_Textures.empty();
        }

    private:
        std::vector<Scope<Buffer>> m_Buffers;
        std::vector<Scope<TextureView>> m_TextureViews;
        std::vector<Scope<Sampler>> m_Samplers;
        std::vector<Scope<Texture>> m_Textures;
    };
} // namespace ark::rhi::vulkan
