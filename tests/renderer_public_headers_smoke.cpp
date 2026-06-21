#include "renderer/EnvironmentResource.h"
#include "renderer/MeshResource.h"
#include "renderer/ModelResource.h"
#include "renderer/PostProcessingSettings.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"
#include "renderer/Renderer.h"
#include "renderer/RendererPreset.h"
#include "renderer/RendererQuality.h"
#include "renderer/SceneResource.h"
#include "renderer/ShadowConstants.h"
#include "renderer/TextureResource.h"
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

    view.setPostProcessingSettings(postProcessing);
    view.setShadowSettings(shadows);

    if (!scene.empty() || scene.size() != 0) {
        return EXIT_FAILURE;
    }
    if (view.shadowSettings().cascades.cascadeCount != ark::MaxShadowCascadeCount) {
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
