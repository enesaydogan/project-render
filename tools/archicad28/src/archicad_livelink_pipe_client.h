#pragma once

#include <string>

class ArchicadLiveLinkPipeClient {
public:
  ArchicadLiveLinkPipeClient() = default;
  ~ArchicadLiveLinkPipeClient();

  bool Connect(const std::string &pipeName);
  void Disconnect();
  bool IsConnected() const;
  const std::string &GetLastError() const;

  bool SendJsonLine(const std::string &line);

private:
  void *m_pipe = nullptr;
  std::string m_lastError;
};