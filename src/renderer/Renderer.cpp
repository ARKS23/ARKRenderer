#include "renderer/Renderer.h"

#include "core/Log.h"
#include "core/Memory.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "rhi/RenderBackend.h"

#include <stdexcept>

namespace ark {
    namespace {
        rhi::SwapChainDesc makeDefaultSwapChainDesc(rhi::Extent2D extent) {
            rhi::SwapChainDesc desc{};
            desc.extent = extent;
            desc.colorFormat = rhi::Format::BGRA8Unorm;
            desc.depthFormat = rhi::Format::D32Float;
            desc.imageCount = 2;
            desc.enableVSync = true;
            return desc;
        }

        class DefaultRenderer final : public Renderer {
        public:
            explicit DefaultRenderer(const RendererDesc& desc) {
                rhi::RenderBackendDesc backendDesc{};
                backendDesc.device.desc.backend = rhi::RenderBackendType::Vulkan;
                backendDesc.device.desc.enableValidation = desc.enableValidation;
                backendDesc.device.nativeWindow = desc.nativeWindow;
                backendDesc.swapChain = makeDefaultSwapChainDesc(desc.extent);

                m_Backend = rhi::createRenderBackend(backendDesc);
                if (!m_Backend->swapChain()) {
                    ARK_WARN("Renderer created without swapchain because window extent is zero");
                }

                ARK_INFO("Renderer initialized");
            }

            ~DefaultRenderer() override {
                m_Backend.reset();
                ARK_INFO("Renderer shutdown");
            }

            void render(RenderScene& scene, const RenderView& view) override {
                (void)scene;
                (void)view;

                // Phase 0.2 只验证 Vulkan 后端生命周期，真实 acquire/submit/present 放到 Phase 0.3。
            }

            void resize(unsigned width, unsigned height) override {
                rhi::Extent2D extent{static_cast<u32>(width), static_cast<u32>(height)};
                if (!rhi::isValidExtent(extent)) {
                    return;
                }

                if (m_Backend->swapChain()) {
                    m_Backend->swapChain()->resize(extent);
                    return;
                }

                rhi::SwapChainCreateInfo swapChainCreateInfo{};
                swapChainCreateInfo.device = &m_Backend->device();
                swapChainCreateInfo.desc = makeDefaultSwapChainDesc(extent);

                m_Backend->setSwapChain(rhi::createSwapChain(swapChainCreateInfo));
            }

        private:
            Scope<rhi::RenderBackend> m_Backend;
        };
    } // namespace

    Scope<Renderer> createRenderer(const RendererDesc& desc) {
        if (desc.nativeWindow.type != rhi::NativeWindowType::GLFW || !desc.nativeWindow.handle) {
            throw std::runtime_error("createRenderer requires a valid GLFW native window handle");
        }

        return makeScope<DefaultRenderer>(desc);
    }
} // namespace ark
