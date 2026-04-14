#include "livelink_scene_sync.h"

#include "../assets/asset_loader.h"
#include "../camera.h"
#include "../dxr_renderer.h"
#include "../ibl_manager.h"
#include "../material/material_livelink.h"
#include "../saved_views.h"
#include "../scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <string_view>

namespace LiveLink {

namespace {

struct ScopedSceneBatchUpdates {
  ScopedSceneBatchUpdates() { Scene::BeginBatchedUpdates(); }
  ~ScopedSceneBatchUpdates() { Scene::EndBatchedUpdates(); }
};

template <typename T>
const T *FindPayload(const SceneDelta &delta) {
  return std::get_if<T>(&delta.payload);
}

constexpr size_t kInvalidHandle = static_cast<size_t>(-1);

std::string BuildLiveLinkMaterialName(const std::string &nodeObjectId,
                                      int materialSlot);

void ComputeLiveLinkTangents(std::vector<Asset::Vertex> &vertices,
                             const std::vector<uint32_t> &indices) {
  if (indices.size() % 3 != 0) return;
  for (size_t i = 0; i < indices.size(); i += 3) {
    Asset::Vertex &v0 = vertices[indices[i]];
    Asset::Vertex &v1 = vertices[indices[i + 1]];
    Asset::Vertex &v2 = vertices[indices[i + 2]];

    float dx1 = v1.pos[0] - v0.pos[0];
    float dy1 = v1.pos[1] - v0.pos[1];
    float dz1 = v1.pos[2] - v0.pos[2];

    float dx2 = v2.pos[0] - v0.pos[0];
    float dy2 = v2.pos[1] - v0.pos[1];
    float dz2 = v2.pos[2] - v0.pos[2];

    float du1 = v1.uv[0] - v0.uv[0];
    float dv1 = v1.uv[1] - v0.uv[1];

    float du2 = v2.uv[0] - v0.uv[0];
    float dv2 = v2.uv[1] - v0.uv[1];

    float r = 1.0f / (du1 * dv2 - dv1 * du2);
    if (std::isnan(r) || std::isinf(r)) {
      r = 1.0f;
    }

    float tx = (dv2 * dx1 - dv1 * dx2) * r;
    float ty = (dv2 * dy1 - dv1 * dy2) * r;
    float tz = (dv2 * dz1 - dv1 * dz2) * r;

    auto applyTangent = [&](Asset::Vertex &v) {
      float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
      float dot = nx * tx + ny * ty + nz * tz;
      float ox = tx - nx * dot;
      float oy = ty - ny * dot;
      float oz = tz - nz * dot;

      float len = std::sqrt(ox * ox + oy * oy + oz * oz);
      if (len > 0.0001f) {
        v.tangent[0] = ox / len;
        v.tangent[1] = oy / len;
        v.tangent[2] = oz / len;
        v.tangent[3] = 1.0f;
      }
    };

    applyTangent(v0);
    applyTangent(v1);
    applyTangent(v2);
  }
}

std::string ResolveNodeName(const SceneDelta &delta,
                            std::string_view preferredName) {
  if (!preferredName.empty()) {
    return std::string(preferredName);
  }
  if (!delta.debugLabel.empty()) {
    return delta.debugLabel;
  }
  if (!delta.target.objectId.empty()) {
    return delta.target.objectId;
  }
  return "LiveLink Node";
}

std::string ResolveMaterialName(const SceneDelta &delta) {
  if (!delta.debugLabel.empty()) {
    return delta.debugLabel;
  }
  if (!delta.target.objectId.empty()) {
    return delta.target.objectId;
  }
  return "Material";
}

std::string ResolveMaterialDisplayName(const SceneDelta &delta,
                                       const MaterialChangedPayload *payload) {
  if (payload && !payload->name.empty()) {
    return payload->name;
  }
  return ResolveMaterialName(delta);
}

int ResolveMaterialIndexByName(const SceneDelta &delta,
                               const MaterialChangedPayload *payload) {
  auto tryFind = [](const std::string &name) {
    return name.empty() ? -1 : Scene::FindMaterialByName(name);
  };

  if (payload) {
    if (!payload->nodeObjectId.empty()) {
      const int placeholderIndex = tryFind(
          BuildLiveLinkMaterialName(payload->nodeObjectId, payload->materialSlot));
      if (placeholderIndex >= 0) {
        return placeholderIndex;
      }
    }

    const int namedIndex = tryFind(payload->name);
    if (namedIndex >= 0) {
      return namedIndex;
    }
  }

  const int targetIndex = tryFind(delta.target.objectId);
  if (targetIndex >= 0) {
    return targetIndex;
  }

  return tryFind(delta.debugLabel);
}

std::filesystem::path Utf8PathFromString(const std::string &value) {
  std::u8string wideBytes;
  wideBytes.reserve(value.size());
  for (unsigned char ch : value) {
    wideBytes.push_back(static_cast<char8_t>(ch));
  }
  return std::filesystem::path(wideBytes);
}

bool IsHdrTextureUri(const std::string &value) {
  const std::filesystem::path path = Utf8PathFromString(value);
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return extension == ".hdr" || extension == ".exr";
}

struct NativeMeshPayloadHeader {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t meshCount = 0;
  uint32_t reserved = 0;
};

struct NativeMeshPayloadMeshHeader {
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  int32_t materialSlot = 0;
  uint32_t reserved = 0;
};

struct NativeMeshPayloadVertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float uv[2];
};

enum : uint32_t {
  kNativeMaterialFlagDoubleSided = 1u << 0,
  kNativeMaterialFlagInvertRoughnessTexture = 1u << 1,
};

struct NativeMeshPayloadMaterialHeader {
  int32_t materialSlot = 0;
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
};

struct NativeMeshPayloadMaterialBindingHeader {
  int32_t materialSlot = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t reserved = 0;
};

struct NativeMaterialLibraryHeader {
  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t materialCount = 0;
  uint32_t reserved = 0;
};

enum class NativeMaterialLibraryTextureBlobEncoding : uint32_t {
  RawRgba8 = 1,
  RawRgba32Float = 2,
  EncodedLdr = 3,
  EncodedHdr = 4,
};

struct NativeMaterialLibraryTextureBlobHeader {
  uint32_t hashLength = 0;
  uint32_t encoding = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t dataSize = 0;
};

struct NativeMaterialLibraryReferenceHeader {
  int32_t materialSlot = 0;
  uint32_t nodeObjectIdLength = 0;
};

struct NativeMaterialLibraryMaterialHeader {
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  uint32_t objectIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
  uint32_t referenceCount = 0;
};

struct NativeMaterialLibraryMaterialHeaderV2 {
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  uint32_t objectIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t baseColorTextureBlobHashLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t normalTextureBlobHashLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t emissiveTextureBlobHashLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t occlusionTextureBlobHashLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
  uint32_t metalRoughTextureBlobHashLength = 0;
  uint32_t referenceCount = 0;
};

struct NativeMaterialLibraryMaterialHeaderV3 {
  uint32_t flags = 0;
  float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float emissiveColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float specularColor[3] = {1.0f, 1.0f, 1.0f};
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float thickness = 0.0f;
  float attenuationDistance = 0.0f;
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float coatIor = 1.5f;
  float anisotropy = 0.0f;
  float anisotropyRotation = 0.0f;
  float sheenWeight = 0.0f;
  float sheenColor[3] = {1.0f, 1.0f, 1.0f};
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  float opacityTextureAmount = 1.0f;
  float coatNormalTextureAmount = 1.0f;
  float specularColorTextureAmount = 1.0f;
  float thicknessTextureAmount = 1.0f;
  float alphaCutoff = 0.35f;
  uint32_t objectIdLength = 0;
  uint32_t nameLength = 0;
  uint32_t materialStableIdLength = 0;
  uint32_t materialModelLength = 0;
  uint32_t alphaModeLength = 0;
  uint32_t baseColorTextureUriLength = 0;
  uint32_t baseColorTextureBlobHashLength = 0;
  uint32_t opacityTextureUriLength = 0;
  uint32_t opacityTextureBlobHashLength = 0;
  uint32_t normalTextureUriLength = 0;
  uint32_t normalTextureBlobHashLength = 0;
  uint32_t coatNormalTextureUriLength = 0;
  uint32_t coatNormalTextureBlobHashLength = 0;
  uint32_t emissiveTextureUriLength = 0;
  uint32_t emissiveTextureBlobHashLength = 0;
  uint32_t occlusionTextureUriLength = 0;
  uint32_t occlusionTextureBlobHashLength = 0;
  uint32_t metalRoughTextureUriLength = 0;
  uint32_t metalRoughTextureBlobHashLength = 0;
  uint32_t specularColorTextureUriLength = 0;
  uint32_t specularColorTextureBlobHashLength = 0;
  uint32_t thicknessTextureUriLength = 0;
  uint32_t thicknessTextureBlobHashLength = 0;
  uint32_t referenceCount = 0;
};

struct NativeMaterialLibraryTextureBlob {
  std::string hash;
  NativeMaterialLibraryTextureBlobEncoding encoding =
      NativeMaterialLibraryTextureBlobEncoding::EncodedLdr;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> data;
};

struct NativeMaterialLibraryRecord {
  std::string objectId;
  MaterialChangedPayload payload;
};

static Asset::Material BuildPlaceholderMaterialRecord(const std::string &name,
                                                      int materialSlot) {
  Asset::Material material{};
  std::string resolvedName = name;
  if (resolvedName.empty()) {
    resolvedName = "Material Slot " + std::to_string((std::max)(0, materialSlot));
  }
  strncpy_s(material.name, resolvedName.c_str(), _TRUNCATE);
  return material;
}

bool ReadNativePayloadString(std::ifstream &stream, uint32_t length,
                             std::string *outValue) {
  if (!outValue) {
    return false;
  }

  outValue->clear();
  if (length == 0) {
    return true;
  }

  outValue->resize(length);
  stream.read(outValue->data(), static_cast<std::streamsize>(length));
  return static_cast<bool>(stream);
}

int AppendNativePayloadTexture(const std::string &textureUri,
                               std::unordered_map<std::string, int> *cache,
                               std::vector<Asset::Texture> *outTextures,
                               std::vector<std::string> *outTextureSourceUris) {
  if (!cache || !outTextures || textureUri.empty()) {
    return -1;
  }

  const auto cached = cache->find(textureUri);
  if (cached != cache->end()) {
    return cached->second;
  }

  const int index = static_cast<int>(outTextures->size());
  outTextures->push_back(Asset::Texture{});
  if (outTextureSourceUris) {
    outTextureSourceUris->push_back(textureUri);
  }
  cache->emplace(textureUri, index);
  return index;
}

bool LoadNativeMeshPayload(const std::string &path,
                           std::vector<Asset::GpuMesh> *outMeshes,
                           std::vector<Asset::Material> *outMaterials,
                           std::vector<std::string> *outMaterialStableIds,
                           std::vector<Asset::Texture> *outTextures,
                           std::vector<std::string> *outTextureSourceUris,
                           bool *outHasFullMaterialDefinitions = nullptr) {
  if (!outMeshes) {
    return false;
  }

  struct ScopedDeferredGpuUpload {
    bool previous = false;
    ScopedDeferredGpuUpload() : previous(Asset::GetDeferGpuUpload()) {
      Asset::SetDeferGpuUpload(true);
    }
    ~ScopedDeferredGpuUpload() { Asset::SetDeferGpuUpload(previous); }
  } scopedDeferredGpuUpload;

  if (outMaterials) {
    outMaterials->clear();
  }
  if (outTextures) {
    outTextures->clear();
  }
  if (outMaterialStableIds) {
    outMaterialStableIds->clear();
  }
  if (outTextureSourceUris) {
    outTextureSourceUris->clear();
  }

  const std::filesystem::path payloadPath = Utf8PathFromString(path);
  std::ifstream stream(payloadPath, std::ios::binary);
  if (!stream) {
    return false;
  }

  NativeMeshPayloadHeader header;
  stream.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!stream || header.magic != 0x48534D50) {
    return false;
  }
  if (outHasFullMaterialDefinitions) {
    *outHasFullMaterialDefinitions = header.version < 5;
  }

