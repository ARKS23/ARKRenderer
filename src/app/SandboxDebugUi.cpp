#include "app/SandboxDebugUi.h"

#include "app/Window.h"
#include "core/Log.h"
#include "renderer/FrameContext.h"
#include "renderer/RenderQueue.h"
#include "rhi/RenderDevice.h"
#include "rhi/SwapChain.h"
#include "rhi/vulkan/VulkanCommandContext.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanFrameResource.h"
#include "rhi/vulkan/VulkanImGuiBackend.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace ark {
    namespace {
        const char* toneMappingLabel(ToneMappingOperator operatorType) {
            switch (operatorType) {
            case ToneMappingOperator::Linear:
                return "Linear";
            case ToneMappingOperator::ACES:
                return "ACES";
            case ToneMappingOperator::Reinhard:
            default:
                return "Reinhard";
            }
        }

        const char* shadowFilterLabel(ShadowFilterMode filterMode) {
            switch (filterMode) {
            case ShadowFilterMode::Pcf3x3:
                return "PCF 3x3";
            case ShadowFilterMode::Pcf5x5:
                return "PCF 5x5";
            case ShadowFilterMode::Hard:
            default:
                return "Hard";
            }
        }

        bool comboToneMapping(ToneMappingOperator& operatorType) {
            const char* items[] = {"Reinhard", "Linear", "ACES"};
            int current = static_cast<int>(operatorType);
            if (!ImGui::Combo("Operator", &current, items, IM_ARRAYSIZE(items))) {
                return false;
            }

            operatorType = static_cast<ToneMappingOperator>(std::clamp(current, 0, 2));
            return true;
        }

        bool comboShadowFilter(ShadowFilterMode& filterMode) {
            const char* items[] = {"Hard", "PCF 3x3", "PCF 5x5"};
            int current = static_cast<int>(filterMode);
            if (!ImGui::Combo("Filter", &current, items, IM_ARRAYSIZE(items))) {
                return false;
            }

            filterMode = static_cast<ShadowFilterMode>(std::clamp(current, 0, 2));
            return true;
        }

        int cascadeCountToComboIndex(u32 cascadeCount) {
            if (cascadeCount <= 1u) {
                return 0;
            }
            if (cascadeCount <= 2u) {
                return 1;
            }
            return 2;
        }

        u32 cascadeCountFromComboIndex(int index) {
            constexpr std::array<u32, 3> CascadeCounts{1u, 2u, MaxShadowCascadeCount};
            return CascadeCounts[static_cast<usize>(std::clamp(index, 0, 2))];
        }

        bool comboCascadeCount(u32& cascadeCount) {
            const char* items[] = {"1", "2", "4"};
            int current = cascadeCountToComboIndex(cascadeCount);
            if (!ImGui::Combo("Cascade Count", &current, items, IM_ARRAYSIZE(items))) {
                return false;
            }

            cascadeCount = cascadeCountFromComboIndex(current);
            return true;
        }

        bool comboCameraMode(SandboxCameraMode& mode) {
            const char* items[] = {"Orbit", "First Person"};
            int current = mode == SandboxCameraMode::FirstPerson ? 1 : 0;
            if (!ImGui::Combo("Mode##Camera", &current, items, IM_ARRAYSIZE(items))) {
                return false;
            }

            mode = current == 1 ? SandboxCameraMode::FirstPerson : SandboxCameraMode::Orbit;
            return true;
        }
    } // namespace

    class SandboxDebugUi::Impl final {
    public:
        Impl(Window& window, SandboxRuntimeSettings& settings)
            : m_Window(window), m_Settings(settings) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;

            ImGui::StyleColorsDark();
        }

        ~Impl() {
            m_Backend.shutdown();
            ImGui::DestroyContext();
        }

        void beginFrame() {
            m_FrameBegun = false;
            m_DrawDataReady = false;

            if (!m_Settings.uiVisible || !m_Backend.isInitialized()) {
                return;
            }

            m_Backend.newFrame();
            ImGui::NewFrame();
            m_FrameBegun = true;
        }

        void buildPanels() {
            if (!m_FrameBegun) {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2{360.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("ARK Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::End();
                return;
            }

            drawCameraPanel();
            drawToneMappingPanel();
            drawBloomPanel();
            drawShadowPanel();
            drawVisibilityPanel();
            drawDiagnosticsPanel();

            ImGui::End();
        }

        void endFrame() {
            if (!m_FrameBegun) {
                return;
            }

            ImGui::Render();
            m_DrawDataReady = true;
            m_FrameBegun = false;
        }

        bool wantsCaptureMouse() const {
            return m_Settings.uiVisible && m_Backend.isInitialized() && ImGui::GetIO().WantCaptureMouse;
        }

        bool wantsCaptureKeyboard() const {
            return m_Settings.uiVisible && m_Backend.isInitialized() && ImGui::GetIO().WantCaptureKeyboard;
        }

        bool isVisible() const {
            return m_Settings.uiVisible;
        }

        bool render(FrameContext& frameContext) {
            captureVisibilityDiagnostics(frameContext);

            if (!m_Settings.uiVisible) {
                return true;
            }

            if (!ensureBackendInitialized(frameContext)) {
                return false;
            }

            if (!m_DrawDataReady) {
                return true;
            }

            auto* commandContext =
                dynamic_cast<rhi::vulkan::VulkanCommandContext*>(frameContext.context);
            auto* frameResource =
                dynamic_cast<rhi::vulkan::VulkanFrameResource*>(frameContext.frameResource);
            if (!commandContext || !frameResource) {
                ARK_ERROR("SandboxDebugUi requires Vulkan frame context");
                return false;
            }

            return m_Backend.renderDrawData(*commandContext, *frameResource, ImGui::GetDrawData());
        }

    private:
        bool ensureBackendInitialized(FrameContext& frameContext) {
            if (m_Backend.isInitialized()) {
                return true;
            }

            auto* device = dynamic_cast<rhi::vulkan::VulkanDevice*>(frameContext.device);
            if (!device || !frameContext.swapChain) {
                ARK_ERROR("SandboxDebugUi requires Vulkan device and swapchain");
                return false;
            }

            const rhi::NativeWindowHandle nativeWindow = m_Window.getNativeWindowHandle();
            if (nativeWindow.type != rhi::NativeWindowType::GLFW || !nativeWindow.handle) {
                ARK_ERROR("SandboxDebugUi requires a GLFW native window");
                return false;
            }

            rhi::vulkan::VulkanImGuiBackendDesc desc{};
            desc.glfwWindow = nativeWindow.handle;
            desc.colorFormat = frameContext.swapChain->getDesc().colorFormat;
            desc.imageCount = frameContext.swapChain->getBackBufferCount();
            desc.installGlfwCallbacks = true;

            return m_Backend.initialize(*device, desc);
        }

        void drawCameraPanel() {
            if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            comboCameraMode(m_Settings.cameraMode);
            ImGui::SliderFloat("Move Speed", &m_Settings.cameraMoveSpeed, 0.1f, 64.0f, "%.2f");
            ImGui::SliderFloat("Fast Multiplier",
                               &m_Settings.cameraFastMoveMultiplier,
                               1.0f,
                               16.0f,
                               "%.1f");
            ImGui::SliderFloat("Mouse Sensitivity",
                               &m_Settings.cameraMouseSensitivity,
                               0.0005f,
                               0.02f,
                               "%.4f");
        }

        void drawToneMappingPanel() {
            if (!ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ToneMappingSettings& toneMapping = m_Settings.view.toneMapping;
            comboToneMapping(toneMapping.operatorType);
            ImGui::SliderFloat("Exposure", &toneMapping.exposure, 0.05f, 8.0f, "%.2f");
            ImGui::SliderFloat("Gamma", &toneMapping.outputGamma, 1.0f, 3.0f, "%.2f");
        }

        void drawBloomPanel() {
            if (!ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            BloomSettings& bloom = m_Settings.view.postProcessing.bloom;
            ImGui::Checkbox("Enabled##Bloom", &bloom.enabled);
            ImGui::SliderFloat("Intensity", &bloom.intensity, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Scatter", &bloom.scatter, 0.05f, 0.95f, "%.2f");
            ImGui::SliderFloat("Threshold", &bloom.threshold, 0.0f, 8.0f, "%.2f");
            ImGui::SliderFloat("Soft Knee", &bloom.softKnee, 0.0f, 1.0f, "%.2f");
            int maxMipCount = static_cast<int>(bloom.maxMipCount);
            if (ImGui::SliderInt("Max Mips", &maxMipCount, 1, 10)) {
                bloom.maxMipCount = static_cast<u32>(maxMipCount);
            }
        }

        void drawShadowPanel() {
            if (!ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            ShadowSettings& shadows = m_Settings.view.shadows;
            ImGui::Checkbox("Enabled##Shadow", &shadows.enabled);
            ImGui::SliderFloat("Strength", &shadows.strength, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Bias", &shadows.bias, 0.0f, 0.02f, "%.5f");
            int mapExtent = static_cast<int>(shadows.mapExtent);
            if (ImGui::SliderInt("Map Extent", &mapExtent, 128, 4096)) {
                shadows.mapExtent = static_cast<u32>(mapExtent);
            }
            ImGui::Checkbox("Fit Bounds", &shadows.fitSceneBounds);
            ImGui::Checkbox("Stabilize", &shadows.stabilizeProjection);
            comboShadowFilter(shadows.filterMode);
            ImGui::SliderFloat("Filter Radius", &shadows.filterRadiusTexels, 0.0f, 8.0f, "%.2f");
            ImGui::SliderFloat("Manual Bounds", &shadows.orthographicHalfExtent, 1.0f, 128.0f, "%.1f");

            drawCascadeShadowPanel(shadows);
        }

        void drawCascadeShadowPanel(ShadowSettings& shadows) {
            CascadeShadowSettings& cascades = shadows.cascades;

            ImGui::Separator();
            ImGui::TextUnformatted("Cascaded Shadow Maps");
            ImGui::Checkbox("Enabled##CSM", &cascades.enabled);
            comboCascadeCount(cascades.cascadeCount);
            ImGui::SliderFloat("Split Lambda", &cascades.splitLambda, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Shadow Distance", &cascades.maxDistance, 1.0f, 512.0f, "%.1f");
            int cascadeExtent = static_cast<int>(cascades.cascadeExtent);
            if (ImGui::SliderInt("Cascade Extent", &cascadeExtent, 128, 4096)) {
                cascades.cascadeExtent = static_cast<u32>(cascadeExtent);
            }
            ImGui::Checkbox("Stabilize##CSM", &cascades.stabilize);

            if (cascades.enabled && !shadows.enabled) {
                ImGui::TextDisabled("CSM waits for Shadow Enabled");
            }

            drawCascadeDiagnostics();
        }

        void drawVisibilityPanel() {
            if (!ImGui::CollapsingHeader("Visibility", ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }

            VisibilitySettings& visibility = m_Settings.view.visibility;
            ImGui::Checkbox("Frustum Culling", &visibility.enableFrustumCulling);
            ImGui::Text("Forward: %zu / %zu",
                        m_LastForwardStats.visibleItems,
                        m_LastForwardStats.totalItems);
            ImGui::Text("Culled: %zu", m_LastForwardStats.culledItems);
            ImGui::Text("Invalid Bounds: %zu", m_LastForwardStats.invalidBoundsItems);
            ImGui::Text("Shadow Casters: %zu", m_LastShadowStats.visibleItems);
        }

        void drawDiagnosticsPanel() {
            if (!ImGui::CollapsingHeader("Diagnostics")) {
                return;
            }

            const ToneMappingSettings& toneMapping = m_Settings.view.toneMapping;
            const BloomSettings& bloom = m_Settings.view.postProcessing.bloom;
            const ShadowSettings& shadows = m_Settings.view.shadows;

            ImGui::Text("Tone: %s", toneMappingLabel(toneMapping.operatorType));
            ImGui::Text("Bloom: %s", bloom.enabled ? "On" : "Off");
            ImGui::Text("Shadow: %s / %s",
                        shadows.enabled ? "On" : "Off",
                        shadowFilterLabel(shadows.filterMode));
            ImGui::Text("Shadow Map Extent: %u", shadows.mapExtent);
            ImGui::Text("CSM: %s, requested=%u, distance=%.1f, extent=%u",
                        shadows.cascades.enabled ? "On" : "Off",
                        shadows.cascades.cascadeCount,
                        shadows.cascades.maxDistance,
                        shadows.cascades.cascadeExtent);
            drawCascadeDiagnostics();
            ImGui::Text("Culling: %s",
                        m_Settings.view.visibility.enableFrustumCulling ? "On" : "Off");
        }

        void drawCascadeDiagnostics() {
            if (!m_LastCascadeShadows.isEnabled()) {
                ImGui::TextDisabled("CSM Frame: inactive");
                return;
            }

            ImGui::Text("CSM Frame: cascades=%u, extent=%u",
                        m_LastCascadeShadows.cascadeCount,
                        m_LastCascadeShadows.cascadeExtent);
            for (u32 index = 0; index < m_LastCascadeShadows.cascadeCount; ++index) {
                const ShadowCascade& cascade = m_LastCascadeShadows.cascades[index];
                ImGui::Text("  C%u: %.2f -> %.2f",
                            index,
                            cascade.nearDistance,
                            cascade.farDistance);
            }
        }

        void captureVisibilityDiagnostics(const FrameContext& frameContext) {
            const RenderQueue* shadowQueue = frameContext.queue;
            const RenderQueue* forwardQueue =
                frameContext.forwardQueue ? frameContext.forwardQueue : frameContext.queue;

            m_LastForwardStats = forwardQueue ? forwardQueue->stats() : RenderQueueStats{};
            m_LastShadowStats = shadowQueue ? shadowQueue->stats() : RenderQueueStats{};
            // UI 面板在 render 前构建，因此这里缓存的是“上一帧 ShadowPass 实际生成”的 CSM 数据。
            m_LastCascadeShadows = frameContext.cascadeShadows;
        }

        Window& m_Window;
        SandboxRuntimeSettings& m_Settings;
        rhi::vulkan::VulkanImGuiBackend m_Backend;
        RenderQueueStats m_LastForwardStats{};
        RenderQueueStats m_LastShadowStats{};
        CascadeShadowFrameData m_LastCascadeShadows{};
        bool m_FrameBegun = false;
        bool m_DrawDataReady = false;
    };

    SandboxDebugUi::SandboxDebugUi(Window& window, SandboxRuntimeSettings& settings)
        : m_Impl(makeScope<Impl>(window, settings)) {
    }

    SandboxDebugUi::~SandboxDebugUi() = default;

    void SandboxDebugUi::beginFrame() {
        m_Impl->beginFrame();
    }

    void SandboxDebugUi::buildPanels() {
        m_Impl->buildPanels();
    }

    void SandboxDebugUi::endFrame() {
        m_Impl->endFrame();
    }

    bool SandboxDebugUi::wantsCaptureMouse() const {
        return m_Impl->wantsCaptureMouse();
    }

    bool SandboxDebugUi::wantsCaptureKeyboard() const {
        return m_Impl->wantsCaptureKeyboard();
    }

    bool SandboxDebugUi::isEnabled() const {
        return m_Impl && m_Impl->isVisible();
    }

    bool SandboxDebugUi::render(FrameContext& frameContext) {
        return m_Impl->render(frameContext);
    }
} // namespace ark
