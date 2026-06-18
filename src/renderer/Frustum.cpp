#include "renderer/Frustum.h"

#include <cmath>

namespace ark {
    namespace {
        FrustumPlane makePlane(float x, float y, float z, float w) {
            FrustumPlane plane{};
            plane.normal = glm::vec3{x, y, z};
            plane.distance = w;

            // 归一化平面，否则signed distance的数值会受矩阵缩放影响
            const float length = glm::length(plane.normal);
            if (std::isfinite(length) && length > 0.0f) {
                plane.normal /= length;
                plane.distance /= length;
            } else {
                plane.normal = glm::vec3{0.0f};
                plane.distance = 0.0f;
            }

            return plane;
        }

        FrustumPlane makePlaneFromRowCombination(const glm::mat4& matrix, int lhsRow, int rhsRow, float rhsSign) {
            return makePlane(matrix[0][lhsRow] + rhsSign * matrix[0][rhsRow],
                             matrix[1][lhsRow] + rhsSign * matrix[1][rhsRow],
                             matrix[2][lhsRow] + rhsSign * matrix[2][rhsRow],
                             matrix[3][lhsRow] + rhsSign * matrix[3][rhsRow]);
        }

        float projectedRadius(const Bounds3& bounds, const glm::vec3& normal) {
            const glm::vec3 halfExtent = bounds.halfExtent();
            const glm::vec3 absNormal = glm::abs(normal);
            return glm::dot(halfExtent, absNormal);
        }
    } // namespace

    bool FrustumPlane::isValid() const {
        return std::isfinite(normal.x) &&
               std::isfinite(normal.y) &&
               std::isfinite(normal.z) &&
               std::isfinite(distance) &&
               glm::length(normal) > 0.0f;
    }

    float FrustumPlane::signedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }

    Frustum Frustum::fromViewProjection(const glm::mat4& viewProjection) {
        Frustum frustum{};

        // 项目使用 Vulkan / GLM zero-to-one depth。clip 条件是：
        // -w <= x <= w, -w <= y <= w, 0 <= z <= w。
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Left)] =
            makePlaneFromRowCombination(viewProjection, 3, 0, 1.0f);
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Right)] =
            makePlaneFromRowCombination(viewProjection, 3, 0, -1.0f);
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Bottom)] =
            makePlaneFromRowCombination(viewProjection, 3, 1, 1.0f);
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Top)] =
            makePlaneFromRowCombination(viewProjection, 3, 1, -1.0f);
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Near)] =
            makePlane(viewProjection[0][2],
                      viewProjection[1][2],
                      viewProjection[2][2],
                      viewProjection[3][2]);
        frustum.m_Planes[static_cast<std::size_t>(FrustumPlaneId::Far)] =
            makePlaneFromRowCombination(viewProjection, 3, 2, -1.0f);

        return frustum;
    }

    bool Frustum::intersects(const Bounds3& bounds) const {
        if (!bounds.isValid()) {
            return true;
        }

        for (const FrustumPlane& plane : m_Planes) {
            if (!plane.isValid()) {
                return true;
            }

            const float centerDistance = plane.signedDistance(bounds.center());
            // AABB 在平面法线方向上的投影半径。centerDistance + radius < 0 代表整个盒子都在平面外侧。
            const float radius = projectedRadius(bounds, plane.normal);
            if (centerDistance + radius < 0.0f) {
                return false;
            }
        }

        return true;
    }

    bool Frustum::contains(const Bounds3& bounds) const {
        if (!bounds.isValid()) {
            return false;
        }

        for (const FrustumPlane& plane : m_Planes) {
            if (!plane.isValid()) {
                return false;
            }

            const float centerDistance = plane.signedDistance(bounds.center());
            // centerDistance - radius < 0 代表盒子有一部分越过该平面，因此不能算完全包含。
            const float radius = projectedRadius(bounds, plane.normal);
            if (centerDistance - radius < 0.0f) {
                return false;
            }
        }

        return true;
    }
} // namespace ark
