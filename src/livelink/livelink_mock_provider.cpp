#include "livelink_mock_provider.h"

#include <cmath>

namespace LiveLink {

namespace {

constexpr const char *kMockSourceApp = "MockLiveLink";
constexpr const char *kNodeAId = "mock-node-a";
constexpr const char *kNodeBId = "mock-node-b";
constexpr const char *kCameraId = "mock-main-camera";
constexpr const char *kMaterialId = "Material";
constexpr const char *kLightId = "mock-light";
constexpr const char *kSelectionId = "mock-selection";

} // namespace

std::string MockLiveLinkProvider::GetProviderName() const {
  return kMockSourceApp;
}

Capability MockLiveLinkProvider::GetCapabilities() const {
  return Capability::FullSceneSync | Capability::IncrementalNodeSync |
         Capability::TransformSync | Capability::VisibilitySync |
         Capability::MeshPayloadSync | Capability::MaterialSync |
         Capability::LightSync |
         Capability::CameraSync | Capability::SelectionSync;
}

ConnectionState MockLiveLinkProvider::GetConnectionState() const {
  return m_state;
}

std::string MockLiveLinkProvider::GetLastError() const { return m_lastError; }

bool MockLiveLinkProvider::Connect() {
  m_lastError.clear();
  m_state = ConnectionState::Connected;
  m_nextSequence = 1;
  m_nextRevision = 1;
  m_sentInitialBatch = false;
  m_sentNodeRemoval = false;
  m_mockNodeBVisible = true;
  m_connectTime = std::chrono::steady_clock::now();
  m_lastTransformAt = m_connectTime;
  m_lastSelectionAt = m_connectTime;
  m_lastCameraAt = m_connectTime;
  m_lastVisibilityAt = m_connectTime;
  m_lastMaterialAt = m_connectTime;
  m_lastLightAt = m_connectTime;
  return true;
}

void MockLiveLinkProvider::Disconnect() {
  m_state = ConnectionState::Disconnected;
}

bool MockLiveLinkProvider::Poll(std::vector<SceneDeltaBatch> &outBatches) {
  if (m_state != ConnectionState::Connected) {
    return true;
  }

  const auto now = std::chrono::steady_clock::now();

  if (!m_sentInitialBatch) {
    SceneDeltaBatch batch = BeginBatch();
    batch.fullSync = true;
    batch.deltas.push_back(MakeSessionOpenedDelta());
    batch.deltas.push_back(MakeFullSceneSyncDelta());
    batch.deltas.push_back(
        MakeNodeAddedDelta(MakeObjectId(ObjectType::Node, kNodeAId),
                           m_nextRevision++, "Mock Node A"));
    batch.deltas.push_back(
        MakeNodeAddedDelta(MakeObjectId(ObjectType::Node, kNodeBId),
                           m_nextRevision++, "Mock Node B"));
    batch.deltas.push_back(MakeMeshPayloadDelta(
      MakeObjectId(ObjectType::Mesh, kNodeAId), m_nextRevision++,
      "assets/Cornell+Box+01.skp"));
    batch.deltas.push_back(MakeCameraDelta(m_nextRevision++, 58.0f));
    batch.deltas.push_back(MakeMaterialDelta(m_nextRevision++, 0.18f, 0.0f,
                         0.0f));
    batch.deltas.push_back(MakeLightDelta(m_nextRevision++, 400.0f,
                        1.0f, 0.85f, 0.7f));
    outBatches.push_back(std::move(batch));
    m_sentInitialBatch = true;
    return true;
  }

  if ((now - m_lastTransformAt) >= std::chrono::milliseconds(400)) {
    const float t = std::chrono::duration<float>(now - m_connectTime).count();
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeNodeTransformDelta(
        MakeObjectId(ObjectType::Node, kNodeAId), m_nextRevision++,
        std::sinf(t) * 1.5f, 0.0f, std::cosf(t) * 1.5f));
    outBatches.push_back(std::move(batch));
    m_lastTransformAt = now;
  }

