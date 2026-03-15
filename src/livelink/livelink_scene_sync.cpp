#include "livelink_scene_sync.h"

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


} // namespace

LiveLinkSceneSync &GetSceneSync() {
  static LiveLinkSceneSync s_sceneSync;
  return s_sceneSync;
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
  case SceneDeltaKind::SelectionChanged:
    return ApplySelectionChanged(batch, delta);
  case SceneDeltaKind::CameraChanged:
    return ApplyCameraChanged(batch, delta);
  case SceneDeltaKind::EnvironmentChanged:
    return ApplyEnvironmentChanged(batch, delta);
  case SceneDeltaKind::MeshPayloadChanged:
  case SceneDeltaKind::MaterialChanged:
  case SceneDeltaKind::LightChanged:
    LogApplyIssue("Warning", batch.providerName, batch.sessionId, &delta,
                  std::string("Delta kind not applied yet: ") +
                      ToString(delta.kind));
    return false;
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

void LiveLinkSceneSync::ClearAllBindings() { m_bindings.clear(); }

void LiveLinkSceneSync::LogValidationIssue(const ValidationIssue &issue) const {
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
  fprintf(stderr,
          "LiveLink: [%s] provider='%s' session='%s' delta='%s' target='%s' %s\n",
          level, providerName.c_str(), sessionId.c_str(), kind, objectId,
          message.c_str());
}

} // namespace LiveLink