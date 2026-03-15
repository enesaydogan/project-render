#include "max_livelink_pipe_client.h"

#include <windows.h>

namespace {

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

} // namespace

MaxLiveLinkPipeClient::~MaxLiveLinkPipeClient() { Disconnect(); }

bool MaxLiveLinkPipeClient::Connect(const std::string &pipeName) {
  Disconnect();

  const std::string pipePath = MakePipePath(pipeName);
  if (!WaitNamedPipeA(pipePath.c_str(), 2000)) {
    m_lastError = "WaitNamedPipeA failed: " +
                  FormatWindowsError(::GetLastError());
    return false;
  }

  HANDLE handle = CreateFileA(pipePath.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    m_lastError = "CreateFileA failed: " +
                  FormatWindowsError(::GetLastError());
    return false;
  }

  m_pipe = handle;
  m_lastError.clear();
  return true;
}

void MaxLiveLinkPipeClient::Disconnect() {
  if (!m_pipe) {
    return;
  }
  CloseHandle(static_cast<HANDLE>(m_pipe));
  m_pipe = nullptr;
}

bool MaxLiveLinkPipeClient::IsConnected() const { return m_pipe != nullptr; }

const std::string &MaxLiveLinkPipeClient::GetLastError() const {
  return m_lastError;
}

bool MaxLiveLinkPipeClient::SendJsonLine(const std::string &line) {
  if (!m_pipe) {
    m_lastError = "Pipe is not connected";
    return false;
  }

  std::string payload = line;
  payload.push_back('\n');

  DWORD bytesWritten = 0;
  if (!WriteFile(static_cast<HANDLE>(m_pipe), payload.data(),
                 static_cast<DWORD>(payload.size()), &bytesWritten, nullptr)) {
    m_lastError = "WriteFile failed: " +
                  FormatWindowsError(::GetLastError());
    Disconnect();
    return false;
  }

  if (bytesWritten != payload.size()) {
    m_lastError = "Named pipe write was truncated";
    Disconnect();
    return false;
  }

  m_lastError.clear();
  return true;
}