#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace LiveLink {

enum class Capability : uint64_t {
  None = 0,
  FullSceneSync = 1ull << 0,
  IncrementalNodeSync = 1ull << 1,
  TransformSync = 1ull << 2,
  VisibilitySync = 1ull << 3,
  MeshPayloadSync = 1ull << 4,
  MaterialSync = 1ull << 5,
  LightSync = 1ull << 6,
  CameraSync = 1ull << 7,
  EnvironmentSync = 1ull << 8,
  SelectionSync = 1ull << 9,
  TimelineSync = 1ull << 10,
};

constexpr Capability operator|(Capability lhs, Capability rhs) {
  return static_cast<Capability>(static_cast<uint64_t>(lhs) |
                                 static_cast<uint64_t>(rhs));
}

constexpr Capability operator&(Capability lhs, Capability rhs) {
  return static_cast<Capability>(static_cast<uint64_t>(lhs) &
                                 static_cast<uint64_t>(rhs));
}

constexpr Capability &operator|=(Capability &lhs, Capability rhs) {
  lhs = lhs | rhs;
  return lhs;
}

constexpr bool HasCapability(Capability flags, Capability value) {
  return (static_cast<uint64_t>(flags & value) != 0ull);
}

enum class ConnectionState {
  Disconnected,
  Connecting,
  Connected,
  Error,
};

enum class ObjectType {
  Unknown,
  Node,
  Mesh,
  Material,
  Light,
  Camera,
  Environment,
  Selection,
};

enum class SceneDeltaKind {
  Unknown,
  SessionOpened,
  SessionClosed,
  FullSceneSync,
  NodeAdded,
  NodeRemoved,
  NodeTransformChanged,
  NodeVisibilityChanged,
  MeshPayloadChanged,
  MaterialLibraryChanged,
  MaterialChanged,
  LightChanged,
  CameraChanged,
  EnvironmentChanged,
  SelectionChanged,
};

struct ObjectId {
  std::string sourceApp;
  std::string documentId;
  std::string objectId;
  ObjectType objectType = ObjectType::Unknown;

  bool Empty() const {
    return sourceApp.empty() || documentId.empty() || objectId.empty() ||
           objectType == ObjectType::Unknown;
  }

  bool operator==(const ObjectId &) const = default;
};

struct ObjectIdHash {
  size_t operator()(const ObjectId &value) const;
};

struct SessionOpenedPayload {
  std::string documentPath;
  std::string displayName;
};

struct SessionClosedPayload {
  std::string reason;
  bool graceful = true;
};

struct FullSceneSyncPayload {
  bool clearsExistingScene = false;
};

struct NodeAddedPayload {
  std::string parentObjectId;
  std::string displayName;
};

struct NodeRemovedPayload {
  bool removeChildren = true;
};

struct NodeTransformPayload {
  std::array<float, 16> worldMatrix = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };
};

struct NodeVisibilityPayload {
  bool visible = true;
};

struct MeshPayloadChangedPayload {
  uint64_t geometryRevision = 0;
  uint64_t vertexCount = 0;
  uint64_t indexCount = 0;
  bool topologyChanged = true;
  std::string payloadUri;
  std::string payloadHash;
};

struct MaterialLibraryChangedPayload {
  std::string payloadUri;
  std::string payloadHash;
};

struct MaterialNodeReference {
  std::string nodeObjectId;
  int materialSlot = 0;
};

