#include "archicad_livelink_pipe_client.h"
#include "resources.hpp"

#include "ACAPinc.h"

#include "UniString.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

using json = nlohmann::json;

namespace {

constexpr const char *kPipeName = "project-render-archicad-livelink";
constexpr const char *kProviderName = "Archicad28Pipe";
constexpr const char *kSourceApp = "Archicad28";

ArchicadLiveLinkPipeClient g_pipeClient;

struct DocumentInfo {
  std::string documentId;
  std::string documentPath;
  std::string displayName;
};

class LiveLinkSessionController {
public:
  bool Start();
  bool StartFullSync();
  bool Stop(bool silent = false);

private:
  bool EnsureConnected();
  bool EnsureSessionOpened();
  bool SendBatch(bool fullSync, json deltas);
  bool SendSessionOpened(bool fullSync);
  bool SendFullSceneSync();
  bool SendSessionClosed();
  DocumentInfo ReadDocumentInfo() const;
  std::string MakeSessionId() const;
  static std::string ToUtf8(const GS::UniString &value);
  static std::string SanitizeDocumentId(std::string value);
  static void Report(const std::string &message, bool withDialog = false);

  bool m_sessionOpen = false;
  std::string m_sessionId;
  DocumentInfo m_documentInfo;
  uint64_t m_nextSequence = 1;
};

LiveLinkSessionController g_controller;

std::string LiveLinkSessionController::ToUtf8(const GS::UniString &value) {
  std::unique_ptr<char[]> utf8(value.CopyUTF8());
  if (!utf8) {
    return {};
  }
  return utf8.get();
}

std::string LiveLinkSessionController::SanitizeDocumentId(std::string value) {
  for (char &ch : value) {
    switch (ch) {
    case '\\':
    case '/':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
    case ' ':
      ch = '_';
      break;
    default:
      break;
    }
  }
  if (value.empty()) {
    value = "untitled";
  }
  return value;
}

void LiveLinkSessionController::Report(const std::string &message,
                                       bool withDialog) {
  ACAPI_WriteReport(GS::UniString(message.c_str()), withDialog);
}

bool LiveLinkSessionController::EnsureConnected() {
  if (g_pipeClient.IsConnected()) {
    return true;
  }
  if (!g_pipeClient.Connect(kPipeName)) {
    Report("project-render LiveLink: failed to connect to pipe '" +
               std::string(kPipeName) + "': " + g_pipeClient.GetLastError(),
           true);
    return false;
  }
  return true;
}

std::string LiveLinkSessionController::MakeSessionId() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return std::string("archicad28-") + std::to_string(GetCurrentProcessId()) +
         "-" + std::to_string(millis);
}

DocumentInfo LiveLinkSessionController::ReadDocumentInfo() const {
  API_ProjectInfo projectInfo = {};
  DocumentInfo info;

  const GSErrCode err = ACAPI_ProjectOperation_Project(&projectInfo);
  if (err != NoError) {
    info.documentId = "archicad28-unsaved";
    info.displayName = "Unsaved Archicad Project";
    return info;
  }

  if (projectInfo.projectName != nullptr) {
    info.displayName = ToUtf8(*projectInfo.projectName);
  }

  if (!projectInfo.teamwork && projectInfo.location != nullptr) {
    info.documentPath = ToUtf8(projectInfo.location->ToDisplayText());
  } else if (projectInfo.location_team != nullptr) {
    info.documentPath = ToUtf8(projectInfo.location_team->ToLogText());
  }

  if (info.displayName.empty()) {
    if (!info.documentPath.empty()) {
      info.displayName = info.documentPath;
    } else if (projectInfo.untitled) {
      info.displayName = "Untitled Archicad Project";
    } else {
      info.displayName = "Archicad Project";
    }
  }

  info.documentId = SanitizeDocumentId(!info.documentPath.empty()
                                           ? info.documentPath
                                           : info.displayName);
  return info;
}

bool LiveLinkSessionController::SendBatch(bool fullSync, json deltas) {
  if (!EnsureConnected()) {
    return false;
  }
  if (m_sessionId.empty()) {
    Report("project-render LiveLink: no active session id", true);
    return false;
  }

  json batch;
  batch["providerName"] = kProviderName;
  batch["sessionId"] = m_sessionId;
  batch["sequence"] = m_nextSequence++;
  batch["fullSync"] = fullSync;
  batch["deltas"] = std::move(deltas);

  if (!g_pipeClient.SendJsonLine(batch.dump())) {
    Report("project-render LiveLink: failed to send batch: " +
               g_pipeClient.GetLastError(),
           true);
    return false;
  }

  return true;
}

