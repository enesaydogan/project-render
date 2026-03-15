#include "livelink_scene_sync.h"

#include "../assets/asset_loader.h"
#include "../camera.h"
#include "../dxr_renderer.h"
#include "../ibl_manager.h"
#include "../scene.h"

#include <cstdio>
#include <string_view>

namespace LiveLink {

namespace {

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


} // namespace

LiveLinkSceneSync &GetSceneSync() {
  static LiveLinkSceneSync s_sceneSync;
  return s_sceneSync;
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
  RemoveBindingsForSession(batch.sessionId);
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
  if (!Asset::LoadModel(payload->payloadUri, meshes, &materials, &textures)) {
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

  for (size_t i = 0; i < payload->baseColor.size(); ++i) {
    material.diffuseColor[i] = payload->baseColor[i];
    material.emissiveColor[i] = payload->emissiveColor[i];
  }
  material.emissiveIntensity = payload->emissiveIntensity;
  material.roughness = payload->roughness;
  material.metalness = payload->metalness;
  material.transmissionWeight = payload->transmissionWeight;
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
  light.emission[0] = payload->color[0] * payload->intensity;
  light.emission[1] = payload->color[1] * payload->intensity;
  light.emission[2] = payload->color[2] * payload->intensity;

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
  g_cameraData.fov = payload->fovDegrees;
  g_cameraData.nearZ = payload->nearPlane;
  g_cameraData.farZ = payload->farPlane;
  UpdateCameraCB();
  cameraBinding.lastAppliedRevision = delta.revision;
  return true;
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
    binding->handleIndex = Scene::AddNode(std::move(node));
  }

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
    const int materialIndex =
        Scene::FindMaterialByName(ResolveMaterialName(delta));
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

  if (outBinding) {
    *outBinding = binding;
  }
  return true;
}

void LiveLinkSceneSync::RemoveBindingsForSession(const std::string &sessionId) {
  for (auto it = m_bindings.begin(); it != m_bindings.end();) {
    if (it->second.sessionId == sessionId) {
      it = m_bindings.erase(it);
    } else {
      ++it;
    }
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

void LiveLinkSceneSync::ClearAllBindings() { m_bindings.clear(); }

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