  outMeshes->clear();
  if (header.version == 1) {
    std::vector<NativeMeshPayloadVertex> sourceVertices(header.meshCount);
    std::vector<uint32_t> indices(header.reserved);
    if (!sourceVertices.empty()) {
      stream.read(reinterpret_cast<char *>(sourceVertices.data()),
                  static_cast<std::streamsize>(sourceVertices.size() *
                                               sizeof(sourceVertices[0])));
    }
    if (!indices.empty()) {
      stream.read(reinterpret_cast<char *>(indices.data()),
                  static_cast<std::streamsize>(indices.size() * sizeof(indices[0])));
    }
    if (!stream) {
      return false;
    }

    std::vector<Asset::Vertex> vertices(sourceVertices.size());
    for (size_t index = 0; index < vertices.size(); ++index) {
      const NativeMeshPayloadVertex &source = sourceVertices[index];
      Asset::Vertex vertex{};
      std::copy(std::begin(source.position), std::end(source.position),
                std::begin(vertex.pos));
      std::copy(std::begin(source.normal), std::end(source.normal),
                std::begin(vertex.normal));
      std::copy(std::begin(source.tangent), std::end(source.tangent),
                std::begin(vertex.tangent));
      std::copy(std::begin(source.uv), std::end(source.uv), std::begin(vertex.uv));
      vertices[index] = vertex;
    }

    ComputeLiveLinkTangents(vertices, indices);
    Asset::GpuMesh mesh = Asset::LoadMeshFromMemory(vertices, indices);
    mesh.materialIndex = 0;
    mesh.materialSlot = 0;
    outMeshes->push_back(std::move(mesh));
    return !outMeshes->empty() && outMeshes->front().vertexCount > 0 &&
           outMeshes->front().indexCount > 0;
  }

  if (header.version != 2 && header.version != 3 && header.version != 4 &&
      header.version != 5) {
    return false;
  }

  for (uint32_t meshIndex = 0; meshIndex < header.meshCount; ++meshIndex) {
    NativeMeshPayloadMeshHeader meshHeader;
    stream.read(reinterpret_cast<char *>(&meshHeader), sizeof(meshHeader));
    if (!stream) {
      return false;
    }

    std::vector<NativeMeshPayloadVertex> sourceVertices(meshHeader.vertexCount);
    std::vector<uint32_t> indices(meshHeader.indexCount);
    if (!sourceVertices.empty()) {
      stream.read(reinterpret_cast<char *>(sourceVertices.data()),
                  static_cast<std::streamsize>(sourceVertices.size() *
                                               sizeof(sourceVertices[0])));
    }
    if (!indices.empty()) {
      stream.read(reinterpret_cast<char *>(indices.data()),
                  static_cast<std::streamsize>(indices.size() * sizeof(indices[0])));
    }
    if (!stream) {
      return false;
    }

    std::vector<Asset::Vertex> vertices(sourceVertices.size());
    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
      const NativeMeshPayloadVertex &source = sourceVertices[vertexIndex];
      Asset::Vertex vertex{};
      std::copy(std::begin(source.position), std::end(source.position),
                std::begin(vertex.pos));
      std::copy(std::begin(source.normal), std::end(source.normal),
                std::begin(vertex.normal));
      std::copy(std::begin(source.tangent), std::end(source.tangent),
                std::begin(vertex.tangent));
      std::copy(std::begin(source.uv), std::end(source.uv), std::begin(vertex.uv));
      vertices[vertexIndex] = vertex;
    }

    ComputeLiveLinkTangents(vertices, indices);
    Asset::GpuMesh mesh = Asset::LoadMeshFromMemory(vertices, indices);
    mesh.materialIndex = (std::max)(0, meshHeader.materialSlot);
    mesh.materialSlot = meshHeader.materialSlot;
    outMeshes->push_back(std::move(mesh));
  }

  if (header.version == 5) {
    const uint32_t bindingCount = header.reserved;
    if (outMaterials) {
      outMaterials->clear();
    }
    if (outMaterialStableIds) {
      outMaterialStableIds->clear();
    }

    for (uint32_t bindingIndex = 0; bindingIndex < bindingCount; ++bindingIndex) {
      NativeMeshPayloadMaterialBindingHeader bindingHeader;
      stream.read(reinterpret_cast<char *>(&bindingHeader), sizeof(bindingHeader));
      if (!stream) {
        return false;
      }

      std::string materialStableId;
      std::string materialName;
      if (!ReadNativePayloadString(stream, bindingHeader.materialStableIdLength,
                                   &materialStableId) ||
          !ReadNativePayloadString(stream, bindingHeader.nameLength,
                                   &materialName)) {
        return false;
      }

      const size_t slot =
          static_cast<size_t>((std::max)(0, bindingHeader.materialSlot));
      if (outMaterials) {
        if (outMaterials->size() <= slot) {
          outMaterials->resize(slot + 1);
        }
        (*outMaterials)[slot] =
            BuildPlaceholderMaterialRecord(materialName, bindingHeader.materialSlot);
      }
      if (outMaterialStableIds) {
        if (outMaterialStableIds->size() <= slot) {
          outMaterialStableIds->resize(slot + 1);
        }
        (*outMaterialStableIds)[slot] = std::move(materialStableId);
      }
    }
  } else if ((header.version == 3 || header.version == 4) && outMaterials) {
    std::unordered_map<std::string, int> textureIndicesByUri;
    const uint32_t materialCount = header.reserved;
    outMaterials->reserve(materialCount);
    if (outMaterialStableIds) {
      outMaterialStableIds->reserve(materialCount);
    }

    for (uint32_t materialIndex = 0; materialIndex < materialCount;
         ++materialIndex) {
      NativeMeshPayloadMaterialHeader materialHeader;
      stream.read(reinterpret_cast<char *>(&materialHeader), sizeof(materialHeader));
      if (!stream) {
        return false;
      }

      std::string name;
  std::string materialStableId;
      std::string materialModel;
      std::string alphaMode;
      std::string baseColorTextureUri;
      std::string normalTextureUri;
      std::string emissiveTextureUri;
      std::string occlusionTextureUri;
      std::string metalRoughTextureUri;
        if (!ReadNativePayloadString(stream, materialHeader.nameLength, &name) ||
          (header.version >= 4 &&
           !ReadNativePayloadString(stream, materialHeader.materialStableIdLength,
                      &materialStableId)) ||
          !ReadNativePayloadString(stream, materialHeader.materialModelLength,
                                   &materialModel) ||
          !ReadNativePayloadString(stream, materialHeader.alphaModeLength,
                                   &alphaMode) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.baseColorTextureUriLength,
                                   &baseColorTextureUri) ||
          !ReadNativePayloadString(stream, materialHeader.normalTextureUriLength,
                                   &normalTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.emissiveTextureUriLength,
                                   &emissiveTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.occlusionTextureUriLength,
                                   &occlusionTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.metalRoughTextureUriLength,
                                   &metalRoughTextureUri)) {
        return false;
      }

      Asset::Material material{};
      strncpy_s(material.name, name.c_str(), _TRUNCATE);
      std::copy(std::begin(materialHeader.baseColor),
                std::end(materialHeader.baseColor),
                std::begin(material.diffuseColor));
      std::copy(std::begin(materialHeader.emissiveColor),
                std::end(materialHeader.emissiveColor),
                std::begin(material.emissiveColor));
      material.emissiveIntensity = materialHeader.emissiveIntensity;
      material.roughness = materialHeader.roughness;
      material.metalness = materialHeader.metalness;
      material.specularWeight = materialHeader.specularWeight;
      material.ior = materialHeader.ior;
      material.transmissionWeight = materialHeader.transmissionWeight;
      std::copy(std::begin(materialHeader.transmissionColor),
                std::end(materialHeader.transmissionColor),
                std::begin(material.transmissionColor));
      material.coatWeight = materialHeader.coatWeight;
      material.coatRoughness = materialHeader.coatRoughness;
      material.thinWalled = materialHeader.thinWalled;
      material.translucency = materialHeader.translucency;
      material.uvScale[0] = materialHeader.uvScale[0];
      material.uvScale[1] = materialHeader.uvScale[1];
      material.uvOffset[0] = materialHeader.uvOffset[0];
      material.uvOffset[1] = materialHeader.uvOffset[1];
      material.triPlanarEnabled = materialHeader.triPlanarEnabled;
      material.triPlanarScale = materialHeader.triPlanarScale;
      material.triPlanarSharpness = materialHeader.triPlanarSharpness;
      material.triPlanarNormalStrength = materialHeader.triPlanarNormalStrength;
      material.doubleSided =
          (materialHeader.flags & kNativeMaterialFlagDoubleSided) != 0;
      material.alphaMode = alphaMode.empty() ? "OPAQUE" : alphaMode;
      material.invertRoughnessTexture =
          (materialHeader.flags & kNativeMaterialFlagInvertRoughnessTexture) !=
          0;
      if (!materialModel.empty()) {
        material.schemaVersion = Asset::Material::kSchemaVersionOpenPbrSubset;
      }

      material.diffuseTexture = AppendNativePayloadTexture(
          baseColorTextureUri, &textureIndicesByUri, outTextures,
          outTextureSourceUris);
      material.normalTexture = AppendNativePayloadTexture(
          normalTextureUri, &textureIndicesByUri, outTextures,
          outTextureSourceUris);
      material.emissiveTexture = AppendNativePayloadTexture(
          emissiveTextureUri, &textureIndicesByUri, outTextures,
          outTextureSourceUris);
      material.occlusionTexture = AppendNativePayloadTexture(
          occlusionTextureUri, &textureIndicesByUri, outTextures,
          outTextureSourceUris);
      material.metalRoughTexture = AppendNativePayloadTexture(
          metalRoughTextureUri, &textureIndicesByUri, outTextures,
          outTextureSourceUris);

      const size_t slot =
          static_cast<size_t>((std::max)(0, materialHeader.materialSlot));
      if (outMaterials->size() <= slot) {
        outMaterials->resize(slot + 1);
      }
      (*outMaterials)[slot] = std::move(material);
      if (outMaterialStableIds) {
        if (outMaterialStableIds->size() <= slot) {
          outMaterialStableIds->resize(slot + 1);
        }
        (*outMaterialStableIds)[slot] = std::move(materialStableId);
      }
    }
  }

  return !outMeshes->empty();
}

