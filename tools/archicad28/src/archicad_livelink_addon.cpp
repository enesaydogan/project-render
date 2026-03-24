#include "archicad_livelink_pipe_client.h"
#include "resources.hpp"

#include "ACAPinc.h"

#include "AttributeIndex.hpp"
#include "ConvexPolygon.hpp"
#include "Model.hpp"
#include "ModelElement.hpp"
#include "ModelMeshBody.hpp"
#include "Polygon.hpp"
#include "TextureCoordinate.hpp"
#include "CH.hpp"
#include "UniString.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

using json = nlohmann::json;

namespace {

void SetMenuItemEnabled(Int32 itemIndex, bool enabled);
void Normalize3(float value[3], const float fallback[3]);
void CALLBACK ScenePollTimerProc(HWND, UINT, UINT_PTR, DWORD);
void CALLBACK CameraPollTimerProc(HWND, UINT, UINT_PTR, DWORD);
GSErrCode ProjectEventHandler(API_NotifyEventID notifID, Int32 param);
GSErrCode ElementEventHandler(const API_NotifyElementType *elemType);

constexpr const char *kPipeName = "project-render-archicad-livelink";
constexpr const char *kProviderName = "Archicad28Pipe";
constexpr const char *kSourceApp = "Archicad28";
constexpr size_t kMaxDeltasPerBatch = 96;
constexpr UINT_PTR kScenePollTimerId = 0xA281;
constexpr UINT_PTR kCameraPollTimerId = 0xA282;
constexpr UINT kScenePollIntervalMs = 250;
constexpr UINT kCameraPollIntervalMs = 33;
constexpr auto kSceneResyncDebounce = std::chrono::milliseconds(900);
constexpr auto kReconnectRetryInterval = std::chrono::milliseconds(1000);

struct NativeMeshPayloadHeader {
  uint32_t magic = 0x48534D50;
  uint32_t version = 2;
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
  float position[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 1.0f, 0.0f};
  float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  float uv[2] = {0.0f, 0.0f};
};

struct DocumentInfo {
  std::string documentId;
  std::string documentPath;
  std::string displayName;
};

struct ElementExportRecord {
  API_Guid guid = APINULLGuid;
  std::string objectId;
  std::string displayName;
  std::string payloadUri;
  uint64_t vertexCount = 0;
  uint64_t indexCount = 0;
  bool visible = true;
  struct MaterialBinding {
    int materialKey = 0;
    int materialSlot = 0;
  };
  std::vector<MaterialBinding> materialBindings;
};

struct MaterialExportRecord {
  struct Reference {
    std::string nodeObjectId;
    int materialSlot = 0;

    bool operator==(const Reference &) const = default;
  };

  int materialKey = 0;
  std::string objectId;
  std::string materialStableId;
  std::string name;
  std::string nodeObjectId;
  int materialSlot = 0;
  std::vector<Reference> references;
  std::string materialModel = "ArchicadSurface";
  std::array<float, 4> baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
  std::string baseColorTextureUri;
  std::array<float, 4> emissiveColor = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 0.0f;
  float roughness = 0.5f;
  float metalness = 0.0f;
  float specularWeight = 0.5f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  std::array<float, 3> transmissionColor = {1.0f, 1.0f, 1.0f};
  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
};

struct CameraExportRecord {
  bool valid = false;
  std::array<float, 3> position = {0.0f, 1.0f, -5.0f};
  std::array<float, 3> forward = {0.0f, 0.0f, 1.0f};
  std::array<float, 3> up = {0.0f, 1.0f, 0.0f};
  float fovDegrees = 60.0f;
  float nearPlane = 0.01f;
  float farPlane = 1000.0f;
};

struct ScopedElementMemo : API_ElementMemo {
  ScopedElementMemo() : API_ElementMemo{} {}
  ~ScopedElementMemo() { ACAPI_DisposeElemMemoHdls(this); }
};

class ScopedTemporarySight {
public:
  ~ScopedTemporarySight() { Reset(); }

  bool CreateAndSelect(std::string *outError) {
    Reset();

    GSErrCode err = ACAPI_Sight_CreateSight(&m_temporarySight);
    if (err != NoError) {
      AssignError(outError, "ACAPI_Sight_CreateSight", err);
      return false;
    }

    err = ACAPI_Sight_SelectSight(m_temporarySight, &m_originalSight);
    if (err != NoError) {
      AssignError(outError, "ACAPI_Sight_SelectSight", err);
      ACAPI_Sight_DeleteSight(m_temporarySight);
      m_temporarySight = nullptr;
      return false;
    }

    return true;
  }

  void Reset() {
    if (m_temporarySight != nullptr) {
      ACAPI_Sight_SelectSight(m_originalSight, &m_temporarySight);
      ACAPI_Sight_DeleteSight(m_temporarySight);
    }
    m_temporarySight = nullptr;
    m_originalSight = nullptr;
  }

private:
  static void AssignError(std::string *outError, const char *operation,
                          GSErrCode error) {
    if (outError == nullptr) {
      return;
    }
    *outError = std::string(operation) + " failed (" +
                std::to_string(static_cast<int>(error)) + ")";
  }

  void *m_temporarySight = nullptr;
  void *m_originalSight = nullptr;
};

ArchicadLiveLinkPipeClient g_pipeClient;

std::string ToUtf8(const GS::UniString &value) {
  std::unique_ptr<char[]> utf8(value.CopyUTF8());
  if (!utf8) {
    return {};
  }
  return utf8.get();
}

std::string SanitizeDocumentId(std::string value) {
  for (char &ch : value) {
    switch (ch) {
    case '\\':
    case '/':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
    case ' ':
      ch = '_';
      break;
    default:
      break;
    }
  }
  if (value.empty()) {
    value = "untitled";
  }
  return value;
}

std::string GuidToString(API_Guid guid) {
  return ToUtf8(APIGuidToString(guid));
}

std::string MakeObjectId(API_Guid guid) { return GuidToString(guid); }

std::string MakeCameraObjectId() { return "camera:active"; }

std::filesystem::path GetPayloadRootDirectory() {
  wchar_t tempPath[MAX_PATH] = {};
  const DWORD length =
      GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
  if (length == 0 || length >= std::size(tempPath)) {
    return std::filesystem::temp_directory_path() / "project-render" /
           "archicad28";
  }
  return std::filesystem::path(tempPath) / "project-render" / "archicad28";
}

std::filesystem::path GetPayloadDirectory(const std::string &documentId) {
  return GetPayloadRootDirectory() / SanitizeDocumentId(documentId);
}

std::string PathToUtf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  std::string utf8;
  utf8.reserve(value.size());
  for (char8_t ch : value) {
    utf8.push_back(static_cast<char>(ch));
  }
  return utf8;
}

void ConvertArchicadPointToEngine(double x, double y, double z, float out[3]) {
  out[0] = static_cast<float>(x);
  out[1] = static_cast<float>(z);
  out[2] = static_cast<float>(-y);
}

void ConvertArchicadVectorToEngine(double x, double y, double z, float out[3]) {
  out[0] = static_cast<float>(x);
  out[1] = static_cast<float>(z);
  out[2] = static_cast<float>(-y);
}

std::array<float, 3> ConvertArchicadPointToEngine(const API_Coord3D &value) {
  std::array<float, 3> result = {};
  ConvertArchicadPointToEngine(value.x, value.y, value.z, result.data());
  return result;
}

void Cross3(const float lhs[3], const float rhs[3], float out[3]) {
  out[0] = lhs[1] * rhs[2] - lhs[2] * rhs[1];
  out[1] = lhs[2] * rhs[0] - lhs[0] * rhs[2];
  out[2] = lhs[0] * rhs[1] - lhs[1] * rhs[0];
}

