#include "renderer/effects/shadow/ShadowCascadeBuilder.h"

#include "renderer/Bounds.h"
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

        bool buildCameraFrustumSliceBounds(const RenderView& view, float nearDistance, float farDistance, Bounds3& worldBounds) {
            // 根据cascade的near/ far深度范围，计算这一段相机视锥切片在世界空间中的AABB
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
            worldBounds = makeInvalidBounds();
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
                    expandBounds(worldBounds, point);
                }
            }

            // 最终得到当前cascade视锥切片的世界空间AABB,后续用它来拟合方向光的正交投影。
            return worldBounds.isValid();
        }

        bool buildCascadeLightMatrix(const Bounds3& worldBounds, const SceneLighting& lighting, const ShadowSettings& settings, glm::mat4& lightViewProjection) {
            // 给某一个 cascade 的 world-space bounds 构造方向光的 lightViewProjection 矩阵。
            if (!worldBounds.isValid()) return false;

            const glm::vec3 lightDirection = normalizeLightDirection(lighting.mainLight.direction);
            const glm::vec3 lightTarget = worldBounds.center();
            const glm::vec3 halfExtent = worldBounds.halfExtent();
            const float radius = std::max(glm::length(halfExtent), MinCascadeHalfExtent);
            const float padding = std::max(0.25f, radius * 0.05f);
            // 方向光没有真实位置；这里沿“光线反方向”退一段距离，只是为了构造稳定的 light view。
            const float lightDistance = radius + padding + settings.nearPlane;
            const glm::vec3 lightPosition = lightTarget - lightDirection * lightDistance;
            const glm::mat4 lightView = glm::lookAt(lightPosition, lightTarget, chooseLightUp(lightDirection));

            // 在 light space 中，x/y 范围决定正交投影的 left/right/bottom/top。
            // z 深度范围决定正交投影 near/far，后续 shadow map 只覆盖这个 cascade slice。
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            float bottom = std::numeric_limits<float>::max();
            float top = std::numeric_limits<float>::lowest();
            float minDepth = std::numeric_limits<float>::max();
            float maxDepth = std::numeric_limits<float>::lowest();

            // 使用 cascade world AABB 的 8 个角点做保守拟合，保证记录在 frame data 中的 bounds 也落入投影。
            for (const glm::vec3& corner : boundsCorners(worldBounds)) {
                const glm::vec4 lightCorner = lightView * glm::vec4{corner, 1.0f};
                // light-space x/y 包住所有角点，得到这一层 cascade 的正交投影横向覆盖。
                left = std::min(left, lightCorner.x);
                right = std::max(right, lightCorner.x);
                bottom = std::min(bottom, lightCorner.y);
                top = std::max(top, lightCorner.y);

                // glm::lookAt RH 视图朝 -Z 看，离光源越远的点 lightCorner.z 越负；转成正深度便于 orthoRH_ZO。
                const float depth = -lightCorner.z;
                minDepth = std::min(minDepth, depth);
                maxDepth = std::max(maxDepth, depth);
            }

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
                                                       const ShadowSettings& settings) {
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
            if (!buildCameraFrustumSliceBounds(view, cascade.nearDistance, cascade.farDistance, cascade.worldBounds) ||
                !buildCascadeLightMatrix(cascade.worldBounds, lighting, settings, cascade.lightViewProjection)) {
                return {};
            }
        }

        return frameData;
    }
} // namespace ark
