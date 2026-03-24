#include "archicad_livelink_pipe_client.h"

#include <windows.h>

namespace {

std::string WideToUtf8(const wchar_t *text, size_t length) {
  if (text == nullptr || length == 0) {
    return {};
  }

  const int utf8Length = WideCharToMultiByte(
      CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (utf8Length <= 0) {
    return {};
  }

  std::string utf8(static_cast<size_t>(utf8Length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), utf8.data(),
                      utf8Length, nullptr, nullptr);
  return utf8;
}

std::string MakePipePath(const std::string &pipeName) {
  if (pipeName.rfind(R"(\\.\pipe\)", 0) == 0) {
    return pipeName;
  }
  return std::string(R"(\\.\pipe\)") + pipeName;
}

std::string FormatWindowsError(DWORD error) {
  LPWSTR buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageW(flags, nullptr, error,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<LPWSTR>(&buffer), 0,
                                      nullptr);
  std::string message;
  if (length != 0 && buffer != nullptr) {
    message = WideToUtf8(buffer, length);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n')) {
      message.pop_back();
    }
  } else {
    message = "Windows error " + std::to_string(error);
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return message;
}

} // namespace

ArchicadLiveLinkPipeClient::~ArchicadLiveLinkPipeClient() { Disconnect(); }

bool ArchicadLiveLinkPipeClient::Connect(const std::string &pipeName) {
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

void ArchicadLiveLinkPipeClient::Disconnect() {
  if (m_pipe == nullptr) {
    return;
  }
  CloseHandle(static_cast<HANDLE>(m_pipe));
  m_pipe = nullptr;
}

bool ArchicadLiveLinkPipeClient::IsConnected() const {
  return m_pipe != nullptr;
}

const std::string &ArchicadLiveLinkPipeClient::GetLastError() const {
  return m_lastError;
}

bool ArchicadLiveLinkPipeClient::SendJsonLine(const std::string &line) {
  if (m_pipe == nullptr) {
    m_lastError = "Pipe is not connected";
    return false;
  }

  std::string payload = line;
  payload.push_back('\n');

  DWORD bytesWritten = 0;
  if (!WriteFile(static_cast<HANDLE>(m_pipe), payload.data(),
                 static_cast<DWORD>(payload.size()), &bytesWritten,
                 nullptr)) {
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