float Clamp01(double value) {
  return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

std::string MakeMaterialObjectId(const std::string &materialStableId,
                                 int materialKey) {
  if (!materialStableId.empty()) {
    return "material:id:" + materialStableId;
  }
  return "material:index:" + std::to_string(materialKey);
}

std::string ResolveTextureUri(const API_Texture &texture) {
  if (texture.status == 0 || texture.fileLoc == nullptr ||
      texture.fileLoc->GetLocalLength() == 0) {
    return {};
  }
  return ToUtf8(texture.fileLoc->ToDisplayText());
}

bool PopulateMaterialExportRecord(int materialKey,
                                  MaterialExportRecord *outMaterial) {
  if (outMaterial == nullptr) {
    return false;
  }

  MaterialExportRecord material;
  material.materialKey = materialKey;

  API_Component3D component = {};
  component.header.typeID = API_UmatID;
  component.header.index = materialKey;
  const GSErrCode err = ACAPI_ModelAccess_GetComponent(&component);
  if (err == NoError) {
    const API_MaterialType &source = component.umat.mater;
    if (source.head.guid != APINULLGuid) {
      material.materialStableId = GuidToString(source.head.guid);
    }
    material.objectId =
        MakeMaterialObjectId(material.materialStableId, materialKey);
    material.name = source.head.name;
    if (material.name.empty()) {
      material.name = std::string("Surface ") + std::to_string(materialKey);
    }
    material.baseColor = {Clamp01(source.surfaceRGB.f_red),
                          Clamp01(source.surfaceRGB.f_green),
                          Clamp01(source.surfaceRGB.f_blue),
                          1.0f - Clamp01(static_cast<double>(source.transpPc) /
                                         100.0)};
    material.baseColorTextureUri = ResolveTextureUri(source.texture);
    material.emissiveColor = {Clamp01(source.emissionRGB.f_red),
                              Clamp01(source.emissionRGB.f_green),
                              Clamp01(source.emissionRGB.f_blue), 1.0f};
    material.emissiveIntensity =
        Clamp01(static_cast<double>(source.emissionAtt) / 100.0);
    material.roughness =
        1.0f - Clamp01(static_cast<double>(source.shine) / 10000.0);
    material.specularWeight =
        Clamp01(static_cast<double>(source.specularPc) / 100.0);
    material.transmissionWeight =
        Clamp01(static_cast<double>(source.transpPc) / 100.0);
    material.transmissionColor = {
        material.baseColor[0], material.baseColor[1], material.baseColor[2]};
    material.alphaMode =
        material.transmissionWeight > 1.0e-3f ? "BLEND" : "OPAQUE";

    switch (source.mtype) {
    case APIMater_MetalID:
      material.metalness = 1.0f;
      material.specularWeight = 1.0f;
      material.ior = 2.0f;
      break;
    case APIMater_GlassID:
      material.roughness = 0.02f;
      material.specularWeight = 1.0f;
      material.ior = 1.52f;
      material.transmissionWeight =
          (std::max)(material.transmissionWeight, 0.85f);
      material.alphaMode = "BLEND";
      break;
    case APIMater_GlowingID:
      material.emissiveIntensity =
          (std::max)(material.emissiveIntensity, 1.0f);
      break;
    default:
      break;
    }
  }

  if (component.umat.mater.texture.fileLoc != nullptr) {
    delete component.umat.mater.texture.fileLoc;
    component.umat.mater.texture.fileLoc = nullptr;
  }

  if (material.objectId.empty()) {
    material.objectId = MakeMaterialObjectId({}, materialKey);
  }
  if (material.name.empty()) {
    material.name = std::string("Surface ") + std::to_string(materialKey);
  }

  *outMaterial = std::move(material);
  return true;
}

void DeduplicateMaterialReferences(MaterialExportRecord *material) {
  if (material == nullptr) {
    return;
  }

  std::sort(material->references.begin(), material->references.end(),
            [](const MaterialExportRecord::Reference &lhs,
               const MaterialExportRecord::Reference &rhs) {
              if (lhs.nodeObjectId != rhs.nodeObjectId) {
                return lhs.nodeObjectId < rhs.nodeObjectId;
              }
              return lhs.materialSlot < rhs.materialSlot;
            });
  material->references.erase(
      std::unique(material->references.begin(), material->references.end()),
      material->references.end());
}

bool ReadCurrentCamera(CameraExportRecord *outCamera) {
  if (outCamera == nullptr) {
    return false;
  }

  API_3DProjectionInfo projectionInfo = {};
  if (ACAPI_View_Get3DProjectionSets(&projectionInfo) != NoError ||
      !projectionInfo.isPersp) {
    return false;
  }

  const API_PerspPars &persp = projectionInfo.u.persp;
  const API_Coord3D cameraPos = {persp.pos.x, persp.pos.y, persp.cameraZ};
  const API_Coord3D targetPos = {persp.target.x, persp.target.y, persp.targetZ};

  CameraExportRecord camera;
  camera.valid = true;
  camera.position = ConvertArchicadPointToEngine(cameraPos);

  float forward[3] = {};
  ConvertArchicadVectorToEngine(targetPos.x - cameraPos.x,
                                targetPos.y - cameraPos.y,
                                targetPos.z - cameraPos.z, forward);
  const float upWorld[3] = {0.0f, 1.0f, 0.0f};
  const float fallbackForward[3] = {0.0f, 0.0f, 1.0f};
  Normalize3(forward, fallbackForward);

  float right[3] = {};
  Cross3(upWorld, forward, right);
  const float fallbackRight[3] = {1.0f, 0.0f, 0.0f};
  Normalize3(right, fallbackRight);

  float up[3] = {};
  Cross3(forward, right, up);
  Normalize3(up, upWorld);

  if (std::fabs(persp.rollAngle) > 1.0e-6) {
    const float cosAngle = static_cast<float>(std::cos(persp.rollAngle));
    const float sinAngle = static_cast<float>(std::sin(persp.rollAngle));
    float rolledRight[3] = {};
    float rolledUp[3] = {};
    for (size_t i = 0; i < 3; ++i) {
      rolledRight[i] = right[i] * cosAngle + up[i] * sinAngle;
      rolledUp[i] = up[i] * cosAngle - right[i] * sinAngle;
    }
    std::copy(std::begin(rolledRight), std::end(rolledRight), std::begin(right));
    std::copy(std::begin(rolledUp), std::end(rolledUp), std::begin(up));
    Normalize3(right, fallbackRight);
    Normalize3(up, upWorld);
  }

  camera.forward = {forward[0], forward[1], forward[2]};
  camera.up = {up[0], up[1], up[2]};

  camera.fovDegrees = static_cast<float>(persp.viewCone);
  camera.nearPlane = 0.01f;
  camera.farPlane =
      (std::max)(1000.0f, static_cast<float>(persp.distance * 4.0));
  *outCamera = camera;
  return true;
}

std::string GetElementInfoString(API_Guid guid) {
  ScopedElementMemo memo;
  if (ACAPI_Element_GetMemo(guid, &memo, APIMemoMask_ElemInfoString) !=
      NoError) {
    return {};
  }
  if (memo.elemInfoString == nullptr) {
    return {};
  }
  return ToUtf8(*memo.elemInfoString);
}

std::string GetElementTypeString(const API_ElemType &type) {
  GS::UniString name;
  ACAPI_Element_GetElemTypeName(type, name);
  if (name.IsEmpty()) {
    return "Element";
  }
  return ToUtf8(name);
}

std::string GetElementDisplayName(API_Guid guid) {
  API_Elem_Head header = {};
  header.guid = guid;
  if (ACAPI_Element_GetHeader(&header) != NoError) {
    return GuidToString(guid);
  }

  const std::string infoString = GetElementInfoString(guid);
  if (!infoString.empty()) {
    return infoString;
  }

  return GetElementTypeString(header.type) + " " + GuidToString(guid);
}

void Normalize3(float value[3], const float fallback[3]) {
  const float lengthSquared = value[0] * value[0] + value[1] * value[1] +
                              value[2] * value[2];
  if (lengthSquared <= 1.0e-12f) {
    value[0] = fallback[0];
    value[1] = fallback[1];
    value[2] = fallback[2];
    return;
  }

  const float inverseLength = 1.0f / std::sqrt(lengthSquared);
  value[0] *= inverseLength;
  value[1] *= inverseLength;
  value[2] *= inverseLength;
}

bool SameVector3(const std::array<float, 3> &lhs,
                 const std::array<float, 3> &rhs) {
  return std::fabs(lhs[0] - rhs[0]) <= 1.0e-4f &&
         std::fabs(lhs[1] - rhs[1]) <= 1.0e-4f &&
         std::fabs(lhs[2] - rhs[2]) <= 1.0e-4f;
}

bool SameCamera(const CameraExportRecord &lhs, const CameraExportRecord &rhs) {
  return lhs.valid == rhs.valid && SameVector3(lhs.position, rhs.position) &&
         SameVector3(lhs.forward, rhs.forward) &&
         SameVector3(lhs.up, rhs.up) &&
         std::fabs(lhs.fovDegrees - rhs.fovDegrees) <= 1.0e-4f &&
         std::fabs(lhs.nearPlane - rhs.nearPlane) <= 1.0e-4f &&
         std::fabs(lhs.farPlane - rhs.farPlane) <= 1.0e-4f;
}

json MakeObjectIdJson(const std::string &documentId, const std::string &objectId,
                      const char *objectType) {
  return json{{"sourceApp", kSourceApp},
              {"documentId", documentId},
              {"objectId", objectId},
              {"objectType", objectType}};
}

json MakeCameraDelta(const std::string &documentId,
                     const CameraExportRecord &camera, uint64_t revision) {
  return json{{"kind", "CameraChanged"},
              {"target",
               MakeObjectIdJson(documentId, MakeCameraObjectId(), "Camera")},
              {"revision", revision},
              {"debugLabel", "Archicad active 3D camera"},
              {"payload",
               json{{"position", camera.position},
                    {"forward", camera.forward},
                    {"up", camera.up},
                    {"fovDegrees", camera.fovDegrees},
                    {"nearPlane", camera.nearPlane},
                    {"farPlane", camera.farPlane}}}};
}

json MakeNodeRemovedDelta(const std::string &documentId,
                          const std::string &objectId, uint64_t revision) {
  return json{{"kind", "NodeRemoved"},
              {"target", MakeObjectIdJson(documentId, objectId, "Node")},
              {"revision", revision},
              {"debugLabel", objectId},
              {"payload", json{{"removeChildren", true}}}};
}

bool ExportElementMeshPayload(const std::string &documentId,
                              const ElementExportRecord &record,
                              const ModelerAPI::Element &element,
                              std::vector<ElementExportRecord::MaterialBinding>
                                  *outMaterialBindings,
                              std::string *outPayloadUri,
                              uint64_t *outVertexCount,
                              uint64_t *outIndexCount) {
  if (outPayloadUri == nullptr || outVertexCount == nullptr ||
      outIndexCount == nullptr) {
    return false;
  }

  static constexpr float kUpFallback[3] = {0.0f, 1.0f, 0.0f};

  struct ExportSubmesh {
    int materialSlot = 0;
    std::vector<NativeMeshPayloadVertex> vertices;
    std::vector<uint32_t> indices;
  };

  std::vector<ExportSubmesh> submeshes;
  std::unordered_map<int, size_t> submeshByMaterialKey;

  const int bodyCount = element.GetTessellatedBodyCount();
  for (int bodyIndex = 1; bodyIndex <= bodyCount; ++bodyIndex) {
    ModelerAPI::MeshBody body;
    element.GetTessellatedBody(bodyIndex, &body);

    const int polygonCount = body.GetPolygonCount();
    for (int polygonIndex = 1; polygonIndex <= polygonCount; ++polygonIndex) {
      ModelerAPI::Polygon polygon;
      body.GetPolygon(polygonIndex, &polygon);
      if (polygon.IsInvisible()) {
        continue;
      }

      ModelerAPI::AttributeIndex materialIndex(
          ModelerAPI::AttributeIndex::MaterialIndex);
      polygon.GetMaterialIndex(materialIndex);
      const int materialKey =
          materialIndex.IsValid() ? materialIndex.GetIndex() : 0;

      auto [slotIt, inserted] =
          submeshByMaterialKey.emplace(materialKey, submeshes.size());
      if (inserted) {
        ExportSubmesh submesh;
        submesh.materialSlot = static_cast<int>(submeshes.size());
        submeshes.push_back(std::move(submesh));
      }
      ExportSubmesh &submesh = submeshes[slotIt->second];

      try {
        const int convexPolygonCount = polygon.GetConvexPolygonCount();
        for (int convexPolygonIndex = 1; convexPolygonIndex <= convexPolygonCount;
             ++convexPolygonIndex) {
          ModelerAPI::ConvexPolygon convexPolygon;
          polygon.GetConvexPolygon(convexPolygonIndex, &convexPolygon);
          const int polygonVertexCount = convexPolygon.GetVertexCount();
          if (polygonVertexCount < 3) {
            continue;
          }

          std::vector<uint32_t> polygonVertexIndices;
          polygonVertexIndices.reserve(static_cast<size_t>(polygonVertexCount));

          for (int vertexIndex = 1; vertexIndex <= polygonVertexCount;
               ++vertexIndex) {
            ModelerAPI::Vertex vertex;
            body.GetVertex(convexPolygon.GetVertexIndex(vertexIndex), &vertex,
                           ModelerAPI::CoordinateSystem::World);
            ModelerAPI::Vector normal = convexPolygon.GetNormalVectorByVertex(
                vertexIndex, ModelerAPI::CoordinateSystem::World);

            NativeMeshPayloadVertex payloadVertex;
            ConvertArchicadPointToEngine(vertex.x, vertex.y, vertex.z,
                                         payloadVertex.position);
            ConvertArchicadVectorToEngine(normal.x, normal.y, normal.z,
                                          payloadVertex.normal);
            Normalize3(payloadVertex.normal, kUpFallback);
            if (polygon.HasMaterialTexture() || polygon.HasPolygonTexture()) {
              try {
                ModelerAPI::TextureCoordinate texCoord;
                polygon.GetTextureCoordinate(&vertex, &texCoord);
                payloadVertex.uv[0] = static_cast<float>(texCoord.u);
                payloadVertex.uv[1] = static_cast<float>(texCoord.v);
              } catch (...) {
              }
            }

            const uint32_t appendedIndex =
                static_cast<uint32_t>(submesh.vertices.size());
            submesh.vertices.push_back(payloadVertex);
            polygonVertexIndices.push_back(appendedIndex);
          }

          for (size_t triangleIndex = 1;
               triangleIndex + 1 < polygonVertexIndices.size(); ++triangleIndex) {
            submesh.indices.push_back(polygonVertexIndices[0]);
            submesh.indices.push_back(polygonVertexIndices[triangleIndex]);
            submesh.indices.push_back(polygonVertexIndices[triangleIndex + 1]);
          }
        }
      } catch (...) {
        continue;
      }
    }
  }

  uint64_t totalVertexCount = 0;
  uint64_t totalIndexCount = 0;
  for (const ExportSubmesh &submesh : submeshes) {
    totalVertexCount += static_cast<uint64_t>(submesh.vertices.size());
    totalIndexCount += static_cast<uint64_t>(submesh.indices.size());
  }
  if (totalIndexCount == 0) {
    return false;
  }

  const std::filesystem::path payloadDirectory = GetPayloadDirectory(documentId);
  std::error_code error;
  std::filesystem::create_directories(payloadDirectory, error);
  if (error) {
    return false;
  }

  const std::filesystem::path payloadPath =
      payloadDirectory / (SanitizeDocumentId(record.objectId) + ".prmesh");

  std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }

  NativeMeshPayloadHeader header;
  header.meshCount = static_cast<uint32_t>(submeshes.size());
  stream.write(reinterpret_cast<const char *>(&header), sizeof(header));

  for (const ExportSubmesh &submesh : submeshes) {
    NativeMeshPayloadMeshHeader meshHeader;
    meshHeader.vertexCount = static_cast<uint32_t>(submesh.vertices.size());
    meshHeader.indexCount = static_cast<uint32_t>(submesh.indices.size());
    meshHeader.materialSlot = submesh.materialSlot;
    stream.write(reinterpret_cast<const char *>(&meshHeader), sizeof(meshHeader));

    if (!submesh.vertices.empty()) {
      stream.write(reinterpret_cast<const char *>(submesh.vertices.data()),
                   static_cast<std::streamsize>(submesh.vertices.size() *
                                                sizeof(submesh.vertices[0])));
    }
    if (!submesh.indices.empty()) {
      stream.write(reinterpret_cast<const char *>(submesh.indices.data()),
                   static_cast<std::streamsize>(submesh.indices.size() *
                                                sizeof(submesh.indices[0])));
    }
  }

  if (!stream.good()) {
    return false;
  }

  if (outMaterialBindings != nullptr) {
    outMaterialBindings->clear();
    outMaterialBindings->reserve(submeshes.size());
    for (const auto &[materialKey, submeshIndex] : submeshByMaterialKey) {
      ElementExportRecord::MaterialBinding binding;
      binding.materialKey = materialKey;
      binding.materialSlot = submeshes[submeshIndex].materialSlot;
      outMaterialBindings->push_back(binding);
    }
    std::sort(outMaterialBindings->begin(), outMaterialBindings->end(),
              [](const ElementExportRecord::MaterialBinding &lhs,
                 const ElementExportRecord::MaterialBinding &rhs) {
                return lhs.materialSlot < rhs.materialSlot;
              });
  }

  *outPayloadUri = PathToUtf8(payloadPath);
  *outVertexCount = totalVertexCount;
  *outIndexCount = totalIndexCount;
  return true;
}

