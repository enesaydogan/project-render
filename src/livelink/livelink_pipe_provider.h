#pragma once

#include "livelink_provider.h"

#include <string>

namespace LiveLink {

class NamedPipeLiveLinkProvider : public ILiveLinkProvider {
public:
  explicit NamedPipeLiveLinkProvider(std::string pipeName);
  ~NamedPipeLiveLinkProvider() override;

  std::string GetProviderName() const override;
  Capability GetCapabilities() const override;
  ConnectionState GetConnectionState() const override;
  std::string GetLastError() const override;

  bool Connect() override;
  void Disconnect() override;
  bool Poll(std::vector<SceneDeltaBatch> &outBatches) override;

private:
  struct Impl;
  std::string m_pipeName;
  std::string m_providerName;
  Capability m_capabilities = Capability::FullSceneSync |
                              Capability::IncrementalNodeSync |
                              Capability::TransformSync |
                              Capability::VisibilitySync |
                              Capability::MeshPayloadSync |
                              Capability::MaterialSync |
                              Capability::LightSync |
                              Capability::CameraSync |
                              Capability::SelectionSync;
  ConnectionState m_state = ConnectionState::Disconnected;
  std::string m_lastError;
  Impl *m_impl = nullptr;
};

} // namespace LiveLink