bool LoadNativeMaterialLibraryPayload(
    const std::string &path,
    std::vector<NativeMaterialLibraryRecord> *outRecords,
    std::unordered_map<std::string, NativeMaterialLibraryTextureBlob>
        *outTextureBlobs = nullptr) {
  if (!outRecords) {
    return false;
  }
  outRecords->clear();
  if (outTextureBlobs) {
    outTextureBlobs->clear();
  }

  const std::filesystem::path payloadPath = Utf8PathFromString(path);
  std::ifstream stream(payloadPath, std::ios::binary);
  if (!stream) {
    return false;
  }

  NativeMaterialLibraryHeader header;
  stream.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!stream || header.magic != 0x54414D50 ||
      (header.version != 1 && header.version != 2 && header.version != 3)) {
    return false;
  }

  if (header.version >= 2) {
    const uint32_t textureBlobCount = header.reserved;
    if (outTextureBlobs) {
      outTextureBlobs->reserve(textureBlobCount);
    }
    for (uint32_t blobIndex = 0; blobIndex < textureBlobCount; ++blobIndex) {
      NativeMaterialLibraryTextureBlobHeader blobHeader;
      stream.read(reinterpret_cast<char *>(&blobHeader), sizeof(blobHeader));
      if (!stream) {
        return false;
      }

      NativeMaterialLibraryTextureBlob blob;
      blob.encoding =
          static_cast<NativeMaterialLibraryTextureBlobEncoding>(
              blobHeader.encoding);
      blob.width = blobHeader.width;
      blob.height = blobHeader.height;
      if (!ReadNativePayloadString(stream, blobHeader.hashLength, &blob.hash)) {
        return false;
      }
      blob.data.resize(blobHeader.dataSize);
      if (!blob.data.empty()) {
        stream.read(reinterpret_cast<char *>(blob.data.data()),
                    static_cast<std::streamsize>(blob.data.size()));
        if (!stream) {
          return false;
        }
      }

      if (outTextureBlobs && !blob.hash.empty() && !blob.data.empty()) {
        outTextureBlobs->emplace(blob.hash, std::move(blob));
      }
    }
  }

  outRecords->reserve(header.materialCount);
  for (uint32_t materialIndex = 0; materialIndex < header.materialCount;
       ++materialIndex) {
    NativeMaterialLibraryRecord record;

    auto applyCommonFields = [&](auto const &materialHeader,
                                 uint32_t referenceCount) {
      record.payload.parametersChanged = true;
      record.payload.texturesChanged = true;
      std::copy(std::begin(materialHeader.baseColor),
                std::end(materialHeader.baseColor),
                std::begin(record.payload.baseColor));
      std::copy(std::begin(materialHeader.emissiveColor),
                std::end(materialHeader.emissiveColor),
                std::begin(record.payload.emissiveColor));
      record.payload.emissiveIntensity = materialHeader.emissiveIntensity;
      record.payload.roughness = materialHeader.roughness;
      record.payload.metalness = materialHeader.metalness;
      record.payload.specularWeight = materialHeader.specularWeight;
      record.payload.ior = materialHeader.ior;
      record.payload.transmissionWeight = materialHeader.transmissionWeight;
      std::copy(std::begin(materialHeader.transmissionColor),
                std::end(materialHeader.transmissionColor),
                std::begin(record.payload.transmissionColor));
      record.payload.coatWeight = materialHeader.coatWeight;
      record.payload.coatRoughness = materialHeader.coatRoughness;
      record.payload.thinWalled = materialHeader.thinWalled;
      record.payload.translucency = materialHeader.translucency;
      record.payload.uvScale[0] = materialHeader.uvScale[0];
      record.payload.uvScale[1] = materialHeader.uvScale[1];
      record.payload.uvOffset[0] = materialHeader.uvOffset[0];
      record.payload.uvOffset[1] = materialHeader.uvOffset[1];
      record.payload.triPlanarEnabled = materialHeader.triPlanarEnabled;
      record.payload.triPlanarScale = materialHeader.triPlanarScale;
      record.payload.triPlanarSharpness = materialHeader.triPlanarSharpness;
      record.payload.triPlanarNormalStrength =
          materialHeader.triPlanarNormalStrength;
      record.payload.doubleSided =
          (materialHeader.flags & kNativeMaterialFlagDoubleSided) != 0;
      record.payload.invertRoughnessTexture =
          (materialHeader.flags & kNativeMaterialFlagInvertRoughnessTexture) !=
          0;

      record.payload.references.reserve(referenceCount);
      for (uint32_t referenceIndex = 0; referenceIndex < referenceCount;
           ++referenceIndex) {
        NativeMaterialLibraryReferenceHeader referenceHeader;
        stream.read(reinterpret_cast<char *>(&referenceHeader),
                    sizeof(referenceHeader));
        if (!stream) {
          return false;
        }

        MaterialNodeReference reference;
        reference.materialSlot = referenceHeader.materialSlot;
        if (!ReadNativePayloadString(stream, referenceHeader.nodeObjectIdLength,
                                     &reference.nodeObjectId)) {
          return false;
        }
        if (!reference.nodeObjectId.empty()) {
          record.payload.references.push_back(std::move(reference));
        }
      }
      return true;
    };

    if (header.version == 1) {
      NativeMaterialLibraryMaterialHeader materialHeader;
      stream.read(reinterpret_cast<char *>(&materialHeader), sizeof(materialHeader));
      if (!stream) {
        return false;
      }

      if (!ReadNativePayloadString(stream, materialHeader.objectIdLength,
                                   &record.objectId) ||
          !ReadNativePayloadString(stream, materialHeader.nameLength,
                                   &record.payload.name) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.materialStableIdLength,
                                   &record.payload.materialStableId) ||
          !ReadNativePayloadString(stream, materialHeader.materialModelLength,
                                   &record.payload.materialModel) ||
          !ReadNativePayloadString(stream, materialHeader.alphaModeLength,
                                   &record.payload.alphaMode) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.baseColorTextureUriLength,
                                   &record.payload.baseColorTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.normalTextureUriLength,
                                   &record.payload.normalTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.emissiveTextureUriLength,
                                   &record.payload.emissiveTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.occlusionTextureUriLength,
                                   &record.payload.occlusionTextureUri) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.metalRoughTextureUriLength,
                                   &record.payload.metalRoughTextureUri)) {
        return false;
      }

      if (!applyCommonFields(materialHeader, materialHeader.referenceCount)) {
        return false;
      }
    } else if (header.version == 2) {
      NativeMaterialLibraryMaterialHeaderV2 materialHeader;
      stream.read(reinterpret_cast<char *>(&materialHeader), sizeof(materialHeader));
      if (!stream) {
        return false;
      }

      if (!ReadNativePayloadString(stream, materialHeader.objectIdLength,
                                   &record.objectId) ||
          !ReadNativePayloadString(stream, materialHeader.nameLength,
                                   &record.payload.name) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.materialStableIdLength,
                                   &record.payload.materialStableId) ||
          !ReadNativePayloadString(stream, materialHeader.materialModelLength,
                                   &record.payload.materialModel) ||
          !ReadNativePayloadString(stream, materialHeader.alphaModeLength,
                                   &record.payload.alphaMode) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.baseColorTextureUriLength,
                                   &record.payload.baseColorTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.baseColorTextureBlobHashLength,
              &record.payload.baseColorTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.normalTextureUriLength,
                                   &record.payload.normalTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.normalTextureBlobHashLength,
              &record.payload.normalTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.emissiveTextureUriLength,
                                   &record.payload.emissiveTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.emissiveTextureBlobHashLength,
              &record.payload.emissiveTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.occlusionTextureUriLength,
                                   &record.payload.occlusionTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.occlusionTextureBlobHashLength,
              &record.payload.occlusionTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.metalRoughTextureUriLength,
                                   &record.payload.metalRoughTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.metalRoughTextureBlobHashLength,
              &record.payload.metalRoughTextureBlobHash)) {
        return false;
      }

      if (!applyCommonFields(materialHeader, materialHeader.referenceCount)) {
        return false;
      }
    } else {
      NativeMaterialLibraryMaterialHeaderV3 materialHeader;
      stream.read(reinterpret_cast<char *>(&materialHeader), sizeof(materialHeader));
      if (!stream) {
        return false;
      }

      if (!ReadNativePayloadString(stream, materialHeader.objectIdLength,
                                   &record.objectId) ||
          !ReadNativePayloadString(stream, materialHeader.nameLength,
                                   &record.payload.name) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.materialStableIdLength,
                                   &record.payload.materialStableId) ||
          !ReadNativePayloadString(stream, materialHeader.materialModelLength,
                                   &record.payload.materialModel) ||
          !ReadNativePayloadString(stream, materialHeader.alphaModeLength,
                                   &record.payload.alphaMode) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.baseColorTextureUriLength,
                                   &record.payload.baseColorTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.baseColorTextureBlobHashLength,
              &record.payload.baseColorTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.opacityTextureUriLength,
                                   &record.payload.opacityTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.opacityTextureBlobHashLength,
              &record.payload.opacityTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.normalTextureUriLength,
                                   &record.payload.normalTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.normalTextureBlobHashLength,
              &record.payload.normalTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.coatNormalTextureUriLength,
                                   &record.payload.coatNormalTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.coatNormalTextureBlobHashLength,
              &record.payload.coatNormalTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.emissiveTextureUriLength,
                                   &record.payload.emissiveTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.emissiveTextureBlobHashLength,
              &record.payload.emissiveTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.occlusionTextureUriLength,
                                   &record.payload.occlusionTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.occlusionTextureBlobHashLength,
              &record.payload.occlusionTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.metalRoughTextureUriLength,
                                   &record.payload.metalRoughTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.metalRoughTextureBlobHashLength,
              &record.payload.metalRoughTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.specularColorTextureUriLength,
                                   &record.payload.specularColorTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.specularColorTextureBlobHashLength,
              &record.payload.specularColorTextureBlobHash) ||
          !ReadNativePayloadString(stream,
                                   materialHeader.thicknessTextureUriLength,
                                   &record.payload.thicknessTextureUri) ||
          !ReadNativePayloadString(
              stream, materialHeader.thicknessTextureBlobHashLength,
              &record.payload.thicknessTextureBlobHash)) {
        return false;
      }

      std::copy(std::begin(materialHeader.specularColor),
                std::end(materialHeader.specularColor),
                std::begin(record.payload.specularColor));
      record.payload.thickness = materialHeader.thickness;
      record.payload.attenuationDistance = materialHeader.attenuationDistance;
      record.payload.coatIor = materialHeader.coatIor;
      record.payload.anisotropy = materialHeader.anisotropy;
      record.payload.anisotropyRotation = materialHeader.anisotropyRotation;
      record.payload.sheenWeight = materialHeader.sheenWeight;
      std::copy(std::begin(materialHeader.sheenColor),
                std::end(materialHeader.sheenColor),
                std::begin(record.payload.sheenColor));
      record.payload.opacityTextureAmount = materialHeader.opacityTextureAmount;
      record.payload.coatNormalTextureAmount =
          materialHeader.coatNormalTextureAmount;
      record.payload.specularColorTextureAmount =
          materialHeader.specularColorTextureAmount;
      record.payload.thicknessTextureAmount =
          materialHeader.thicknessTextureAmount;
      record.payload.alphaCutoff = materialHeader.alphaCutoff;

      if (!applyCommonFields(materialHeader, materialHeader.referenceCount)) {
        return false;
      }
    }

    outRecords->push_back(std::move(record));
  }

  return true;
}