bool ExportSceneElements(const DocumentInfo &documentInfo,
                         const GS::Array<API_Guid> *requestedElementGuids,
                         std::vector<ElementExportRecord> *outElements,
                         std::vector<MaterialExportRecord> *outMaterials,
                         std::string *outError) {
  if (outElements == nullptr || outMaterials == nullptr) {
    return false;
  }

  GS::Array<API_Guid> elementGuids;
  if (requestedElementGuids != nullptr) {
    elementGuids = *requestedElementGuids;
  } else {
    const GSErrCode listErr = ACAPI_Element_GetElemList(API_ZombieElemID,
                                                        &elementGuids,
                                                        APIFilt_In3D);
    if (listErr != NoError) {
      if (outError != nullptr) {
        *outError = "ACAPI_Element_GetElemList failed (" +
                    std::to_string(static_cast<int>(listErr)) + ")";
      }
      return false;
    }
  }
  if (elementGuids.IsEmpty()) {
    outElements->clear();
    outMaterials->clear();
    return true;
  }

  ScopedTemporarySight temporarySight;
  if (!temporarySight.CreateAndSelect(outError)) {
    return false;
  }

  const GSErrCode modelErr =
      ACAPI_ModelAccess_GenerateModelWithSeparateComponents(elementGuids);
  if (modelErr != NoError) {
    if (outError != nullptr) {
      *outError =
          "ACAPI_ModelAccess_GenerateModelWithSeparateComponents failed (" +
          std::to_string(static_cast<int>(modelErr)) + ")";
    }
    return false;
  }

  ModelerAPI::Model model;
  const GSErrCode selectedSightErr = ACAPI_Sight_GetSelectedSightModel(model);
  if (selectedSightErr != NoError) {
    if (outError != nullptr) {
      *outError = "ACAPI_Sight_GetSelectedSightModel failed (" +
                  std::to_string(static_cast<int>(selectedSightErr)) + ")";
    }
    return false;
  }

  std::vector<ElementExportRecord> exportedElements;
  std::unordered_map<int, MaterialExportRecord> materialsByKey;
  const int elementCount = model.GetElementCount();
  exportedElements.reserve(static_cast<size_t>(elementCount));
  for (int elementIndex = 1; elementIndex <= elementCount; ++elementIndex) {
    ModelerAPI::Element element;
    model.GetElement(elementIndex, &element);

    const API_Guid guid = GSGuid2APIGuid(element.GetElemGuid());
    ElementExportRecord record;
    record.guid = guid;
    record.objectId = MakeObjectId(guid);
    record.displayName = GetElementDisplayName(guid);

    if (!ExportElementMeshPayload(documentInfo.documentId, record, element,
                                  &record.materialBindings,
                                  &record.payloadUri, &record.vertexCount,
                                  &record.indexCount)) {
      continue;
    }

    for (const ElementExportRecord::MaterialBinding &binding :
         record.materialBindings) {
      auto [it, inserted] =
          materialsByKey.emplace(binding.materialKey, MaterialExportRecord{});
      MaterialExportRecord &material = it->second;
      if (inserted) {
        PopulateMaterialExportRecord(binding.materialKey, &material);
      }
      if (material.nodeObjectId.empty()) {
        material.nodeObjectId = record.objectId;
        material.materialSlot = binding.materialSlot;
      }
      material.references.push_back({record.objectId, binding.materialSlot});
    }

    exportedElements.push_back(std::move(record));
  }

  std::vector<MaterialExportRecord> exportedMaterials;
  exportedMaterials.reserve(materialsByKey.size());
  for (auto &[_, material] : materialsByKey) {
    DeduplicateMaterialReferences(&material);
    exportedMaterials.push_back(std::move(material));
  }

  *outElements = std::move(exportedElements);
  *outMaterials = std::move(exportedMaterials);
  return true;
}

