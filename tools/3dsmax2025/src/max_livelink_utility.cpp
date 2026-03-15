#include "max_livelink_pipe_client.h"

#include <max.h>
#include <utilapi.h>
#include <iparamm2.h>

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace {

HINSTANCE g_instance = nullptr;
MaxLiveLinkPipeClient g_pipeClient;
constexpr const char *kPipeName = "project-render-max-livelink";
constexpr const char *kSourceApp = "3dsMax2025";
constexpr UINT_PTR kPollTimerId = 0x5052;
constexpr UINT kPollIntervalMs = 250;

class ProjectRenderLiveLinkUtility;
extern ProjectRenderLiveLinkUtility g_utility;

std::string WStringToUtf8(const std::wstring &value) {
  if (value.empty()) {
    return {};
  }
  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
  if (utf8Length <= 0) {
    return {};
  }
  std::string utf8(utf8Length, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      utf8.data(), utf8Length, nullptr, nullptr);
  return utf8;
}

std::string ToUtf8(const MCHAR *text) {
  if (!text) {
    return {};
  }
#ifdef UNICODE
  return WStringToUtf8(text);
#else
  return text;
#endif
}

std::string MakeDocumentId(Interface *ip) {
  const std::string currentFile = ip ? ToUtf8(ip->GetCurFileName()) : std::string();
  return currentFile.empty() ? std::string("untitled.max") : currentFile;
}

std::string MakeSessionId() {
  static std::atomic<uint64_t> s_sessionCounter{1};
  const uint64_t sessionOrdinal = s_sessionCounter.fetch_add(1);
  return "3dsmax2025-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()) + "-" +
         std::to_string(sessionOrdinal);
}

std::string MakeNodeObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return "node:" + std::to_string(static_cast<unsigned long long>(node->GetHandle()));
}

struct NodeSnapshot {
  ULONG_PTR handle = 0;
  ULONG_PTR parentHandle = 0;
  std::string name;
  bool visible = true;
  std::array<float, 16> worldMatrix = {};
};

bool NearlyEqual(float a, float b) {
  return std::fabs(a - b) <= 1.0e-4f;
}

