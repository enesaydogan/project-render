#include "archicad_livelink_pipe_client.h"
#include "resources.hpp"

#include "ACAPinc.h"

#include "AttributeIndex.hpp"
#include "ConvexPolygon.hpp"
#include "Model.hpp"
#include "ModelElement.hpp"
#include "ModelMeshBody.hpp"
#include "Polygon.hpp"
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
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

using json = nlohmann::json;

namespace {

constexpr const char *kPipeName = "project-render-archicad-livelink";
constexpr const char *kProviderName = "Archicad28Pipe";
constexpr const char *kSourceApp = "Archicad28";
constexpr size_t kMaxDeltasPerBatch = 96;

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
  std::string objectId;
  std::string displayName;
  std::string payloadUri;
  uint64_t vertexCount = 0;
  uint64_t indexCount = 0;
  bool visible = true;
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

json MakeObjectIdJson(const std::string &documentId, const std::string &objectId,
                      const char *objectType) {
  return json{{"sourceApp", kSourceApp},
              {"documentId", documentId},
              {"objectId", objectId},
              {"objectType", objectType}};
}

bool ExportElementMeshPayload(const std::string &documentId,
                              const ElementExportRecord &record,
                              const ModelerAPI::Element &element,
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
      const int materialKey = materialIndex.IsValid() ? materialIndex.GetIndex() : 0;

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
            payloadVertex.position[0] = static_cast<float>(vertex.x);
            payloadVertex.position[1] = static_cast<float>(vertex.y);
            payloadVertex.position[2] = static_cast<float>(vertex.z);
            payloadVertex.normal[0] = static_cast<float>(normal.x);
            payloadVertex.normal[1] = static_cast<float>(normal.y);
            payloadVertex.normal[2] = static_cast<float>(normal.z);
            Normalize3(payloadVertex.normal, kUpFallback);

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

  *outPayloadUri = PathToUtf8(payloadPath);
  *outVertexCount = totalVertexCount;
  *outIndexCount = totalIndexCount;
  return true;
}

bool ExportSceneElements(const DocumentInfo &documentInfo,
                         std::vector<ElementExportRecord> *outElements,
                         std::string *outError) {
  if (outElements == nullptr) {
    return false;
  }

  GS::Array<API_Guid> elementGuids;
  const GSErrCode listErr =
      ACAPI_Element_GetElemList(API_ZombieElemID, &elementGuids, APIFilt_In3D);
  if (listErr != NoError) {
    if (outError != nullptr) {
      *outError = "ACAPI_Element_GetElemList failed (" +
                  std::to_string(static_cast<int>(listErr)) + ")";
    }
    return false;
  }
  if (elementGuids.IsEmpty()) {
    outElements->clear();
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
  const int elementCount = model.GetElementCount();
  exportedElements.reserve(static_cast<size_t>(elementCount));
  for (int elementIndex = 1; elementIndex <= elementCount; ++elementIndex) {
    ModelerAPI::Element element;
    model.GetElement(elementIndex, &element);

    const API_Guid guid = GSGuid2APIGuid(element.GetElemGuid());
    ElementExportRecord record;
    record.objectId = MakeObjectId(guid);
    record.displayName = GetElementDisplayName(guid);

    if (!ExportElementMeshPayload(documentInfo.documentId, record, element,
                                  &record.payloadUri, &record.vertexCount,
                                  &record.indexCount)) {
      continue;
    }

    exportedElements.push_back(std::move(record));
  }

  *outElements = std::move(exportedElements);
  return true;
}

class LiveLinkSessionController {
public:
  bool Start();
  bool SyncNow();
  bool Stop(bool silent = false);

private:
  bool EnsureConnected();
  bool EnsureSessionOpened();
  bool BeginNewSession(bool reportOnSuccess);
  bool SendBatch(bool fullSync, json deltas);
  bool SendSessionOpened();
  bool SendSessionClosed();
  bool ExportFullScene(bool startingSession);
  DocumentInfo ReadDocumentInfo() const;
  std::string MakeSessionId() const;
  void AppendExportDeltas(const std::vector<ElementExportRecord> &elements,
                          std::vector<json> *outDeltas,
                          uint64_t *ioRevision) const;
  bool SendDeltaChunks(const std::vector<json> &deltas, bool firstBatchFullSync);
  static void Report(const std::string &message, bool withDialog = false);

  bool m_sessionOpen = false;
  std::string m_sessionId;
  DocumentInfo m_documentInfo;
  uint64_t m_nextSequence = 1;
  uint64_t m_nextRevision = 1;
};

LiveLinkSessionController g_controller;

void LiveLinkSessionController::Report(const std::string &message,
                                       bool withDialog) {
  ACAPI_WriteReport(GS::UniString(message.c_str()), withDialog);
}

bool LiveLinkSessionController::EnsureConnected() {
  if (g_pipeClient.IsConnected()) {
    return true;
  }
  if (!g_pipeClient.Connect(kPipeName)) {
    Report("project-render LiveLink: failed to connect to pipe '" +
               std::string(kPipeName) + "': " + g_pipeClient.GetLastError(),
           true);
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

bool LiveLinkSessionController::SendBatch(bool fullSync, json deltas) {
  if (!EnsureConnected()) {
    return false;
  }
  if (m_sessionId.empty()) {
    Report("project-render LiveLink: no active session id", true);
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
           true);
    return false;
  }

  return true;
}

bool LiveLinkSessionController::SendSessionOpened() {
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
  return SendBatch(false, std::move(deltas));
}

bool LiveLinkSessionController::SendSessionClosed() {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionClosed"},
                        {"payload",
                         json{{"reason", "User stopped Archicad LiveLink"},
                              {"graceful", true}}}});
  return SendBatch(false, std::move(deltas));
}

bool LiveLinkSessionController::BeginNewSession(bool reportOnSuccess) {
  if (!EnsureConnected()) {
    return false;
  }

  m_documentInfo = ReadDocumentInfo();
  m_sessionId = MakeSessionId();
  m_nextSequence = 1;
  m_nextRevision = 1;

  if (!SendSessionOpened()) {
    m_sessionId.clear();
    return false;
  }

  m_sessionOpen = true;
  if (reportOnSuccess) {
    Report("project-render LiveLink: session started for '" +
           m_documentInfo.displayName + "'");
  }
  return true;
}

bool LiveLinkSessionController::EnsureSessionOpened() {
  if (m_sessionOpen) {
    return true;
  }
  return BeginNewSession(true);
}

void LiveLinkSessionController::AppendExportDeltas(
    const std::vector<ElementExportRecord> &elements, std::vector<json> *outDeltas,
    uint64_t *ioRevision) const {
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
}

bool LiveLinkSessionController::SendDeltaChunks(const std::vector<json> &deltas,
                                                bool firstBatchFullSync) {
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
    if (!SendBatch(firstBatch && firstBatchFullSync, std::move(chunk))) {
      return false;
    }
    firstBatch = false;
    offset = end;
  }

