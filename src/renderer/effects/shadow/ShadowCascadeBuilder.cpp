#include "renderer/effects/shadow/ShadowCascadeBuilder.h"

#include "renderer/core/Bounds.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>

namespace ark {
    namespace {
        constexpr float MinCameraNearDistance = 0.001f;
        constexpr float MinCascadeDepthRange = 0.01f;
        constexpr float MinCascadeHalfExtent = 0.25f;
        constexpr float CascadeSplitOverlapRatio = 0.05f;

        struct FrustumSliceFit {
            Bounds3 worldBounds;
            glm::vec3 stableCenter{0.0f};
            float stableRadius = 0.0f;
        };

        bool isFiniteVec3(const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isValidRange(float rangeMin, float rangeMax) {
            return std::isfinite(rangeMin) && std::isfinite(rangeMax) && rangeMin <= rangeMax;
        }

        glm::vec3 normalizeLightDirection(const glm::vec3& direction) {
            constexpr float MinDirectionLengthSquared = 1.0e-6f;
            if (glm::dot(direction, direction) <= MinDirectionLengthSquared) {
                return glm::normalize(SceneLighting{}.mainLight.direction);
            }

            return glm::normalize(direction);
        }

        glm::vec3 chooseLightUp(const glm::vec3& lightDirection) {
            const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(worldUp, lightDirection)) < 0.95f) {
                return worldUp;
            }

            return glm::vec3{0.0f, 0.0f, 1.0f};
        }

        void expandRangeToMinHalfExtent(float& rangeMin, float& rangeMax, float minHalfExtent) {
            const float center = (rangeMin + rangeMax) * 0.5f;
            const float halfExtent = std::max((rangeMax - rangeMin) * 0.5f, minHalfExtent);
            rangeMin = center - halfExtent;
            rangeMax = center + halfExtent;
        }

        float maxDistanceFromPointToBounds(const glm::vec3& point, const Bounds3& bounds) {
            if (!bounds.isValid()) {
                return 0.0f;
            }

            float result = 0.0f;
            for (const glm::vec3& corner : boundsCorners(bounds)) {
                result = std::max(result, glm::length(corner - point));
            }
            return result;
        }

        void includeDepthRangeFromBounds(const glm::mat4& lightView,
                                         const Bounds3& bounds,
                                         float& minDepth,
                                         float& maxDepth) {
            if (!bounds.isValid()) {
                return;
            }

            for (const glm::vec3& corner : boundsCorners(bounds)) {
                const glm::vec4 lightCorner = lightView * glm::vec4{corner, 1.0f};
                const float depth = -lightCorner.z;
                minDepth = std::min(minDepth, depth);
                maxDepth = std::max(maxDepth, depth);
            }
        }

        bool computeTexelSize(float rangeMin, float rangeMax, u32 mapExtent, float& texelSize) {
            if (mapExtent == 0) {
                return false;
            }

            const float rangeSize = rangeMax - rangeMin;
            if (!std::isfinite(rangeSize) || rangeSize <= 0.0f) {
                return false;
            }

            // 一个 shadow texel 在 light-space 正交投影中的世界长度。
            // 后续 snapping 只平移投影范围，不改变 rangeSize，因此不会改变当前 cascade 的覆盖尺度。
            texelSize = rangeSize / static_cast<float>(mapExtent);
            return std::isfinite(texelSize) && texelSize > 0.0f;
        }

        void snapHorizontalRangeToReferenceTexelGrid(float& rangeMin,
                                                     float& rangeMax,
                                                     float referenceCoord,
                                                     u32 mapExtent) {
            float texelSize = 0.0f;
            if (!computeTexelSize(rangeMin, rangeMax, mapExtent, texelSize) ||
                !std::isfinite(referenceCoord)) {
                return;
            }

            // 把稳定参考点映射到当前投影范围内的 texel 坐标，再四舍五入到最近 texel。
            const float referenceTexelCoord = (referenceCoord - rangeMin) / texelSize;
            const float snappedReferenceOffset = std::floor(referenceTexelCoord + 0.5f) * texelSize;
            // 反推出新的 rangeMin，并把 min/max 同步平移；这样参考点落在 texel 中心/边界的固定网格上。
            const float snappedRangeMin = referenceCoord - snappedReferenceOffset;
            const float offset = snappedRangeMin - rangeMin;
            rangeMin += offset;
            rangeMax += offset;
        }

