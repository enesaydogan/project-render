#pragma once

#include "livelink_provider.h"

#include <chrono>
#include <cstdint>

namespace LiveLink {

class MockLiveLinkProvider : public ILiveLinkProvider {
public:
  std::string GetProviderName() const override;
  Capability GetCapabilities() const override;
  ConnectionState GetConnectionState() const override;
  std::string GetLastError() const override;

  bool Connect() override;
  void Disconnect() override;
  bool Poll(std::vector<SceneDeltaBatch> &outBatches) override;

private:
  SceneDelta MakeSessionOpenedDelta() const;
  SceneDelta MakeFullSceneSyncDelta() const;
  SceneDelta MakeNodeAddedDelta(const ObjectId &target, uint64_t revision,
                                const std::string &displayName) const;
  SceneDelta MakeNodeTransformDelta(const ObjectId &target, uint64_t revision,
                                    float x, float y, float z) const;
  SceneDelta MakeNodeVisibilityDelta(const ObjectId &target,
                                     uint64_t revision, bool visible) const;
  SceneDelta MakeNodeRemovedDelta(const ObjectId &target,
                                  uint64_t revision) const;
  SceneDelta MakeMeshPayloadDelta(const ObjectId &target, uint64_t revision,
                                  const std::string &payloadUri) const;
  SceneDelta MakeSelectionDelta(uint64_t revision,
                                const std::string &selectedObjectId) const;
  SceneDelta MakeCameraDelta(uint64_t revision, float fovDegrees) const;
  SceneDelta MakeMaterialDelta(uint64_t revision, float roughness,
                               float metalness, float emissionScale) const;
  SceneDelta MakeLightDelta(uint64_t revision, float intensity,
                            float r, float g, float b) const;

  SceneDeltaBatch BeginBatch();
  ObjectId MakeObjectId(ObjectType type, const std::string &objectId) const;

  ConnectionState m_state = ConnectionState::Disconnected;
  std::string m_lastError;
  std::string m_sessionId = "mock-session";
  std::string m_documentId = "mock-document";
  uint64_t m_nextSequence = 1;
  uint64_t m_nextRevision = 1;
  bool m_sentInitialBatch = false;
  bool m_sentNodeRemoval = false;
  bool m_sentInitialMeshPayload = false;
  bool m_mockNodeBVisible = true;
  std::chrono::steady_clock::time_point m_connectTime;
  std::chrono::steady_clock::time_point m_lastTransformAt;
  std::chrono::steady_clock::time_point m_lastSelectionAt;
  std::chrono::steady_clock::time_point m_lastCameraAt;
  std::chrono::steady_clock::time_point m_lastVisibilityAt;
  std::chrono::steady_clock::time_point m_lastMaterialAt;
  std::chrono::steady_clock::time_point m_lastLightAt;
};

} // namespace LiveLink