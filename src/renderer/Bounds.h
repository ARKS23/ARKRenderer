#pragma once

#include <algorithm>
#include <array>
#include <limits>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

namespace ark {
    // AABB包围盒结构
    struct Bounds3 {
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{std::numeric_limits<float>::lowest()};
        bool valid = false;

        bool isValid() const {
            return valid;
        }

        glm::vec3 center() const {
            return valid ? (min + max) * 0.5f : glm::vec3{0.0f};
        }

        glm::vec3 extent() const {
            return valid ? max - min : glm::vec3{0.0f};
        }

        glm::vec3 halfExtent() const {
            return extent() * 0.5f;
        }
    };

    inline Bounds3 makeInvalidBounds() {
        return {};
    }

    inline Bounds3 makeBoundsFromPoint(const glm::vec3& point) {
        Bounds3 bounds{};
        bounds.min = point;
        bounds.max = point;
        bounds.valid = true;
        return bounds;
    }

    // 一个点并入包围盒
    inline void expandBounds(Bounds3& bounds, const glm::vec3& point) {
        if (!bounds.valid) {
            bounds = makeBoundsFromPoint(point);
            return;
        }

        bounds.min = glm::vec3{
            std::min(bounds.min.x, point.x),
            std::min(bounds.min.y, point.y),
            std::min(bounds.min.z, point.z),
        };
        bounds.max = glm::vec3{
            std::max(bounds.max.x, point.x),
            std::max(bounds.max.y, point.y),
            std::max(bounds.max.z, point.z),
        };
    }

    // 另一个包围盒并入包围盒
    inline void mergeBounds(Bounds3& bounds, const Bounds3& other) {
        if (!other.valid) {
            return;
        }

        expandBounds(bounds, other.min);
        expandBounds(bounds, other.max);
    }

    // 返回AABB的8个角点
    inline std::array<glm::vec3, 8> boundsCorners(const Bounds3& bounds) {
        if (!bounds.valid) {
            return {};
        }

        return {
            glm::vec3{bounds.min.x, bounds.min.y, bounds.min.z},
            glm::vec3{bounds.max.x, bounds.min.y, bounds.min.z},
            glm::vec3{bounds.min.x, bounds.max.y, bounds.min.z},
            glm::vec3{bounds.max.x, bounds.max.y, bounds.min.z},
            glm::vec3{bounds.min.x, bounds.min.y, bounds.max.z},
            glm::vec3{bounds.max.x, bounds.min.y, bounds.max.z},
            glm::vec3{bounds.min.x, bounds.max.y, bounds.max.z},
            glm::vec3{bounds.max.x, bounds.max.y, bounds.max.z},
        };
    }

    // 将包围盒变换到世界空间
    inline Bounds3 transformBounds(const Bounds3& bounds, const glm::mat4& transform) {
        if (!bounds.valid) {
            return makeInvalidBounds();
        }

        // AABB 经过旋转、非等比缩放或负缩放后，不能只变换 min/max 两点；
        // 必须变换 8 个角点后重新合并，才能得到正确的 world-space AABB。
        Bounds3 transformed = makeInvalidBounds();
        for (const glm::vec3& corner : boundsCorners(bounds)) {
            const glm::vec4 worldCorner = transform * glm::vec4{corner, 1.0f};
            expandBounds(transformed, glm::vec3{worldCorner});
        }

        return transformed;
    }
} // namespace ark
