#include "livelink_pipe_provider.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LiveLink {

namespace {

using json = nlohmann::json;

constexpr const char *kDefaultProviderName = "3dsMax2025Pipe";

std::string MakePipePath(const std::string &pipeName) {
  if (pipeName.rfind(R"(\\.\pipe\)", 0) == 0) {
    return pipeName;
  }
  return std::string(R"(\\.\pipe\)") + pipeName;
}

std::string FormatWindowsError(DWORD error) {
  LPSTR buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageA(flags, nullptr, error, 0,
                                      reinterpret_cast<LPSTR>(&buffer), 0,
                                      nullptr);
  std::string message;
  if (length != 0 && buffer) {
    message.assign(buffer, length);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n')) {
      message.pop_back();
    }
  } else {
    message = "Windows error " + std::to_string(error);
  }
  if (buffer) {
    LocalFree(buffer);
  }
  return message;
}

ObjectType ParseObjectType(std::string_view value) {
  if (value == "Node") {
    return ObjectType::Node;
  }
  if (value == "Mesh") {
    return ObjectType::Mesh;
  }
  if (value == "Material") {
    return ObjectType::Material;
  }
  if (value == "Light") {
    return ObjectType::Light;
  }
  if (value == "Camera") {
    return ObjectType::Camera;
  }
  if (value == "Environment") {
    return ObjectType::Environment;
  }
  if (value == "Selection") {
    return ObjectType::Selection;
  }
  return ObjectType::Unknown;
}

SceneDeltaKind ParseSceneDeltaKind(std::string_view value) {
  if (value == "SessionOpened") {
    return SceneDeltaKind::SessionOpened;
  }
  if (value == "SessionClosed") {
    return SceneDeltaKind::SessionClosed;
  }
  if (value == "FullSceneSync") {
    return SceneDeltaKind::FullSceneSync;
  }
  if (value == "NodeAdded") {
    return SceneDeltaKind::NodeAdded;
  }
  if (value == "NodeRemoved") {
    return SceneDeltaKind::NodeRemoved;
  }
  if (value == "NodeTransformChanged") {
    return SceneDeltaKind::NodeTransformChanged;
  }
  if (value == "NodeVisibilityChanged") {
    return SceneDeltaKind::NodeVisibilityChanged;
  }
  if (value == "MeshPayloadChanged") {
    return SceneDeltaKind::MeshPayloadChanged;
  }
  if (value == "MaterialChanged") {
    return SceneDeltaKind::MaterialChanged;
  }
  if (value == "LightChanged") {
    return SceneDeltaKind::LightChanged;
  }
  if (value == "CameraChanged") {
    return SceneDeltaKind::CameraChanged;
  }
  if (value == "EnvironmentChanged") {
    return SceneDeltaKind::EnvironmentChanged;
  }
  if (value == "SelectionChanged") {
    return SceneDeltaKind::SelectionChanged;
  }
  return SceneDeltaKind::Unknown;
}

ObjectId ParseObjectId(const json &value) {
  ObjectId objectId;
  if (!value.is_object()) {
    return objectId;
  }
  objectId.sourceApp = value.value("sourceApp", "");
  objectId.documentId = value.value("documentId", "");
  objectId.objectId = value.value("objectId", "");
  objectId.objectType = ParseObjectType(value.value("objectType", "Unknown"));
  return objectId;
}

template <size_t N>
bool FillFloatArray(const json &value, std::array<float, N> *out) {
  if (!out || !value.is_array() || value.size() != N) {
    return false;
  }
  for (size_t i = 0; i < N; ++i) {
    (*out)[i] = value[i].get<float>();
  }
  return true;
}