class LiveLinkSessionController {
public:
  bool Start();
  bool SyncNow();
  bool Stop(bool silent = false);
  void RefreshMenuState() const;
  void OnScenePollTimer();
  void OnCameraPollTimer();
  void MarkSceneDirty(API_NotifyEventID notifID);
  void OnElementNotification(const API_NotifyElementType *elemType);

private:
  using Clock = std::chrono::steady_clock;

  bool EnsureConnected(bool withDialog = true);
  bool EnsureSessionOpened(bool reportOnSuccess = true,
                           bool withDialog = true);
  bool BeginNewSession(bool reportOnSuccess, bool withDialog);
  bool SendBatch(bool fullSync, json deltas, bool withDialog = true);
  bool SendSessionOpened(bool withDialog = true);
  bool SendSessionClosed(bool withDialog = true);
  bool ExportFullScene(bool startingSession, bool reportSuccess = true,
                       bool withDialog = true);
  bool ExportDirtyElements(bool reportSuccess = true,
                           bool withDialog = true);
  bool SendCameraDeltaIfChanged(bool withDialog);
  DocumentInfo ReadDocumentInfo() const;
  std::string MakeSessionId() const;
  void AppendExportDeltas(const std::vector<ElementExportRecord> &elements,
                          const std::vector<MaterialExportRecord> &materials,
                          const CameraExportRecord *camera,
                          std::vector<json> *outDeltas,
                          uint64_t *ioRevision) const;
  bool SendDeltaChunks(const std::vector<json> &deltas,
                       bool firstBatchFullSync, bool withDialog = true);
  void BuildMaterialRecordsForKeys(
      const std::unordered_set<int> &materialKeys,
      std::vector<MaterialExportRecord> *outMaterials) const;
  void SyncObservedElements();
  void ResetTrackedSceneState(bool detachObservers);
  void MarkElementDirty(const API_Guid &guid);
  void MarkElementRemoved(const API_Guid &guid);
  void ResetSessionState(bool disconnectPipe);
  void HandleSessionLost(std::chrono::milliseconds retryDelay);
  void StartTimers();
  void StopTimers();
  void ScheduleSceneSync(std::chrono::milliseconds delay);
  void ClearPendingSceneSync();
  static void Report(const std::string &message, bool withDialog = false);

  bool m_syncActive = false;
  bool m_sessionOpen = false;
  bool m_commandInProgress = false;
  bool m_sceneDirty = false;
  bool m_fullSceneResyncNeeded = true;
  std::string m_sessionId;
  DocumentInfo m_documentInfo;
  uint64_t m_nextSequence = 1;
  uint64_t m_nextRevision = 1;
  CameraExportRecord m_lastCamera = {};
  std::unordered_map<std::string, ElementExportRecord> m_exportedElements;
  std::unordered_set<std::string> m_observedElementObjectIds;
  std::unordered_set<std::string> m_dirtyElementObjectIds;
  std::unordered_set<std::string> m_removedElementObjectIds;
  Clock::time_point m_nextSceneSyncDeadline = Clock::time_point{};
  UINT_PTR m_scenePollTimer = 0;
  UINT_PTR m_cameraPollTimer = 0;
};

LiveLinkSessionController g_controller;

void LiveLinkSessionController::Report(const std::string &message,
                                       bool withDialog) {
  ACAPI_WriteReport(GS::UniString(message.c_str(), CC_UTF8), withDialog);
}

void LiveLinkSessionController::RefreshMenuState() const {
  const bool commandAvailable = !m_commandInProgress;
  SetMenuItemEnabled(1, commandAvailable && !m_syncActive);
  SetMenuItemEnabled(2, commandAvailable && m_syncActive);
  SetMenuItemEnabled(3, commandAvailable && m_syncActive);
}

void LiveLinkSessionController::ResetSessionState(bool disconnectPipe) {
  if (disconnectPipe) {
    g_pipeClient.Disconnect();
  }
  m_sessionOpen = false;
  m_sessionId.clear();
  m_documentInfo = {};
  m_nextSequence = 1;
  m_nextRevision = 1;
  m_lastCamera = {};
}