  return true;
}

bool LiveLinkSessionController::ExportFullScene(bool startingSession) {
  if (!EnsureSessionOpened()) {
    return false;
  }

  std::vector<ElementExportRecord> elements;
  std::string exportError;
  if (!ExportSceneElements(m_documentInfo, &elements, &exportError)) {
    Report("project-render LiveLink: failed to export Archicad scene: " +
               exportError,
           true);
    return false;
  }

  std::vector<json> deltas;
  deltas.reserve(elements.size() * 4 + 1);
  deltas.push_back(json{{"kind", "FullSceneSync"},
                        {"payload", json{{"clearsExistingScene", true}}}});

  uint64_t revision = m_nextRevision;
  AppendExportDeltas(elements, &deltas, &revision);
  if (!SendDeltaChunks(deltas, true)) {
    return false;
  }
  m_nextRevision = revision;

  Report("project-render LiveLink: exported " + std::to_string(elements.size()) +
         " Archicad elements" +
         (startingSession ? std::string(" for '") + m_documentInfo.displayName +
                                "'"
                          : std::string()));
  return true;
}

bool LiveLinkSessionController::Start() {
  if (m_sessionOpen) {
    Report("project-render LiveLink: session is already active");
    return true;
  }
  if (!BeginNewSession(false)) {
    return false;
  }
  return ExportFullScene(true);
}

bool LiveLinkSessionController::SyncNow() { return ExportFullScene(false); }

bool LiveLinkSessionController::Stop(bool silent) {
  if (!m_sessionOpen) {
    g_pipeClient.Disconnect();
    if (!silent) {
      Report("project-render LiveLink: no active session to stop");
    }
    return true;
  }

  const bool sent = SendSessionClosed();
  g_pipeClient.Disconnect();
  m_sessionOpen = false;
  m_sessionId.clear();
  m_documentInfo = {};
  m_nextSequence = 1;
  m_nextRevision = 1;

  if (sent) {
    Report("project-render LiveLink: session stopped");
  }
  return sent;
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

  // Explicitly enable menu items. 
  // ArchiCAD may require these to be set after registration to avoid being greyed out by default.
  SetMenuItemEnabled(1, true);
  SetMenuItemEnabled(2, true);
  SetMenuItemEnabled(3, true);

  ACAPI_WriteReport(GS::UniString("project-render LiveLink: initialized"), false);

  return NoError;
}

GSErrCode FreeData(void) {
  g_controller.Stop(true);
  return NoError;
}