struct MaterialChangedPayload {
  bool parametersChanged = true;
  bool texturesChanged = false;
  std::string nodeObjectId;
  std::string materialStableId;
  int materialSlot = 0;
  std::vector<MaterialNodeReference> references;
  std::string name;
  std::string materialModel;
  std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  std::string baseColorTextureUri;
  std::string baseColorTextureBlobHash;
  std::string normalTextureUri;
  std::string normalTextureBlobHash;
  std::string emissiveTextureUri;
  std::string emissiveTextureBlobHash;
  std::string occlusionTextureUri;
  std::string occlusionTextureBlobHash;
  std::string metalRoughTextureUri;
  std::string metalRoughTextureBlobHash;
  std::array<float, 4> emissiveColor = {0.0f, 0.0f, 0.0f, 1.0f};
  float emissiveIntensity = 1.0f;
  float roughness = 0.2f;
  float metalness = 0.0f;
  float specularWeight = 1.0f;
  float ior = 1.5f;
  float transmissionWeight = 0.0f;
  std::array<float, 3> transmissionColor = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float thinWalled = 0.0f;
  float translucency = 0.0f;
  std::array<float, 2> uvScale = {1.0f, 1.0f};
  std::array<float, 2> uvOffset = {0.0f, 0.0f};
  float triPlanarEnabled = 0.0f;
  float triPlanarScale = 1.0f;
  float triPlanarSharpness = 4.0f;
  float triPlanarNormalStrength = 1.0f;
  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
  bool invertRoughnessTexture = false;
};

struct LightChangedPayload {
  std::string lightType = "Omni";
  float intensity = 0.0f;
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
  std::array<float, 3> position = {0.0f, 2.0f, 0.0f};
  std::array<float, 3> direction = {0.0f, -1.0f, 0.0f};
  float radius = 0.1f;
  float innerConeDegrees = 30.0f;
  float outerConeDegrees = 45.0f;
  std::array<float, 2> areaExtents = {1.0f, 1.0f};
};

struct CameraChangedPayload {
  std::array<float, 3> position = {0.0f, 1.0f, -5.0f};
  std::array<float, 3> forward = {0.0f, 0.0f, 1.0f};
  std::array<float, 3> up = {0.0f, 1.0f, 0.0f};
  float fovDegrees = 60.0f;
  float nearPlane = 0.01f;
  float farPlane = 1000.0f;
};

struct EnvironmentChangedPayload {
  std::string environmentUri;
  float intensity = 1.0f;
};

struct SelectionChangedPayload {
  std::vector<std::string> selectedObjectIds;
};

using SceneDeltaPayload =
    std::variant<std::monostate, SessionOpenedPayload, SessionClosedPayload,
                 FullSceneSyncPayload, NodeAddedPayload, NodeRemovedPayload,
                 NodeTransformPayload, NodeVisibilityPayload,
                 MeshPayloadChangedPayload, MaterialLibraryChangedPayload,
                 MaterialChangedPayload,
                 LightChangedPayload, CameraChangedPayload,
                 EnvironmentChangedPayload, SelectionChangedPayload>;

struct SceneDelta {
  SceneDeltaKind kind = SceneDeltaKind::Unknown;
  ObjectId target;
  uint64_t revision = 0;
  std::string debugLabel;
  SceneDeltaPayload payload;
  std::chrono::steady_clock::time_point createdAt;

  SceneDelta();
};

struct SessionInfo {
  std::string sessionId;
  std::string providerName;
  std::string sourceApp;
  std::string documentId;
  std::string documentPath;
  std::string displayName;
  ConnectionState connectionState = ConnectionState::Disconnected;
  Capability capabilities = Capability::None;
  uint64_t lastReceivedBatchSequence = 0;
  uint64_t lastAcceptedRevision = 0;
};

struct SceneDeltaBatch {
  std::string sessionId;
  std::string providerName;
  uint64_t sequence = 0;
  bool fullSync = false;
  std::vector<SceneDelta> deltas;
};

struct ValidationIssue {
  enum class Severity {
    Info,
    Warning,
    Error,
  };

  Severity severity = Severity::Info;
  std::string providerName;
  std::string sessionId;
  std::string message;
};

const char *ToString(Capability capability);
std::string ToStringFlags(Capability capabilities);
const char *ToString(ConnectionState state);
const char *ToString(ObjectType type);
const char *ToString(SceneDeltaKind kind);
const char *ToString(ValidationIssue::Severity severity);

} // namespace LiveLink
