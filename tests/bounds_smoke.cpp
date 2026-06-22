#include "renderer/core/Bounds.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/ext/matrix_transform.hpp>

namespace {
    bool near(float lhs, float rhs, float epsilon = 0.0001f) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    bool nearVec3(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = 0.0001f) {
        return near(lhs.x, rhs.x, epsilon) && near(lhs.y, rhs.y, epsilon) && near(lhs.z, rhs.z, epsilon);
    }

    bool validateInvalidBounds() {
        const ark::Bounds3 bounds = ark::makeInvalidBounds();
        if (bounds.isValid() || !nearVec3(bounds.center(), glm::vec3{0.0f}) ||
            !nearVec3(bounds.extent(), glm::vec3{0.0f}) ||
            !nearVec3(bounds.halfExtent(), glm::vec3{0.0f})) {
            std::cerr << "Invalid bounds helper returned unexpected values\n";
            return false;
        }

        const ark::Bounds3 transformed = ark::transformBounds(bounds, glm::mat4{1.0f});
        if (transformed.isValid()) {
            std::cerr << "Invalid bounds should stay invalid after transform\n";
            return false;
        }

        return true;
    }

    bool validateExpandAndMerge() {
        ark::Bounds3 bounds = ark::makeInvalidBounds();
        ark::expandBounds(bounds, glm::vec3{-1.0f, 2.0f, 3.0f});
        ark::expandBounds(bounds, glm::vec3{4.0f, -2.0f, 5.0f});
        if (!bounds.isValid() ||
            !nearVec3(bounds.min, glm::vec3{-1.0f, -2.0f, 3.0f}) ||
            !nearVec3(bounds.max, glm::vec3{4.0f, 2.0f, 5.0f}) ||
            !nearVec3(bounds.center(), glm::vec3{1.5f, 0.0f, 4.0f}) ||
            !nearVec3(bounds.extent(), glm::vec3{5.0f, 4.0f, 2.0f})) {
            std::cerr << "Expanded bounds values are invalid\n";
            return false;
        }

        ark::Bounds3 other = ark::makeBoundsFromPoint(glm::vec3{-3.0f, 0.0f, -2.0f});
        ark::expandBounds(other, glm::vec3{-2.0f, 8.0f, -1.0f});
        ark::mergeBounds(bounds, other);
        if (!nearVec3(bounds.min, glm::vec3{-3.0f, -2.0f, -2.0f}) ||
            !nearVec3(bounds.max, glm::vec3{4.0f, 8.0f, 5.0f})) {
            std::cerr << "Merged bounds values are invalid\n";
            return false;
        }

        ark::mergeBounds(bounds, ark::makeInvalidBounds());
        if (!nearVec3(bounds.min, glm::vec3{-3.0f, -2.0f, -2.0f}) ||
            !nearVec3(bounds.max, glm::vec3{4.0f, 8.0f, 5.0f})) {
            std::cerr << "Invalid bounds merge should be a no-op\n";
            return false;
        }

        return true;
    }

    bool validateCorners() {
        ark::Bounds3 bounds = ark::makeBoundsFromPoint(glm::vec3{-1.0f, -2.0f, -3.0f});
        ark::expandBounds(bounds, glm::vec3{4.0f, 5.0f, 6.0f});

        const std::array<glm::vec3, 8> corners = ark::boundsCorners(bounds);
        if (!nearVec3(corners.front(), bounds.min) || !nearVec3(corners.back(), bounds.max)) {
            std::cerr << "Bounds corner order or values are invalid\n";
            return false;
        }

        return true;
    }

    bool validateTransform() {
        ark::Bounds3 bounds = ark::makeBoundsFromPoint(glm::vec3{-1.0f, -1.0f, -1.0f});
        ark::expandBounds(bounds, glm::vec3{1.0f, 1.0f, 1.0f});

        const glm::mat4 translated = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, -2.0f, 5.0f});
        const ark::Bounds3 moved = ark::transformBounds(bounds, translated);
        if (!moved.isValid() ||
            !nearVec3(moved.min, glm::vec3{2.0f, -3.0f, 4.0f}) ||
            !nearVec3(moved.max, glm::vec3{4.0f, -1.0f, 6.0f})) {
            std::cerr << "Translated bounds are invalid\n";
            return false;
        }

        const glm::mat4 nonUniformScale = glm::scale(glm::mat4{1.0f}, glm::vec3{-2.0f, 3.0f, 0.5f});
        const ark::Bounds3 scaled = ark::transformBounds(bounds, nonUniformScale);
        if (!nearVec3(scaled.min, glm::vec3{-2.0f, -3.0f, -0.5f}) ||
            !nearVec3(scaled.max, glm::vec3{2.0f, 3.0f, 0.5f})) {
            std::cerr << "Negative/non-uniform scaled bounds are invalid\n";
            return false;
        }

        ark::Bounds3 rectangular = ark::makeBoundsFromPoint(glm::vec3{-1.0f, -2.0f, 0.0f});
        ark::expandBounds(rectangular, glm::vec3{1.0f, 2.0f, 0.0f});
        const glm::mat4 rotated = glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
        const ark::Bounds3 rotatedBounds = ark::transformBounds(rectangular, rotated);
        if (!nearVec3(rotatedBounds.min, glm::vec3{-2.0f, -1.0f, 0.0f}) ||
            !nearVec3(rotatedBounds.max, glm::vec3{2.0f, 1.0f, 0.0f})) {
            std::cerr << "Rotated bounds are invalid\n";
            return false;
        }

        return true;
    }
} // namespace

int main() {
    return validateInvalidBounds() &&
                   validateExpandAndMerge() &&
                   validateCorners() &&
                   validateTransform()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
