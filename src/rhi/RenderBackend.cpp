#include "rhi/RenderBackend.h"

#include "rhi/detail/RenderBackendFactory.h"

#include <stdexcept>
#include <utility>

namespace ark::rhi {
    RenderBackend::RenderBackend(Scope<RenderDevice> device, Scope<SwapChain> swapChain, Scope<DeviceContext> context)
        : m_Device(std::move(device)), m_SwapChain(std::move(swapChain)), m_Context(std::move(context)) {
        if (!m_Device) {
            throw std::invalid_argument("RenderBackend requires a valid RenderDevice");
        }

        if (!m_Context) {
            throw std::invalid_argument("RenderBackend requires a valid DeviceContext");
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

    DeviceContext& RenderBackend::context() {
        return *m_Context;
    }

    const DeviceContext& RenderBackend::context() const {
        return *m_Context;
    }

    SwapChain* RenderBackend::swapChain() {
        return m_SwapChain.get();
    }

    const SwapChain* RenderBackend::swapChain() const {
        return m_SwapChain.get();
    }

    SwapChain& RenderBackend::recreateSwapChain(const SwapChainDesc& desc) {
        SwapChainCreateInfo createInfo{};
        createInfo.desc = desc;
        createInfo.device = m_Device.get();

        m_Device->waitIdle();
        m_SwapChain.reset();
        m_SwapChain = detail::createSwapChain(createInfo);
        return *m_SwapChain;
    }

    Scope<RenderBackend> createRenderBackend(const RenderBackendDesc& desc) {
        Scope<RenderDevice> device = detail::createRenderDevice(desc.device);
        Scope<DeviceContext> context = detail::createDeviceContext(*device);

        Scope<SwapChain> swapChain;
        if (isValidExtent(desc.swapChain.extent)) {
            SwapChainCreateInfo swapChainCreateInfo{};
            swapChainCreateInfo.desc = desc.swapChain;
            swapChainCreateInfo.device = device.get();
            swapChain = detail::createSwapChain(swapChainCreateInfo);
        }

        return makeScope<RenderBackend>(std::move(device), std::move(swapChain), std::move(context));
    }
} // namespace ark::rhi