void LiveLinkSessionController::ResetTrackedSceneState(bool detachObservers) {
  if (detachObservers) {
    for (const std::string &objectId : m_observedElementObjectIds) {
      const GSErrCode err = ACAPI_Element_DetachObserver(
          APIGuidFromString(objectId.c_str()));
      if (err != NoError && err != APIERR_BADID) {
        Report("project-render LiveLink: failed to detach Archicad element "
               "observer for " +
               objectId + " (" + std::to_string(static_cast<int>(err)) + ")");
      }
    }
  }

  m_exportedElements.clear();
  m_observedElementObjectIds.clear();
  m_dirtyElementObjectIds.clear();
  m_removedElementObjectIds.clear();
}

void LiveLinkSessionController::HandleSessionLost(
    std::chrono::milliseconds retryDelay) {
  ResetSessionState(false);
  m_fullSceneResyncNeeded = true;
  m_sceneDirty = true;
  if (m_syncActive) {
    ScheduleSceneSync(retryDelay);
  } else {
    ClearPendingSceneSync();
  }
}

void LiveLinkSessionController::ScheduleSceneSync(
    std::chrono::milliseconds delay) {
  m_nextSceneSyncDeadline = Clock::now() + delay;
}

void LiveLinkSessionController::ClearPendingSceneSync() {
  m_nextSceneSyncDeadline = Clock::time_point{};
}

void LiveLinkSessionController::StartTimers() {
  if (m_scenePollTimer == 0) {
    m_scenePollTimer =
        SetTimer(nullptr, kScenePollTimerId, kScenePollIntervalMs,
                 &ScenePollTimerProc);
  }
  if (m_cameraPollTimer == 0) {
    m_cameraPollTimer =
        SetTimer(nullptr, kCameraPollTimerId, kCameraPollIntervalMs,
                 &CameraPollTimerProc);
  }
}

void LiveLinkSessionController::StopTimers() {
  if (m_scenePollTimer != 0) {
    KillTimer(nullptr, m_scenePollTimer);
    m_scenePollTimer = 0;
  }
  if (m_cameraPollTimer != 0) {
    KillTimer(nullptr, m_cameraPollTimer);
    m_cameraPollTimer = 0;
  }
}

bool LiveLinkSessionController::EnsureConnected(bool withDialog) {
  if (g_pipeClient.IsConnected()) {
    return true;
  }
  if (!g_pipeClient.Connect(kPipeName)) {
    Report("project-render LiveLink: failed to connect to pipe '" +
               std::string(kPipeName) + "': " + g_pipeClient.GetLastError(),
           withDialog);
    return false;
  }
  return true;
}

std::string LiveLinkSessionController::MakeSessionId() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return std::string("archicad28-") + std::to_string(GetCurrentProcessId()) +
         "-" + std::to_string(millis);
}

DocumentInfo LiveLinkSessionController::ReadDocumentInfo() const {
  API_ProjectInfo projectInfo = {};
  DocumentInfo info;

  const GSErrCode err = ACAPI_ProjectOperation_Project(&projectInfo);
  if (err != NoError) {
    info.documentId = "archicad28-unsaved";
    info.displayName = "Unsaved Archicad Project";
    return info;
  }

  if (projectInfo.projectName != nullptr) {
    info.displayName = ToUtf8(*projectInfo.projectName);
  }

  if (!projectInfo.teamwork && projectInfo.location != nullptr) {
    info.documentPath = ToUtf8(projectInfo.location->ToDisplayText());
  } else if (projectInfo.location_team != nullptr) {
    info.documentPath = ToUtf8(projectInfo.location_team->ToLogText());
  }

  if (info.displayName.empty()) {
    if (!info.documentPath.empty()) {
      info.displayName = info.documentPath;
    } else if (projectInfo.untitled) {
      info.displayName = "Untitled Archicad Project";
    } else {
      info.displayName = "Archicad Project";
    }
  }

  info.documentId = SanitizeDocumentId(!info.documentPath.empty()
                                           ? info.documentPath
                                           : info.displayName);
  return info;
}

bool LiveLinkSessionController::SendBatch(bool fullSync, json deltas,
                                          bool withDialog) {
  if (!EnsureConnected(withDialog)) {
    return false;
  }
  if (m_sessionId.empty()) {
    Report("project-render LiveLink: no active session id", withDialog);
    return false;
  }

  json batch;
  batch["providerName"] = kProviderName;
  batch["sessionId"] = m_sessionId;
  batch["sequence"] = m_nextSequence++;
  batch["fullSync"] = fullSync;
  batch["deltas"] = std::move(deltas);

  if (!g_pipeClient.SendJsonLine(batch.dump())) {
    Report("project-render LiveLink: failed to send batch: " +
               g_pipeClient.GetLastError(),
           withDialog);
    return false;
  }

  return true;
}

bool LiveLinkSessionController::SendSessionOpened(bool withDialog) {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionOpened"},
                        {"target",
                         json{{"sourceApp", kSourceApp},
                              {"documentId", m_documentInfo.documentId},
                              {"objectId", "session"},
                              {"objectType", "Unknown"}}},
                        {"payload",
                         json{{"documentPath", m_documentInfo.documentPath},
                              {"displayName", m_documentInfo.displayName}}}});
  return SendBatch(false, std::move(deltas), withDialog);
}

bool LiveLinkSessionController::SendSessionClosed(bool withDialog) {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionClosed"},
                        {"payload",
                         json{{"reason", "User stopped Archicad LiveLink"},
                              {"graceful", true}}}});
  return SendBatch(false, std::move(deltas), withDialog);
}

bool LiveLinkSessionController::BeginNewSession(bool reportOnSuccess,
                                                bool withDialog) {
  if (!EnsureConnected(withDialog)) {
    return false;
  }

  m_documentInfo = ReadDocumentInfo();
  m_sessionId = MakeSessionId();
  m_nextSequence = 1;
  m_nextRevision = 1;

  if (!SendSessionOpened(withDialog)) {
    ResetSessionState(false);
    return false;
  }

  m_sessionOpen = true;
  if (reportOnSuccess) {
    Report("project-render LiveLink: session started for '" +
           m_documentInfo.displayName + "'");
  }
  return true;
}

bool LiveLinkSessionController::EnsureSessionOpened(bool reportOnSuccess,
                                                    bool withDialog) {
  if (m_sessionOpen &&
      (!g_pipeClient.IsConnected() || m_sessionId.empty())) {
    ResetSessionState(false);
  }

  if (m_sessionOpen) {
    return true;
  }
  return BeginNewSession(reportOnSuccess, withDialog);
}