void Normalize3(float value[3], const float fallback[3]) {
  const float lenSq = value[0] * value[0] + value[1] * value[1] +
                      value[2] * value[2];
  if (lenSq <= 1.0e-12f) {
    value[0] = fallback[0];
    value[1] = fallback[1];
    value[2] = fallback[2];
    return;
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  value[0] *= invLen;
  value[1] *= invLen;
  value[2] *= invLen;
}

bool CameraPayloadChanged(const CameraChangedPayload &lhs,
                          const CameraChangedPayload &rhs) {
  constexpr float kPositionEpsilon = 0.001f;
  constexpr float kDirectionEpsilon = 0.0005f;
  constexpr float kScalarEpsilon = 0.001f;
  auto changedArray3 = [](const std::array<float, 3> &a,
                          const std::array<float, 3> &b,
                          float epsilon) {
    return fabsf(a[0] - b[0]) > epsilon || fabsf(a[1] - b[1]) > epsilon ||
           fabsf(a[2] - b[2]) > epsilon;
  };
  return changedArray3(lhs.position, rhs.position, kPositionEpsilon) ||
         lhs.displayName != rhs.displayName ||
         changedArray3(lhs.forward, rhs.forward, kDirectionEpsilon) ||
         changedArray3(lhs.up, rhs.up, kDirectionEpsilon) ||
         fabsf(lhs.fovDegrees - rhs.fovDegrees) > kScalarEpsilon ||
         fabsf(lhs.nearPlane - rhs.nearPlane) > kScalarEpsilon ||
         fabsf(lhs.farPlane - rhs.farPlane) > kScalarEpsilon;
}

SavedViews::SavedView BuildExternalSavedView(const SceneDelta &delta,
                                             const CameraChangedPayload &payload,
                                             const std::string &sessionId) {
  SavedViews::SavedView view = SavedViews::CaptureCurrentState();
  view.name = ResolveNodeName(delta, payload.displayName);
  view.sourceSessionId = sessionId;
  view.sourceObjectId = delta.target.objectId;
  view.external = true;
  view.thumbnailRgba.clear();
  view.thumbnailWidth = 0;
  view.thumbnailHeight = 0;

  view.pos[0] = payload.position[0];
  view.pos[1] = payload.position[1];
  view.pos[2] = payload.position[2];
  view.forward[0] = payload.forward[0];
  view.forward[1] = payload.forward[1];
  view.forward[2] = payload.forward[2];
  view.up[0] = payload.up[0];
  view.up[1] = payload.up[1];
  view.up[2] = payload.up[2];
  const float fallbackForward[3] = {0.0f, 0.0f, 1.0f};
  const float fallbackUp[3] = {0.0f, 1.0f, 0.0f};
  Normalize3(view.forward, fallbackForward);
  Normalize3(view.up, fallbackUp);
  const float dot = view.forward[0] * view.up[0] +
                    view.forward[1] * view.up[1] +
                    view.forward[2] * view.up[2];
  view.up[0] -= dot * view.forward[0];
  view.up[1] -= dot * view.forward[1];
  view.up[2] -= dot * view.forward[2];
  Normalize3(view.up, fallbackUp);
  view.fov = payload.fovDegrees;
  view.nearZ = payload.nearPlane;
  view.farZ = payload.farPlane;
  view.yaw = atan2f(view.forward[0], -view.forward[2]);
  view.pitch = asinf(std::clamp(view.forward[1], -1.0f, 1.0f));
  return view;
}

LightType ParseEngineLightType(std::string_view value) {
  if (value == "Directional") {
    return LightType::Directional;
  }
  if (value == "Spot") {
    return LightType::Spot;
  }
  if (value == "AreaRect") {
    return LightType::AreaRect;
  }
  if (value == "AreaDisk") {
    return LightType::AreaDisk;
  }
  if (value == "IES") {
    return LightType::IES;
  }
  return LightType::Omni;
}

std::string BuildLiveLinkMaterialName(const std::string &nodeObjectId,
                                      int materialSlot) {
  return std::string("material:") + nodeObjectId + ":slot:" +
         std::to_string((std::max)(0, materialSlot));
}


} // namespace

LiveLinkSceneSync &GetSceneSync() {
  static LiveLinkSceneSync s_sceneSync;
  return s_sceneSync;
}

void LiveLinkSceneSync::DetachCameraControl() { m_cameraControlDetached = true; }

void LiveLinkSceneSync::ResumeCameraControl() {
  m_cameraControlDetached = false;
  ApplyCachedCameraState(m_cachedExternalCamera);
}

bool LiveLinkSceneSync::IsCameraControlDetached() const {
  return m_cameraControlDetached;
}

LiveLinkSceneSync::StatsSnapshot LiveLinkSceneSync::GetStatsSnapshot() const {
  StatsSnapshot stats;

  const auto &nodes = Scene::GetNodes();
  for (const Scene::Node &node : nodes) {
    if (!node.liveLinkManaged) {
      continue;
    }
    ++stats.nodeCount;
    stats.meshCount += node.meshIndices.size();
  }

  const size_t lightLimit = Scene::GetLights().size();
  const size_t materialLimit = Scene::GetMaterialCount();
  for (const auto &[_, binding] : m_bindings) {
    if (binding.objectId.Empty() ||
        binding.handleKind == EngineHandleKind::Unknown) {
      continue;
    }

    ++stats.totalBindingCount;
    if (!binding.sessionId.empty()) {
      ++stats.activeSessionBindingCount;
    }

    switch (binding.handleKind) {
    case EngineHandleKind::SceneLight:
      if (binding.handleIndex < lightLimit) {
        ++stats.lightCount;
      }
      break;
    case EngineHandleKind::SceneMaterial:
      if (binding.handleIndex < materialLimit) {
        ++stats.materialCount;
      }
      break;
    case EngineHandleKind::MainCamera:
      stats.cameraBound = true;
      break;
    case EngineHandleKind::Environment:
      stats.environmentBound = true;
      break;
    case EngineHandleKind::SceneNode:
    case EngineHandleKind::Unknown:
      break;
    }
  }

  return stats;
}

std::vector<LiveLinkSceneSync::PersistedBinding>
LiveLinkSceneSync::ExportPersistedBindings() const {
  std::vector<PersistedBinding> bindings;
  bindings.reserve(m_bindings.size());
  for (const auto &[_, binding] : m_bindings) {
    if (binding.objectId.Empty() ||
        binding.handleKind == EngineHandleKind::Unknown) {
      continue;
    }

    PersistedBinding persistedBinding;
    persistedBinding.objectId = binding.objectId;
    persistedBinding.handleKind = binding.handleKind;
    persistedBinding.handleIndex = binding.handleIndex;
    bindings.push_back(std::move(persistedBinding));
  }
  return bindings;
}

void LiveLinkSceneSync::RestorePersistedBindings(
    const std::vector<PersistedBinding> &bindings) {
  ClearAllBindings();
  for (const PersistedBinding &persistedBinding : bindings) {
    if (persistedBinding.objectId.Empty() ||
        persistedBinding.handleKind == EngineHandleKind::Unknown) {
      continue;
    }

    ObjectBinding &binding = m_bindings[persistedBinding.objectId];
    binding.objectId = persistedBinding.objectId;
    binding.handleKind = persistedBinding.handleKind;
    binding.handleIndex = persistedBinding.handleIndex;
    binding.sessionId.clear();
    binding.lastAppliedRevision = 0;
  }
}

std::vector<LiveLinkDiagnosticEntry>
LiveLinkSceneSync::GetRecentDiagnostics() const {
  return m_recentDiagnostics;
}

void LiveLinkSceneSync::ApplyQueuedBatches(LiveLinkCoordinator &coordinator) {
  std::vector<ValidationIssue> issues = coordinator.ConsumeValidationIssues();
  for (const ValidationIssue &issue : issues) {
    LogValidationIssue(issue);
  }

  std::vector<SceneDeltaBatch> batches = coordinator.ConsumeQueuedBatches();
  ScopedSceneBatchUpdates scopedBatchUpdates;
  for (const SceneDeltaBatch &batch : batches) {
    ApplyBatch(batch);
  }
}

bool LiveLinkSceneSync::ApplyBatch(const SceneDeltaBatch &batch) {
  ScopedSceneBatchUpdates scopedBatchUpdates;
  bool appliedAny = false;
  for (const SceneDelta &delta : batch.deltas) {
    appliedAny = ApplyDelta(batch, delta) || appliedAny;
  }
  return appliedAny;
}

