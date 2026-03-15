#include "max_livelink_pipe_client.h"

#include <max.h>
#include <utilapi.h>
#include <iparamm2.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <string>

using json = nlohmann::json;

namespace {

HINSTANCE g_instance = nullptr;
MaxLiveLinkPipeClient g_pipeClient;
constexpr const char *kPipeName = "project-render-max-livelink";
constexpr const char *kSourceApp = "3dsMax2025";

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
  return "3dsmax2025-" + std::to_string(GetCurrentProcessId());
}

std::string MakeNodeObjectId(INode *node) {
  if (!node) {
    return {};
  }
  return "node:" + std::to_string(static_cast<unsigned long long>(node->GetHandle()));
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

void AppendNodeSnapshot(Interface *ip, INode *node, const std::string &documentId,
                        uint64_t *revision, json *outDeltas) {
  if (!ip || !node || !revision || !outDeltas) {
    return;
  }

  const std::string objectId = MakeNodeObjectId(node);
  const std::string nodeName = ToUtf8(node->GetName());
  INode *parent = node->GetParentNode();
  const std::string parentId =
      (parent && !parent->IsRootNode()) ? MakeNodeObjectId(parent) : std::string();

  outDeltas->push_back(json{
      {"kind", "NodeAdded"},
      {"target", MakeObjectId(documentId, objectId, "Node")},
      {"revision", (*revision)++},
      {"debugLabel", nodeName},
      {"payload", json{{"parentObjectId", parentId}, {"displayName", nodeName}}},
  });

  const Matrix3 worldMatrix = node->GetNodeTM(ip->GetTime());
  outDeltas->push_back(json{
      {"kind", "NodeTransformChanged"},
      {"target", MakeObjectId(documentId, objectId, "Node")},
      {"revision", (*revision)++},
      {"debugLabel", nodeName},
      {"payload", json{{"worldMatrix", Matrix3ToColumnMajor4x4(worldMatrix)}}},
  });

  outDeltas->push_back(json{
      {"kind", "NodeVisibilityChanged"},
      {"target", MakeObjectId(documentId, objectId, "Node")},
      {"revision", (*revision)++},
      {"debugLabel", nodeName},
      {"payload", json{{"visible", !node->IsNodeHidden(TRUE)}}},
  });

  for (int childIndex = 0; childIndex < node->NumberOfChildren(); ++childIndex) {
    AppendNodeSnapshot(ip, node->GetChildNode(childIndex), documentId, revision,
                       outDeltas);
  }
}

bool SendInitialSnapshot(Interface *ip) {
  if (!ip) {
    return false;
  }
  if (!g_pipeClient.IsConnected() && !g_pipeClient.Connect(kPipeName)) {
    return false;
  }

  const std::string documentId = MakeDocumentId(ip);
  const std::string sessionId = MakeSessionId();

  json batch;
  batch["providerName"] = "3dsMax2025Pipe";
  batch["sessionId"] = sessionId;
  batch["sequence"] = 1;
  batch["fullSync"] = true;
  batch["deltas"] = json::array();

  batch["deltas"].push_back(json{
      {"kind", "SessionOpened"},
      {"target", json{{"sourceApp", kSourceApp},
                       {"documentId", documentId},
                       {"objectId", "session"},
                       {"objectType", "Unknown"}}},
      {"payload", json{{"documentPath", documentId},
                        {"displayName", documentId}}},
  });
  batch["deltas"].push_back(json{
      {"kind", "FullSceneSync"},
      {"payload", json{{"clearsExistingScene", false}}},
  });

  uint64_t revision = 1;
  INode *root = ip->GetRootNode();
  if (root) {
    for (int childIndex = 0; childIndex < root->NumberOfChildren(); ++childIndex) {
      AppendNodeSnapshot(ip, root->GetChildNode(childIndex), documentId, &revision,
                         &batch["deltas"]);
    }
  }

  return g_pipeClient.SendJsonLine(batch.dump());
}

void SendSessionClosed() {
  if (!g_pipeClient.IsConnected()) {
    return;
  }

  json batch;
  batch["providerName"] = "3dsMax2025Pipe";
  batch["sessionId"] = MakeSessionId();
  batch["sequence"] = 2;
  batch["fullSync"] = false;
  batch["deltas"] = json::array(
      {json{{"kind", "SessionClosed"},
            {"payload", json{{"reason", "3ds Max utility closed"},
                              {"graceful", true}}}}});
  g_pipeClient.SendJsonLine(batch.dump());
}

class ProjectRenderLiveLinkUtility final : public UtilityObj {
public:
  void BeginEditParams(Interface *ip, IUtil *iu) override {
    m_interface = ip;
    m_iu = iu;
    SendInitialSnapshot(ip);
  }

  void EndEditParams(Interface * /*ip*/, IUtil * /*iu*/) override {
    SendSessionClosed();
    g_pipeClient.Disconnect();
    m_interface = nullptr;
    m_iu = nullptr;
  }

  void DeleteThis() override {}
  void Init(HWND /*hWnd*/) override {}
  void Destroy(HWND /*hWnd*/) override {}

private:
  Interface *m_interface = nullptr;
  IUtil *m_iu = nullptr;
};

ProjectRenderLiveLinkUtility g_utility;

class ProjectRenderLiveLinkClassDesc final : public ClassDesc2 {
public:
  int IsPublic() override { return TRUE; }
  void *Create(BOOL /*loading*/) override { return &g_utility; }
  const TCHAR *ClassName() override { return _T("project-render LiveLink"); }
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