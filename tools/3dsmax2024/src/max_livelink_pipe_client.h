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

  // Takes ownership so callers can move a dump() result without a second copy
  // before the trailing newline is appended.
  bool SendJsonLine(std::string line);

private:
  void *m_pipe = nullptr;
  void *m_writeEvent = nullptr;
  std::string m_lastError;
};