bool ParsePayload(const SceneDeltaKind kind, const json &payloadJson,
                  SceneDeltaPayload *outPayload) {
  if (!outPayload) {
    return false;
  }
  if (!payloadJson.is_object()) {
    *outPayload = std::monostate{};
    return true;
  }

  switch (kind) {
  case SceneDeltaKind::SessionOpened: {
    SessionOpenedPayload payload;
    payload.documentPath = payloadJson.value("documentPath", "");
    payload.displayName = payloadJson.value("displayName", "");
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::SessionClosed: {
    SessionClosedPayload payload;
    payload.reason = payloadJson.value("reason", "");
    payload.graceful = payloadJson.value("graceful", true);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::FullSceneSync: {
    FullSceneSyncPayload payload;
    payload.clearsExistingScene = payloadJson.value("clearsExistingScene", false);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::NodeAdded: {
    NodeAddedPayload payload;
    payload.parentObjectId = payloadJson.value("parentObjectId", "");
    payload.displayName = payloadJson.value("displayName", "");
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::NodeRemoved: {
    NodeRemovedPayload payload;
    payload.removeChildren = payloadJson.value("removeChildren", true);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::NodeTransformChanged: {
    NodeTransformPayload payload;
    if (!FillFloatArray(payloadJson.at("worldMatrix"), &payload.worldMatrix)) {
      return false;
    }
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::NodeVisibilityChanged: {
    NodeVisibilityPayload payload;
    payload.visible = payloadJson.value("visible", true);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::MeshPayloadChanged: {
    MeshPayloadChangedPayload payload;
    payload.geometryRevision = payloadJson.value("geometryRevision", 0ull);
    payload.vertexCount = payloadJson.value("vertexCount", 0ull);
    payload.indexCount = payloadJson.value("indexCount", 0ull);
    payload.topologyChanged = payloadJson.value("topologyChanged", true);
    payload.payloadUri = payloadJson.value("payloadUri", "");
    payload.payloadHash = payloadJson.value("payloadHash", "");
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::MaterialChanged: {
    MaterialChangedPayload payload;
    payload.parametersChanged = payloadJson.value("parametersChanged", true);
    payload.texturesChanged = payloadJson.value("texturesChanged", false);
    payload.nodeObjectId = payloadJson.value("nodeObjectId", "");
    payload.materialStableId = payloadJson.value("materialStableId", "");
    payload.materialSlot = payloadJson.value("materialSlot", 0);
    payload.name = payloadJson.value("name", "");
    payload.materialModel = payloadJson.value("materialModel", "");
    if (payloadJson.contains("baseColor")) {
      FillFloatArray(payloadJson.at("baseColor"), &payload.baseColor);
    }
    payload.baseColorTextureUri = payloadJson.value("baseColorTextureUri", "");
    payload.normalTextureUri = payloadJson.value("normalTextureUri", "");
    payload.emissiveTextureUri = payloadJson.value("emissiveTextureUri", "");
    payload.occlusionTextureUri = payloadJson.value("occlusionTextureUri", "");
    payload.metalRoughTextureUri = payloadJson.value("metalRoughTextureUri", "");
    if (payloadJson.contains("emissiveColor")) {
      FillFloatArray(payloadJson.at("emissiveColor"), &payload.emissiveColor);
    }
    payload.emissiveIntensity = payloadJson.value("emissiveIntensity", 1.0f);
    payload.roughness = payloadJson.value("roughness", 0.2f);
    payload.metalness = payloadJson.value("metalness", 0.0f);
    payload.specularWeight = payloadJson.value("specularWeight", 1.0f);
    payload.ior = payloadJson.value("ior", 1.5f);
    payload.transmissionWeight = payloadJson.value("transmissionWeight", 0.0f);
    if (payloadJson.contains("transmissionColor")) {
      FillFloatArray(payloadJson.at("transmissionColor"),
                     &payload.transmissionColor);
    }
    payload.coatWeight = payloadJson.value("coatWeight", 0.0f);
    payload.coatRoughness = payloadJson.value("coatRoughness", 0.1f);
    payload.thinWalled = payloadJson.value("thinWalled", 0.0f);
    payload.translucency = payloadJson.value("translucency", 0.0f);
    if (payloadJson.contains("uvScale")) {
      FillFloatArray(payloadJson.at("uvScale"), &payload.uvScale);
    }
    if (payloadJson.contains("uvOffset")) {
      FillFloatArray(payloadJson.at("uvOffset"), &payload.uvOffset);
    }
    payload.triPlanarEnabled = payloadJson.value("triPlanarEnabled", 0.0f);
    payload.triPlanarScale = payloadJson.value("triPlanarScale", 1.0f);
    payload.triPlanarSharpness =
        payloadJson.value("triPlanarSharpness", 4.0f);
    payload.triPlanarNormalStrength =
        payloadJson.value("triPlanarNormalStrength", 1.0f);
    payload.doubleSided = payloadJson.value("doubleSided", false);
    payload.alphaMode = payloadJson.value("alphaMode", "OPAQUE");
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::LightChanged: {
    LightChangedPayload payload;
    payload.lightType = payloadJson.value("lightType", "Omni");
    payload.intensity = payloadJson.value("intensity", 0.0f);
    if (payloadJson.contains("color")) {
      FillFloatArray(payloadJson.at("color"), &payload.color);
    }
    if (payloadJson.contains("position")) {
      FillFloatArray(payloadJson.at("position"), &payload.position);
    }
    if (payloadJson.contains("direction")) {
      FillFloatArray(payloadJson.at("direction"), &payload.direction);
    }
    payload.radius = payloadJson.value("radius", 0.1f);
    payload.innerConeDegrees =
        payloadJson.value("innerConeDegrees", 30.0f);
    payload.outerConeDegrees =
        payloadJson.value("outerConeDegrees", 45.0f);
    if (payloadJson.contains("areaExtents")) {
      FillFloatArray(payloadJson.at("areaExtents"), &payload.areaExtents);
    }
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::CameraChanged: {
    CameraChangedPayload payload;
    if (payloadJson.contains("position")) {
      FillFloatArray(payloadJson.at("position"), &payload.position);
    }
    if (payloadJson.contains("forward")) {
      FillFloatArray(payloadJson.at("forward"), &payload.forward);
    }
    if (payloadJson.contains("up")) {
      FillFloatArray(payloadJson.at("up"), &payload.up);
    }
    payload.fovDegrees = payloadJson.value("fovDegrees", 60.0f);
    payload.nearPlane = payloadJson.value("nearPlane", 0.01f);
    payload.farPlane = payloadJson.value("farPlane", 1000.0f);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::EnvironmentChanged: {
    EnvironmentChangedPayload payload;
    payload.environmentUri = payloadJson.value("environmentUri", "");
    payload.intensity = payloadJson.value("intensity", 1.0f);
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::SelectionChanged: {
    SelectionChangedPayload payload;
    if (payloadJson.contains("selectedObjectIds") &&
        payloadJson.at("selectedObjectIds").is_array()) {
      for (const json &entry : payloadJson.at("selectedObjectIds")) {
        if (entry.is_string()) {
          payload.selectedObjectIds.push_back(entry.get<std::string>());
        }
      }
    }
    *outPayload = std::move(payload);
    return true;
  }
  case SceneDeltaKind::Unknown:
    return false;
  }

  return false;
}

std::optional<SceneDelta> ParseDelta(const json &value) {
  if (!value.is_object()) {
    return std::nullopt;
  }

  SceneDelta delta;
  delta.kind = ParseSceneDeltaKind(value.value("kind", "Unknown"));
  delta.target = ParseObjectId(value.value("target", json::object()));
  delta.revision = value.value("revision", 0ull);
  delta.debugLabel = value.value("debugLabel", "");
  if (!ParsePayload(delta.kind, value.value("payload", json::object()),
                    &delta.payload)) {
    return std::nullopt;
  }
  return delta;
}

std::optional<SceneDeltaBatch> ParseBatchLine(const std::string &line,
                                              std::string *outError) {
  try {
    const json root = json::parse(line);
    if (!root.is_object()) {
      if (outError) {
        *outError = "Pipe message was not a JSON object";
      }
      return std::nullopt;
    }

    SceneDeltaBatch batch;
    batch.sessionId = root.value("sessionId", "");
    batch.providerName = root.value("providerName", "");
    batch.sequence = root.value("sequence", 0ull);
    batch.fullSync = root.value("fullSync", false);

    const json deltasJson = root.value("deltas", json::array());
    if (!deltasJson.is_array()) {
      if (outError) {
        *outError = "Batch deltas field was not an array";
      }
      return std::nullopt;
    }

    for (const json &deltaJson : deltasJson) {
      std::optional<SceneDelta> delta = ParseDelta(deltaJson);
      if (!delta) {
        if (outError) {
          *outError = "Failed to parse one or more deltas from pipe message";
        }
        return std::nullopt;
      }
      batch.deltas.push_back(std::move(*delta));
    }

    return batch;
  } catch (const std::exception &e) {
    if (outError) {
      *outError = e.what();
    }
    return std::nullopt;
  }
}

} // namespace

struct NamedPipeLiveLinkProvider::Impl {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  bool clientConnected = false;
  std::string readBuffer;
};

NamedPipeLiveLinkProvider::NamedPipeLiveLinkProvider(std::string pipeName)
    : m_pipeName(pipeName.empty() ? "project-render-max-livelink" : std::move(pipeName)),
      m_providerName(kDefaultProviderName), m_impl(new Impl()) {}

NamedPipeLiveLinkProvider::~NamedPipeLiveLinkProvider() {
  Disconnect();
  delete m_impl;
}

std::string NamedPipeLiveLinkProvider::GetProviderName() const {
  return m_providerName;
}

Capability NamedPipeLiveLinkProvider::GetCapabilities() const {
  return m_capabilities;
}

ConnectionState NamedPipeLiveLinkProvider::GetConnectionState() const {
  return m_state;
}

std::string NamedPipeLiveLinkProvider::GetLastError() const { return m_lastError; }

bool NamedPipeLiveLinkProvider::Connect() {
  Disconnect();

  const std::string pipePath = MakePipePath(m_pipeName);
  m_impl->pipe = CreateNamedPipeA(
      pipePath.c_str(), PIPE_ACCESS_INBOUND,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
      1, 0, 64 * 1024, 0, nullptr);
  if (m_impl->pipe == INVALID_HANDLE_VALUE) {
    m_lastError = "CreateNamedPipeA failed: " +
                  FormatWindowsError(::GetLastError());
    m_state = ConnectionState::Error;
    return false;
  }

  m_impl->clientConnected = false;
  m_impl->readBuffer.clear();
  m_lastError.clear();
  m_state = ConnectionState::Connecting;
  return true;
}

void NamedPipeLiveLinkProvider::Disconnect() {
  if (!m_impl) {
    return;
  }
  if (m_impl->pipe != INVALID_HANDLE_VALUE) {
    if (m_impl->clientConnected) {
      FlushFileBuffers(m_impl->pipe);
      DisconnectNamedPipe(m_impl->pipe);
    }
    CloseHandle(m_impl->pipe);
    m_impl->pipe = INVALID_HANDLE_VALUE;
  }
  m_impl->clientConnected = false;
  m_impl->readBuffer.clear();
  m_state = ConnectionState::Disconnected;
}

bool NamedPipeLiveLinkProvider::Poll(std::vector<SceneDeltaBatch> &outBatches) {
  if (!m_impl || m_impl->pipe == INVALID_HANDLE_VALUE) {
    m_lastError = "Named pipe provider is not connected";
    m_state = ConnectionState::Error;
    return false;
  }

  if (!m_impl->clientConnected) {
    if (ConnectNamedPipe(m_impl->pipe, nullptr)) {
      m_impl->clientConnected = true;
      m_state = ConnectionState::Connected;
    } else {
      const DWORD error = ::GetLastError();
      if (error == ERROR_PIPE_CONNECTED) {
        m_impl->clientConnected = true;
        m_state = ConnectionState::Connected;
      } else if (error == ERROR_PIPE_LISTENING || error == ERROR_NO_DATA) {
        m_state = ConnectionState::Connecting;
        return true;
      } else {
        m_lastError = "ConnectNamedPipe failed: " + FormatWindowsError(error);
        m_state = ConnectionState::Error;
        return false;
      }
    }
  }

  DWORD bytesAvailable = 0;
  if (!PeekNamedPipe(m_impl->pipe, nullptr, 0, nullptr, &bytesAvailable,
                     nullptr)) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_BROKEN_PIPE) {
      DisconnectNamedPipe(m_impl->pipe);
      m_impl->clientConnected = false;
      m_impl->readBuffer.clear();
      m_state = ConnectionState::Connecting;
      m_lastError.clear();
      return true;
    }

    m_lastError = "PeekNamedPipe failed: " + FormatWindowsError(error);
    m_state = ConnectionState::Error;
    return false;
  }

  while (bytesAvailable > 0) {
    std::vector<char> buffer(bytesAvailable);
    DWORD bytesRead = 0;
    if (!ReadFile(m_impl->pipe, buffer.data(), bytesAvailable, &bytesRead,
                  nullptr)) {
      const DWORD error = ::GetLastError();
      if (error == ERROR_BROKEN_PIPE) {
        DisconnectNamedPipe(m_impl->pipe);
        m_impl->clientConnected = false;
        m_impl->readBuffer.clear();
        m_state = ConnectionState::Connecting;
        m_lastError.clear();
        return true;
      }
      m_lastError = "ReadFile failed: " + FormatWindowsError(error);
      m_state = ConnectionState::Error;
      return false;
    }

    m_impl->readBuffer.append(buffer.data(), buffer.data() + bytesRead);
    if (!PeekNamedPipe(m_impl->pipe, nullptr, 0, nullptr, &bytesAvailable,
                       nullptr)) {
      bytesAvailable = 0;
    }
  }

  size_t newlinePos = std::string::npos;
  while ((newlinePos = m_impl->readBuffer.find('\n')) != std::string::npos) {
    std::string line = m_impl->readBuffer.substr(0, newlinePos);
    m_impl->readBuffer.erase(0, newlinePos + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    std::string parseError;
    std::optional<SceneDeltaBatch> batch = ParseBatchLine(line, &parseError);
    if (!batch) {
      m_lastError = "Failed to parse pipe batch: " + parseError;
      m_state = ConnectionState::Error;
      return false;
    }

    if (batch->providerName.empty()) {
      batch->providerName = m_providerName;
    }
    outBatches.push_back(std::move(*batch));
  }

  return true;
}

} // namespace LiveLink