bool LiveLinkSceneSync::ApplyDelta(const SceneDeltaBatch &batch,
                                   const SceneDelta &delta) {
  switch (delta.kind) {
  case SceneDeltaKind::SessionOpened:
    return ApplySessionOpened(batch, delta);
  case SceneDeltaKind::SessionClosed:
    return ApplySessionClosed(batch, delta);
  case SceneDeltaKind::FullSceneSync:
    return ApplyFullSceneSync(batch, delta);
  case SceneDeltaKind::NodeAdded:
    return ApplyNodeAdded(batch, delta);
  case SceneDeltaKind::NodeRemoved:
    return ApplyNodeRemoved(batch, delta);
  case SceneDeltaKind::NodeTransformChanged:
    return ApplyNodeTransformChanged(batch, delta);
  case SceneDeltaKind::NodeVisibilityChanged:
    return ApplyNodeVisibilityChanged(batch, delta);
  case SceneDeltaKind::MeshPayloadChanged:
    return ApplyMeshPayloadChanged(batch, delta);
  case SceneDeltaKind::MaterialLibraryChanged:
    return ApplyMaterialLibraryChanged(batch, delta);
  case SceneDeltaKind::MaterialChanged:
    return ApplyMaterialChanged(batch, delta);
  case SceneDeltaKind::LightChanged:
    return ApplyLightChanged(batch, delta);
  case SceneDeltaKind::SelectionChanged:
    return ApplySelectionChanged(batch, delta);
  case SceneDeltaKind::CameraChanged:
    return ApplyCameraChanged(batch, delta);
  case SceneDeltaKind::EnvironmentChanged:
    return ApplyEnvironmentChanged(batch, delta);
  case SceneDeltaKind::Unknown:
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Ignoring unknown delta kind");
    return false;
  }

  return false;
}

bool LiveLinkSceneSync::ApplySessionOpened(const SceneDeltaBatch &batch,
                                           const SceneDelta &delta) {
  m_cameraControlDetached = false;
  const SessionOpenedPayload *payload = FindPayload<SessionOpenedPayload>(delta);
  if (payload && !payload->displayName.empty()) {
    fprintf(stderr, "LiveLink: session opened provider='%s' session='%s' document='%s'\n",
            batch.providerName.c_str(), batch.sessionId.c_str(),
            payload->displayName.c_str());
  }
  return true;
}

bool LiveLinkSceneSync::ApplySessionClosed(const SceneDeltaBatch &batch,
                                           const SceneDelta &delta) {
  RemoveSessionContent(batch.sessionId);
  m_cameraControlDetached = false;
  if (m_cachedExternalCamera.valid &&
      m_cachedExternalCamera.sessionId == batch.sessionId) {
    m_cachedExternalCamera = CachedCameraState{};
  }
  const SessionClosedPayload *payload = FindPayload<SessionClosedPayload>(delta);
  if (payload && !payload->reason.empty()) {
    fprintf(stderr, "LiveLink: session closed provider='%s' session='%s' reason='%s'\n",
            batch.providerName.c_str(), batch.sessionId.c_str(),
            payload->reason.c_str());
  }
  return true;
}

bool LiveLinkSceneSync::ApplyFullSceneSync(const SceneDeltaBatch &batch,
                                           const SceneDelta &delta) {
  const FullSceneSyncPayload *payload = FindPayload<FullSceneSyncPayload>(delta);
  if (payload && payload->clearsExistingScene) {
    Scene::ResetScene();
    Scene::SelectNode(kInvalidHandle);
    ClearAllBindings();
    m_cachedExternalCamera = CachedCameraState{};
    m_cameraControlDetached = false;
    fprintf(stderr,
            "LiveLink: full scene sync reset provider='%s' session='%s'\n",
            batch.providerName.c_str(), batch.sessionId.c_str());
  }
  return true;
}

bool LiveLinkSceneSync::ApplyNodeAdded(const SceneDeltaBatch &batch,
                                       const SceneDelta &delta) {
  const NodeAddedPayload *payload = FindPayload<NodeAddedPayload>(delta);
  ObjectBinding *binding = FindBinding(delta.target);
  const std::string preferredName =
      ResolveNodeName(delta, payload ? payload->displayName : std::string_view{});

  if (!EnsureNodeBinding(batch, delta, preferredName, &binding) || !binding) {
    return false;
  }

  if (binding->handleIndex != kInvalidHandle) {
    Scene::RenameNode(binding->handleIndex, preferredName);
    size_t parentIndex = kInvalidHandle;
    if (payload && !payload->parentObjectId.empty()) {
      ObjectId parentObjectId = delta.target;
      parentObjectId.objectId = payload->parentObjectId;
      parentObjectId.objectType = ObjectType::Node;
      if (const ObjectBinding *parentBinding = FindBinding(parentObjectId)) {
        if (parentBinding->handleKind == EngineHandleKind::SceneNode &&
            parentBinding->handleIndex < Scene::GetNodes().size()) {
          parentIndex = parentBinding->handleIndex;
        }
      }
    }
    Scene::SetNodeParent(binding->handleIndex, parentIndex);
  }
  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplyNodeRemoved(const SceneDeltaBatch &batch,
                                         const SceneDelta &delta) {
  const NodeRemovedPayload *payload = FindPayload<NodeRemovedPayload>(delta);
  ObjectBinding *binding = FindBinding(delta.target);
  if (!binding) {
    return true;
  }

  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (binding->handleKind != EngineHandleKind::SceneNode ||
      binding->handleIndex == kInvalidHandle) {
    if (binding->handleKind == EngineHandleKind::SceneLight &&
        binding->handleIndex != kInvalidHandle &&
        binding->handleIndex < Scene::GetLights().size()) {
      const size_t removedLightIndex = binding->handleIndex;
      Scene::RemoveLight(removedLightIndex);
      m_bindings.erase(delta.target);
      ReindexSceneLightBindingsAfterRemoval(removedLightIndex);
      return true;
    }
    if (binding->handleKind == EngineHandleKind::SavedView &&
        binding->handleIndex != kInvalidHandle) {
      const size_t removedViewIndex = binding->handleIndex;
      SavedViews::RemoveExternalView(batch.sessionId, delta.target.objectId);
      m_bindings.erase(delta.target);
      ReindexSavedViewBindingsAfterRemoval(removedViewIndex);
      return true;
    }
    m_bindings.erase(delta.target);
    return true;
  }

  const size_t removedIndex = binding->handleIndex;
  if (removedIndex >= Scene::GetNodes().size()) {
    m_bindings.erase(delta.target);
    return true;
  }

  const bool removeChildren = payload ? payload->removeChildren : true;
  const size_t removedParentIndex = Scene::GetNodes()[removedIndex].parentIndex;

  std::vector<size_t> removedNodeIndices;
  removedNodeIndices.push_back(removedIndex);
  if (removeChildren) {
    for (size_t nodeIndex = 0; nodeIndex < Scene::GetNodes().size(); ++nodeIndex) {
      if (nodeIndex == removedIndex) {
        continue;
      }
      size_t parentIndex = Scene::GetNodes()[nodeIndex].parentIndex;
      while (parentIndex != kInvalidHandle && parentIndex < Scene::GetNodes().size()) {
        if (parentIndex == removedIndex) {
          removedNodeIndices.push_back(nodeIndex);
          break;
        }
        parentIndex = Scene::GetNodes()[parentIndex].parentIndex;
      }
    }
  } else {
    for (size_t nodeIndex = 0; nodeIndex < Scene::GetNodes().size(); ++nodeIndex) {
      if (Scene::GetNodes()[nodeIndex].parentIndex == removedIndex) {
        Scene::SetNodeParent(nodeIndex, removedParentIndex);
      }
    }
  }

  std::sort(removedNodeIndices.begin(), removedNodeIndices.end());
  removedNodeIndices.erase(
      std::unique(removedNodeIndices.begin(), removedNodeIndices.end()),
      removedNodeIndices.end());

  std::vector<ObjectId> bindingsToErase;
  std::vector<std::string> removedNodeObjectIds;
  for (const auto &[objectId, existingBinding] : m_bindings) {
    if (existingBinding.handleKind != EngineHandleKind::SceneNode ||
        existingBinding.handleIndex == kInvalidHandle) {
      continue;
    }
    if (!std::binary_search(removedNodeIndices.begin(), removedNodeIndices.end(),
                            existingBinding.handleIndex)) {
      continue;
    }
    bindingsToErase.push_back(objectId);
    removedNodeObjectIds.push_back(objectId.objectId);
  }

  for (const auto &[objectId, existingBinding] : m_bindings) {
    if (existingBinding.handleKind != EngineHandleKind::SceneMaterial) {
      continue;
    }
    for (const std::string &removedNodeObjectId : removedNodeObjectIds) {
      const std::string prefix = std::string("material:") + removedNodeObjectId + ":";
      if (objectId.objectId.rfind(prefix, 0) == 0) {
        bindingsToErase.push_back(objectId);
        break;
      }
    }
  }

  for (const ObjectId &objectId : bindingsToErase) {
    m_bindings.erase(objectId);
  }

  std::sort(removedNodeIndices.rbegin(), removedNodeIndices.rend());
  for (size_t nodeIndex : removedNodeIndices) {
    if (nodeIndex >= Scene::GetNodes().size()) {
      continue;
    }
    Scene::RemoveNode(nodeIndex);
    ReindexSceneNodeBindingsAfterRemoval(nodeIndex);
  }

  return true;
}

bool LiveLinkSceneSync::ApplyNodeTransformChanged(const SceneDeltaBatch &batch,
                                                  const SceneDelta &delta) {
  const NodeTransformPayload *payload = FindPayload<NodeTransformPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "NodeTransformChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (!EnsureNodeBinding(batch, delta, ResolveNodeName(delta, {}), &binding) ||
      !binding || binding->handleIndex == kInvalidHandle) {
    return false;
  }

  if (!Scene::UpdateNodeTransform(binding->handleIndex,
                                  payload->worldMatrix.data())) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to apply node transform");
    return false;
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplyNodeVisibilityChanged(const SceneDeltaBatch &batch,
                                                   const SceneDelta &delta) {
  const NodeVisibilityPayload *payload = FindPayload<NodeVisibilityPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "NodeVisibilityChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (!EnsureNodeBinding(batch, delta, ResolveNodeName(delta, {}), &binding) ||
      !binding || binding->handleIndex == kInvalidHandle) {
    return false;
  }

  if (!Scene::SetNodeVisibility(binding->handleIndex, payload->visible)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to apply node visibility");
    return false;
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplyMeshPayloadChanged(const SceneDeltaBatch &batch,
                                                const SceneDelta &delta) {
  const MeshPayloadChangedPayload *payload =
      FindPayload<MeshPayloadChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "MeshPayloadChanged missing payload");
    return false;
  }
  if (payload->payloadUri.empty()) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "MeshPayloadChanged missing payload URI");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (!binding) {
    binding = FindRelatedBinding(delta.target, EngineHandleKind::SceneNode);
  }
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }
  if (!binding || binding->handleKind != EngineHandleKind::SceneNode ||
      binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetNodes().size()) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Mesh payload target is not bound to a scene node");
    return false;
  }

  std::vector<Asset::GpuMesh> meshes;
  std::vector<Asset::Material> materials;
  std::vector<std::string> materialStableIds;
  std::vector<Asset::Texture> textures;
  std::vector<std::string> textureSourceUris;
  bool meshPayloadHasFullMaterialDefinitions = true;
  const std::filesystem::path payloadPath =
      Utf8PathFromString(payload->payloadUri);
  const std::string extension = payloadPath.extension().string();
  const bool loaded = extension == ".prmesh"
                          ? LoadNativeMeshPayload(payload->payloadUri, &meshes,
                                                  &materials,
                                                  &materialStableIds,
                                                  &textures,
                                                  &textureSourceUris,
                                                  &meshPayloadHasFullMaterialDefinitions)
                          : Asset::LoadModel(payload->payloadUri, meshes,
                                             &materials, &textures);
  if (!loaded) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  std::string("Failed to load mesh payload: ") +
                      payload->payloadUri);
    return false;
  }
  if (meshes.empty()) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Loaded mesh payload contained no meshes");
    return false;
  }

  if (extension == ".prmesh" && materials.empty()) {
    int maxMaterialSlot = -1;
    for (Asset::GpuMesh &mesh : meshes) {
      maxMaterialSlot = (std::max)(maxMaterialSlot, mesh.materialIndex);
    }

    if (maxMaterialSlot >= 0) {
      materials.resize(static_cast<size_t>(maxMaterialSlot) + 1);
      for (int materialSlot = 0; materialSlot <= maxMaterialSlot; ++materialSlot) {
        Asset::Material &material = materials[static_cast<size_t>(materialSlot)];
        const std::string materialName =
            BuildLiveLinkMaterialName(delta.target.objectId, materialSlot);
        strncpy_s(material.name, materialName.c_str(), _TRUNCATE);
      }
    }
  }

  Scene::ImportedNodePayload importedPayload;
  importedPayload.sourcePath = payload->payloadUri;
  importedPayload.displayName = ResolveNodeName(delta, delta.debugLabel);
  importedPayload.meshes = std::move(meshes);
  importedPayload.materials = std::move(materials);
  importedPayload.materialStableIds = std::move(materialStableIds);
  importedPayload.textures = std::move(textures);
  importedPayload.textureSourceUris = std::move(textureSourceUris);
  importedPayload.materialsContainFullDefinitions =
      extension != ".prmesh" || meshPayloadHasFullMaterialDefinitions;

  if (!Scene::ReplaceNodeImportedContent(binding->handleIndex,
                                         std::move(importedPayload))) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to replace node content from mesh payload");
    return false;
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplyMaterialLibraryChanged(const SceneDeltaBatch &batch,
                                                    const SceneDelta &delta) {
  const MaterialLibraryChangedPayload *payload =
      FindPayload<MaterialLibraryChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "MaterialLibraryChanged missing payload");
    return false;
  }
  if (payload->payloadUri.empty()) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "MaterialLibraryChanged missing payload URI");
    return false;
  }

  std::vector<NativeMaterialLibraryRecord> records;
  std::unordered_map<std::string, NativeMaterialLibraryTextureBlob> textureBlobs;
  if (!LoadNativeMaterialLibraryPayload(payload->payloadUri, &records,
                                        &textureBlobs)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  std::string("Failed to load material library payload: ") +
                      payload->payloadUri);
    return false;
  }

  PruneTextureCacheEntries();
  for (const auto &[blobHash, blob] : textureBlobs) {
    if (blobHash.empty()) {
      continue;
    }
    const auto cached = m_textureIndicesByBlobHash.find(blobHash);
    if (cached != m_textureIndicesByBlobHash.end()) {
      continue;
    }

    Asset::Texture texture;
    switch (blob.encoding) {
    case NativeMaterialLibraryTextureBlobEncoding::RawRgba8:
      if (blob.width == 0 || blob.height == 0) {
        continue;
      }
      texture = Asset::LoadTextureFromMemory(blob.data.data(),
                                             static_cast<int>(blob.width),
                                             static_cast<int>(blob.height),
                                             DXGI_FORMAT_R8G8B8A8_UNORM);
      break;
    case NativeMaterialLibraryTextureBlobEncoding::RawRgba32Float:
      if (blob.width == 0 || blob.height == 0) {
        continue;
      }
      texture = Asset::LoadTextureFromMemory(blob.data.data(),
                                             static_cast<int>(blob.width),
                                             static_cast<int>(blob.height),
                                             DXGI_FORMAT_R32G32B32A32_FLOAT);
      break;
    case NativeMaterialLibraryTextureBlobEncoding::EncodedHdr:
      texture =
          Asset::LoadTextureFromEncodedMemory(blob.data.data(), blob.data.size(),
                                              true);
      break;
    case NativeMaterialLibraryTextureBlobEncoding::EncodedLdr:
    default:
      texture =
          Asset::LoadTextureFromEncodedMemory(blob.data.data(), blob.data.size(),
                                              false);
      break;
    }

    if (!texture.resource) {
      fprintf(stderr,
              "LiveLink: failed to decode embedded material texture '%s'\n",
              blobHash.c_str());
      continue;
    }

    const int textureIndex = Scene::AddTexture(std::move(texture));
    if (textureIndex >= 0) {
      m_textureIndicesByBlobHash.emplace(blobHash, textureIndex);
    }
  }

  bool appliedAny = false;
  for (const NativeMaterialLibraryRecord &record : records) {
    if (record.objectId.empty()) {
      continue;
    }

    SceneDelta materialDelta;
    materialDelta.kind = SceneDeltaKind::MaterialChanged;
    materialDelta.target = delta.target;
    materialDelta.target.objectId = record.objectId;
    materialDelta.target.objectType = ObjectType::Material;
    materialDelta.revision = delta.revision;
    materialDelta.debugLabel = record.payload.name;
    materialDelta.payload = record.payload;

    appliedAny = ApplyMaterialChanged(batch, materialDelta) || appliedAny;
  }

  return appliedAny;
}

