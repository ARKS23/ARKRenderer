#pragma once

#include "renderer/core/Bounds.h"

#include <array>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

namespace ark {
    enum class FrustumPlaneId : std::size_t {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
    };

    // 平面方程：Ax + By + Cz + D = 0，其中 (A, B, C) 为平面法线，D 为平面到原点的距离。
    struct FrustumPlane {
        glm::vec3 normal{0.0f};
        float distance = 0.0f;

        bool isValid() const;
        float signedDistance(const glm::vec3& point) const; // 点到平面的有符号距离，正数在法线方向，负数在反法线方向，0 在平面上。
    };

    // 视锥体结构，包含 6 个裁剪平面
    class Frustum {
    public:
        static constexpr std::size_t PlaneCount = 6;

        // 从 world->clip 的 view-projection 矩阵提取 CPU 可见性测试用的 6 个裁剪平面。
        static Frustum fromViewProjection(const glm::mat4& viewProjection);

        const std::array<FrustumPlane, PlaneCount>& planes() const {
            return m_Planes;
        }

        const FrustumPlane& plane(FrustumPlaneId id) const {
            return m_Planes[static_cast<std::size_t>(id)];
        }

        // AABB 与视锥相交即可视为可见；invalid bounds 保守返回 true，避免误剔除资源异常对象。
        bool intersects(const Bounds3& bounds) const;

        // AABB 完全位于视锥内才返回 true；invalid bounds 不代表可靠体积，因此返回 false。
        bool contains(const Bounds3& bounds) const;

    private:
        std::array<FrustumPlane, PlaneCount> m_Planes{};
    };
} // namespace ark

