#include "rhi/RenderBackend.h"

#include <stdexcept>
#include <utility>

namespace ark::rhi {
    RenderBackend::RenderBackend(Scope<RenderDevice> device, Scope<SwapChain> swapChain)
        : m_Device(std::move(device)), m_SwapChain(std::move(swapChain)) {
        if (!m_Device) {
            throw std::invalid_argument("RenderBackend requires a valid RenderDevice");
        }
    }

    RenderBackend::~RenderBackend() {
        if (m_Device) {
            m_Device->waitIdle();
        }
    }

    RenderDevice& RenderBackend::device() {
        return *m_Device;
    }

    const RenderDevice& RenderBackend::device() const {
        return *m_Device;
    }

    SwapChain* RenderBackend::swapChain() {
        return m_SwapChain.get();
    }

    const SwapChain* RenderBackend::swapChain() const {
        return m_SwapChain.get();
    }

    void RenderBackend::setSwapChain(Scope<SwapChain> swapChain) {
        m_SwapChain = std::move(swapChain);
    }

    Scope<RenderBackend> createRenderBackend(const RenderBackendDesc& desc) {
        Scope<RenderDevice> device = createRenderDevice(desc.device);

        Scope<SwapChain> swapChain;
        if (isValidExtent(desc.swapChain.extent)) {
            SwapChainCreateInfo swapChainCreateInfo{};
            swapChainCreateInfo.desc = desc.swapChain;
            swapChainCreateInfo.device = device.get();
            swapChain = createSwapChain(swapChainCreateInfo);
        }

        return makeScope<RenderBackend>(std::move(device), std::move(swapChain));
    }
} // namespace ark::rhi
