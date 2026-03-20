#include "livelink_scene_sync.h"

#include "../assets/asset_loader.h"
#include "../camera.h"
#include "../dxr_renderer.h"
#include "../ibl_manager.h"
#include "../scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
  if (!delta.target.objectId.empty()) {
    return delta.target.objectId;
  }
  if (!delta.debugLabel.empty()) {
    return delta.debugLabel;
  }
  return "Material";
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

bool LoadNativeMeshPayload(const std::string &path,
                           std::vector<Asset::GpuMesh> *outMeshes) {
  if (!outMeshes) {
    return false;
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

    Asset::GpuMesh mesh = Asset::LoadMeshFromMemory(vertices, indices);
    mesh.materialIndex = 0;
    outMeshes->push_back(std::move(mesh));
    return !outMeshes->empty() && outMeshes->front().vertexCount > 0 &&
           outMeshes->front().indexCount > 0;
  }

  if (header.version != 2) {
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

    Asset::GpuMesh mesh = Asset::LoadMeshFromMemory(vertices, indices);
    mesh.materialIndex = (std::max)(0, meshHeader.materialSlot);
    outMeshes->push_back(std::move(mesh));
  }

  return !outMeshes->empty();
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
         changedArray3(lhs.forward, rhs.forward, kDirectionEpsilon) ||
         changedArray3(lhs.up, rhs.up, kDirectionEpsilon) ||
         fabsf(lhs.fovDegrees - rhs.fovDegrees) > kScalarEpsilon ||
         fabsf(lhs.nearPlane - rhs.nearPlane) > kScalarEpsilon ||
         fabsf(lhs.farPlane - rhs.farPlane) > kScalarEpsilon;
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
  (void)batch;
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
    m_bindings.erase(delta.target);
    return true;
  }

  const size_t removedIndex = binding->handleIndex;
  if (removedIndex < Scene::GetNodes().size()) {
    Scene::RemoveNode(removedIndex);
  }

  m_bindings.erase(delta.target);
  ReindexSceneNodeBindingsAfterRemoval(removedIndex);
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
  std::vector<Asset::Texture> textures;
      const std::filesystem::path payloadPath =
        Utf8PathFromString(payload->payloadUri);
    const std::string extension = payloadPath.extension().string();
  const bool loaded = extension == ".prmesh"
                          ? LoadNativeMeshPayload(payload->payloadUri, &meshes)
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

  if (extension == ".prmesh") {
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
  importedPayload.textures = std::move(textures);

  if (!Scene::ReplaceNodeImportedContent(binding->handleIndex,
                                         std::move(importedPayload))) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to replace node content from mesh payload");
    return false;
  }

  binding->lastAppliedRevision = delta.revision;
  return true;
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

  Asset::Material material;
  if (!Scene::GetMaterial(binding->handleIndex, &material)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to fetch bound material");
    return false;
  }

  PruneTextureCacheEntries();

  for (size_t i = 0; i < payload->baseColor.size(); ++i) {
    material.diffuseColor[i] = payload->baseColor[i];
    material.emissiveColor[i] = payload->emissiveColor[i];
  }

  auto resolveTextureIndex = [this](const std::string &textureUri) {
    if (textureUri.empty()) {
      return -1;
    }

    auto cached = m_textureIndicesByUri.find(textureUri);
    if (cached != m_textureIndicesByUri.end()) {
      return cached->second;
    }

    const std::filesystem::path texturePath = Utf8PathFromString(textureUri);
    std::error_code error;
    if (texturePath.empty() || !std::filesystem::exists(texturePath, error)) {
      return -1;
    }

    const int textureIndex =
        Scene::AddTextureFromFile(textureUri, IsHdrTextureUri(textureUri));
    if (textureIndex >= 0) {
      m_textureIndicesByUri.emplace(textureUri, textureIndex);
    }
    return textureIndex;
  };

  material.diffuseTexture = resolveTextureIndex(payload->baseColorTextureUri);
  material.normalTexture = resolveTextureIndex(payload->normalTextureUri);
  material.emissiveTexture = resolveTextureIndex(payload->emissiveTextureUri);
  material.occlusionTexture = resolveTextureIndex(payload->occlusionTextureUri);
  material.metalRoughTexture = resolveTextureIndex(payload->metalRoughTextureUri);
  material.emissiveIntensity = payload->emissiveIntensity;
  material.roughness = payload->roughness;
  material.metalness = payload->metalness;
  material.specularWeight = payload->specularWeight;
  material.ior = payload->ior;
  material.transmissionWeight = payload->transmissionWeight;
  for (size_t i = 0; i < payload->transmissionColor.size(); ++i) {
    material.transmissionColor[i] = payload->transmissionColor[i];
  }
  material.coatWeight = payload->coatWeight;
  material.coatRoughness = payload->coatRoughness;
  material.thinWalled = payload->thinWalled;
  material.translucency = payload->translucency;
  material.uvScale[0] = fabsf(payload->uvScale[0]) > 1.0e-6f ? payload->uvScale[0] : 1.0f;
  material.uvScale[1] = fabsf(payload->uvScale[1]) > 1.0e-6f ? payload->uvScale[1] : 1.0f;
  material.uvOffset[0] = payload->uvOffset[0];
  material.uvOffset[1] = payload->uvOffset[1];
  material.doubleSided = payload->doubleSided;
  material.alphaMode = payload->alphaMode.empty() ? "OPAQUE" : payload->alphaMode;
  if (!payload->materialModel.empty()) {
    material.schemaVersion = Asset::Material::kSchemaVersionOpenPbrSubset;
  }

  const std::string materialName = ResolveMaterialName(delta);
  strncpy_s(material.name, materialName.c_str(), _TRUNCATE);

  if (!Scene::UpdateMaterial(binding->handleIndex, material)) {
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  "Failed to apply material change");
    return false;
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
  ObjectBinding *binding = FindBinding(delta.target);
  if (!binding) {
    int materialIndex = -1;
    if (const MaterialChangedPayload *payload =
            FindPayload<MaterialChangedPayload>(delta)) {
      if (!payload->nodeObjectId.empty()) {
        ObjectId nodeObjectId = delta.target;
        nodeObjectId.objectId = payload->nodeObjectId;
        nodeObjectId.objectType = ObjectType::Node;
        if (const ObjectBinding *nodeBinding = FindBinding(nodeObjectId)) {
          if (nodeBinding->handleKind == EngineHandleKind::SceneNode &&
              nodeBinding->handleIndex < Scene::GetNodes().size()) {
            const Scene::Node &node =
                Scene::GetNodes()[nodeBinding->handleIndex];
            const int materialSlot = (std::max)(0, payload->materialSlot);
            if (materialSlot <
                static_cast<int>(node.linkedMaterialIndices.size())) {
              const int candidate =
                  node.linkedMaterialIndices[static_cast<size_t>(materialSlot)];
              if (candidate >= 0 &&
                  candidate < static_cast<int>(Scene::GetMaterialCount())) {
                materialIndex = candidate;
              }
            }
          }
        }
      }
    }

    if (materialIndex < 0) {
      materialIndex = Scene::FindMaterialByName(ResolveMaterialName(delta));
    }
    if (materialIndex < 0) {
      LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                    "No scene material matched the live-link target");
      return false;
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

  if (binding->handleIndex == kInvalidHandle ||
      binding->handleIndex >= Scene::GetMaterialCount()) {
    const int materialIndex =
        Scene::FindMaterialByName(ResolveMaterialName(delta));
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
  for (auto &[_, binding] : m_bindings) {
    if (binding.sessionId == sessionId) {
      binding.sessionId.clear();
      binding.lastAppliedRevision = 0;
    }
  }
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

void LiveLinkSceneSync::ClearAllBindings() {
  m_bindings.clear();
  m_textureIndicesByUri.clear();
  m_cachedExternalCamera = CachedCameraState{};
  m_cameraControlDetached = false;
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