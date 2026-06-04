#pragma once

namespace ark {
    class RenderScene;
    class RenderView;

    class Renderer {
    public:
        virtual ~Renderer() = default;

        virtual void render(RenderScene& scene, const RenderView& view) = 0;
        virtual void resize(unsigned width, unsigned height) = 0;
    };
} // namespace ark
