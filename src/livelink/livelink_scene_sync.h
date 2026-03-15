#pragma once

#include "livelink_coordinator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace LiveLink {

class LiveLinkSceneSync {
public:
  void ApplyQueuedBatches(LiveLinkCoordinator &coordinator);

private:
  enum class EngineHandleKind {
    Unknown,
    SceneNode,
    MainCamera,
    Environment,
  };

  struct ObjectBinding {
    ObjectId objectId;
    std::string sessionId;
    EngineHandleKind handleKind = EngineHandleKind::Unknown;
    size_t handleIndex = static_cast<size_t>(-1);
    uint64_t lastAppliedRevision = 0;
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
  bool ApplySelectionChanged(const SceneDeltaBatch &batch,
                             const SceneDelta &delta);
  bool ApplyCameraChanged(const SceneDeltaBatch &batch, const SceneDelta &delta);
  bool ApplyEnvironmentChanged(const SceneDeltaBatch &batch,
                               const SceneDelta &delta);

  ObjectBinding *FindBinding(const ObjectId &objectId);
  const ObjectBinding *FindBinding(const ObjectId &objectId) const;
  ObjectBinding &BindObject(const ObjectId &objectId,
                            const std::string &sessionId,
                            EngineHandleKind handleKind,
                            size_t handleIndex = static_cast<size_t>(-1));
  bool EnsureNodeBinding(const SceneDeltaBatch &batch, const SceneDelta &delta,
                         const std::string &preferredName,
                         ObjectBinding **outBinding = nullptr);
  void RemoveBindingsForSession(const std::string &sessionId);
  void ReindexSceneNodeBindingsAfterRemoval(size_t removedIndex);
  void ClearAllBindings();
  void LogValidationIssue(const ValidationIssue &issue) const;
  void LogApplyIssue(const char *level, const std::string &providerName,
                     const std::string &sessionId, const SceneDelta *delta,
                     const std::string &message) const;

  std::unordered_map<ObjectId, ObjectBinding, ObjectIdHash> m_bindings;
};

LiveLinkSceneSync &GetSceneSync();

} // namespace LiveLink