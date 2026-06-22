#pragma once

#include "app/SandboxRuntimeSettings.h"
#include "core/Memory.h"
#include "renderer/core/FrameOverlay.h"
#include "rhi/RHICommon.h"

namespace ark {
    class Window;

    class SandboxDebugUi final : public FrameOverlay {
    public:
        SandboxDebugUi(Window& window, SandboxRuntimeSettings& settings);
        ~SandboxDebugUi() override;

        SandboxDebugUi(const SandboxDebugUi&) = delete;
        SandboxDebugUi& operator=(const SandboxDebugUi&) = delete;

        void beginFrame();
        void buildPanels();
        void endFrame();

        bool wantsCaptureMouse() const;
        bool wantsCaptureKeyboard() const;

        bool isEnabled() const override;
        bool render(FrameContext& frameContext) override;

    private:
        class Impl;
        Scope<Impl> m_Impl;
    };
} // namespace ark
