#pragma once

#include "renderer/RenderPass.h"

#include <vector>

namespace ark {
    class RenderGraph {
    public:
        void addPass(RenderPass* pass) {
            m_Passes.push_back(pass);
        }

        void execute(FrameContext& frameContext) {
            for (RenderPass* pass : m_Passes) {
                if (pass) {
                    pass->execute(frameContext);
                }
            }
        }

    private:
        std::vector<RenderPass*> m_Passes;
    };
} // namespace ark