void LiveLinkSessionController::AppendExportDeltas(
    const std::vector<ElementExportRecord> &elements,
    const std::vector<MaterialExportRecord> &materials,
    const CameraExportRecord *camera,
    std::vector<json> *outDeltas, uint64_t *ioRevision) const {
  if (outDeltas == nullptr || ioRevision == nullptr) {
    return;
  }

  static const std::array<float, 16> kIdentity = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };

  for (const ElementExportRecord &element : elements) {
    outDeltas->push_back(json{{"kind", "NodeAdded"},
                              {"target", MakeObjectIdJson(m_documentInfo.documentId,
                                                            element.objectId,
                                                            "Node")},
                              {"revision", (*ioRevision)++},
                              {"debugLabel", element.displayName},
                              {"payload",
                               json{{"parentObjectId", ""},
                                    {"displayName", element.displayName}}}});

    outDeltas->push_back(json{{"kind", "NodeTransformChanged"},
                              {"target", MakeObjectIdJson(m_documentInfo.documentId,
                                                            element.objectId,
                                                            "Node")},
                              {"revision", (*ioRevision)++},
                              {"debugLabel", element.displayName},
                              {"payload", json{{"worldMatrix", kIdentity}}}});

    outDeltas->push_back(json{{"kind", "NodeVisibilityChanged"},
                              {"target", MakeObjectIdJson(m_documentInfo.documentId,
                                                            element.objectId,
                                                            "Node")},
                              {"revision", (*ioRevision)++},
                              {"debugLabel", element.displayName},
                              {"payload", json{{"visible", element.visible}}}});

    const uint64_t geometryRevision = (*ioRevision)++;
    outDeltas->push_back(json{{"kind", "MeshPayloadChanged"},
                              {"target", MakeObjectIdJson(m_documentInfo.documentId,
                                                            element.objectId,
                                                            "Node")},
                              {"revision", geometryRevision},
                              {"debugLabel", element.displayName},
                              {"payload",
                               json{{"geometryRevision", geometryRevision},
                                    {"vertexCount", element.vertexCount},
                                    {"indexCount", element.indexCount},
                                    {"topologyChanged", true},
                                    {"payloadUri", element.payloadUri},
                                    {"payloadHash",
                                     std::to_string(element.vertexCount) + ":" +
                                         std::to_string(element.indexCount)}}}});
  }

  for (const MaterialExportRecord &material : materials) {
    json references = json::array();
    for (const auto &reference : material.references) {
      references.push_back(
          json{{"nodeObjectId", reference.nodeObjectId},
               {"materialSlot", reference.materialSlot}});
    }

    outDeltas->push_back(json{
        {"kind", "MaterialChanged"},
        {"target", MakeObjectIdJson(m_documentInfo.documentId, material.objectId,
                                     "Material")},
        {"revision", (*ioRevision)++},
        {"debugLabel", material.name},
        {"payload",
         json{{"parametersChanged", true},
              {"texturesChanged", !material.baseColorTextureUri.empty()},
              {"nodeObjectId", material.nodeObjectId},
              {"materialStableId", material.materialStableId},
              {"materialSlot", material.materialSlot},
              {"references", std::move(references)},
              {"name", material.name},
              {"materialModel", material.materialModel},
              {"baseColor", material.baseColor},
              {"baseColorTextureUri", material.baseColorTextureUri},
              {"emissiveColor", material.emissiveColor},
              {"emissiveIntensity", material.emissiveIntensity},
              {"roughness", material.roughness},
              {"metalness", material.metalness},
              {"specularWeight", material.specularWeight},
              {"ior", material.ior},
              {"transmissionWeight", material.transmissionWeight},
              {"transmissionColor", material.transmissionColor},
              {"doubleSided", material.doubleSided},
              {"alphaMode", material.alphaMode}}}});
  }

  if (camera != nullptr && camera->valid) {
    outDeltas->push_back(
        MakeCameraDelta(m_documentInfo.documentId, *camera, (*ioRevision)++));
  }
}

bool LiveLinkSessionController::SendDeltaChunks(const std::vector<json> &deltas,
                                                bool firstBatchFullSync,
                                                bool withDialog) {
  if (deltas.empty()) {
    return true;
  }

  size_t offset = 0;
  bool firstBatch = true;
  while (offset < deltas.size()) {
    json chunk = json::array();
    const size_t end = std::min(offset + kMaxDeltasPerBatch, deltas.size());
    for (size_t index = offset; index < end; ++index) {
      chunk.push_back(deltas[index]);
    }
    if (!SendBatch(firstBatch && firstBatchFullSync, std::move(chunk),
                   withDialog)) {
      return false;
    }
    firstBatch = false;
    offset = end;
  }

  return true;
}

void LiveLinkSessionController::BuildMaterialRecordsForKeys(
    const std::unordered_set<int> &materialKeys,
    std::vector<MaterialExportRecord> *outMaterials) const {
  if (outMaterials == nullptr) {
    return;
  }

  outMaterials->clear();
  if (materialKeys.empty()) {
    return;
  }

  std::vector<int> sortedMaterialKeys(materialKeys.begin(), materialKeys.end());
  std::sort(sortedMaterialKeys.begin(), sortedMaterialKeys.end());
  outMaterials->reserve(sortedMaterialKeys.size());

  for (int materialKey : sortedMaterialKeys) {
    MaterialExportRecord material;
    PopulateMaterialExportRecord(materialKey, &material);

    for (const auto &[_, element] : m_exportedElements) {
      for (const ElementExportRecord::MaterialBinding &binding :
           element.materialBindings) {
        if (binding.materialKey != materialKey) {
          continue;
        }
        if (material.nodeObjectId.empty()) {
          material.nodeObjectId = element.objectId;
          material.materialSlot = binding.materialSlot;
        }
        material.references.push_back({element.objectId, binding.materialSlot});
      }
    }

    if (material.references.empty()) {
      continue;
    }

    DeduplicateMaterialReferences(&material);
    outMaterials->push_back(std::move(material));
  }
}

void LiveLinkSessionController::SyncObservedElements() {
  std::unordered_set<std::string> desiredObjectIds;
  desiredObjectIds.reserve(m_exportedElements.size());

  for (const auto &[objectId, element] : m_exportedElements) {
    desiredObjectIds.insert(objectId);
    if (m_observedElementObjectIds.contains(objectId)) {
      continue;
    }

    const GSErrCode err = ACAPI_Element_AttachObserver(element.guid);
    if (err == NoError || err == APIERR_LINKEXIST) {
      m_observedElementObjectIds.insert(objectId);
      continue;
    }

    Report("project-render LiveLink: failed to observe Archicad element " +
           objectId + " (" + std::to_string(static_cast<int>(err)) + ")");
  }

  std::vector<std::string> staleObservedObjectIds;
  staleObservedObjectIds.reserve(m_observedElementObjectIds.size());
  for (const std::string &objectId : m_observedElementObjectIds) {
    if (!desiredObjectIds.contains(objectId)) {
      staleObservedObjectIds.push_back(objectId);
    }
  }

  for (const std::string &objectId : staleObservedObjectIds) {
    const GSErrCode err =
        ACAPI_Element_DetachObserver(APIGuidFromString(objectId.c_str()));
    if (err != NoError && err != APIERR_BADID) {
      Report("project-render LiveLink: failed to stop observing Archicad "
             "element " +
             objectId + " (" + std::to_string(static_cast<int>(err)) + ")");
    }
    m_observedElementObjectIds.erase(objectId);
  }
}

void LiveLinkSessionController::MarkElementDirty(const API_Guid &guid) {
  if (guid == APINULLGuid) {
    return;
  }

  const std::string objectId = MakeObjectId(guid);
  m_removedElementObjectIds.erase(objectId);
  m_dirtyElementObjectIds.insert(objectId);
  m_sceneDirty = true;
}

void LiveLinkSessionController::MarkElementRemoved(const API_Guid &guid) {
  if (guid == APINULLGuid) {
    return;
  }

  const std::string objectId = MakeObjectId(guid);
  m_dirtyElementObjectIds.erase(objectId);
  m_removedElementObjectIds.insert(objectId);
  m_sceneDirty = true;
}

bool LiveLinkSessionController::SendCameraDeltaIfChanged(bool withDialog) {
  if (!m_sessionOpen) {
    return false;
  }

  CameraExportRecord camera;
  if (!ReadCurrentCamera(&camera)) {
    return true;
  }
  if (SameCamera(camera, m_lastCamera)) {
    return true;
  }

  json deltas = json::array();
  deltas.push_back(
      MakeCameraDelta(m_documentInfo.documentId, camera, m_nextRevision++));
  if (!SendBatch(false, std::move(deltas), withDialog)) {
    HandleSessionLost(kReconnectRetryInterval);
    return false;
  }

  m_lastCamera = camera;
  return true;
}

bool LiveLinkSessionController::ExportFullScene(bool startingSession,
                                                bool reportSuccess,
                                                bool withDialog) {
  if (!EnsureSessionOpened(false, withDialog)) {
    if (m_syncActive) {
      ScheduleSceneSync(kReconnectRetryInterval);
    }
    return false;
  }

  std::vector<ElementExportRecord> elements;
  std::vector<MaterialExportRecord> materials;
  std::string exportError;
  if (!ExportSceneElements(m_documentInfo, nullptr, &elements, &materials,
                           &exportError)) {
    if (startingSession) {
      ResetSessionState(true);
    }
    if (m_syncActive) {
      m_fullSceneResyncNeeded = true;
      m_sceneDirty = true;
      ScheduleSceneSync(kReconnectRetryInterval);
    }
    Report("project-render LiveLink: failed to export Archicad scene: " +
               exportError,
           withDialog);
    return false;
  }

  std::vector<json> deltas;
  deltas.reserve(elements.size() * 4 + materials.size() + 2);
  deltas.push_back(json{{"kind", "FullSceneSync"},
                        {"payload", json{{"clearsExistingScene", true}}}});

  CameraExportRecord exportedCamera = {};
  const bool hasExportedCamera = ReadCurrentCamera(&exportedCamera);
  uint64_t revision = m_nextRevision;
  AppendExportDeltas(elements, materials,
                     hasExportedCamera ? &exportedCamera : nullptr, &deltas,
                     &revision);
  if (!SendDeltaChunks(deltas, true, withDialog)) {
    HandleSessionLost(kReconnectRetryInterval);
    return false;
  }
  m_nextRevision = revision;
  m_fullSceneResyncNeeded = false;
  m_sceneDirty = false;
  ClearPendingSceneSync();
  m_exportedElements.clear();
  m_exportedElements.reserve(elements.size());
  for (const ElementExportRecord &element : elements) {
    m_exportedElements[element.objectId] = element;
  }
  m_dirtyElementObjectIds.clear();
  m_removedElementObjectIds.clear();
  SyncObservedElements();
  m_lastCamera = hasExportedCamera ? exportedCamera : CameraExportRecord{};

  if (reportSuccess) {
    Report("project-render LiveLink: exported " +
           std::to_string(elements.size()) + " Archicad elements" +
           (startingSession ? std::string(" for '") + m_documentInfo.displayName +
                                  "'"
                            : std::string()));
  }
  return true;
}

