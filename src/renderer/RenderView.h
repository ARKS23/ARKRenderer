#pragma once

#include "rhi/RHICommon.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace ark {
    class RenderView {
    public:
        void setMatrices(const glm::mat4& view, const glm::mat4& projection) {
            m_View = view;
            m_Projection = projection;
        }

        void setDefaultPerspective(rhi::Extent2D extent) {
            const float aspect =
                extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);

            m_View = glm::lookAt(glm::vec3{0.0f, 0.0f, -4.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            m_Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
            m_Projection[1][1] *= -1.0f;
        }

        const glm::mat4& viewMatrix() const {
            return m_View;
        }

        const glm::mat4& projectionMatrix() const {
            return m_Projection;
        }

    private:
        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
    };
} // namespace ark
