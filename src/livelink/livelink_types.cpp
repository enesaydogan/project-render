#include "livelink_types.h"

#include <functional>
#include <sstream>

namespace LiveLink {

namespace {

static size_t HashCombine(size_t seed, size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  return seed;
}

} // namespace

size_t ObjectIdHash::operator()(const ObjectId &value) const {
  size_t seed = std::hash<std::string>{}(value.sourceApp);
  seed = HashCombine(seed, std::hash<std::string>{}(value.documentId));
  seed = HashCombine(seed, std::hash<std::string>{}(value.objectId));
  seed = HashCombine(seed, static_cast<size_t>(value.objectType));
  return seed;
}

SceneDelta::SceneDelta() : createdAt(std::chrono::steady_clock::now()) {}

const char *ToString(Capability capability) {
  switch (capability) {
  case Capability::None:
    return "None";
  case Capability::FullSceneSync:
    return "FullSceneSync";
  case Capability::IncrementalNodeSync:
    return "IncrementalNodeSync";
  case Capability::TransformSync:
    return "TransformSync";
  case Capability::VisibilitySync:
    return "VisibilitySync";
  case Capability::MeshPayloadSync:
    return "MeshPayloadSync";
  case Capability::MaterialSync:
    return "MaterialSync";
  case Capability::LightSync:
    return "LightSync";
  case Capability::CameraSync:
    return "CameraSync";
  case Capability::EnvironmentSync:
    return "EnvironmentSync";
  case Capability::SelectionSync:
    return "SelectionSync";
  case Capability::TimelineSync:
    return "TimelineSync";
  }
  return "UnknownCapability";
}

std::string ToStringFlags(Capability capabilities) {
  if (capabilities == Capability::None) {
    return "None";
  }

  constexpr Capability kAllFlags[] = {
      Capability::FullSceneSync,     Capability::IncrementalNodeSync,
      Capability::TransformSync,     Capability::VisibilitySync,
      Capability::MeshPayloadSync,   Capability::MaterialSync,
      Capability::LightSync,         Capability::CameraSync,
      Capability::EnvironmentSync,   Capability::SelectionSync,
      Capability::TimelineSync,
  };

  std::ostringstream stream;
  bool first = true;
  for (Capability flag : kAllFlags) {
    if (!HasCapability(capabilities, flag)) {
      continue;
    }
    if (!first) {
      stream << ", ";
    }
    stream << ToString(flag);
    first = false;
  }
  return stream.str();
}

const char *ToString(ConnectionState state) {
  switch (state) {
  case ConnectionState::Disconnected:
    return "Disconnected";
  case ConnectionState::Connecting:
    return "Connecting";
  case ConnectionState::Connected:
    return "Connected";
  case ConnectionState::Error:
    return "Error";
  }
  return "UnknownConnectionState";
}

const char *ToString(ObjectType type) {
  switch (type) {
  case ObjectType::Unknown:
    return "Unknown";
  case ObjectType::Node:
    return "Node";
  case ObjectType::Mesh:
    return "Mesh";
  case ObjectType::Material:
    return "Material";
  case ObjectType::Light:
    return "Light";
  case ObjectType::Camera:
    return "Camera";
  case ObjectType::Environment:
    return "Environment";
  case ObjectType::Selection:
    return "Selection";
  }
  return "UnknownObjectType";
}

const char *ToString(SceneDeltaKind kind) {
  switch (kind) {
  case SceneDeltaKind::Unknown:
    return "Unknown";
  case SceneDeltaKind::SessionOpened:
    return "SessionOpened";
  case SceneDeltaKind::SessionClosed:
    return "SessionClosed";
  case SceneDeltaKind::FullSceneSync:
    return "FullSceneSync";
  case SceneDeltaKind::NodeAdded:
    return "NodeAdded";
  case SceneDeltaKind::NodeRemoved:
    return "NodeRemoved";
  case SceneDeltaKind::NodeTransformChanged:
    return "NodeTransformChanged";
  case SceneDeltaKind::NodeVisibilityChanged:
    return "NodeVisibilityChanged";
  case SceneDeltaKind::MeshPayloadChanged:
    return "MeshPayloadChanged";
  case SceneDeltaKind::MaterialLibraryChanged:
    return "MaterialLibraryChanged";
  case SceneDeltaKind::MaterialChanged:
    return "MaterialChanged";
  case SceneDeltaKind::LightChanged:
    return "LightChanged";
  case SceneDeltaKind::CameraChanged:
    return "CameraChanged";
  case SceneDeltaKind::EnvironmentChanged:
    return "EnvironmentChanged";
  case SceneDeltaKind::SelectionChanged:
    return "SelectionChanged";
  }
  return "UnknownSceneDeltaKind";
}

const char *ToString(ValidationIssue::Severity severity) {
  switch (severity) {
  case ValidationIssue::Severity::Info:
    return "Info";
  case ValidationIssue::Severity::Warning:
    return "Warning";
  case ValidationIssue::Severity::Error:
    return "Error";
  }
  return "UnknownSeverity";
}

} // namespace LiveLink
