#include "asset/GltfLoader.h"

#include "core/Log.h"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ark::asset {
    namespace {
        constexpr const char* PositionAttributeName = "POSITION";
        constexpr const char* NormalAttributeName = "NORMAL";
        constexpr const char* Texcoord0AttributeName = "TEXCOORD_0";
        constexpr const char* TangentAttributeName = "TANGENT";
        constexpr u32 InvalidMaterialIndex = std::numeric_limits<u32>::max();

        using Matrix4 = std::array<float, 16>;

        bool skipImageLoad(tinygltf::Image* image,
                           const int imageIndex,
                           std::string*,
                           std::string*,
                           int,
                           int,
                           const unsigned char*,
                           int,
                           void*) {
            if (image) {
                image->image.clear();
                image->width = 0;
                image->height = 0;
                image->component = 0;
                image->bits = 0;
                image->pixel_type = 0;
            }

            (void)imageIndex;
            return true;
        }

        bool isSupportedGltfVersion(const tinygltf::Model& model) {
            return model.asset.version.empty() || model.asset.version == "2.0";
        }

        const tinygltf::Accessor* getAccessor(const tinygltf::Model& model, int accessorIndex) {
            if (accessorIndex < 0 || static_cast<usize>(accessorIndex) >= model.accessors.size()) {
                return nullptr;
            }

            return &model.accessors[static_cast<usize>(accessorIndex)];
        }

        const tinygltf::BufferView* getBufferView(const tinygltf::Model& model, int bufferViewIndex) {
            if (bufferViewIndex < 0 || static_cast<usize>(bufferViewIndex) >= model.bufferViews.size()) {
                return nullptr;
            }

            return &model.bufferViews[static_cast<usize>(bufferViewIndex)];
        }

        const tinygltf::Buffer* getBuffer(const tinygltf::Model& model, int bufferIndex) {
            if (bufferIndex < 0 || static_cast<usize>(bufferIndex) >= model.buffers.size()) {
                return nullptr;
            }

            return &model.buffers[static_cast<usize>(bufferIndex)];
        }

        Matrix4 identityMatrix() {
            return Matrix4{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
        }

        Matrix4 multiplyMatrix(const Matrix4& lhs, const Matrix4& rhs) {
            Matrix4 result{};
            for (usize column = 0; column < 4; ++column) {
                for (usize row = 0; row < 4; ++row) {
                    float value = 0.0f;
                    for (usize k = 0; k < 4; ++k) {
                        value += lhs[k * 4 + row] * rhs[column * 4 + k];
                    }
                    result[column * 4 + row] = value;
                }
            }
            return result;
        }

        Matrix4 translationMatrix(const std::vector<double>& translation) {
            Matrix4 matrix = identityMatrix();
            if (translation.size() == 3) {
                matrix[12] = static_cast<float>(translation[0]);
                matrix[13] = static_cast<float>(translation[1]);
                matrix[14] = static_cast<float>(translation[2]);
            }
            return matrix;
        }

        Matrix4 scaleMatrix(const std::vector<double>& scale) {
            Matrix4 matrix = identityMatrix();
            if (scale.size() == 3) {
                matrix[0] = static_cast<float>(scale[0]);
                matrix[5] = static_cast<float>(scale[1]);
                matrix[10] = static_cast<float>(scale[2]);
            }
            return matrix;
        }

        Matrix4 rotationMatrix(const std::vector<double>& rotation) {
            Matrix4 matrix = identityMatrix();
            if (rotation.size() != 4) {
                return matrix;
            }

            const float x = static_cast<float>(rotation[0]);
            const float y = static_cast<float>(rotation[1]);
            const float z = static_cast<float>(rotation[2]);
            const float w = static_cast<float>(rotation[3]);
            const float xx = x * x;
            const float yy = y * y;
            const float zz = z * z;
            const float xy = x * y;
            const float xz = x * z;
            const float yz = y * z;
            const float wx = w * x;
            const float wy = w * y;
            const float wz = w * z;

            matrix[0] = 1.0f - 2.0f * (yy + zz);
            matrix[1] = 2.0f * (xy + wz);
            matrix[2] = 2.0f * (xz - wy);
            matrix[4] = 2.0f * (xy - wz);
            matrix[5] = 1.0f - 2.0f * (xx + zz);
            matrix[6] = 2.0f * (yz + wx);
            matrix[8] = 2.0f * (xz + wy);
            matrix[9] = 2.0f * (yz - wx);
            matrix[10] = 1.0f - 2.0f * (xx + yy);
            return matrix;
        }

        Matrix4 nodeLocalMatrix(const tinygltf::Node& node) {
            if (node.matrix.size() == 16) {
                Matrix4 matrix{};
                for (usize i = 0; i < matrix.size(); ++i) {
                    matrix[i] = static_cast<float>(node.matrix[i]);
                }
                return matrix;
            }

            // glTF TRS 对 column vector 的组合顺序是 T * R * S。
            const Matrix4 translation = translationMatrix(node.translation);
            const Matrix4 rotation = rotationMatrix(node.rotation);
            const Matrix4 scale = scaleMatrix(node.scale);
            return multiplyMatrix(multiplyMatrix(translation, rotation), scale);
        }

        TransformData toTransformData(const Matrix4& matrix) {
            TransformData transform{};
            for (usize i = 0; i < matrix.size(); ++i) {
                transform.matrix[i] = matrix[i];
            }
            return transform;
        }

        u64 componentByteSize(int componentType) {
            switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return 1;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return 2;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return 4;
            default:
                return 0;
            }
        }

        u32 componentCount(int type) {
            switch (type) {
            case TINYGLTF_TYPE_SCALAR:
                return 1;
            case TINYGLTF_TYPE_VEC2:
                return 2;
            case TINYGLTF_TYPE_VEC3:
                return 3;
            case TINYGLTF_TYPE_VEC4:
                return 4;
            default:
                return 0;
            }
        }

        const u8* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, u64& stride) {
            const tinygltf::BufferView* view = getBufferView(model, accessor.bufferView);
            if (!view) {
                return nullptr;
            }

            const tinygltf::Buffer* buffer = getBuffer(model, view->buffer);
            if (!buffer) {
                return nullptr;
            }

            const u64 componentSize = componentByteSize(accessor.componentType);
            const u32 componentNum = componentCount(accessor.type);
            if (componentSize == 0 || componentNum == 0) {
                return nullptr;
            }

            stride = view->byteStride != 0 ? static_cast<u64>(view->byteStride) : componentSize * componentNum;
            const u64 offset = static_cast<u64>(view->byteOffset) + static_cast<u64>(accessor.byteOffset);
            if (offset > buffer->data.size()) {
                return nullptr;
            }

            const u64 lastByteOffset =
                accessor.count == 0 ? offset : offset + (static_cast<u64>(accessor.count) - 1) * stride + componentSize * componentNum;
            if (lastByteOffset > buffer->data.size()) {
                return nullptr;
            }

            return buffer->data.data() + offset;
        }

        bool readFloatVec3(const tinygltf::Model& model,
                           const tinygltf::Accessor& accessor,
                           usize elementIndex,
                           float out[3]) {
            if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3) {
                return false;
            }

            u64 stride = 0;
            const u8* data = accessorData(model, accessor, stride);
            if (!data) {
                return false;
            }

            const u8* element = data + static_cast<u64>(elementIndex) * stride;
            std::memcpy(out, element, sizeof(float) * 3);
            return true;
        }

        bool readFloatVec2(const tinygltf::Model& model,
                           const tinygltf::Accessor& accessor,
                           usize elementIndex,
                           float out[2]) {
            if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC2) {
                return false;
            }

            u64 stride = 0;
            const u8* data = accessorData(model, accessor, stride);
            if (!data) {
                return false;
            }

            const u8* element = data + static_cast<u64>(elementIndex) * stride;
            std::memcpy(out, element, sizeof(float) * 2);
            return true;
        }

        bool readFloatVec4(const tinygltf::Model& model,
                           const tinygltf::Accessor& accessor,
                           usize elementIndex,
                           float out[4]) {
            if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC4) {
                return false;
            }

            u64 stride = 0;
            const u8* data = accessorData(model, accessor, stride);
            if (!data) {
                return false;
            }

            const u8* element = data + static_cast<u64>(elementIndex) * stride;
            std::memcpy(out, element, sizeof(float) * 4);
            return true;
        }

        bool appendIndices(const tinygltf::Model& model,
                           const tinygltf::Accessor& accessor,
                           std::vector<u32>& indices) {
            if (accessor.type != TINYGLTF_TYPE_SCALAR) {
                ARK_ERROR("glTF indices accessor must be scalar");
                return false;
            }

            if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                ARK_ERROR("glTF indices accessor uses unsupported component type");
                return false;
            }

            u64 stride = 0;
            const u8* data = accessorData(model, accessor, stride);
            if (!data) {
                ARK_ERROR("glTF indices accessor data is invalid");
                return false;
            }

            indices.resize(accessor.count);
            for (usize i = 0; i < accessor.count; ++i) {
                const u8* element = data + static_cast<u64>(i) * stride;
                switch (accessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    indices[i] = *element;
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                    u16 value = 0;
                    std::memcpy(&value, element, sizeof(value));
                    indices[i] = value;
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                    u32 value = 0;
                    std::memcpy(&value, element, sizeof(value));
                    indices[i] = value;
                    break;
                }
                default:
                    return false;
                }
            }

            return true;
        }

        int findAttributeAccessor(const tinygltf::Primitive& primitive, const char* name) {
            const auto iter = primitive.attributes.find(name);
            return iter == primitive.attributes.end() ? -1 : iter->second;
        }

        bool loadPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, MeshPrimitiveData& mesh) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                ARK_ERROR("glTF primitive mode is not TRIANGLES");
                return false;
            }

            const tinygltf::Accessor* positionAccessor = getAccessor(model, findAttributeAccessor(primitive, PositionAttributeName));
            const tinygltf::Accessor* normalAccessor = getAccessor(model, findAttributeAccessor(primitive, NormalAttributeName));
            const tinygltf::Accessor* uvAccessor = getAccessor(model, findAttributeAccessor(primitive, Texcoord0AttributeName));
            const tinygltf::Accessor* tangentAccessor = getAccessor(model, findAttributeAccessor(primitive, TangentAttributeName));
            const tinygltf::Accessor* indexAccessor = getAccessor(model, primitive.indices);

            if (!positionAccessor || !normalAccessor || !uvAccessor || !indexAccessor) {
                ARK_ERROR("glTF primitive requires POSITION, NORMAL, TEXCOORD_0 and indices");
                return false;
            }

            if (positionAccessor->count == 0 || positionAccessor->count > std::numeric_limits<u32>::max()) {
                ARK_ERROR("glTF vertex count is invalid");
                return false;
            }

            if (normalAccessor->count != positionAccessor->count || uvAccessor->count != positionAccessor->count) {
                ARK_ERROR("glTF POSITION/NORMAL/TEXCOORD_0 counts do not match");
                return false;
            }

            if (tangentAccessor && tangentAccessor->count != positionAccessor->count) {
                ARK_ERROR("glTF TANGENT count does not match POSITION count");
                return false;
            }

            mesh.vertices.resize(positionAccessor->count);
            mesh.debugName = "GltfPrimitive";
            for (usize i = 0; i < positionAccessor->count; ++i) {
                MeshVertex& vertex = mesh.vertices[i];
                if (!readFloatVec3(model, *positionAccessor, i, vertex.position) ||
                    !readFloatVec3(model, *normalAccessor, i, vertex.normal) ||
                    !readFloatVec2(model, *uvAccessor, i, vertex.uv0)) {
                    ARK_ERROR("glTF vertex attribute data is invalid");
                    return false;
                }

                // glTF TANGENT 是可选输入；缺失时在索引读取后由 CPU helper 根据 UV 生成。
                if (tangentAccessor && !readFloatVec4(model, *tangentAccessor, i, vertex.tangent)) {
                    ARK_ERROR("glTF TANGENT attribute data is invalid");
                    return false;
                }
            }

            if (!appendIndices(model, *indexAccessor, mesh.indices)) {
                return false;
            }

            if (!tangentAccessor && !generateTangents(mesh)) {
                ARK_ERROR("glTF tangent generation failed");
                return false;
            }

            mesh.materialIndex = primitive.material >= 0 ? static_cast<u32>(primitive.material) : 0;
            return !mesh.empty();
        }

        std::string makePrimitiveDebugName(const tinygltf::Mesh& mesh, usize meshIndex, usize primitiveIndex) {
            const std::string meshName = mesh.name.empty() ? "GltfMesh." + std::to_string(meshIndex) : mesh.name;
            return meshName + ".Primitive." + std::to_string(primitiveIndex);
        }

        std::string makeNodeDebugName(const tinygltf::Node& node, usize nodeIndex) {
            return node.name.empty() ? "GltfNode." + std::to_string(nodeIndex) : node.name;
        }

        const tinygltf::Texture* getTexture(const tinygltf::Model& model, int textureIndex) {
            if (textureIndex < 0 || static_cast<usize>(textureIndex) >= model.textures.size()) {
                return nullptr;
            }

            return &model.textures[static_cast<usize>(textureIndex)];
        }

        TextureFilter toTextureFilter(int filter, TextureFilter fallback) {
            switch (filter) {
            case TINYGLTF_TEXTURE_FILTER_NEAREST:
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                return TextureFilter::Nearest;
            case TINYGLTF_TEXTURE_FILTER_LINEAR:
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                return TextureFilter::Linear;
            case -1:
                return fallback;
            default:
                ARK_WARN("glTF sampler filter is unsupported: {}", filter);
                return fallback;
            }
        }

        TextureFilter toTextureMipFilter(int minFilter) {
            switch (minFilter) {
            case TINYGLTF_TEXTURE_FILTER_NEAREST:
            case TINYGLTF_TEXTURE_FILTER_LINEAR:
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                return TextureFilter::Nearest;
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                return TextureFilter::Linear;
            case -1:
                return TextureFilter::Linear;
            default:
                ARK_WARN("glTF sampler mip filter is unsupported: {}", minFilter);
                return TextureFilter::Linear;
            }
        }

        TextureAddressMode toTextureAddressMode(int wrapMode) {
            switch (wrapMode) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                return TextureAddressMode::Repeat;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return TextureAddressMode::ClampToEdge;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return TextureAddressMode::MirroredRepeat;
            default:
                ARK_WARN("glTF sampler wrap mode is unsupported: {}", wrapMode);
                return TextureAddressMode::Repeat;
            }
        }

        TextureSamplerData resolveTextureSampler(const tinygltf::Model& model,
                                                 const tinygltf::Texture& texture,
                                                 const char* slotName) {
            TextureSamplerData sampler{};
            if (texture.sampler < 0) {
                return sampler;
            }

            if (static_cast<usize>(texture.sampler) >= model.samplers.size()) {
                ARK_WARN("glTF texture slot references an invalid sampler: {}", slotName);
                return sampler;
            }

            const tinygltf::Sampler& gltfSampler = model.samplers[static_cast<usize>(texture.sampler)];
            sampler.minFilter = toTextureFilter(gltfSampler.minFilter, TextureFilter::Linear);
            sampler.magFilter = toTextureFilter(gltfSampler.magFilter, TextureFilter::Linear);
            sampler.mipFilter = toTextureMipFilter(gltfSampler.minFilter);
            sampler.addressU = toTextureAddressMode(gltfSampler.wrapS);
            sampler.addressV = toTextureAddressMode(gltfSampler.wrapT);
            return sampler;
        }

        Path resolveTexturePath(const Path& gltfPath, const tinygltf::Model& model, int textureIndex) {
            const tinygltf::Texture* texture = getTexture(model, textureIndex);
            if (!texture) {
                return {};
            }

            const int imageIndex = texture->source;
            if (imageIndex < 0 || static_cast<usize>(imageIndex) >= model.images.size()) {
                return {};
            }

            const std::string& uri = model.images[static_cast<usize>(imageIndex)].uri;
            if (uri.empty() || uri.rfind("data:", 0) == 0) {
                return {};
            }

            Path uriPath{uri};
            if (uriPath.is_absolute()) {
                return uriPath;
            }

            return gltfPath.parent_path() / uriPath;
        }

        MaterialTextureSlotData resolveTextureSlot(const Path& gltfPath,
                                                   const tinygltf::Model& model,
                                                   int textureIndex,
                                                   int texCoord,
                                                   const char* slotName) {
            MaterialTextureSlotData slot{};
            slot.path = resolveTexturePath(gltfPath, model, textureIndex);
            if (textureIndex < 0 || slot.path.empty()) {
                return slot;
            }

            const tinygltf::Texture* texture = getTexture(model, textureIndex);
            if (!texture) {
                return slot;
            }

            slot.texCoord = texCoord < 0 ? 0 : static_cast<u32>(texCoord);
            slot.hasSampler = texture->sampler >= 0 &&
                              static_cast<usize>(texture->sampler) < model.samplers.size();
            slot.sampler = resolveTextureSampler(model, *texture, slotName);
            if (slot.texCoord != 0) {
                ARK_WARN("glTF texture slot uses unsupported texCoord: slot={}, texCoord={}",
                         slotName,
                         slot.texCoord);
            }
            return slot;
        }

        void copyFloatArray(const std::vector<double>& values, float* output, usize count) {
            if (values.size() != count) {
                return;
            }

            for (usize i = 0; i < count; ++i) {
                output[i] = static_cast<float>(values[i]);
            }
        }

        bool loadMaterial(const Path& gltfPath,
                          const tinygltf::Model& gltfModel,
                          u32 sourceMaterialIndex,
                          MaterialData& material) {
            if (sourceMaterialIndex >= gltfModel.materials.size()) {
                ARK_ERROR("glTF material index is out of range");
                return false;
            }

            const tinygltf::Material& gltfMaterial = gltfModel.materials[sourceMaterialIndex];
            const tinygltf::PbrMetallicRoughness& pbr = gltfMaterial.pbrMetallicRoughness;
            material.debugName = gltfMaterial.name.empty()
                                     ? "GltfMaterial." + std::to_string(sourceMaterialIndex)
                                     : gltfMaterial.name;
            copyFloatArray(pbr.baseColorFactor, material.baseColorFactor, 4);
            copyFloatArray(gltfMaterial.emissiveFactor, material.emissiveFactor, 3);
            material.metallicFactor = static_cast<float>(pbr.metallicFactor);
            material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
            material.normalScale = static_cast<float>(gltfMaterial.normalTexture.scale);
            material.occlusionStrength = static_cast<float>(gltfMaterial.occlusionTexture.strength);

            material.baseColorTexture = resolveTextureSlot(gltfPath,
                                                           gltfModel,
                                                           pbr.baseColorTexture.index,
                                                           pbr.baseColorTexture.texCoord,
                                                           "baseColorTexture");
            material.baseColorTexturePath = material.baseColorTexture.path;
            if (material.baseColorTexturePath.empty()) {
                ARK_ERROR("glTF material requires an external baseColorTexture: {}", gltfPath.string());
                return false;
            }

            // Optional texture slots 缺失时保留空路径，renderer 层继续使用 fallback texture。
            material.normalTexture = resolveTextureSlot(gltfPath,
                                                        gltfModel,
                                                        gltfMaterial.normalTexture.index,
                                                        gltfMaterial.normalTexture.texCoord,
                                                        "normalTexture");
            material.normalTexturePath = material.normalTexture.path;
            material.metallicRoughnessTexture = resolveTextureSlot(gltfPath,
                                                                   gltfModel,
                                                                   pbr.metallicRoughnessTexture.index,
                                                                   pbr.metallicRoughnessTexture.texCoord,
                                                                   "metallicRoughnessTexture");
            material.metallicRoughnessTexturePath = material.metallicRoughnessTexture.path;
            material.occlusionTexture = resolveTextureSlot(gltfPath,
                                                           gltfModel,
                                                           gltfMaterial.occlusionTexture.index,
                                                           gltfMaterial.occlusionTexture.texCoord,
                                                           "occlusionTexture");
            material.occlusionTexturePath = material.occlusionTexture.path;
            material.emissiveTexture = resolveTextureSlot(gltfPath,
                                                          gltfModel,
                                                          gltfMaterial.emissiveTexture.index,
                                                          gltfMaterial.emissiveTexture.texCoord,
                                                          "emissiveTexture");
            material.emissiveTexturePath = material.emissiveTexture.path;
            if (gltfMaterial.normalTexture.index >= 0 && material.normalTexturePath.empty()) {
                ARK_WARN("glTF optional texture slot is unsupported or invalid: normalTexture");
            }
            if (pbr.metallicRoughnessTexture.index >= 0 && material.metallicRoughnessTexturePath.empty()) {
                ARK_WARN("glTF optional texture slot is unsupported or invalid: metallicRoughnessTexture");
            }
            if (gltfMaterial.occlusionTexture.index >= 0 && material.occlusionTexturePath.empty()) {
                ARK_WARN("glTF optional texture slot is unsupported or invalid: occlusionTexture");
            }
            if (gltfMaterial.emissiveTexture.index >= 0 && material.emissiveTexturePath.empty()) {
                ARK_WARN("glTF optional texture slot is unsupported or invalid: emissiveTexture");
            }
            return true;
        }

        bool loadTinyGltfModel(const Path& path, tinygltf::Model& model) {
            tinygltf::TinyGLTF loader;
            loader.SetImageLoader(skipImageLoad, nullptr);

            std::string error;
            std::string warning;
            const bool loaded = path.extension() == ".glb"
                                    ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
                                    : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());

            if (!warning.empty()) {
                ARK_WARN("glTF load warning: {}", warning);
            }

            if (!loaded) {
                ARK_ERROR("Failed to load glTF file: {} ({})", path.string(), error.empty() ? "unknown error" : error);
                return false;
            }

            return true;
        }

        bool appendMeshInstances(const tinygltf::Model& gltfModel,
                                 const std::vector<std::vector<u32>>& meshPrimitiveMap,
                                 usize nodeIndex,
                                 const Matrix4& parentTransform,
                                 ModelData& model) {
            if (nodeIndex >= gltfModel.nodes.size()) {
                ARK_ERROR("glTF scene node index is out of range");
                return false;
            }

            const tinygltf::Node& node = gltfModel.nodes[nodeIndex];
            if (node.skin >= 0) {
                ARK_ERROR("glTF skin is not supported by Phase 0.10 loader");
                return false;
            }

            if (node.camera >= 0) {
                ARK_WARN("glTF node camera is ignored by Phase 0.10 loader");
            }

            const Matrix4 worldTransform = multiplyMatrix(parentTransform, nodeLocalMatrix(node));
            if (node.mesh >= 0) {
                const usize meshIndex = static_cast<usize>(node.mesh);
                if (meshIndex >= meshPrimitiveMap.size()) {
                    ARK_ERROR("glTF node mesh index is out of range");
                    return false;
                }

                const std::string nodeName = makeNodeDebugName(node, nodeIndex);
                for (u32 primitiveMeshIndex : meshPrimitiveMap[meshIndex]) {
                    MeshPrimitiveInstanceData instance{};
                    instance.meshIndex = primitiveMeshIndex;
                    instance.localTransform = toTransformData(worldTransform);
                    instance.debugName = nodeName + "." + model.meshes[primitiveMeshIndex].debugName;
                    model.instances.push_back(std::move(instance));
                }
            }

            for (int childIndex : node.children) {
                if (childIndex < 0) {
                    ARK_ERROR("glTF child node index is invalid");
                    return false;
                }

                if (!appendMeshInstances(gltfModel, meshPrimitiveMap, static_cast<usize>(childIndex), worldTransform, model)) {
                    return false;
                }
            }

            return true;
        }

        bool loadSceneInstances(const tinygltf::Model& gltfModel,
                                const std::vector<std::vector<u32>>& meshPrimitiveMap,
                                ModelData& model) {
            if (gltfModel.scenes.empty()) {
                ARK_WARN("glTF file has no scene; creating identity primitive instances");
                for (usize meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
                    MeshPrimitiveInstanceData instance{};
                    instance.meshIndex = static_cast<u32>(meshIndex);
                    instance.localTransform = toTransformData(identityMatrix());
                    instance.debugName = model.meshes[meshIndex].debugName;
                    model.instances.push_back(std::move(instance));
                }
                return true;
            }

            const int selectedSceneIndex = gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0;
            if (selectedSceneIndex < 0 || static_cast<usize>(selectedSceneIndex) >= gltfModel.scenes.size()) {
                ARK_ERROR("glTF default scene index is out of range");
                return false;
            }

            const tinygltf::Scene& scene = gltfModel.scenes[static_cast<usize>(selectedSceneIndex)];
            const Matrix4 rootTransform = identityMatrix();
            for (int nodeIndex : scene.nodes) {
                if (nodeIndex < 0) {
                    ARK_ERROR("glTF root node index is invalid");
                    return false;
                }

                if (!appendMeshInstances(gltfModel, meshPrimitiveMap, static_cast<usize>(nodeIndex), rootTransform, model)) {
                    return false;
                }
            }

            if (model.instances.empty()) {
                ARK_ERROR("glTF scene does not contain drawable mesh instances");
                return false;
            }

            return true;
        }
    } // namespace

    ModelData GltfLoader::loadModel(const Path& path) {
        return loadGltfModel(path);
    }

    ModelData loadGltfModel(const Path& path) {
        tinygltf::Model gltfModel;
        if (!loadTinyGltfModel(path, gltfModel)) {
            return {};
        }

        if (!isSupportedGltfVersion(gltfModel)) {
            ARK_ERROR("Only glTF 2.0 is supported: {}", path.string());
            return {};
        }

        if (gltfModel.meshes.empty()) {
            ARK_ERROR("glTF file does not contain meshes: {}", path.string());
            return {};
        }

        if (gltfModel.materials.empty() || gltfModel.materials.size() > std::numeric_limits<u32>::max()) {
            ARK_ERROR("glTF file requires at least one material in u32 range: {}", path.string());
            return {};
        }

        ModelData model{};
        model.debugName = path.string();

        // 先保留 glTF 原始 material index，最后再压缩成 ModelData 的连续 material 数组。
        std::vector<u32> sourceMaterialIndices;
        std::vector<std::vector<u32>> meshPrimitiveMap(gltfModel.meshes.size());
        for (usize meshIndex = 0; meshIndex < gltfModel.meshes.size(); ++meshIndex) {
            const tinygltf::Mesh& gltfMesh = gltfModel.meshes[meshIndex];
            for (usize primitiveIndex = 0; primitiveIndex < gltfMesh.primitives.size(); ++primitiveIndex) {
                MeshPrimitiveData meshData{};
                if (!loadPrimitive(gltfModel, gltfMesh.primitives[primitiveIndex], meshData)) {
                    return {};
                }

                if (meshData.materialIndex >= gltfModel.materials.size()) {
                    ARK_ERROR("glTF primitive material index is out of range");
                    return {};
                }

                sourceMaterialIndices.push_back(meshData.materialIndex);
                meshData.debugName = makePrimitiveDebugName(gltfMesh, meshIndex, primitiveIndex);

                if (model.meshes.size() >= std::numeric_limits<u32>::max()) {
                    ARK_ERROR("glTF primitive count exceeds u32 range");
                    return {};
                }

                meshPrimitiveMap[meshIndex].push_back(static_cast<u32>(model.meshes.size()));
                model.meshes.push_back(std::move(meshData));
            }
        }

        if (model.meshes.empty()) {
            ARK_ERROR("glTF file does not contain valid mesh primitives: {}", path.string());
            return {};
        }

        if (!loadSceneInstances(gltfModel, meshPrimitiveMap, model)) {
            return {};
        }

        std::vector<u32> materialRemap(gltfModel.materials.size(), InvalidMaterialIndex);
        for (usize meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            const u32 sourceMaterialIndex = sourceMaterialIndices[meshIndex];
            u32& remappedIndex = materialRemap[sourceMaterialIndex];
            if (remappedIndex == InvalidMaterialIndex) {
                MaterialData material{};
                if (!loadMaterial(path, gltfModel, sourceMaterialIndex, material)) {
                    return {};
                }

                remappedIndex = static_cast<u32>(model.materials.size());
                model.materials.push_back(std::move(material));
            }

            model.meshes[meshIndex].materialIndex = remappedIndex;
        }

        ARK_INFO("Loaded glTF model: {} (primitives={}, materials={}, instances={})",
                 path.string(),
                 model.meshes.size(),
                 model.materials.size(),
                 model.instances.size());
        return model;
    }
} // namespace ark::asset