        void snapVerticalRangeToReferenceTexelGrid(float& rangeMin,
                                                   float& rangeMax,
                                                   float referenceCoord,
                                                   u32 mapExtent) {
            float texelSize = 0.0f;
            if (!computeTexelSize(rangeMin, rangeMax, mapExtent, texelSize) ||
                !std::isfinite(referenceCoord)) {
                return;
            }

            // Vulkan 投影沿用 glm::orthoRH_ZO 后翻转 Y 的约定，因此 Y 方向 snapping 要和 ShadowPass 单图路径一致。
            // 翻转后最终 shadow texel Y 与 -(bottom + y) 成正比，所以这里使用和 X 不同的符号关系。
            const float referenceTexelCoord = -(rangeMin + referenceCoord) / texelSize;
            const float snappedReferenceOffset = std::floor(referenceTexelCoord + 0.5f) * texelSize;
            const float snappedRangeMin = -referenceCoord - snappedReferenceOffset;
            const float offset = snappedRangeMin - rangeMin;
            rangeMin += offset;
            rangeMax += offset;
        }

        void stabilizeLightProjectionRange(float& left,
                                           float& right,
                                           float& bottom,
                                           float& top,
                                           const glm::mat4& lightView,
                                           const glm::vec3& stableReferenceWorld,
                                           u32 mapExtent,
                                           bool enabled) {
            if (!enabled) {
                return;
            }

            // stableReferenceWorld 通常取 cascade 中心。相机轻微移动时，把这个点吸附到固定 texel 网格，
            // 可以避免投影边界连续小幅滑动导致 shadow map 采样结果“游泳”。
            const glm::vec4 stableReference = lightView * glm::vec4{stableReferenceWorld, 1.0f};
            snapHorizontalRangeToReferenceTexelGrid(left, right, stableReference.x, mapExtent);
            snapVerticalRangeToReferenceTexelGrid(bottom, top, stableReference.y, mapExtent);
        }

        bool buildCameraFrustumSliceFit(const RenderView& view,
                                        float nearDistance,
                                        float farDistance,
                                        FrustumSliceFit& fit) {
            // 根据 cascade 的 near/far 深度范围，计算这一段相机视锥切片在世界空间中的 AABB。
            // 同时在 view-space 中计算稳定中心/半径，供后续 light-space 正交投影使用。
            const glm::mat4& projection = view.projectionMatrix();
            // 当前只处理常规 perspective projection；[0][0]/[1][1] 分别等价于 1/tan(fovX/2)、1/tan(fovY/2)。
            const float projectionX = std::abs(projection[0][0]);
            const float projectionY = std::abs(projection[1][1]);
            if (!std::isfinite(projectionX) || !std::isfinite(projectionY) ||
                projectionX <= 0.0f || projectionY <= 0.0f ||
                !std::isfinite(nearDistance) || !std::isfinite(farDistance) ||
                nearDistance <= 0.0f || farDistance <= nearDistance) {
                return false;
            }

            const glm::mat4 inverseView = glm::affineInverse(view.viewMatrix());
            const glm::vec3 viewCenter{0.0f, 0.0f, -(nearDistance + farDistance) * 0.5f};
            fit.worldBounds = makeInvalidBounds();
            fit.stableRadius = 0.0f;
            const std::array<float, 2> distances{nearDistance, farDistance};
            for (const float distance : distances) {
                // 在 RH view space 中相机朝 -Z 看；给定深度 distance 时，slice 平面的 z 为 -distance。
                // 由 projection scale 反推该平面半宽/半高，得到 near/far 两个平面各 4 个角点。
                const float halfWidth = distance / projectionX;
                const float halfHeight = distance / projectionY;
                const std::array<glm::vec3, 4> viewCorners{{
                    glm::vec3{-halfWidth, -halfHeight, -distance},
                    glm::vec3{halfWidth, -halfHeight, -distance},
                    glm::vec3{-halfWidth, halfHeight, -distance},
                    glm::vec3{halfWidth, halfHeight, -distance},
                }};

                // CSM 后续要在世界空间中按方向光重新拟合，把 8 个 slice 角点变回 world space。
                for (const glm::vec3& viewCorner : viewCorners) {
                    const glm::vec4 worldCorner = inverseView * glm::vec4{viewCorner, 1.0f};
                    const glm::vec3 point{worldCorner};
                    if (!isFiniteVec3(point)) {
                        return false;
                    }
                    expandBounds(fit.worldBounds, point);
                    fit.stableRadius = std::max(fit.stableRadius, glm::length(viewCorner - viewCenter));
                }
            }

            const glm::vec4 worldCenter = inverseView * glm::vec4{viewCenter, 1.0f};
            fit.stableCenter = glm::vec3{worldCenter};
            fit.stableRadius = std::max(fit.stableRadius, MinCascadeHalfExtent);
            // 最终得到当前 cascade 视锥切片的世界空间 AABB；后续 debug 仍可用它表示 receiver coverage。
            return fit.worldBounds.isValid() && isFiniteVec3(fit.stableCenter) &&
                   std::isfinite(fit.stableRadius) && fit.stableRadius > 0.0f;
        }

