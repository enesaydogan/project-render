#pragma once

#include <cstdint>
#include <string>

class MaxLiveLinkPipeClient {
public:
  MaxLiveLinkPipeClient() = default;
  ~MaxLiveLinkPipeClient();

  bool Connect(const std::string &pipeName);
  void Disconnect();
  bool IsConnected() const;
  const std::string &GetLastError() const;

  bool SendJsonLine(const std::string &line);

private:
  void *m_pipe = nullptr;
  void *m_writeEvent = nullptr;
  std::string m_lastError;
};