bool LiveLinkSceneSync::ApplyMaterialChanged(const SceneDeltaBatch &batch,
                                             const SceneDelta &delta) {
  const MaterialChangedPayload *payload =
      FindPayload<MaterialChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "MaterialChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (!EnsureMaterialBinding(batch, delta, &binding) || !binding ||
      binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetMaterialCount()) {
    return false;
  }

  const std::string materialName = ResolveMaterialDisplayName(delta, payload);
  if (!payload->materialStableId.empty()) {
    Scene::SetMaterialStableId(binding->handleIndex, payload->materialStableId);
  }

  auto resolveReference = [&](const std::string &nodeObjectIdValue,
                              int materialSlotValue, size_t *outNodeIndex,
                              size_t *outMaterialSlot) {
    if (!outNodeIndex || !outMaterialSlot || nodeObjectIdValue.empty()) {
      return false;
    }

    ObjectId nodeObjectId = delta.target;
    nodeObjectId.objectId = nodeObjectIdValue;
    nodeObjectId.objectType = ObjectType::Node;
    const ObjectBinding *nodeBinding = FindBinding(nodeObjectId);
    if (!nodeBinding || nodeBinding->handleKind != EngineHandleKind::SceneNode ||
        nodeBinding->handleIndex >= Scene::GetNodes().size()) {
      return false;
    }

    const size_t materialSlot =
        static_cast<size_t>((std::max)(0, materialSlotValue));
    *outNodeIndex = nodeBinding->handleIndex;
    *outMaterialSlot = materialSlot;
    return true;
  };

  auto rebindReference = [&](const std::string &nodeObjectIdValue,
                             int materialSlotValue) {
    size_t nodeIndex = kInvalidHandle;
    size_t materialSlot = 0;
    if (!resolveReference(nodeObjectIdValue, materialSlotValue, &nodeIndex,
                          &materialSlot)) {
      return;
    }

    Scene::RebindNodeMaterialSlot(nodeIndex, materialSlot,
                                  static_cast<int>(binding->handleIndex));
  };

  auto updateReferenceName = [&](const std::string &nodeObjectIdValue,
                                 int materialSlotValue) {
    size_t nodeIndex = kInvalidHandle;
    size_t materialSlot = 0;
    if (!resolveReference(nodeObjectIdValue, materialSlotValue, &nodeIndex,
                          &materialSlot)) {
      return;
    }

    Scene::UpdateNodeMaterialSourceName(nodeIndex, materialSlot, materialName);
  };

  if (!payload->references.empty()) {
    for (const MaterialNodeReference &reference : payload->references) {
      rebindReference(reference.nodeObjectId, reference.materialSlot);
    }
  } else if (!payload->nodeObjectId.empty()) {
    rebindReference(payload->nodeObjectId, payload->materialSlot);
  }

  Asset::Material material;
  if (!Scene::GetMaterial(binding->handleIndex, &material)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to fetch bound material");
    return false;
  }

  PruneTextureCacheEntries();

  auto resolveEmbeddedOrUriTextureIndex =
      [this](const std::string &textureBlobHash,
             const std::string &textureUri) {
        if (!textureBlobHash.empty()) {
          const auto cachedBlob = m_textureIndicesByBlobHash.find(textureBlobHash);
          if (cachedBlob != m_textureIndicesByBlobHash.end()) {
            return cachedBlob->second;
          }
        }

        if (textureUri.empty()) {
          return -1;
        }

        const auto cachedUri = m_textureIndicesByUri.find(textureUri);
        if (cachedUri != m_textureIndicesByUri.end()) {
          return cachedUri->second;
        }

        const int textureIndex =
            Scene::AddTextureFromFile(textureUri, IsHdrTextureUri(textureUri));
        if (textureIndex >= 0) {
          m_textureIndicesByUri.emplace(textureUri, textureIndex);
        } else {
          fprintf(stderr,
                  "LiveLink: failed to bind material texture '%s'\n",
                  textureUri.c_str());
        }
        return textureIndex;
      };
      MaterialLiveLink::ApplyPayloadToMaterial(*payload,
                           resolveEmbeddedOrUriTextureIndex,
                           &material);

  strncpy_s(material.name, materialName.c_str(), _TRUNCATE);

  if (!Scene::UpdateMaterial(binding->handleIndex, material)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to apply material change");
    return false;
  }

  if (!payload->references.empty()) {
    for (const MaterialNodeReference &reference : payload->references) {
      updateReferenceName(reference.nodeObjectId, reference.materialSlot);
    }
  } else if (!payload->nodeObjectId.empty()) {
    updateReferenceName(payload->nodeObjectId, payload->materialSlot);
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplyLightChanged(const SceneDeltaBatch &batch,
                                          const SceneDelta &delta) {
  const LightChangedPayload *payload = FindPayload<LightChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "LightChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (!EnsureLightBinding(batch, delta, &binding) || !binding ||
      binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetLights().size()) {
    return false;
  }

  Light light = Scene::GetLights()[binding->handleIndex];
  light.type = static_cast<uint32_t>(ParseEngineLightType(payload->lightType));
  light.position[0] = payload->position[0];
  light.position[1] = payload->position[1];
  light.position[2] = payload->position[2];
  light.emission[0] = payload->color[0] * payload->intensity;
  light.emission[1] = payload->color[1] * payload->intensity;
  light.emission[2] = payload->color[2] * payload->intensity;
  light.direction[0] = payload->direction[0];
  light.direction[1] = payload->direction[1];
  light.direction[2] = payload->direction[2];
  {
    const float fallbackDirection[3] = {0.0f, -1.0f, 0.0f};
    Normalize3(light.direction, fallbackDirection);
  }
  light.radius = payload->radius;
  light.innerConeAngle =
      cosf(DirectX::XMConvertToRadians(payload->innerConeDegrees));
  light.outerConeAngle =
      cosf(DirectX::XMConvertToRadians(payload->outerConeDegrees));
  light.areaExtents[0] = payload->areaExtents[0];
  light.areaExtents[1] = payload->areaExtents[1];

  if (!Scene::UpdateLight(binding->handleIndex, light)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to apply light change");
    return false;
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
}

bool LiveLinkSceneSync::ApplySelectionChanged(const SceneDeltaBatch &batch,
                                              const SceneDelta &delta) {
  const SelectionChangedPayload *payload = FindPayload<SelectionChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "SelectionChanged missing payload");
    return false;
  }

  if (payload->selectedObjectIds.empty()) {
    Scene::SelectNode(kInvalidHandle);
    return true;
  }

  for (const std::string &selectedObjectId : payload->selectedObjectIds) {
    for (const auto &[_, binding] : m_bindings) {
      if (binding.sessionId != batch.sessionId ||
          binding.handleKind != EngineHandleKind::SceneNode ||
          binding.handleIndex == kInvalidHandle ||
          binding.objectId.objectId != selectedObjectId) {
        continue;
      }
      if (binding.handleIndex < Scene::GetNodes().size()) {
        Scene::SelectNode(binding.handleIndex);
        return true;
      }
    }
  }

  Scene::SelectNode(kInvalidHandle);
  return true;
}