  if (!m_sentNodeRemoval &&
      (now - m_connectTime) >= std::chrono::seconds(8)) {
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeNodeRemovedDelta(
        MakeObjectId(ObjectType::Node, kNodeBId), m_nextRevision++));
    outBatches.push_back(std::move(batch));
    m_sentNodeRemoval = true;
  } else if (!m_sentNodeRemoval &&
             (now - m_lastVisibilityAt) >= std::chrono::seconds(2)) {
    m_mockNodeBVisible = !m_mockNodeBVisible;
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeNodeVisibilityDelta(
        MakeObjectId(ObjectType::Node, kNodeBId), m_nextRevision++,
        m_mockNodeBVisible));
    outBatches.push_back(std::move(batch));
    m_lastVisibilityAt = now;
  }

  if ((now - m_lastSelectionAt) >= std::chrono::seconds(3)) {
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeSelectionDelta(m_nextRevision++, kNodeAId));
    outBatches.push_back(std::move(batch));
    m_lastSelectionAt = now;
  }

  if ((now - m_lastCameraAt) >= std::chrono::seconds(5)) {
    const float t = std::chrono::duration<float>(now - m_connectTime).count();
    const float fov = 55.0f + std::sinf(t * 0.4f) * 8.0f;
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeCameraDelta(m_nextRevision++, fov));
    outBatches.push_back(std::move(batch));
    m_lastCameraAt = now;
  }

  if ((now - m_lastMaterialAt) >= std::chrono::seconds(6)) {
    const float t = std::chrono::duration<float>(now - m_connectTime).count();
    const float roughness = 0.08f + (std::sinf(t * 0.41f) * 0.5f + 0.5f) * 0.72f;
    const float metalness = (std::cosf(t * 0.19f) * 0.5f + 0.5f) * 0.15f;
    const float emissionScale = (std::sinf(t * 0.27f) * 0.5f + 0.5f) * 2.0f;
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(
        MakeMaterialDelta(m_nextRevision++, roughness, metalness, emissionScale));
    outBatches.push_back(std::move(batch));
    m_lastMaterialAt = now;
  }

  if ((now - m_lastLightAt) >= std::chrono::seconds(4)) {
    const float t = std::chrono::duration<float>(now - m_connectTime).count();
    const float intensity = 300.0f + (std::sinf(t * 0.7f) * 0.5f + 0.5f) * 900.0f;
    SceneDeltaBatch batch = BeginBatch();
    batch.deltas.push_back(MakeLightDelta(
        m_nextRevision++, intensity,
        1.0f,
        0.75f + 0.25f * (std::sinf(t * 0.31f) * 0.5f + 0.5f),
        0.55f + 0.35f * (std::cosf(t * 0.23f) * 0.5f + 0.5f)));
    outBatches.push_back(std::move(batch));
    m_lastLightAt = now;
  }

  return true;
}

