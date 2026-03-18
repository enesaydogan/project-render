#pragma once

#include "livelink_coordinator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace LiveLink {

struct LiveLinkDiagnosticEntry {
  uint64_t sequence = 0;
  std::string level;
  std::string providerName;
  std::string sessionId;
  std::string deltaKind;
  std::string targetId;
  std::string message;
};

class LiveLinkSceneSync {
public:
  struct StatsSnapshot {
    size_t nodeCount = 0;
    size_t meshCount = 0;
    size_t lightCount = 0;
    size_t materialCount = 0;
    size_t totalBindingCount = 0;
    size_t activeSessionBindingCount = 0;
    bool cameraBound = false;
    bool environmentBound = false;
  };

  enum class EngineHandleKind {
    Unknown,
    SceneNode,
    SceneLight,
    SceneMaterial,
    MainCamera,
    Environment,
  };

  struct PersistedBinding {
    ObjectId objectId;
    EngineHandleKind handleKind = EngineHandleKind::Unknown;
    size_t handleIndex = static_cast<size_t>(-1);
  };

  void ApplyQueuedBatches(LiveLinkCoordinator &coordinator);
  std::vector<LiveLinkDiagnosticEntry> GetRecentDiagnostics() const;
  void DetachCameraControl();
  void ResumeCameraControl();
  bool IsCameraControlDetached() const;
  StatsSnapshot GetStatsSnapshot() const;
  std::vector<PersistedBinding> ExportPersistedBindings() const;
  void RestorePersistedBindings(const std::vector<PersistedBinding> &bindings);

private:
  struct ObjectBinding {
    ObjectId objectId;
    std::string sessionId;
    EngineHandleKind handleKind = EngineHandleKind::Unknown;
    size_t handleIndex = static_cast<size_t>(-1);
    uint64_t lastAppliedRevision = 0;
  };

  struct CachedCameraState {
    bool valid = false;
    ObjectId objectId;
    std::string sessionId;
    uint64_t revision = 0;
    CameraChangedPayload payload;
  };

  bool ApplyBatch(const SceneDeltaBatch &batch);
  bool ApplyDelta(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplySessionOpened(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplySessionClosed(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyFullSceneSync(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyNodeAdded(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyNodeRemoved(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyNodeTransformChanged(const SceneDeltaBatch &batch,
                                 const SceneDelta &delta);
  bool ApplyNodeVisibilityChanged(const SceneDeltaBatch &batch,
                                  const SceneDelta &delta);
  bool ApplyMeshPayloadChanged(const SceneDeltaBatch &batch,
                               const SceneDelta &delta);
  bool ApplyMaterialChanged(const SceneDeltaBatch &batch,
                            const SceneDelta &delta);
  bool ApplyLightChanged(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplySelectionChanged(const SceneDeltaBatch &batch,
                             const SceneDelta &delta);
  bool ApplyCameraChanged(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyEnvironmentChanged(const SceneDeltaBatch &batch,
                               const SceneDelta &delta);
  void ApplyCachedCameraState(const CachedCameraState &state);

  ObjectBinding *FindBinding(const ObjectId &objectId);
  const ObjectBinding *FindBinding(const ObjectId &objectId) const;
  ObjectBinding *FindRelatedBinding(const ObjectId &objectId,
                                    EngineHandleKind handleKind);
  ObjectBinding &BindObject(const ObjectId &objectId,
                            const std::string &sessionId,
                            EngineHandleKind handleKind,
                            size_t handleIndex = static_cast<size_t>(-1));
  bool EnsureNodeBinding(const SceneDeltaBatch &batch, const SceneDelta &delta,
                         const std::string &preferredName,
                         ObjectBinding **outBinding = nullptr);
  bool EnsureLightBinding(const SceneDeltaBatch &batch, const SceneDelta &delta,
                          ObjectBinding **outBinding = nullptr);
  bool EnsureMaterialBinding(const SceneDeltaBatch &batch,
                             const SceneDelta &delta,
                             ObjectBinding **outBinding = nullptr);
  void RemoveSessionContent(const std::string &sessionId);
  void ReindexSceneNodeBindingsAfterRemoval(size_t removedIndex);
  void ReindexSceneLightBindingsAfterRemoval(size_t removedIndex);
  void ClearAllBindings();
  void AppendDiagnosticEntry(const char *level, const std::string &providerName,
                             const std::string &sessionId,
                             const std::string &deltaKind,
                             const std::string &targetId,
                             const std::string &message) const;
  void LogValidationIssue(const ValidationIssue &issue) const;
  void LogApplyIssue(const char *level, const std::string &providerName,
                     const std::string &sessionId, const SceneDelta *delta,
                     const std::string &message) const;

  std::unordered_map<ObjectId, ObjectBinding, ObjectIdHash> m_bindings;
  std::unordered_map<std::string, int> m_textureIndicesByUri;
  CachedCameraState m_cachedExternalCamera;
  bool m_cameraControlDetached = false;
  mutable uint64_t m_nextDiagnosticSequence = 1;
  mutable std::vector<LiveLinkDiagnosticEntry> m_recentDiagnostics;
};

LiveLinkSceneSync &GetSceneSync();

} // namespace LiveLink