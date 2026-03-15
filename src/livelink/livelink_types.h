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

struct MaterialChangedPayload {
  bool parametersChanged = true;
  bool texturesChanged = false;
  std::string materialModel;
};

struct LightChangedPayload {
  float intensity = 0.0f;
  std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
};

struct CameraChangedPayload {
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
                 MeshPayloadChangedPayload, MaterialChangedPayload,
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