SceneDelta MockLiveLinkProvider::MakeSessionOpenedDelta() const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::SessionOpened;
  delta.target.sourceApp = kMockSourceApp;
  delta.target.documentId = m_documentId;
  delta.debugLabel = "Mock session opened";
  delta.payload = SessionOpenedPayload{"mock://scene", "Mock LiveLink Scene"};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeFullSceneSyncDelta() const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::FullSceneSync;
  delta.debugLabel = "Mock full scene sync";
  delta.payload = FullSceneSyncPayload{false};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeNodeAddedDelta(const ObjectId &target,
                                                    uint64_t revision,
                                                    const std::string &displayName) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::NodeAdded;
  delta.target = target;
  delta.revision = revision;
  delta.debugLabel = displayName;
  delta.payload = NodeAddedPayload{"", displayName};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeNodeTransformDelta(const ObjectId &target,
                                                        uint64_t revision,
                                                        float x, float y,
                                                        float z) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::NodeTransformChanged;
  delta.target = target;
  delta.revision = revision;
  delta.debugLabel = "Mock transform";
  NodeTransformPayload payload;
  payload.worldMatrix = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      x,    y,    z,    1.0f,
  };
  delta.payload = payload;
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeNodeVisibilityDelta(const ObjectId &target,
                                                         uint64_t revision,
                                                         bool visible) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::NodeVisibilityChanged;
  delta.target = target;
  delta.revision = revision;
  delta.debugLabel = visible ? "Mock visible" : "Mock hidden";
  delta.payload = NodeVisibilityPayload{visible};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeNodeRemovedDelta(const ObjectId &target,
                                                      uint64_t revision) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::NodeRemoved;
  delta.target = target;
  delta.revision = revision;
  delta.debugLabel = "Mock node removed";
  delta.payload = NodeRemovedPayload{true};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeMeshPayloadDelta(
    const ObjectId &target, uint64_t revision,
    const std::string &payloadUri) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::MeshPayloadChanged;
  delta.target = target;
  delta.revision = revision;
  delta.debugLabel = "Mock mesh payload";
  delta.payload = MeshPayloadChangedPayload{revision, 0, 0, true, payloadUri,
                                            "mock-payload"};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeSelectionDelta(
    uint64_t revision, const std::string &selectedObjectId) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::SelectionChanged;
  delta.target = MakeObjectId(ObjectType::Selection, kSelectionId);
  delta.revision = revision;
  delta.debugLabel = "Mock selection";
  delta.payload = SelectionChangedPayload{{selectedObjectId}};
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeCameraDelta(uint64_t revision,
                                                 float fovDegrees) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::CameraChanged;
  delta.target = MakeObjectId(ObjectType::Camera, kCameraId);
  delta.revision = revision;
  delta.debugLabel = "Mock camera";
  CameraChangedPayload payload;
  payload.position = {0.0f, 1.0f, -5.0f};
  payload.forward = {0.0f, 0.0f, 1.0f};
  payload.up = {0.0f, 1.0f, 0.0f};
  payload.fovDegrees = fovDegrees;
  payload.nearPlane = 0.05f;
  payload.farPlane = 1500.0f;
  delta.payload = payload;
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeMaterialDelta(uint64_t revision,
                                                   float roughness,
                                                   float metalness,
                                                   float emissionScale) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::MaterialChanged;
  delta.target = MakeObjectId(ObjectType::Material, kMaterialId);
  delta.revision = revision;
  delta.debugLabel = "Mock material";

  MaterialChangedPayload payload;
  payload.materialModel = "OpenPBR";
  payload.baseColor = {0.92f, 0.88f, 0.82f, 1.0f};
  payload.emissiveColor = {1.0f, 0.72f, 0.35f, 1.0f};
  payload.emissiveIntensity = emissionScale;
  payload.roughness = roughness;
  payload.metalness = metalness;
  payload.workflow = 0;
  payload.specularWeight = 1.0f;
  payload.ior = 1.5f;
  payload.transmissionWeight = 0.0f;
  payload.transmissionColor = {1.0f, 1.0f, 1.0f};
  payload.coatWeight = 0.0f;
  payload.coatRoughness = 0.1f;
  payload.thinWalled = 0.0f;
  payload.translucency = 0.0f;
  payload.doubleSided = false;
  payload.alphaMode = "OPAQUE";
  delta.payload = payload;
  return delta;
}

SceneDelta MockLiveLinkProvider::MakeLightDelta(uint64_t revision,
                                                float intensity,
                                                float r, float g, float b) const {
  SceneDelta delta;
  delta.kind = SceneDeltaKind::LightChanged;
  delta.target = MakeObjectId(ObjectType::Light, kLightId);
  delta.revision = revision;
  delta.debugLabel = "Mock light";
  LightChangedPayload payload;
  payload.lightType = "Omni";
  payload.intensity = intensity;
  payload.color = {r, g, b};
  payload.position = {0.0f, 2.0f, 0.0f};
  payload.direction = {0.0f, -1.0f, 0.0f};
  payload.radius = 0.1f;
  payload.innerConeDegrees = 30.0f;
  payload.outerConeDegrees = 45.0f;
  payload.areaExtents = {1.0f, 1.0f};
  delta.payload = payload;
  return delta;
}

SceneDeltaBatch MockLiveLinkProvider::BeginBatch() {
  SceneDeltaBatch batch;
  batch.sessionId = m_sessionId;
  batch.providerName = GetProviderName();
  batch.sequence = m_nextSequence++;
  return batch;
}

ObjectId MockLiveLinkProvider::MakeObjectId(ObjectType type,
                                            const std::string &objectId) const {
  ObjectId id;
  id.sourceApp = kMockSourceApp;
  id.documentId = m_documentId;
  id.objectId = objectId;
  id.objectType = type;
  return id;
}

} // namespace LiveLink