        bool buildCascadeLightMatrix(const Bounds3& receiverBounds,
                                     const Bounds3* casterBounds,
                                     const glm::vec3& stableCenter,
                                     float stableRadius,
                                     const SceneLighting& lighting,
                                     const ShadowSettings& settings,
                                     glm::mat4& lightViewProjection) {
            // 给某一个 cascade 的 world-space bounds 构造方向光的 lightViewProjection 矩阵。
            // receiverBounds 决定本级 cascade 的受影区域；casterBounds 只用于扩展 light-space depth。
            if (!receiverBounds.isValid() || !isFiniteVec3(stableCenter) ||
                !std::isfinite(stableRadius) || stableRadius <= 0.0f) {
                return false;
            }

            const glm::vec3 lightDirection = normalizeLightDirection(lighting.mainLight.direction);
            const glm::vec3 lightTarget = stableCenter;
            Bounds3 depthBounds = receiverBounds;
            if (casterBounds && casterBounds->isValid()) {
                mergeBounds(depthBounds, *casterBounds);
            }

            // 先使用 view-space frustum sphere 的稳定半径，再保守扩大到 receiver AABB，
            // 避免 debug/coverage bounds 的角点落出 light-space XY 投影。
            const float radius = std::max({
                stableRadius,
                maxDistanceFromPointToBounds(lightTarget, receiverBounds),
                MinCascadeHalfExtent,
            });
            const float coverageRadius =
                std::max(maxDistanceFromPointToBounds(lightTarget, depthBounds), radius);
            const float padding = std::max(0.25f, radius * 0.05f);
            // 方向光没有真实位置；这里后退足够距离，避免大型 caster bounds 落到 light near plane 前。
            const float lightDistance = coverageRadius + padding + settings.nearPlane;
            const glm::vec3 lightPosition = lightTarget - lightDirection * lightDistance;
            const glm::mat4 lightView = glm::lookAt(lightPosition, lightTarget, chooseLightUp(lightDirection));

            // 稳定 CSM 使用固定半径决定每级 XY 正交范围，避免 AABB min/max 随相机旋转产生明显跳变。
            const glm::vec4 lightTargetPosition = lightView * glm::vec4{lightTarget, 1.0f};
            float left = lightTargetPosition.x - radius;
            float right = lightTargetPosition.x + radius;
            float bottom = lightTargetPosition.y - radius;
            float top = lightTargetPosition.y + radius;
            float minDepth = std::numeric_limits<float>::max();
            float maxDepth = std::numeric_limits<float>::lowest();

            // z 深度范围仍用 receiver/caster bounds 拟合，保证遮挡物不会被 light near/far 裁掉。
            for (const glm::vec3& corner : boundsCorners(receiverBounds)) {
                const glm::vec4 lightCorner = lightView * glm::vec4{corner, 1.0f};
                // glm::lookAt RH 视图朝 -Z 看，离光源越远的点 lightCorner.z 越负；转成正深度便于 orthoRH_ZO。
                const float depth = -lightCorner.z;
                minDepth = std::min(minDepth, depth);
                maxDepth = std::max(maxDepth, depth);
            }
            // casterBounds 只扩展 light-space 深度，避免远处/高处遮挡物被 cascade near/far 裁掉。
            includeDepthRangeFromBounds(lightView, depthBounds, minDepth, maxDepth);

            if (!isValidRange(left, right) || !isValidRange(bottom, top) || !isValidRange(minDepth, maxDepth)) {
                return false;
            }

            left -= padding;
            right += padding;
            bottom -= padding;
            top += padding;
            // 避免非常薄的 slice 生成接近 0 的投影范围，导致矩阵数值不稳定。
            expandRangeToMinHalfExtent(left, right, MinCascadeHalfExtent);
            expandRangeToMinHalfExtent(bottom, top, MinCascadeHalfExtent);
            // texel snapping: 将投影中心吸附到 shadow texel grid，降低相机小幅移动时的阴影游泳/闪烁。
            stabilizeLightProjectionRange(left,
                                          right,
                                          bottom,
                                          top,
                                          lightView,
                                          lightTarget,
                                          settings.cascades.cascadeExtent,
                                          settings.stabilizeProjection && settings.cascades.stabilize);

            const float nearPlane = std::max(settings.nearPlane, minDepth - padding);
            const float farPlane = std::max(maxDepth + padding, nearPlane + MinCascadeDepthRange);

            glm::mat4 lightProjection = glm::orthoRH_ZO(left, right, bottom, top, nearPlane, farPlane);
            lightProjection[1][1] *= -1.0f;
            lightViewProjection = lightProjection * lightView;
            return true;
        }
    } // namespace

    CascadeSplitDistances computeCascadeSplitDistances(const ShadowSettings& settings) {
        CascadeSplitDistances result{};
        const u32 cascadeCount = settings.cascades.cascadeCount;
        if (cascadeCount == 0 || cascadeCount > MaxShadowCascadeCount) {
            return result;
        }

        const float nearDistance = std::max(settings.nearPlane, MinCameraNearDistance);
        const float farDistance = std::max(settings.cascades.maxDistance, nearDistance + MinCascadeDepthRange);
        return computeCascadeSplitDistances(nearDistance,
                                            farDistance,
                                            cascadeCount,
                                            settings.cascades.splitLambda);
    }

    CascadeSplitDistances computeCascadeSplitDistances(float nearDistance,
                                                       float farDistance,
                                                       u32 cascadeCount,
                                                       float splitLambda) {
        CascadeSplitDistances result{}; // 每一级cascade的深度切分边界

        // 合法性检查
        if (cascadeCount == 0 || cascadeCount > MaxShadowCascadeCount ||
            !std::isfinite(nearDistance) || !std::isfinite(farDistance)) {
            return result;
        }

        nearDistance = std::max(nearDistance, MinCameraNearDistance);
        farDistance = std::max(farDistance, nearDistance + MinCascadeDepthRange);
        splitLambda = std::clamp(splitLambda, 0.0f, 1.0f);
        result.cascadeCount = cascadeCount;
        result.distances[0] = nearDistance;

        for (u32 index = 1; index < cascadeCount; ++index) {
            const float t = static_cast<float>(index) / static_cast<float>(cascadeCount);
            // linear split 让每一级覆盖相同深度范围，远处不容易被裁掉，但近处 texel density 较低。
            const float linear = nearDistance + (farDistance - nearDistance) * t;
            // logarithmic split 把更多级联边界压向近处，提升近处阴影精度，但远处覆盖会更稀疏。
            const float logarithmic = nearDistance * std::pow(farDistance / nearDistance, t);
            // practical split 在两者之间插值：0=纯线性，1=纯对数，常用 0.5~0.8 做大场景折中。
            const float split = linear * (1.0f - splitLambda) + logarithmic * splitLambda;
            // 防止极端参数或浮点误差让相邻 cascade 重叠/倒序，后续矩阵拟合依赖严格递增边界。
            const float minSplit = result.distances[index - 1] + MinCascadeDepthRange;
            result.distances[index] = std::clamp(split, minSplit, farDistance);
        }

        result.distances[cascadeCount] = farDistance;
        return result;
    }

    CascadeShadowFrameData buildCascadeShadowFrameData(const RenderView& view,
                                                       const SceneLighting& lighting,
                                                       const ShadowSettings& settings,
                                                       const Bounds3* casterBounds) {
        CascadeShadowFrameData frameData{};
        if (!settings.cascades.enabled) {
            return frameData;
        }

        const float cameraNear = std::max(view.cameraNearPlane(), settings.nearPlane);
        const float cameraFar = std::max(view.cameraFarPlane(), cameraNear + MinCascadeDepthRange);
        const float cascadeFar = std::min(cameraFar, settings.cascades.maxDistance);
        const CascadeSplitDistances splitDistances = computeCascadeSplitDistances(cameraNear,
                                                                                  cascadeFar,
                                                                                  settings.cascades.cascadeCount,
                                                                                  settings.cascades.splitLambda);
        if (!splitDistances.isValid()) {
            return frameData;
        }

        frameData.enabled = true;
        frameData.cascadeCount = splitDistances.cascadeCount;
        frameData.cascadeExtent = settings.cascades.cascadeExtent;

        for (u32 index = 0; index < splitDistances.cascadeCount; ++index) {
            ShadowCascade& cascade = frameData.cascades[index];
            cascade.nearDistance = splitDistances.distances[index];
            cascade.farDistance = splitDistances.distances[index + 1];

            const float splitLength = cascade.farDistance - cascade.nearDistance;
            const float overlap = std::max(splitLength * CascadeSplitOverlapRatio, MinCascadeDepthRange);
            const float fitNearDistance = index == 0
                                              ? cascade.nearDistance
                                              : std::max(cameraNear, cascade.nearDistance - overlap);
            const float fitFarDistance = index + 1 == splitDistances.cascadeCount
                                             ? cascade.farDistance
                                             : std::min(cascadeFar, cascade.farDistance + overlap);

            FrustumSliceFit fit{};
            if (!buildCameraFrustumSliceFit(view, fitNearDistance, fitFarDistance, fit)) {
                return {};
            }
            cascade.worldBounds = fit.worldBounds;

            if (!buildCascadeLightMatrix(cascade.worldBounds,
                                         casterBounds,
                                         fit.stableCenter,
                                         fit.stableRadius,
                                         lighting,
                                         settings,
                                         cascade.lightViewProjection)) {
                return {};
            }
        }

        return frameData;
    }
} // namespace ark