bool LiveLinkSessionController::ExportDirtyElements(bool reportSuccess,
                                                    bool withDialog) {
  if (!EnsureSessionOpened(false, withDialog)) {
    if (m_syncActive) {
      ScheduleSceneSync(kReconnectRetryInterval);
    }
    return false;
  }

  if (m_dirtyElementObjectIds.empty() && m_removedElementObjectIds.empty()) {
    m_sceneDirty = false;
    ClearPendingSceneSync();
    return true;
  }

  std::vector<std::string> removedObjectIds(m_removedElementObjectIds.begin(),
                                            m_removedElementObjectIds.end());
  std::vector<std::string> dirtyObjectIds(m_dirtyElementObjectIds.begin(),
                                          m_dirtyElementObjectIds.end());
  std::sort(removedObjectIds.begin(), removedObjectIds.end());
  std::sort(dirtyObjectIds.begin(), dirtyObjectIds.end());

  std::unordered_set<int> affectedMaterialKeys;
  for (const std::string &objectId : removedObjectIds) {
    auto existingIt = m_exportedElements.find(objectId);
    if (existingIt == m_exportedElements.end()) {
      continue;
    }
    for (const ElementExportRecord::MaterialBinding &binding :
         existingIt->second.materialBindings) {
      affectedMaterialKeys.insert(binding.materialKey);
    }
    m_exportedElements.erase(existingIt);
  }

  GS::Array<API_Guid> dirtyGuids;
  for (const std::string &objectId : dirtyObjectIds) {
    if (m_removedElementObjectIds.contains(objectId)) {
      continue;
    }
    const API_Guid guid = APIGuidFromString(objectId.c_str());
    API_Elem_Head header = {};
    header.guid = guid;
    if (ACAPI_Element_GetHeader(&header) == NoError) {
      dirtyGuids.Push(guid);
      continue;
    }

    auto existingIt = m_exportedElements.find(objectId);
    if (existingIt != m_exportedElements.end()) {
      for (const ElementExportRecord::MaterialBinding &binding :
           existingIt->second.materialBindings) {
        affectedMaterialKeys.insert(binding.materialKey);
      }
      m_exportedElements.erase(existingIt);
    }
    removedObjectIds.push_back(objectId);
  }

  std::vector<ElementExportRecord> changedElements;
  if (!dirtyGuids.IsEmpty()) {
    std::vector<MaterialExportRecord> ignoredMaterials;
    std::string exportError;
    if (!ExportSceneElements(m_documentInfo, &dirtyGuids, &changedElements,
                             &ignoredMaterials, &exportError)) {
      m_fullSceneResyncNeeded = true;
      m_sceneDirty = true;
      ScheduleSceneSync(kReconnectRetryInterval);
      Report("project-render LiveLink: failed to export Archicad deltas: " +
                 exportError,
             withDialog);
      return false;
    }
  }

  std::unordered_set<std::string> exportedChangedObjectIds;
  exportedChangedObjectIds.reserve(changedElements.size());
  for (const ElementExportRecord &element : changedElements) {
    exportedChangedObjectIds.insert(element.objectId);

    auto existingIt = m_exportedElements.find(element.objectId);
    if (existingIt != m_exportedElements.end()) {
      for (const ElementExportRecord::MaterialBinding &binding :
           existingIt->second.materialBindings) {
        affectedMaterialKeys.insert(binding.materialKey);
      }
    }
    for (const ElementExportRecord::MaterialBinding &binding :
         element.materialBindings) {
      affectedMaterialKeys.insert(binding.materialKey);
    }
    m_exportedElements[element.objectId] = element;
  }

  for (const std::string &objectId : dirtyObjectIds) {
    if (exportedChangedObjectIds.contains(objectId) ||
        m_removedElementObjectIds.contains(objectId)) {
      continue;
    }

    auto existingIt = m_exportedElements.find(objectId);
    if (existingIt == m_exportedElements.end()) {
      continue;
    }

    for (const ElementExportRecord::MaterialBinding &binding :
         existingIt->second.materialBindings) {
      affectedMaterialKeys.insert(binding.materialKey);
    }
    removedObjectIds.push_back(objectId);
    m_exportedElements.erase(existingIt);
  }

  std::sort(removedObjectIds.begin(), removedObjectIds.end());
  removedObjectIds.erase(
      std::unique(removedObjectIds.begin(), removedObjectIds.end()),
      removedObjectIds.end());

  std::vector<MaterialExportRecord> materials;
  BuildMaterialRecordsForKeys(affectedMaterialKeys, &materials);

  std::vector<json> deltas;
  deltas.reserve(removedObjectIds.size() + changedElements.size() * 4 +
                 materials.size());
  uint64_t revision = m_nextRevision;
  for (const std::string &objectId : removedObjectIds) {
    deltas.push_back(
        MakeNodeRemovedDelta(m_documentInfo.documentId, objectId, revision++));
  }
  AppendExportDeltas(changedElements, materials, nullptr, &deltas, &revision);

  if (!deltas.empty() && !SendDeltaChunks(deltas, false, withDialog)) {
    HandleSessionLost(kReconnectRetryInterval);
    return false;
  }

  m_nextRevision = revision;
  for (const std::string &objectId : removedObjectIds) {
    m_removedElementObjectIds.erase(objectId);
    m_dirtyElementObjectIds.erase(objectId);
  }
  for (const std::string &objectId : dirtyObjectIds) {
    m_dirtyElementObjectIds.erase(objectId);
  }
  m_sceneDirty = !m_fullSceneResyncNeeded &&
                 (!m_dirtyElementObjectIds.empty() ||
                  !m_removedElementObjectIds.empty());
  if (!m_sceneDirty) {
    ClearPendingSceneSync();
  }
  SyncObservedElements();

  if (reportSuccess) {
    Report("project-render LiveLink: synced " +
           std::to_string(changedElements.size()) + " changed Archicad "
           "elements and removed " + std::to_string(removedObjectIds.size()) +
           " elements");
  }
  return true;
}

bool LiveLinkSessionController::Start() {
  if (m_syncActive) {
    Report("project-render LiveLink: session is already active");
    RefreshMenuState();
    return true;
  }

  m_commandInProgress = true;
  RefreshMenuState();
  m_fullSceneResyncNeeded = true;
  m_sceneDirty = true;
  ClearPendingSceneSync();

  const bool ok = ExportFullScene(true, true, true);
  if (ok) {
    m_syncActive = true;
    StartTimers();
    m_lastCamera = {};
    SendCameraDeltaIfChanged(false);
  } else {
    StopTimers();
    ResetSessionState(true);
    ResetTrackedSceneState(true);
    m_syncActive = false;
    m_sceneDirty = false;
    m_fullSceneResyncNeeded = true;
    ClearPendingSceneSync();
  }
  m_commandInProgress = false;
  RefreshMenuState();
  return ok;
}

bool LiveLinkSessionController::SyncNow() {
  if (!m_syncActive) {
    Report("project-render LiveLink: start a LiveLink session first", true);
    return false;
  }

  m_commandInProgress = true;
  RefreshMenuState();
  m_fullSceneResyncNeeded = true;
  m_sceneDirty = true;
  ClearPendingSceneSync();

  const bool ok = ExportFullScene(!m_sessionOpen, true, true);
  m_commandInProgress = false;
  RefreshMenuState();
  return ok;
}

