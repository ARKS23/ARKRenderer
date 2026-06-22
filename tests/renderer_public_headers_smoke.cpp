#include "renderer/resources/EnvironmentResource.h"
#include "renderer/resources/MeshResource.h"
#include "renderer/resources/ModelResource.h"
#include "renderer/settings/PostProcessingSettings.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"
#include "renderer/presets/RendererPreset.h"
#include "renderer/settings/RendererQuality.h"
#include "renderer/scene/SceneResource.h"
#include "renderer/settings/ShadowConstants.h"
#include "renderer/settings/ShadowDebugSettings.h"
#include "renderer/resources/TextureResource.h"
#include "renderer/material/MaterialResource.h"

#include <cstdlib>
#include <type_traits>

int main() {
    static_assert(std::is_default_constructible_v<ark::RenderScene>);
    static_assert(std::is_default_constructible_v<ark::RenderView>);
    static_assert(std::is_default_constructible_v<ark::RendererDesc>);
    static_assert(std::is_default_constructible_v<ark::RendererQualityDesc>);
    static_assert(std::is_default_constructible_v<ark::RendererPresetDesc>);
    static_assert(std::is_default_constructible_v<ark::SceneResourceLoadDesc>);
    static_assert(std::is_default_constructible_v<ark::ShadowDebugSettings>);
    static_assert(std::is_default_constructible_v<ark::ModelResource>);
    static_assert(std::is_default_constructible_v<ark::MeshResource>);
    static_assert(std::is_default_constructible_v<ark::TextureResource>);
    static_assert(std::is_default_constructible_v<ark::EnvironmentResource>);
    static_assert(std::is_default_constructible_v<ark::MaterialResource>);

    ark::RendererDesc rendererDesc{};
    ark::RenderScene scene{};
    ark::RenderView view{};
    ark::RendererPresetDesc presetDesc{};
    ark::RendererQualityDesc qualityDesc{};
    ark::SceneResourceLoadDesc sceneLoadDesc{};
    ark::PostProcessingSettings postProcessing{};
    ark::ShadowSettings shadows{};
    ark::ShadowDebugSettings shadowDebug{};
    shadowDebug.enabled = true;
    shadowDebug.mode = ark::ShadowDebugMode::CascadeColor;
    shadowDebug.showPreview = true;
    shadowDebug.previewCascadeIndex = ark::MaxShadowCascadeCount + 4u;

    view.setPostProcessingSettings(postProcessing);
    view.setShadowSettings(shadows);
    view.setShadowDebugSettings(shadowDebug);

    if (!scene.empty() || scene.size() != 0) {
        return EXIT_FAILURE;
    }
    if (view.shadowSettings().cascades.cascadeCount != ark::MaxShadowCascadeCount) {
        return EXIT_FAILURE;
    }
    if (!view.shadowDebugSettings().enabled ||
        view.shadowDebugSettings().mode != ark::ShadowDebugMode::CascadeColor ||
        !view.shadowDebugSettings().showPreview ||
        view.shadowDebugSettings().previewCascadeIndex != ark::MaxShadowCascadeCount - 1u) {
        return EXIT_FAILURE;
    }

    shadowDebug.enabled = false;
    shadowDebug.mode = ark::ShadowDebugMode::LightDepth;
    shadowDebug.showPreview = true;
    view.setShadowDebugSettings(shadowDebug);
    if (view.shadowDebugSettings().enabled ||
        view.shadowDebugSettings().mode != ark::ShadowDebugMode::None ||
        view.shadowDebugSettings().showPreview) {
        return EXIT_FAILURE;
    }
    if (ark::sanitizeRendererQualityDesc(qualityDesc).environmentBake.brdfLutSampleCount !=
        ark::DefaultEnvironmentBakeBrdfLutSampleCount) {
        return EXIT_FAILURE;
    }

    (void)rendererDesc;
    (void)presetDesc;
    (void)sceneLoadDesc;

    return EXIT_SUCCESS;
}
