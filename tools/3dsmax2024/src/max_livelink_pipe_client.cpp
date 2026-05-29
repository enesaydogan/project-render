#include "max_livelink_pipe_client.h"

#include <windows.h>

namespace {

constexpr DWORD kPipeConnectTimeoutMs = 2000;
constexpr DWORD kPipeWriteTimeoutMs = 30000;
constexpr DWORD kPipeCancelWaitTimeoutMs = 100;

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
  HANDLE handle = CreateFileA(pipePath.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (::GetLastError() == ERROR_PIPE_BUSY) {
      if (!WaitNamedPipeA(pipePath.c_str(), kPipeConnectTimeoutMs)) {
        m_lastError = "WaitNamedPipeA failed: " +
                      FormatWindowsError(::GetLastError());
        return false;
      }
      handle = CreateFileA(pipePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
  }

  if (handle == INVALID_HANDLE_VALUE) {
    m_lastError = "CreateFileA failed: " +
                  FormatWindowsError(::GetLastError());
    return false;
  }

  HANDLE writeEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (!writeEvent) {
    m_lastError = "CreateEventA failed: " +
                  FormatWindowsError(::GetLastError());
    CloseHandle(handle);
    return false;
  }

  m_pipe = handle;
  m_writeEvent = writeEvent;
  m_lastError.clear();
  return true;
}

void MaxLiveLinkPipeClient::Disconnect() {
  if (m_writeEvent) {
    CloseHandle(static_cast<HANDLE>(m_writeEvent));
    m_writeEvent = nullptr;
  }
  if (m_pipe) {
    CloseHandle(static_cast<HANDLE>(m_pipe));
    m_pipe = nullptr;
  }
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

  HANDLE pipe = static_cast<HANDLE>(m_pipe);
  HANDLE eventHandle = static_cast<HANDLE>(m_writeEvent);
  if (!eventHandle) {
    m_lastError = "Pipe write event is not initialized";
    Disconnect();
    return false;
  }
  ResetEvent(eventHandle);

  OVERLAPPED overlapped = {};
  overlapped.hEvent = eventHandle;
  DWORD bytesWritten = 0;
  if (!WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()),
                 &bytesWritten, &overlapped)) {
    const DWORD writeError = ::GetLastError();
    if (writeError != ERROR_IO_PENDING) {
      m_lastError = "WriteFile failed: " + FormatWindowsError(writeError);
      Disconnect();
      return false;
    }

    const DWORD waitResult =
        WaitForSingleObject(eventHandle, kPipeWriteTimeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
      CancelIoEx(pipe, &overlapped);
      WaitForSingleObject(eventHandle, kPipeCancelWaitTimeoutMs);
      m_lastError = "WriteFile timed out after " +
                    std::to_string(kPipeWriteTimeoutMs) + " ms";
      Disconnect();
      return false;
    }
    if (waitResult != WAIT_OBJECT_0 ||
        !GetOverlappedResult(pipe, &overlapped, &bytesWritten, FALSE)) {
      const DWORD resultError = ::GetLastError();
      m_lastError = "GetOverlappedResult failed: " +
                    FormatWindowsError(resultError);
      Disconnect();
      return false;
    }
  }

  if (bytesWritten != payload.size()) {
    m_lastError = "Named pipe write was truncated";
    Disconnect();
    return false;
  }

  m_lastError.clear();
  return true;
}