bool LiveLinkSceneSync::ApplyCameraChanged(const SceneDeltaBatch &batch,
                                           const SceneDelta &delta) {
  const CameraChangedPayload *payload = FindPayload<CameraChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "CameraChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  if (delta.target.objectId != "camera:active") {
    SavedViews::SavedView externalView =
        BuildExternalSavedView(delta, *payload, batch.sessionId);
    const size_t savedViewIndex = SavedViews::UpsertExternalView(externalView);
    ObjectBinding &savedViewBinding =
        binding ? *binding
                : BindObject(delta.target, batch.sessionId,
                             EngineHandleKind::SavedView, savedViewIndex);
    savedViewBinding.sessionId = batch.sessionId;
    savedViewBinding.handleKind = EngineHandleKind::SavedView;
    savedViewBinding.handleIndex = savedViewIndex;
    savedViewBinding.lastAppliedRevision = delta.revision;
    return true;
  }

  ObjectBinding &cameraBinding =
      binding ? *binding
              : BindObject(delta.target, batch.sessionId,
                           EngineHandleKind::MainCamera, kInvalidHandle);
  if (cameraBinding.sessionId != batch.sessionId) {
    cameraBinding.lastAppliedRevision = 0;
  }
  cameraBinding.sessionId = batch.sessionId;
  const CachedCameraState previousCameraState = m_cachedExternalCamera;
  m_cachedExternalCamera.valid = true;
  m_cachedExternalCamera.objectId = delta.target;
  m_cachedExternalCamera.sessionId = batch.sessionId;
  m_cachedExternalCamera.revision = delta.revision;
  m_cachedExternalCamera.payload = *payload;

  if (m_cameraControlDetached) {
    const bool sameExternalCamera =
        previousCameraState.valid &&
        previousCameraState.sessionId == batch.sessionId &&
        previousCameraState.objectId == delta.target;
    const bool dccCameraMoved =
        !sameExternalCamera ||
        CameraPayloadChanged(previousCameraState.payload, *payload);
    if (dccCameraMoved) {
      m_cameraControlDetached = false;
      ApplyCachedCameraState(m_cachedExternalCamera);
    }
    cameraBinding.lastAppliedRevision = delta.revision;
    return true;
  }

  ApplyCachedCameraState(m_cachedExternalCamera);
  cameraBinding.lastAppliedRevision = delta.revision;
  return true;
}

void LiveLinkSceneSync::ApplyCachedCameraState(const CachedCameraState &state) {
  if (!state.valid) {
    return;
  }

  const CameraChangedPayload &payload = state.payload;
  g_cameraData.pos[0] = payload.position[0];
  g_cameraData.pos[1] = payload.position[1];
  g_cameraData.pos[2] = payload.position[2];
  g_cameraData.forward[0] = payload.forward[0];
  g_cameraData.forward[1] = payload.forward[1];
  g_cameraData.forward[2] = payload.forward[2];
  g_cameraData.up[0] = payload.up[0];
  g_cameraData.up[1] = payload.up[1];
  g_cameraData.up[2] = payload.up[2];
  const float fallbackForward[3] = {0.0f, 0.0f, 1.0f};
  const float fallbackUp[3] = {0.0f, 1.0f, 0.0f};
  Normalize3(g_cameraData.forward, fallbackForward);
  Normalize3(g_cameraData.up, fallbackUp);
  const float dot = g_cameraData.forward[0] * g_cameraData.up[0] +
                    g_cameraData.forward[1] * g_cameraData.up[1] +
                    g_cameraData.forward[2] * g_cameraData.up[2];
  g_cameraData.up[0] -= dot * g_cameraData.forward[0];
  g_cameraData.up[1] -= dot * g_cameraData.forward[1];
  g_cameraData.up[2] -= dot * g_cameraData.forward[2];
  Normalize3(g_cameraData.up, fallbackUp);
  g_cameraData.fov = payload.fovDegrees;
  g_cameraData.nearZ = payload.nearPlane;
  g_cameraData.farZ = payload.farPlane;
  g_camYaw = atan2f(g_cameraData.forward[0], -g_cameraData.forward[2]);
  g_camPitch = asinf(std::clamp(g_cameraData.forward[1], -1.0f, 1.0f));
  UpdateCameraCB();
}

bool LiveLinkSceneSync::ApplyEnvironmentChanged(const SceneDeltaBatch &batch,
                                                const SceneDelta &delta) {
  const EnvironmentChangedPayload *payload =
      FindPayload<EnvironmentChangedPayload>(delta);
  if (!payload) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "EnvironmentChanged missing payload");
    return false;
  }

  ObjectBinding *binding = FindBinding(delta.target);
  if (binding && delta.revision > 0 &&
      delta.revision <= binding->lastAppliedRevision) {
    return true;
  }

  ObjectBinding &environmentBinding =
      binding ? *binding
              : BindObject(delta.target, batch.sessionId,
                           EngineHandleKind::Environment, kInvalidHandle);
  if (environmentBinding.sessionId != batch.sessionId) {
    environmentBinding.lastAppliedRevision = 0;
  }
  environmentBinding.sessionId = batch.sessionId;

  bool changed = false;
  if (!payload->environmentUri.empty() &&
      payload->environmentUri != IBLManager::Get().GetEnvironmentMapPath()) {
    IBLManager::Get().SetIBLSource(IBLManager::IBLSource::File);
    if (!IBLManager::Get().LoadEnvironmentMap(payload->environmentUri)) {
      LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                    std::string("Failed to load environment map: ") +
                        payload->environmentUri);
      return false;
    }
    changed = true;
  }

  if (payload->intensity >= 0.0f) {
    IBLManager::Get().SetSkyIntensity(payload->intensity);
    changed = true;
  }

  if (changed) {
    DxrRenderer::CreateRayTracingPipeline(0, 0);
    DxrRenderer::ResetAccumulation();
  }

  environmentBinding.lastAppliedRevision = delta.revision;
  return true;
}

LiveLinkSceneSync::ObjectBinding *
LiveLinkSceneSync::FindBinding(const ObjectId &objectId) {
  auto it = m_bindings.find(objectId);
  return it == m_bindings.end() ? nullptr : &it->second;
}

const LiveLinkSceneSync::ObjectBinding *
LiveLinkSceneSync::FindBinding(const ObjectId &objectId) const {
  auto it = m_bindings.find(objectId);
  return it == m_bindings.end() ? nullptr : &it->second;
}

LiveLinkSceneSync::ObjectBinding *
LiveLinkSceneSync::FindRelatedBinding(const ObjectId &objectId,
                                      EngineHandleKind handleKind) {
  for (auto &[_, binding] : m_bindings) {
    if (binding.handleKind != handleKind) {
      continue;
    }
    if (binding.objectId.sourceApp != objectId.sourceApp ||
        binding.objectId.documentId != objectId.documentId ||
        binding.objectId.objectId != objectId.objectId) {
      continue;
    }
    return &binding;
  }
  return nullptr;
}

LiveLinkSceneSync::ObjectBinding &
LiveLinkSceneSync::BindObject(const ObjectId &objectId,
                              const std::string &sessionId,
                              EngineHandleKind handleKind,
                              size_t handleIndex) {
  ObjectBinding &binding = m_bindings[objectId];
  binding.objectId = objectId;
  binding.sessionId = sessionId;
  binding.handleKind = handleKind;
  binding.handleIndex = handleIndex;
  return binding;
}