bool SameMatrix(const std::array<float, 16> &lhs,
                const std::array<float, 16> &rhs) {
  for (size_t index = 0; index < lhs.size(); ++index) {
    if (!NearlyEqual(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

std::array<float, 16> Matrix3ToColumnMajor4x4(const Matrix3 &matrix) {
  Point3 translation = matrix.GetTrans();
  std::array<float, 16> result = {
      matrix.GetRow(0).x, matrix.GetRow(1).x, matrix.GetRow(2).x, 0.0f,
      matrix.GetRow(0).y, matrix.GetRow(1).y, matrix.GetRow(2).y, 0.0f,
      matrix.GetRow(0).z, matrix.GetRow(1).z, matrix.GetRow(2).z, 0.0f,
      translation.x,      translation.y,      translation.z,      1.0f,
  };
  return result;
}

json MakeObjectId(const std::string &documentId, const std::string &objectId,
                  const char *objectType) {
  return json{
      {"sourceApp", kSourceApp},
      {"documentId", documentId},
      {"objectId", objectId},
      {"objectType", objectType},
  };
}

NodeSnapshot CaptureNodeSnapshot(Interface *ip, INode *node) {
  NodeSnapshot snapshot;
  if (!ip || !node) {
    return snapshot;
  }

  snapshot.handle = node->GetHandle();
  snapshot.parentHandle =
      (node->GetParentNode() && !node->GetParentNode()->IsRootNode())
          ? node->GetParentNode()->GetHandle()
          : 0;
  snapshot.name = ToUtf8(node->GetName());
  snapshot.visible = !node->IsNodeHidden(TRUE);
  snapshot.worldMatrix = Matrix3ToColumnMajor4x4(node->GetNodeTM(ip->GetTime()));
  return snapshot;
}

void GatherNodeSnapshots(Interface *ip, INode *node,
                         std::unordered_map<ULONG_PTR, NodeSnapshot> *outState) {
  if (!ip || !node || !outState) {
    return;
  }

  NodeSnapshot snapshot = CaptureNodeSnapshot(ip, node);
  outState->insert_or_assign(snapshot.handle, snapshot);
  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    GatherNodeSnapshots(ip, node->GetChildNode(childIndex), outState);
  }
}

void AppendNodeAddedDelta(const std::string &documentId,
                          const NodeSnapshot &snapshot, uint64_t *revision,
                          json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  const std::string parentId = snapshot.parentHandle != 0
                                   ? "node:" + std::to_string(snapshot.parentHandle)
                                   : std::string();
  outDeltas->push_back(json{{"kind", "NodeAdded"},
                            {"target", MakeObjectId(documentId, objectId, "Node")},
                            {"revision", (*revision)++},
                            {"debugLabel", snapshot.name},
                            {"payload", json{{"parentObjectId", parentId},
                                              {"displayName", snapshot.name}}}});
}

void AppendNodeTransformDelta(const std::string &documentId,
                              const NodeSnapshot &snapshot, uint64_t *revision,
                              json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  outDeltas->push_back(
      json{{"kind", "NodeTransformChanged"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name},
           {"payload", json{{"worldMatrix", snapshot.worldMatrix}}}});
}

void AppendNodeVisibilityDelta(const std::string &documentId,
                               const NodeSnapshot &snapshot, uint64_t *revision,
                               json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(snapshot.handle);
  outDeltas->push_back(
      json{{"kind", "NodeVisibilityChanged"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", snapshot.name},
           {"payload", json{{"visible", snapshot.visible}}}});
}

void AppendNodeRemovedDelta(const std::string &documentId, ULONG_PTR handle,
                            uint64_t *revision, json *outDeltas) {
  if (!revision || !outDeltas) {
    return;
  }

  const std::string objectId = "node:" + std::to_string(handle);
  outDeltas->push_back(
      json{{"kind", "NodeRemoved"},
           {"target", MakeObjectId(documentId, objectId, "Node")},
           {"revision", (*revision)++},
           {"debugLabel", objectId},
           {"payload", json{{"removeChildren", true}}}});
}

bool EnsurePipeConnected() {
  return g_pipeClient.IsConnected() || g_pipeClient.Connect(kPipeName);
}

bool SendBatch(const std::string &sessionId, uint64_t sequence, bool fullSync,
               const json &deltas) {
  if (sessionId.empty() || !EnsurePipeConnected()) {
    return false;
  }

  json batch;
  batch["providerName"] = "3dsMax2025Pipe";
  batch["sessionId"] = sessionId;
  batch["sequence"] = sequence;
  batch["fullSync"] = fullSync;
  batch["deltas"] = deltas;
  return g_pipeClient.SendJsonLine(batch.dump());
}

bool SendInitialSnapshot(Interface *ip,
                         std::unordered_map<ULONG_PTR, NodeSnapshot> *outState,
                         std::string *outSessionId,
                         std::string *outDocumentId,
                         uint64_t *outNextSequence,
                         uint64_t *outNextRevision) {
  if (!ip || !EnsurePipeConnected()) {
    return false;
  }

  const std::string documentId = MakeDocumentId(ip);
  const std::string sessionId = MakeSessionId();

  std::unordered_map<ULONG_PTR, NodeSnapshot> state;
  if (INode *root = ip->GetRootNode()) {
    for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
      GatherNodeSnapshots(ip, root->GetChildNode(childIndex), &state);
    }
  }

  json deltas = json::array();
  deltas.push_back(json{
      {"kind", "SessionOpened"},
      {"target", json{{"sourceApp", kSourceApp},
                       {"documentId", documentId},
                       {"objectId", "session"},
                       {"objectType", "Unknown"}}},
      {"payload", json{{"documentPath", documentId},
                        {"displayName", documentId}}},
  });
  deltas.push_back(json{
      {"kind", "FullSceneSync"},
      {"payload", json{{"clearsExistingScene", false}}},
  });

  uint64_t revision = 1;
  for (const auto &[_, snapshot] : state) {
    AppendNodeAddedDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeTransformDelta(documentId, snapshot, &revision, &deltas);
    AppendNodeVisibilityDelta(documentId, snapshot, &revision, &deltas);
  }

  if (!SendBatch(sessionId, 1, true, deltas)) {
    return false;
  }

  if (outState) {
    *outState = std::move(state);
  }
  if (outSessionId) {
    *outSessionId = sessionId;
  }
  if (outDocumentId) {
    *outDocumentId = documentId;
  }
  if (outNextSequence) {
    *outNextSequence = 2;
  }
  if (outNextRevision) {
    *outNextRevision = revision;
  }
  return true;
}

void SendSessionClosed(const std::string &sessionId, uint64_t sequence) {
  if (sessionId.empty() || !g_pipeClient.IsConnected()) {
    return;
  }

  SendBatch(sessionId, sequence, false,
            json::array({json{{"kind", "SessionClosed"},
                              {"payload", json{{"reason", "3ds Max utility closed"},
                                                {"graceful", true}}}}}));
}

class ProjectRenderLiveLinkUtility final : public UtilityObj {
public:
  void BeginEditParams(Interface *ip, IUtil *iu) override {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_interface = ip;
    m_iu = iu;
    SendInitialSnapshot(ip, &m_lastNodeState, &m_sessionId, &m_documentId,
                        &m_nextSequence, &m_nextRevision);
    if (m_pollTimer == 0) {
      m_pollTimer = SetTimer(nullptr, kPollTimerId, kPollIntervalMs,
                             &ProjectRenderLiveLinkUtility::PollTimerProc);
    }
  }

  void EndEditParams(Interface * /*ip*/, IUtil * /*iu*/) override {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_pollTimer != 0) {
      KillTimer(nullptr, m_pollTimer);
      m_pollTimer = 0;
    }
    SendSessionClosed(m_sessionId, m_nextSequence++);
    g_pipeClient.Disconnect();
    m_interface = nullptr;
    m_iu = nullptr;
    m_lastNodeState.clear();
    m_sessionId.clear();
    m_documentId.clear();
    m_nextSequence = 1;
    m_nextRevision = 1;
  }

  void SelectionSetChanged(Interface *ip, IUtil *iu) override {
    UtilityObj::SelectionSetChanged(ip, iu);
    SendSelectionDelta();
  }

  void DeleteThis() override {}

private:
  static void CALLBACK PollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    g_utility.PollSceneChanges();
  }

  void PollSceneChanges() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!m_interface || m_sessionId.empty() || m_documentId.empty() ||
        !EnsurePipeConnected()) {
      return;
    }

    std::unordered_map<ULONG_PTR, NodeSnapshot> currentState;
    if (INode *root = m_interface->GetRootNode()) {
      for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
        GatherNodeSnapshots(m_interface, root->GetChildNode(childIndex),
                            &currentState);
      }
    }

    json deltas = json::array();
    for (const auto &[handle, snapshot] : currentState) {
      auto previousIt = m_lastNodeState.find(handle);
      if (previousIt == m_lastNodeState.end()) {
        AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
        continue;
      }

      const NodeSnapshot &previous = previousIt->second;
      if (previous.parentHandle != snapshot.parentHandle ||
          previous.name != snapshot.name) {
        AppendNodeAddedDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
      if (!SameMatrix(previous.worldMatrix, snapshot.worldMatrix)) {
        AppendNodeTransformDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
      if (previous.visible != snapshot.visible) {
        AppendNodeVisibilityDelta(m_documentId, snapshot, &m_nextRevision, &deltas);
      }
    }

    for (const auto &[handle, _] : m_lastNodeState) {
      if (currentState.find(handle) == currentState.end()) {
        AppendNodeRemovedDelta(m_documentId, handle, &m_nextRevision, &deltas);
      }
    }

    if (!deltas.empty() && SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
      m_lastNodeState = std::move(currentState);
    } else if (deltas.empty()) {
      m_lastNodeState = std::move(currentState);
    }
  }

  void SendSelectionDelta() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!m_interface || m_sessionId.empty() || m_documentId.empty() ||
        !EnsurePipeConnected()) {
      return;
    }

    json selectedObjectIds = json::array();
    const int selectedCount = m_interface->GetSelNodeCount();
    for (int index = 0; index < selectedCount; ++index) {
      if (INode *node = m_interface->GetSelNode(index)) {
        selectedObjectIds.push_back(MakeNodeObjectId(node));
      }
    }

    json deltas = json::array({json{{"kind", "SelectionChanged"},
                                    {"target", MakeObjectId(m_documentId, "selection", "Selection")},
                                    {"revision", m_nextRevision++},
                                    {"debugLabel", "3ds Max selection"},
                                    {"payload", json{{"selectedObjectIds", selectedObjectIds}}}}});
    if (SendBatch(m_sessionId, m_nextSequence, false, deltas)) {
      ++m_nextSequence;
    }
  }

  Interface *m_interface = nullptr;
  IUtil *m_iu = nullptr;
  std::mutex m_sendMutex;
  UINT_PTR m_pollTimer = 0;
  std::unordered_map<ULONG_PTR, NodeSnapshot> m_lastNodeState;
  std::string m_sessionId;
  std::string m_documentId;
  uint64_t m_nextSequence = 1;
  uint64_t m_nextRevision = 1;
};

