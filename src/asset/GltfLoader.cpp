#include "asset/GltfLoader.h"

#include "core/Log.h"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <cstring>
#include <limits>
#include <string>

namespace ark::asset {
    namespace {
        constexpr const char* PositionAttributeName = "POSITION";
        constexpr const char* NormalAttributeName = "NORMAL";
        constexpr const char* Texcoord0AttributeName = "TEXCOORD_0";

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

        const tinygltf::Primitive* firstPrimitive(const tinygltf::Model& model) {
            if (model.meshes.empty() || model.meshes.front().primitives.empty()) {
                return nullptr;
            }

            return &model.meshes.front().primitives.front();
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

            mesh.vertices.resize(positionAccessor->count);
            for (usize i = 0; i < positionAccessor->count; ++i) {
                MeshVertex& vertex = mesh.vertices[i];
                if (!readFloatVec3(model, *positionAccessor, i, vertex.position) ||
                    !readFloatVec3(model, *normalAccessor, i, vertex.normal) ||
                    !readFloatVec2(model, *uvAccessor, i, vertex.uv0)) {
                    ARK_ERROR("glTF vertex attribute data is invalid");
                    return false;
                }
            }

            if (!appendIndices(model, *indexAccessor, mesh.indices)) {
                return false;
            }

            mesh.materialIndex = primitive.material >= 0 ? static_cast<u32>(primitive.material) : 0;
            mesh.debugName = "GltfPrimitive";
            return !mesh.empty();
        }

        Path resolveTexturePath(const Path& gltfPath, const tinygltf::Model& model, int materialIndex) {
            if (materialIndex < 0 || static_cast<usize>(materialIndex) >= model.materials.size()) {
                return {};
            }

            const tinygltf::Material& material = model.materials[static_cast<usize>(materialIndex)];
            const int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
            if (textureIndex < 0 || static_cast<usize>(textureIndex) >= model.textures.size()) {
                return {};
            }

            const int imageIndex = model.textures[static_cast<usize>(textureIndex)].source;
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

        const tinygltf::Primitive* primitive = firstPrimitive(gltfModel);
        if (!primitive) {
            ARK_ERROR("glTF file does not contain a mesh primitive: {}", path.string());
            return {};
        }

        ModelData model{};
        model.debugName = path.string();
        model.meshes.resize(1);
        if (!loadPrimitive(gltfModel, *primitive, model.meshes.front())) {
            return {};
        }

        MaterialData material{};
        material.debugName = "GltfMaterial";
        material.baseColorTexturePath =
            resolveTexturePath(path, gltfModel, static_cast<int>(model.meshes.front().materialIndex));
        if (material.baseColorTexturePath.empty()) {
            ARK_ERROR("glTF material requires an external baseColorTexture: {}", path.string());
            return {};
        }

        model.meshes.front().materialIndex = 0;
        model.materials.push_back(material);
        ARK_INFO("Loaded glTF model: {}", path.string());
        return model;
    }
} // namespace ark::asset
