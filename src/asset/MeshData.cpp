#include "asset/MeshData.h"

#include "core/Log.h"

#include <cmath>
#include <vector>

namespace ark::asset {
    namespace {
        constexpr float TangentEpsilon = 1.0e-6f;

        struct Vec2 {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct Vec3 {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vec3 makeVec3(const float value[3]) {
            return Vec3{value[0], value[1], value[2]};
        }

        Vec2 makeVec2(const float value[2]) {
            return Vec2{value[0], value[1]};
        }

        Vec3 operator+(Vec3 lhs, Vec3 rhs) {
            return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
        }

        Vec3 operator-(Vec3 lhs, Vec3 rhs) {
            return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        Vec2 operator-(Vec2 lhs, Vec2 rhs) {
            return Vec2{lhs.x - rhs.x, lhs.y - rhs.y};
        }

        Vec3 operator*(Vec3 value, float scale) {
            return Vec3{value.x * scale, value.y * scale, value.z * scale};
        }

        float dot(Vec3 lhs, Vec3 rhs) {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        Vec3 cross(Vec3 lhs, Vec3 rhs) {
            return Vec3{
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x,
            };
        }

        float lengthSquared(Vec3 value) {
            return dot(value, value);
        }

        bool isFinite(Vec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isValidDirection(Vec3 value) {
            return isFinite(value) && lengthSquared(value) > TangentEpsilon;
        }

        Vec3 normalize(Vec3 value) {
            const float lenSq = lengthSquared(value);
            if (lenSq <= TangentEpsilon || !isFinite(value)) {
                return {};
            }

            const float invLen = 1.0f / std::sqrt(lenSq);
            return value * invLen;
        }

        Vec3 fallbackTangent(Vec3 normal) {
            Vec3 n = normalize(normal);
            if (!isValidDirection(n)) {
                return Vec3{1.0f, 0.0f, 0.0f};
            }

            const Vec3 axis = std::fabs(n.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
            Vec3 tangent = axis - n * dot(n, axis);
            tangent = normalize(tangent);
            return isValidDirection(tangent) ? tangent : Vec3{1.0f, 0.0f, 0.0f};
        }

        void storeTangent(MeshVertex& vertex, Vec3 tangent, float handedness) {
            vertex.tangent[0] = tangent.x;
            vertex.tangent[1] = tangent.y;
            vertex.tangent[2] = tangent.z;
            vertex.tangent[3] = handedness < 0.0f ? -1.0f : 1.0f;
        }
    } // namespace

    bool generateTangents(MeshPrimitiveData& mesh) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            ARK_ERROR("Tangent generation requires non-empty mesh data");
            return false;
        }

        if (mesh.indices.size() % 3 != 0) {
            ARK_ERROR("Tangent generation requires triangle index count");
            return false;
        }

        std::vector<Vec3> tangentSums(mesh.vertices.size());
        std::vector<Vec3> bitangentSums(mesh.vertices.size());
        u32 validTriangleCount = 0;
        u32 degenerateTriangleCount = 0;

        for (usize index = 0; index < mesh.indices.size(); index += 3) {
            const u32 i0 = mesh.indices[index];
            const u32 i1 = mesh.indices[index + 1];
            const u32 i2 = mesh.indices[index + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                ARK_ERROR("Tangent generation found an out-of-range index");
                return false;
            }

            const MeshVertex& v0 = mesh.vertices[i0];
            const MeshVertex& v1 = mesh.vertices[i1];
            const MeshVertex& v2 = mesh.vertices[i2];
            const Vec3 p0 = makeVec3(v0.position);
            const Vec3 p1 = makeVec3(v1.position);
            const Vec3 p2 = makeVec3(v2.position);
            const Vec2 uv0 = makeVec2(v0.uv0);
            const Vec2 uv1 = makeVec2(v1.uv0);
            const Vec2 uv2 = makeVec2(v2.uv0);

            const Vec3 edge1 = p1 - p0;
            const Vec3 edge2 = p2 - p0;
            const Vec2 duv1 = uv1 - uv0;
            const Vec2 duv2 = uv2 - uv0;
            const float denominator = duv1.x * duv2.y - duv2.x * duv1.y;
            if (std::fabs(denominator) <= TangentEpsilon) {
                ++degenerateTriangleCount;
                continue;
            }

            const float invDenominator = 1.0f / denominator;
            const Vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * invDenominator;
            const Vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * invDenominator;
            if (!isValidDirection(tangent) || !isValidDirection(bitangent)) {
                ++degenerateTriangleCount;
                continue;
            }

            tangentSums[i0] = tangentSums[i0] + tangent;
            tangentSums[i1] = tangentSums[i1] + tangent;
            tangentSums[i2] = tangentSums[i2] + tangent;
            bitangentSums[i0] = bitangentSums[i0] + bitangent;
            bitangentSums[i1] = bitangentSums[i1] + bitangent;
            bitangentSums[i2] = bitangentSums[i2] + bitangent;
            ++validTriangleCount;
        }

        if (validTriangleCount == 0) {
            ARK_WARN("Tangent generation used fallback tangents for all vertices: {}", mesh.debugName);
        } else if (degenerateTriangleCount > 0) {
            ARK_WARN("Tangent generation skipped degenerate triangles: mesh={}, count={}",
                     mesh.debugName,
                     degenerateTriangleCount);
        }

        for (usize vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            MeshVertex& vertex = mesh.vertices[vertexIndex];
            Vec3 normal = normalize(makeVec3(vertex.normal));
            if (!isValidDirection(normal)) {
                normal = Vec3{0.0f, 0.0f, 1.0f};
            }

            Vec3 tangent = tangentSums[vertexIndex] - normal * dot(normal, tangentSums[vertexIndex]);
            tangent = normalize(tangent);
            if (!isValidDirection(tangent)) {
                tangent = fallbackTangent(normal);
            }

            const Vec3 bitangent = bitangentSums[vertexIndex];
            const float handedness =
                isValidDirection(bitangent) && dot(cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
            storeTangent(vertex, tangent, handedness);
        }

        return true;
    }
} // namespace ark::asset