ProjectRenderLiveLinkUtility g_utility;

class ProjectRenderLiveLinkClassDesc final : public ClassDesc2 {
public:
  int IsPublic() override { return TRUE; }
  void *Create(BOOL /*loading*/) override { return &g_utility; }
  const TCHAR *ClassName() override { return _T("project-render LiveLink"); }
  const TCHAR *NonLocalizedClassName() override {
    return _T("project-render LiveLink");
  }
  SClass_ID SuperClassID() override { return UTILITY_CLASS_ID; }
  Class_ID ClassID() override { return Class_ID(0x5e5824a1, 0x3a0f6b4d); }
  const TCHAR *Category() override { return _T("project-render"); }
  const TCHAR *InternalName() override { return _T("ProjectRenderLiveLink"); }
  HINSTANCE HInstance() override { return g_instance; }
};

ProjectRenderLiveLinkClassDesc g_classDesc;

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/) {
  if (fdwReason == DLL_PROCESS_ATTACH) {
    g_instance = hinstDLL;
    DisableThreadLibraryCalls(hinstDLL);
  }
  return TRUE;
}

extern "C" __declspec(dllexport) const TCHAR *LibDescription() {
  return _T("project-render LiveLink for 3ds Max 2025");
}

extern "C" __declspec(dllexport) int LibNumberClasses() { return 1; }

extern "C" __declspec(dllexport) ClassDesc *LibClassDesc(int index) {
  return index == 0 ? &g_classDesc : nullptr;
}

extern "C" __declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

extern "C" __declspec(dllexport) ULONG CanAutoDefer() { return 1; }