bool LiveLinkSessionController::Stop(bool silent) {
  const bool hadSyncActive = m_syncActive;
  m_commandInProgress = true;
  RefreshMenuState();
  StopTimers();
  m_syncActive = false;

  if (!m_sessionOpen) {
    g_pipeClient.Disconnect();
    ResetTrackedSceneState(true);
    m_commandInProgress = false;
    m_sceneDirty = false;
    m_fullSceneResyncNeeded = true;
    ClearPendingSceneSync();
    RefreshMenuState();
    if (!silent && hadSyncActive) {
      Report("project-render LiveLink: session stopped");
    } else if (!silent) {
      Report("project-render LiveLink: no active session to stop");
    }
    return true;
  }

  const bool sent = SendSessionClosed(!silent);
  ResetSessionState(true);
  ResetTrackedSceneState(true);
  m_commandInProgress = false;
  m_sceneDirty = false;
  m_fullSceneResyncNeeded = true;
  ClearPendingSceneSync();
  RefreshMenuState();

  if (sent && !silent) {
    Report("project-render LiveLink: session stopped");
  }
  return sent;
}

void LiveLinkSessionController::OnScenePollTimer() {
  if (!m_syncActive || m_commandInProgress) {
    return;
  }
  if (!m_fullSceneResyncNeeded && !m_sceneDirty &&
      m_dirtyElementObjectIds.empty() && m_removedElementObjectIds.empty()) {
    return;
  }
  if (m_nextSceneSyncDeadline != Clock::time_point{} &&
      Clock::now() < m_nextSceneSyncDeadline) {
    return;
  }

  const bool ok = m_fullSceneResyncNeeded
                      ? ExportFullScene(!m_sessionOpen, false, false)
                      : ExportDirtyElements(false, false);
  if (!ok) {
    ScheduleSceneSync(kReconnectRetryInterval);
  }
}

void LiveLinkSessionController::OnCameraPollTimer() {
  if (!m_syncActive || m_commandInProgress || !m_sessionOpen ||
      m_fullSceneResyncNeeded) {
    return;
  }

  SendCameraDeltaIfChanged(false);
}

void LiveLinkSessionController::MarkSceneDirty(API_NotifyEventID notifID) {
  if (!m_syncActive || m_commandInProgress) {
    return;
  }

  switch (notifID) {
  case APINotify_AllInputFinished:
    if (!m_dirtyElementObjectIds.empty() || !m_removedElementObjectIds.empty()) {
      m_sceneDirty = true;
      ScheduleSceneSync(kSceneResyncDebounce);
    }
    break;
  case APINotify_ReceiveChanges:
    m_sceneDirty = true;
    m_fullSceneResyncNeeded = true;
    ScheduleSceneSync(kSceneResyncDebounce);
    break;
  default:
    break;
  }
}

void LiveLinkSessionController::OnElementNotification(
    const API_NotifyElementType *elemType) {
  if (!m_syncActive || m_commandInProgress || elemType == nullptr) {
    return;
  }

  switch (elemType->notifID) {
  case APINotifyElement_New:
  case APINotifyElement_Copy:
  case APINotifyElement_Change:
  case APINotifyElement_Edit:
  case APINotifyElement_Undo_Modified:
  case APINotifyElement_Undo_Deleted:
  case APINotifyElement_Redo_Created:
  case APINotifyElement_Redo_Modified:
  case APINotifyElement_PropertyValueChange:
  case APINotifyElement_ClassificationChange:
    MarkElementDirty(elemType->elemHead.guid);
    break;
  case APINotifyElement_Delete:
  case APINotifyElement_Undo_Created:
  case APINotifyElement_Redo_Deleted:
    MarkElementRemoved(elemType->elemHead.guid);
    break;
  default:
    return;
  }

  ScheduleSceneSync(kSceneResyncDebounce);
}

void SetMenuItemEnabled(Int32 itemIndex, bool enabled) {
  API_MenuItemRef itemRef = {};
  itemRef.menuResID = PROJECT_RENDER_ARCHICAD_MENU_STRINGS;
  itemRef.itemIndex = itemIndex;

  GSFlags itemFlags = 0;
  // Get existing flags if possible, but don't fail if the menu is not yet fully initialized
  ACAPI_MenuItem_GetMenuItemFlags(&itemRef, &itemFlags);

  if (enabled) {
    itemFlags &= ~API_MenuItemDisabled;
  } else {
    itemFlags |= API_MenuItemDisabled;
  }

  ACAPI_MenuItem_SetMenuItemFlags(&itemRef, &itemFlags);
}

GSErrCode MenuCommandHandler(const API_MenuParams *menuParams) {
  if (menuParams == nullptr ||
      menuParams->menuItemRef.menuResID != PROJECT_RENDER_ARCHICAD_MENU_STRINGS) {
    return NoError;
  }

  switch (menuParams->menuItemRef.itemIndex) {
  case 1:
    g_controller.Start();
    break;
  case 2:
    g_controller.SyncNow();
    break;
  case 3:
    g_controller.Stop();
    break;
  default:
    break;
  }

  return NoError;
}

void CALLBACK ScenePollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
  g_controller.OnScenePollTimer();
}

void CALLBACK CameraPollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
  g_controller.OnCameraPollTimer();
}

GSErrCode ProjectEventHandler(API_NotifyEventID notifID, Int32) {
  g_controller.MarkSceneDirty(notifID);
  return NoError;
}

GSErrCode ElementEventHandler(const API_NotifyElementType *elemType) {
  g_controller.OnElementNotification(elemType);
  return NoError;
}

} // namespace

API_AddonType CheckEnvironment(API_EnvirParams *envir) {
  RSGetIndString(&envir->addOnInfo.name, PROJECT_RENDER_ARCHICAD_ADDON_STRINGS,
                 1, ACAPI_GetOwnResModule());
  RSGetIndString(&envir->addOnInfo.description,
                 PROJECT_RENDER_ARCHICAD_ADDON_STRINGS, 2,
                 ACAPI_GetOwnResModule());
  return APIAddon_Normal;
}

GSErrCode RegisterInterface(void) {
  return ACAPI_MenuItem_RegisterMenu(
      PROJECT_RENDER_ARCHICAD_MENU_STRINGS,
      PROJECT_RENDER_ARCHICAD_MENU_PROMPT_STRINGS, MenuCode_UserDef,
      MenuFlag_Default);
}

GSErrCode Initialize(void) {
  const GSErrCode err = ACAPI_MenuItem_InstallMenuHandler(
      PROJECT_RENDER_ARCHICAD_MENU_STRINGS, MenuCommandHandler);
  if (err != NoError) {
    ACAPI_WriteReport(
        GS::UniString("project-render LiveLink: failed to install menu handler"),
        true);
    return err;
  }

  const GSErrCode notifyErr = ACAPI_ProjectOperation_CatchProjectEvent(
      APINotify_AllInputFinished | APINotify_ReceiveChanges,
      ProjectEventHandler);
  if (notifyErr != NoError) {
    ACAPI_WriteReport(
        GS::UniString("project-render LiveLink: failed to register project "
                      "change notifications"),
        false);
  }

  const GSErrCode catchNewErr =
      ACAPI_Element_CatchNewElement(nullptr, ElementEventHandler);
  if (catchNewErr != NoError) {
    ACAPI_WriteReport(
        GS::UniString("project-render LiveLink: failed to register new "
                      "element notifications"),
        false);
  }

  const GSErrCode observerErr =
      ACAPI_Element_InstallElementObserver(ElementEventHandler);
  if (observerErr != NoError) {
    ACAPI_WriteReport(
        GS::UniString("project-render LiveLink: failed to install element "
                      "observer"),
        false);
  }

  g_controller.RefreshMenuState();

  ACAPI_WriteReport(GS::UniString("project-render LiveLink: initialized"), false);

  return NoError;
}

GSErrCode FreeData(void) {
  g_controller.Stop(true);
  ACAPI_Element_CatchNewElement(nullptr, nullptr);
  ACAPI_Element_InstallElementObserver(nullptr);
  ACAPI_ProjectOperation_CatchProjectEvent(
      APINotify_AllInputFinished | APINotify_ReceiveChanges, nullptr);
  return NoError;
}
