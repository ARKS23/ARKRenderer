#pragma once

#include "renderer/RenderPass.h"

#include <vector>

namespace ark {
    class RenderGraph {
    public:
        void addPass(RenderPass* pass) {
            m_Passes.push_back(pass);
        }

        bool execute(FrameContext& frameContext) {
            for (RenderPass* pass : m_Passes) {
                if (pass && !pass->execute(frameContext)) {
                    return false;
                }
            }

            return true;
        }

    private:
        std::vector<RenderPass*> m_Passes;
    };
} // namespace ark