bool LiveLinkSceneSync::EnsureNodeBinding(const SceneDeltaBatch &batch,
                                          const SceneDelta &delta,
                                          const std::string &preferredName,
                                          ObjectBinding **outBinding) {
  ObjectBinding *binding = FindBinding(delta.target);
  if (!binding) {
    Scene::Node node;
    node.name = preferredName;
    node.liveLinkManaged = true;
    const size_t nodeIndex = Scene::AddNode(std::move(node));
    binding = &BindObject(delta.target, batch.sessionId,
                          EngineHandleKind::SceneNode, nodeIndex);
  }

  if (binding->handleKind != EngineHandleKind::SceneNode) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Object binding exists but is not a scene node");
    return false;
  }

  if (binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetNodes().size()) {
    Scene::Node node;
    node.name = preferredName;
    node.liveLinkManaged = true;
    binding->handleIndex = Scene::AddNode(std::move(node));
  }

  if (binding->sessionId != batch.sessionId) {
    binding->lastAppliedRevision = 0;
  }
  binding->sessionId = batch.sessionId;
  Scene::SetNodeLiveLinkManaged(binding->handleIndex, true);

  if (!preferredName.empty() && binding->handleIndex < Scene::GetNodes().size()) {
    Scene::RenameNode(binding->handleIndex, preferredName);
  }

  if (outBinding) {
    *outBinding = binding;
  }
  return true;
}

bool LiveLinkSceneSync::EnsureLightBinding(const SceneDeltaBatch &batch,
                                           const SceneDelta &delta,
                                           ObjectBinding **outBinding) {
  ObjectBinding *binding = FindBinding(delta.target);
  if (!binding) {
    const size_t lightIndex = Scene::AddLight(LightType::Omni);
    binding = &BindObject(delta.target, batch.sessionId,
                          EngineHandleKind::SceneLight, lightIndex);
  }

  if (binding->handleKind != EngineHandleKind::SceneLight) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Object binding exists but is not a scene light");
    return false;
  }

  if (binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetLights().size()) {
    binding->handleIndex = Scene::AddLight(LightType::Omni);
  }

  if (binding->sessionId != batch.sessionId) {
    binding->lastAppliedRevision = 0;
  }
  binding->sessionId = batch.sessionId;
  if (outBinding) {
    *outBinding = binding;
  }
  return true;
}

bool LiveLinkSceneSync::EnsureMaterialBinding(const SceneDeltaBatch &batch,
                                              const SceneDelta &delta,
                                              ObjectBinding **outBinding) {
  const MaterialChangedPayload *payload = FindPayload<MaterialChangedPayload>(delta);
  auto resolveMaterialIndexFromNodeSlot = [&]() {
    if (!payload || payload->nodeObjectId.empty()) {
      return -1;
    }

    ObjectId nodeObjectId = delta.target;
    nodeObjectId.objectId = payload->nodeObjectId;
    nodeObjectId.objectType = ObjectType::Node;
    const ObjectBinding *nodeBinding = FindBinding(nodeObjectId);
    if (!nodeBinding || nodeBinding->handleKind != EngineHandleKind::SceneNode ||
        nodeBinding->handleIndex >= Scene::GetNodes().size()) {
      return -1;
    }

    const Scene::Node &node = Scene::GetNodes()[nodeBinding->handleIndex];
    const int materialSlot = (std::max)(0, payload->materialSlot);
    if (materialSlot >= static_cast<int>(node.linkedMaterialIndices.size())) {
      return -1;
    }

    const int candidate =
        node.linkedMaterialIndices[static_cast<size_t>(materialSlot)];
    if (candidate < 0 ||
        candidate >= static_cast<int>(Scene::GetMaterialCount())) {
      return -1;
    }

    return candidate;
  };

  ObjectBinding *binding = FindBinding(delta.target);
  int materialIndex = resolveMaterialIndexFromNodeSlot();
  if (materialIndex < 0 && payload && !payload->materialStableId.empty()) {
    materialIndex = Scene::FindMaterialByStableId(payload->materialStableId);
  }
  if (materialIndex < 0) {
    materialIndex = ResolveMaterialIndexByName(delta, payload);
  }

  if (!binding) {
    if (materialIndex < 0) {
      Asset::Material placeholder{};
      const std::string materialName = ResolveMaterialDisplayName(delta, payload);
      if (!materialName.empty()) {
        strncpy_s(placeholder.name, materialName.c_str(), _TRUNCATE);
      }
      materialIndex =
          Scene::FindOrCreateMaterial(placeholder,
                                      payload ? payload->materialStableId
                                              : std::string());
      if (materialIndex < 0) {
        LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                      "No scene material matched the live-link target");
        return false;
      }
    }
    binding = &BindObject(delta.target, batch.sessionId,
                          EngineHandleKind::SceneMaterial,
                          static_cast<size_t>(materialIndex));
  }

  if (binding->handleKind != EngineHandleKind::SceneMaterial) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Object binding exists but is not a scene material");
    return false;
  }

  if (materialIndex >= 0) {
    binding->handleIndex = static_cast<size_t>(materialIndex);
  }

  if (binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetMaterialCount()) {
    materialIndex = ResolveMaterialIndexByName(delta, payload);
    if (materialIndex < 0) {
      LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                    "Bound material no longer exists");
      return false;
    }
    binding->handleIndex = static_cast<size_t>(materialIndex);
  }

  if (binding->sessionId != batch.sessionId) {
    binding->lastAppliedRevision = 0;
  }
  binding->sessionId = batch.sessionId;
  if (outBinding) {
    *outBinding = binding;
  }
  return true;
}

void LiveLinkSceneSync::RemoveSessionContent(const std::string &sessionId) {
  for (auto it = m_bindings.begin(); it != m_bindings.end();) {
    if (it->second.sessionId != sessionId) {
      ++it;
      continue;
    }
    if (it->second.handleKind == EngineHandleKind::SavedView) {
      it = m_bindings.erase(it);
      continue;
    }
    it->second.sessionId.clear();
    it->second.lastAppliedRevision = 0;
    ++it;
  }
  SavedViews::RemoveExternalViewsForSession(sessionId);
  if (m_cachedExternalCamera.valid && m_cachedExternalCamera.sessionId == sessionId) {
    m_cachedExternalCamera = CachedCameraState{};
  }
  PruneTextureCacheEntries();
  m_cameraControlDetached = false;
}

void LiveLinkSceneSync::PruneTextureCacheEntries() {
  for (auto it = m_textureIndicesByUri.begin(); it != m_textureIndicesByUri.end();) {
    const int textureIndex = it->second;
    if (textureIndex < 0 ||
        textureIndex >= static_cast<int>(Scene::GetTextureCount())) {
      it = m_textureIndicesByUri.erase(it);
      continue;
    }

    std::error_code error;
    const std::filesystem::path texturePath = Utf8PathFromString(it->first);
    if (!texturePath.empty() && !std::filesystem::exists(texturePath, error)) {
      it = m_textureIndicesByUri.erase(it);
      continue;
    }

    ++it;
  }

  for (auto it = m_textureIndicesByBlobHash.begin();
       it != m_textureIndicesByBlobHash.end();) {
    const int textureIndex = it->second;
    if (textureIndex < 0 ||
        textureIndex >= static_cast<int>(Scene::GetTextureCount())) {
      it = m_textureIndicesByBlobHash.erase(it);
      continue;
    }
    ++it;
  }
}

void LiveLinkSceneSync::ReindexSceneNodeBindingsAfterRemoval(size_t removedIndex) {
  for (auto &[_, binding] : m_bindings) {
    if (binding.handleKind != EngineHandleKind::SceneNode ||
        binding.handleIndex == kInvalidHandle) {
      continue;
    }
    if (binding.handleIndex > removedIndex) {
      --binding.handleIndex;
    }
  }
}

void LiveLinkSceneSync::ReindexSceneLightBindingsAfterRemoval(size_t removedIndex) {
  for (auto &[_, binding] : m_bindings) {
    if (binding.handleKind != EngineHandleKind::SceneLight ||
        binding.handleIndex == kInvalidHandle) {
      continue;
    }
    if (binding.handleIndex > removedIndex) {
      --binding.handleIndex;
    }
  }
}

void LiveLinkSceneSync::ReindexSavedViewBindingsAfterRemoval(size_t removedIndex) {
  for (auto &[_, binding] : m_bindings) {
    if (binding.handleKind != EngineHandleKind::SavedView ||
        binding.handleIndex == kInvalidHandle) {
      continue;
    }
    if (binding.handleIndex > removedIndex) {
      --binding.handleIndex;
    }
  }
}

void LiveLinkSceneSync::ClearAllBindings() {
  m_bindings.clear();
  m_textureIndicesByUri.clear();
  m_textureIndicesByBlobHash.clear();
  m_cachedExternalCamera = CachedCameraState{};
  m_cameraControlDetached = false;
  SavedViews::RemoveAllExternalViews();
}

void LiveLinkSceneSync::AppendDiagnosticEntry(
    const char *level, const std::string &providerName,
    const std::string &sessionId, const std::string &deltaKind,
    const std::string &targetId, const std::string &message) const {
  LiveLinkDiagnosticEntry entry;
  entry.sequence = m_nextDiagnosticSequence++;
  entry.level = level ? level : "Info";
  entry.providerName = providerName;
  entry.sessionId = sessionId;
  entry.deltaKind = deltaKind;
  entry.targetId = targetId;
  entry.message = message;
  m_recentDiagnostics.push_back(std::move(entry));
  constexpr size_t kMaxDiagnosticEntries = 64;
  if (m_recentDiagnostics.size() > kMaxDiagnosticEntries) {
    m_recentDiagnostics.erase(
        m_recentDiagnostics.begin(),
        m_recentDiagnostics.begin() +
            (m_recentDiagnostics.size() - kMaxDiagnosticEntries));
  }
}

void LiveLinkSceneSync::LogValidationIssue(const ValidationIssue &issue) const {
  AppendDiagnosticEntry(ToString(issue.severity), issue.providerName,
                        issue.sessionId, "Validation", "", issue.message);
  fprintf(stderr,
          "LiveLink: [%s] provider='%s' session='%s' %s\n",
          ToString(issue.severity), issue.providerName.c_str(),
          issue.sessionId.c_str(), issue.message.c_str());
}

void LiveLinkSceneSync::LogApplyIssue(const char *level,
                                      const std::string &providerName,
                                      const std::string &sessionId,
                                      const SceneDelta *delta,
                                      const std::string &message) const {
  const char *kind = delta ? ToString(delta->kind) : "Unknown";
  const char *objectId =
      (delta && !delta->target.objectId.empty()) ? delta->target.objectId.c_str()
                                                 : "";
  AppendDiagnosticEntry(level, providerName, sessionId, kind, objectId,
                        message);
  fprintf(stderr,
          "LiveLink: [%s] provider='%s' session='%s' delta='%s' target='%s' %s\n",
          level, providerName.c_str(), sessionId.c_str(), kind, objectId,
          message.c_str());
}

} // namespace LiveLink