bool LiveLinkSessionController::SendSessionOpened(bool fullSync) {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionOpened"},
                        {"target",
                         json{{"sourceApp", kSourceApp},
                              {"documentId", m_documentInfo.documentId},
                              {"objectId", "session"},
                              {"objectType", "Unknown"}}},
                        {"payload",
                         json{{"documentPath", m_documentInfo.documentPath},
                              {"displayName", m_documentInfo.displayName}}}});
  if (fullSync) {
    deltas.push_back(json{{"kind", "FullSceneSync"},
                          {"payload", json{{"clearsExistingScene", false}}}});
  }
  return SendBatch(fullSync, std::move(deltas));
}

bool LiveLinkSessionController::SendFullSceneSync() {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "FullSceneSync"},
                        {"payload", json{{"clearsExistingScene", false}}}});
  return SendBatch(true, std::move(deltas));
}

bool LiveLinkSessionController::SendSessionClosed() {
  json deltas = json::array();
  deltas.push_back(json{{"kind", "SessionClosed"},
                        {"payload",
                         json{{"reason", "User stopped Archicad LiveLink"},
                              {"graceful", true}}}});
  return SendBatch(false, std::move(deltas));
}

bool LiveLinkSessionController::EnsureSessionOpened() {
  if (m_sessionOpen) {
    return true;
  }

  if (!EnsureConnected()) {
    return false;
  }

  m_documentInfo = ReadDocumentInfo();
  m_sessionId = MakeSessionId();
  m_nextSequence = 1;
  if (!SendSessionOpened(false)) {
    m_sessionId.clear();
    return false;
  }

  m_sessionOpen = true;
  Report("project-render LiveLink: session started for '" +
         m_documentInfo.displayName + "'");
  return true;
}

bool LiveLinkSessionController::Start() {
  if (m_sessionOpen) {
    Report("project-render LiveLink: session is already active");
    return true;
  }
  return EnsureSessionOpened();
}

bool LiveLinkSessionController::StartFullSync() {
  if (!m_sessionOpen) {
    if (!EnsureConnected()) {
      return false;
    }
    m_documentInfo = ReadDocumentInfo();
    m_sessionId = MakeSessionId();
    m_nextSequence = 1;
    if (!SendSessionOpened(true)) {
      m_sessionId.clear();
      return false;
    }
    m_sessionOpen = true;
    Report("project-render LiveLink: full sync session started for '" +
           m_documentInfo.displayName + "'");
    return true;
  }

  if (!SendFullSceneSync()) {
    return false;
  }

  Report("project-render LiveLink: full sync marker sent for '" +
         m_documentInfo.displayName + "'");
  return true;
}

bool LiveLinkSessionController::Stop(bool silent) {
  if (!m_sessionOpen) {
    g_pipeClient.Disconnect();
    if (!silent) {
      Report("project-render LiveLink: no active session to stop");
    }
    return true;
  }

  const bool sent = SendSessionClosed();
  g_pipeClient.Disconnect();
  m_sessionOpen = false;
  m_sessionId.clear();
  m_documentInfo = {};
  m_nextSequence = 1;

  if (sent) {
    Report("project-render LiveLink: session stopped");
  }
  return sent;
}

GSErrCode MenuCommandHandler(const API_MenuParams *menuParams) {
  if (menuParams == nullptr ||
      menuParams->menuItemRef.menuResID != PROJECT_RENDER_ARCHICAD_MENU_STRINGS) {
    return NoError;
  }

  switch (menuParams->menuItemRef.itemIndex) {
  case 1:
    g_controller.Start();
    break;
  case 2:
    g_controller.StartFullSync();
    break;
  case 3:
    g_controller.Stop();
    break;
  default:
    break;
  }

  return NoError;
}

} // namespace

API_AddonType CheckEnvironment(API_EnvirParams *envir) {
  RSGetIndString(&envir->addOnInfo.name, PROJECT_RENDER_ARCHICAD_ADDON_STRINGS,
                 1, ACAPI_GetOwnResModule());
  RSGetIndString(&envir->addOnInfo.description,
                 PROJECT_RENDER_ARCHICAD_ADDON_STRINGS, 2,
                 ACAPI_GetOwnResModule());
  return APIAddon_Preload;
}

GSErrCode RegisterInterface(void) {
  return ACAPI_MenuItem_RegisterMenu(PROJECT_RENDER_ARCHICAD_MENU_STRINGS, 0,
                                     MenuCode_UserDef,
                                     MenuFlag_SeparatorBefore |
                                         MenuFlag_SeparatorAfter);
}

GSErrCode Initialize(void) {
  const GSErrCode err = ACAPI_MenuItem_InstallMenuHandler(
      PROJECT_RENDER_ARCHICAD_MENU_STRINGS, MenuCommandHandler);
  if (err != NoError) {
    ACAPI_WriteReport(GS::UniString("project-render LiveLink: failed to install menu handler"),
                      true);
  }
  return err;
}

GSErrCode FreeData(void) {
  g_controller.Stop(true);
  return NoError;
}