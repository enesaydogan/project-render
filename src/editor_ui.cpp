#include "editor_ui.h"

#include <d3d12.h>
#include <windows.h>
#include <wrl.h>

#include "ImGuizmo.h"
#include "camera.h"
#include "clouds.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "file_import.h"
#include "ibl_manager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "input_handler.h"
#include "material_editor.h"
#include "oidn_denoiser.h"
#include "raster_renderer.h"
#include "scene.h"
#include "scene_io.h"
#include "streamline_manager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <unordered_set>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace DX12Context;

// ── extern globals defined in main.cpp ──────────────────────────────────────
extern HWND g_hwnd;
extern bool g_appClosing;
extern bool g_cloudRenderingEnabled;
extern CloudManager g_cloudManager;
extern RenderMode g_currentRenderMode;
extern bool g_drawGrid;
extern bool g_verboseRenderLogs;
extern bool g_rasterDebugUV;
extern bool g_rasterWireframe;
extern bool g_rasterDebugDepth;
extern ComPtr<ID3D12RootSignature> g_rootSignature;
extern CameraCB g_cameraData;
extern void RequestGrassRuntimeRefreshForSceneLoad();
// use DX12Context::g_streamline
extern float g_camSpeed;
extern float g_mouseSensitivity;
extern ComPtr<ID3D12Resource> g_constantBuffer;

// From dxr_renderer.h
extern bool g_rayTracingSupported;

// Export render target state (defined in main.cpp)
extern ComPtr<ID3D12Resource> g_exportRenderTarget;
extern ComPtr<ID3D12DescriptorHeap> g_exportRtvHeap;
extern UINT g_exportRenderTargetWidth;
extern UINT g_exportRenderTargetHeight;
extern D3D12_RESOURCE_STATES g_exportRenderTargetState;
extern D3D12_CPU_DESCRIPTOR_HANDLE g_exportPreviewSrvCpu;
extern D3D12_GPU_DESCRIPTOR_HANDLE g_exportPreviewSrvGpu;
extern bool g_exportPreviewSrvAllocated;
extern DescriptorHeapAllocator g_cbvSrvAllocator;

// ── globals owned by this translation unit ──────────────────────────────────

const RenderResolutionPreset g_renderResolutionPresets[] = {
    {"1280 x 720 (HD)", 1280, 720},
    {"1920 x 1080 (Full HD)", 1920, 1080},
    {"2560 x 1440 (QHD)", 2560, 1440},
    {"3840 x 2160 (4K)", 3840, 2160},
};
const int g_renderResolutionPresetCount =
    (int)(sizeof(g_renderResolutionPresets) /
          sizeof(g_renderResolutionPresets[0]));

RenderExportSettings g_renderExportSettings;
RenderExportJobState g_renderExportJob;
RenderBatchExportState g_renderBatchExport;
RenderAnimationExportState g_renderAnimationExport;
std::string g_renderExportStatus;

bool g_showRenderModeWindow = false;
bool g_showAssetsWindow = false;
bool g_showLightsWindow = false;
bool g_showMaterialEditor = false;
bool g_showControlsWindow = false;
bool g_showRenderSettingsWindow = false;
static bool g_showReGIRDebugWindow = false;
bool g_forceUncollapse = false;

// Master toggle: when false the entire ImGui frame is skipped and no UI
// elements are drawn.  Controlled via F5 key in input_handler.cpp.
namespace Input {
  bool g_imguiEnabled = false;
}

int g_debugMode = 0; // 0=None, 1=Albedo, 2=Normal, 3=Emissive, ...

struct SceneIoJobState {
  bool active = false;
  bool isSave = false;
  std::future<bool> worker;
  std::string path;
  std::string stage;
  float progress = 0.0f;
};

static SceneIoJobState g_sceneIoJob;
static std::mutex g_sceneIoUiMutex;
static std::atomic<float> g_sceneIoProgressAtomic{0.0f};
static std::atomic<uint32_t> g_sceneIoTickAtomic{0};
static std::string g_sceneIoStageAtomic;
static std::string g_currentScenePath;

static bool RecreateDxrPipelineSafe(UINT width, UINT height,
                                    const char *context);

static std::filesystem::path SceneIoNativePathFromUtf8(
    const std::string &utf8Path) {
  if (utf8Path.empty()) {
    return {};
  }

#ifdef _WIN32
  const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            utf8Path.c_str(), -1, nullptr, 0);
  if (wideCount > 0) {
    std::wstring wide(static_cast<size_t>(wideCount), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                              utf8Path.c_str(), -1,
                                              wide.data(), wideCount);
    if (converted == wideCount) {
      if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
      }
      return std::filesystem::path(wide);
    }
  }
#endif

  return std::filesystem::path(utf8Path);
}

static std::string NormalizeSceneSaveJobPath(const std::string &utf8Path) {
  std::filesystem::path path = SceneIoNativePathFromUtf8(utf8Path);
  if (path.empty()) {
    return {};
  }

  const std::filesystem::path extension = path.extension();
  if (extension.empty() || extension == L".") {
    path += L".prs";
  }

#ifdef _WIN32
  return WStringToUtf8(path.wstring());
#else
  return path.string();
#endif
}

static void GetCurrentDxrPreviewSize(UINT &width, UINT &height) {
  D3D12_RECT previewRect = {0, 0, (LONG)g_windowWidth, (LONG)g_windowHeight};
  GetSafeFramePreviewRect(g_windowWidth, g_windowHeight, previewRect);
  width = (UINT)(std::max<LONG>)(1, previewRect.right - previewRect.left);
  height = (UINT)(std::max<LONG>)(1, previewRect.bottom - previewRect.top);
}

struct HiddenProcessResult {
  int exitCode = -1;
  DWORD launchError = 0;
  std::string output;
};
static std::future<HiddenProcessResult> g_animationEncodeWorker;
static std::string g_animationEncodeMessage;

struct PreviewOverlayState {
  bool visible = false;
  bool needsPresent = false;
  unsigned int width = 0;
  unsigned int height = 0;
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float forward[3] = {0.0f, 0.0f, 0.0f};
  float up[3] = {0.0f, 0.0f, 0.0f};
  float fov = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  RenderMode renderMode = RenderMode::Raster;
};

static PreviewOverlayState g_previewOverlay;

static void ReleasePreviewOverlayImage() {
  if (g_exportRenderTarget) {
    // The preview target can still be referenced by the previous frame's
    // direct present command list when the user dismisses it.
    // Drain the direct queue before releasing that SRV-backed texture.
    WaitGPUIdle();
  }
  g_previewOverlay = {};
  g_exportRenderTarget.Reset();
  g_exportRtvHeap.Reset();
  g_exportRenderTargetWidth = 0;
  g_exportRenderTargetHeight = 0;
  g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
}

void LatchPreviewRenderImage() {
  g_previewOverlay.visible = true;
  g_previewOverlay.needsPresent = true;
  g_previewOverlay.width = g_renderExportJob.targetWidth;
  g_previewOverlay.height = g_renderExportJob.targetHeight;
  for (int i = 0; i < 3; ++i) {
    g_previewOverlay.pos[i] = g_cameraData.pos[i];
    g_previewOverlay.forward[i] = g_cameraData.forward[i];
    g_previewOverlay.up[i] = g_cameraData.up[i];
  }
  g_previewOverlay.fov = g_cameraData.fov;
  g_previewOverlay.nearZ = g_cameraData.nearZ;
  g_previewOverlay.farZ = g_cameraData.farZ;
  g_previewOverlay.yaw = g_camYaw;
  g_previewOverlay.pitch = g_camPitch;
  g_previewOverlay.renderMode = g_renderExportJob.previousMode;
}

static bool HasPreviewOverlayViewChanged() {
  if (!g_previewOverlay.visible) {
    return false;
  }
  if (g_currentRenderMode != g_previewOverlay.renderMode) {
    return true;
  }
  for (int i = 0; i < 3; ++i) {
    if (g_cameraData.pos[i] != g_previewOverlay.pos[i] ||
        g_cameraData.forward[i] != g_previewOverlay.forward[i] ||
        g_cameraData.up[i] != g_previewOverlay.up[i]) {
      return true;
    }
  }
  return g_cameraData.fov != g_previewOverlay.fov ||
         g_cameraData.nearZ != g_previewOverlay.nearZ ||
         g_cameraData.farZ != g_previewOverlay.farZ ||
         g_camYaw != g_previewOverlay.yaw ||
         g_camPitch != g_previewOverlay.pitch;
}

static void UpdatePreviewOverlayState() {
  if (g_renderExportJob.active || !g_previewOverlay.visible) {
    return;
  }
  if (!g_exportRenderTarget || g_exportPreviewSrvGpu.ptr == 0) {
    ReleasePreviewOverlayImage();
    return;
  }
  if (HasPreviewOverlayViewChanged()) {
    ReleasePreviewOverlayImage();
    if (g_renderExportStatus.rfind("Preview ready", 0) == 0) {
      g_renderExportStatus.clear();
    }
  }
}

static void SceneIoProgressSink(float progress01, const char *stage) {
  g_sceneIoProgressAtomic.store((std::clamp)(progress01, 0.0f, 1.0f),
                                std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(g_sceneIoUiMutex);
    g_sceneIoStageAtomic = stage ? stage : "";
  }
  g_sceneIoTickAtomic.fetch_add(1, std::memory_order_relaxed);
}

void StartSceneIoJob(bool isSave, const std::string &utf8Path) {
  const std::string jobPath =
      isSave ? NormalizeSceneSaveJobPath(utf8Path) : utf8Path;
  if (g_sceneIoJob.active || jobPath.empty()) {
    return;
  }

  // Scene I/O reads or mutates global mesh/material/texture arrays and DXR
  // resources. The main thread pauses scene rendering and editing while any job
  // is active so async saves cannot serialize half-applied scene edits.
  DxrRenderer::WaitForAsyncRestirIdle();
  WaitGPUIdle();

  g_sceneIoJob.active = true;
  g_sceneIoJob.isSave = isSave;
  g_sceneIoJob.path = jobPath;
  g_sceneIoJob.progress = 0.0f;
  g_sceneIoJob.stage = isSave ? "Preparing save" : "Preparing load";
  g_sceneIoProgressAtomic.store(0.0f, std::memory_order_relaxed);
  g_sceneIoTickAtomic.store(0, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(g_sceneIoUiMutex);
    g_sceneIoStageAtomic = g_sceneIoJob.stage;
  }

  SceneIO::SetProgressCallback(&SceneIoProgressSink);
  g_sceneIoJob.worker =
      std::async(std::launch::async, [isSave, jobPath]() -> bool {
        return isSave ? SceneIO::SaveScene(jobPath) : SceneIO::LoadScene(jobPath);
      });
}

bool IsSceneLoadInProgress() {
  return g_sceneIoJob.active && !g_sceneIoJob.isSave;
}

bool IsSceneIoJobActive() {
  return g_sceneIoJob.active;
}

bool IsSceneIoSaveJob() {
  return g_sceneIoJob.active && g_sceneIoJob.isSave;
}

float GetSceneIoProgress() {
  return g_sceneIoJob.progress;
}

std::string GetSceneIoStage() {
  return g_sceneIoJob.stage;
}

bool HasCurrentScenePath() {
  return !g_currentScenePath.empty();
}

std::string GetCurrentScenePath() {
  return g_currentScenePath;
}

void SetCurrentScenePath(const std::string &utf8Path) {
  g_currentScenePath = utf8Path;
}

static void UpdateSceneIoJob() {
  if (!g_sceneIoJob.active) {
    return;
  }

  g_sceneIoJob.progress =
      g_sceneIoProgressAtomic.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(g_sceneIoUiMutex);
    if (!g_sceneIoStageAtomic.empty()) {
      g_sceneIoJob.stage = g_sceneIoStageAtomic;
    }
  }

  if (g_sceneIoJob.worker.valid() &&
      g_sceneIoJob.worker.wait_for(std::chrono::milliseconds(0)) ==
          std::future_status::ready) {
    bool ok = false;
    try {
      ok = g_sceneIoJob.worker.get();
    } catch (const std::exception &e) {
      fprintf(stderr, "Scene I/O worker exception: %s\n", e.what());
      ok = false;
    } catch (...) {
      fprintf(stderr, "Scene I/O worker exception: unknown\n");
      ok = false;
    }

    SceneIO::SetProgressCallback(nullptr);
    g_sceneIoJob.active = false;
    g_sceneIoJob.progress = 1.0f;
    g_sceneIoJob.stage = ok ? (g_sceneIoJob.isSave ? "Save complete"
                                                    : "Load complete")
                            : (g_sceneIoJob.isSave ? "Save failed"
                                                   : "Load failed");

    if (ok) {
      g_currentScenePath = g_sceneIoJob.path;
      fprintf(stderr, "%s scene %s\n",
              g_sceneIoJob.isSave ? "Saved" : "Loaded", g_sceneIoJob.path.c_str());
      if (!g_sceneIoJob.isSave) {
        Input::ResetTransientInputState();
        RequestGrassRuntimeRefreshForSceneLoad();
        Scene::RequestRendererFullRebuild();
        g_cloudManager.RequestBake();
        DxrRenderer::ResetStreamlineHistory();
        DxrRenderer::ResetAccumulation();
        UpdateCameraCB();
        if (g_currentRenderMode == RenderMode::DXR && g_rayTracingSupported) {
          UINT dxrWidth = 1;
          UINT dxrHeight = 1;
          GetCurrentDxrPreviewSize(dxrWidth, dxrHeight);
          if (!RecreateDxrPipelineSafe(dxrWidth, dxrHeight,
                                       "scene load complete")) {
            g_currentRenderMode = RenderMode::Raster;
          } else {
            DxrRenderer::RequestSceneLoadWarmup("scene load complete");
          }
        }
      }
    } else {
      fprintf(stderr, "Failed to %s scene %s\n",
              g_sceneIoJob.isSave ? "save" : "load", g_sceneIoJob.path.c_str());
    }
  }
}

static void DrawSceneIoOverlay() {
  if (!g_sceneIoJob.active) {
    return;
  }

  ImGuiViewport *vp = ImGui::GetMainViewport();
  if (!vp) {
    return;
  }

  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + 12.0f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.90f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav |
                           ImGuiWindowFlags_NoDocking;

  if (ImGui::Begin("##SceneIOOverlay", nullptr, flags)) {
    const char *title = g_sceneIoJob.isSave ? "Saving scene" : "Loading scene";
    const int dots = (int)(ImGui::GetTime() * 4.0) % 4;
    char anim[8] = "";
    for (int i = 0; i < dots; ++i) anim[i] = '.';
    ImGui::Text("%s%s", title, anim);
    ImGui::TextWrapped("%s", g_sceneIoJob.stage.c_str());
    ImGui::ProgressBar((std::clamp)(g_sceneIoJob.progress, 0.0f, 1.0f),
                       ImVec2(420.0f, 0.0f));
  }
  ImGui::End();
}

// ── helper lambdas/functions that were local to WinMain ─────────────────────

static DxrRenderer::DenoiserMode DenoiserModeFromIndex(int idx) {
  if (idx == 1)
    return DxrRenderer::DenoiserMode::OIDN_CPU;
  if (idx == 2)
    return DxrRenderer::DenoiserMode::OIDN_GPU;
  if (idx == 3)
    return DxrRenderer::DenoiserMode::OptiX;
  return DxrRenderer::DenoiserMode::Off;
}

static int DenoiserIndexFromMode(DxrRenderer::DenoiserMode mode) {
  switch (mode) {
  case DxrRenderer::DenoiserMode::OIDN_CPU:
    return 1;
  case DxrRenderer::DenoiserMode::OIDN_GPU:
    return 2;
  case DxrRenderer::DenoiserMode::OptiX:
    return 3;
  default:
    return 0;
  }
}

static const char *DebugLightTypeName(uint32_t type) {
  switch (static_cast<LightType>(type)) {
  case LightType::Directional:
    return "Directional";
  case LightType::Omni:
    return "Point";
  case LightType::Spot:
    return "Spot";
  case LightType::AreaRect:
    return "Area Rect";
  case LightType::AreaDisk:
    return "Area Disk";
  case LightType::IES:
    return "IES";
  }
  return "Unknown";
}

static UINT FlattenedLightIndexForInstance(int instanceIndex) {
  if (instanceIndex < 0) {
    return 0xFFFFFFFFu;
  }
  const auto &instances = Scene::GetLightInstances();
  const auto &prototypes = Scene::GetLightPrototypes();
  UINT flattenedIndex = 0;
  for (size_t i = 0; i < instances.size(); ++i) {
    const LightInstance &inst = instances[i];
    if (inst.prototypeIndex >= prototypes.size()) {
      continue;
    }
    const LightPrototype &proto = prototypes[inst.prototypeIndex];
    const bool emits = proto.enabled && inst.enabled;
    if (static_cast<int>(i) == instanceIndex) {
      return emits ? flattenedIndex : 0xFFFFFFFFu;
    }
    if (emits) {
      ++flattenedIndex;
    }
  }
  return 0xFFFFFFFFu;
}

static float BytesToMiB(uint64_t bytes) {
  return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

static bool RecreateDxrPipelineSafe(UINT width, UINT height,
                                    const char *context);

static std::wstring Utf8ToWString(const std::string &utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                             (int)utf8.size(), nullptr, 0);
  if (sizeNeeded <= 0) {
    return {};
  }
  std::wstring wide(sizeNeeded, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(),
                      sizeNeeded);
  return wide;
}

static std::wstring SanitizeFileStem(const std::wstring &input) {
  std::wstring sanitized;
  sanitized.reserve(input.size());
  for (wchar_t ch : input) {
    const bool invalid = ch < 32 || ch == L'<' || ch == L'>' || ch == L':' ||
                         ch == L'"' || ch == L'/' || ch == L'\\' || ch == L'|' ||
                         ch == L'?' || ch == L'*';
    sanitized.push_back(invalid ? L'_' : ch);
  }
  while (!sanitized.empty() &&
         (sanitized.back() == L' ' || sanitized.back() == L'.')) {
    sanitized.pop_back();
  }
  size_t firstValid = 0;
  while (firstValid < sanitized.size() && sanitized[firstValid] == L' ') {
    ++firstValid;
  }
  sanitized.erase(0, firstValid);
  if (sanitized.empty()) {
    sanitized = L"view";
  }
  return sanitized;
}

static std::wstring BuildBatchOutputPath(const std::wstring &directory,
                                         const std::wstring &baseName,
                                         const std::string &viewName,
                                         size_t viewOrdinal,
                                         std::unordered_set<std::wstring>
                                             &usedStems) {
  namespace fs = std::filesystem;

  std::wstring viewStem = SanitizeFileStem(Utf8ToWString(viewName));
  if (viewStem.empty()) {
    viewStem = L"view" + std::to_wstring(viewOrdinal + 1);
  }

  std::wstring stem = SanitizeFileStem(baseName) + L"-" + viewStem;
  if (!usedStems.insert(stem).second) {
    stem += L"-" + std::to_wstring(viewOrdinal + 1);
  }
  return (fs::path(directory) / fs::path(stem + L".png")).wstring();
}

static std::wstring BuildAnimationOutputPath(const std::wstring &directory,
                                             const std::string &baseName,
                                             int frameIndex,
                                             int totalFrames) {
  namespace fs = std::filesystem;

  const int digits =
      (std::max)(4, static_cast<int>(std::to_string((std::max)(1, totalFrames)).size()));
  std::wstring stem = SanitizeFileStem(Utf8ToWString(baseName));
  if (stem.empty()) {
    stem = L"final";
  }
  std::wstring frameSuffix = std::to_wstring(frameIndex + 1);
  if (frameSuffix.size() < static_cast<size_t>(digits)) {
    frameSuffix.insert(frameSuffix.begin(), digits - frameSuffix.size(), L'0');
  }
  return (fs::path(directory) /
          fs::path(stem + L"-" + frameSuffix + L".png"))
      .wstring();
}

static int GetAnimationFrameDigits(int totalFrames) {
  return (std::max)(4,
                    static_cast<int>(std::to_string((std::max)(1, totalFrames)).size()));
}

static std::wstring BuildAnimationFramePattern(const std::wstring &directory,
                                               const std::string &baseName,
                                               int totalFrames) {
  namespace fs = std::filesystem;

  const int digits = GetAnimationFrameDigits(totalFrames);
  std::wstring stem = SanitizeFileStem(Utf8ToWString(baseName));
  if (stem.empty()) {
    stem = L"final";
  }
  return (fs::path(directory) /
          fs::path(stem + L"-%0" + std::to_wstring(digits) + L"d.png"))
      .wstring();
}

static std::wstring BuildAnimationVideoOutputPath(const std::wstring &directory,
                                                  const std::string &baseName) {
  namespace fs = std::filesystem;

  std::wstring stem = SanitizeFileStem(Utf8ToWString(baseName));
  if (stem.empty()) {
    stem = L"final";
  }
  return (fs::path(directory) / fs::path(stem + L".mp4")).wstring();
}

static std::wstring BuildAnimationTempDirectory(const std::wstring &directory,
                                                const std::string &baseName) {
  namespace fs = std::filesystem;

  std::wstring stem = SanitizeFileStem(Utf8ToWString(baseName));
  if (stem.empty()) {
    stem = L"final";
  }
  return (fs::path(directory) /
          fs::path(L".project-render-" + stem + L"-frames-" +
                   std::to_wstring(GetTickCount64())))
      .wstring();
}

static std::wstring SearchExecutablePath(const wchar_t *fileName) {
  const DWORD required = SearchPathW(nullptr, fileName, nullptr, 0, nullptr, nullptr);
  if (required == 0) {
    return {};
  }
  std::wstring path(required, L'\0');
  const DWORD written = SearchPathW(nullptr, fileName, nullptr,
                                    static_cast<DWORD>(path.size()),
                                    path.data(), nullptr);
  if (written == 0) {
    return {};
  }
  if (!path.empty() && path.back() == L'\0') {
    path.pop_back();
  } else {
    path.resize(written);
  }
  return path;
}

static std::wstring FindFfmpegExecutable() {
  wchar_t modulePath[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) != 0) {
    const std::filesystem::path appDir = std::filesystem::path(modulePath).parent_path();
    const std::filesystem::path localFfmpeg = appDir / L"ffmpeg.exe";
    if (std::filesystem::exists(localFfmpeg)) {
      return localFfmpeg.wstring();
    }
  }
  return SearchExecutablePath(L"ffmpeg.exe");
}

static std::wstring QuoteCommandLineArg(const std::wstring &arg) {
  if (arg.empty()) {
    return L"\"\"";
  }

  bool needsQuotes = false;
  for (wchar_t ch : arg) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      needsQuotes = true;
      break;
    }
  }
  if (!needsQuotes) {
    return arg;
  }

  std::wstring quoted;
  quoted.push_back(L'"');
  int backslashCount = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashCount;
      continue;
    }
    if (ch == L'"') {
      quoted.append(backslashCount * 2 + 1, L'\\');
      quoted.push_back(ch);
      backslashCount = 0;
      continue;
    }
    if (backslashCount > 0) {
      quoted.append(backslashCount, L'\\');
      backslashCount = 0;
    }
    quoted.push_back(ch);
  }
  if (backslashCount > 0) {
    quoted.append(backslashCount * 2, L'\\');
  }
  quoted.push_back(L'"');
  return quoted;
}

static std::string FormatDurationHms(double totalSeconds) {
  if (totalSeconds < 0.0) {
    totalSeconds = 0.0;
  }
  const int roundedSeconds = static_cast<int>(std::round(totalSeconds));
  const int hours = roundedSeconds / 3600;
  const int minutes = (roundedSeconds / 60) % 60;
  const int seconds = roundedSeconds % 60;
  char buffer[32] = {};
  if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  }
  return buffer;
}

static std::string TrimProcessOutput(std::string text) {
  constexpr size_t kMaxLength = 600;
  while (!text.empty() &&
         (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
    text.pop_back();
  }
  if (text.size() <= kMaxLength) {
    return text;
  }
  return text.substr(text.size() - kMaxLength);
}

static HiddenProcessResult RunHiddenProcessCapture(
    const std::wstring &commandLine,
    const std::wstring &workingDirectory) {
  HiddenProcessResult result;
  SECURITY_ATTRIBUTES securityAttributes = {};
  securityAttributes.nLength = sizeof(securityAttributes);
  securityAttributes.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
    result.launchError = GetLastError();
    return result;
  }
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startupInfo = {};
  startupInfo.cb = sizeof(startupInfo);
  startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  startupInfo.wShowWindow = SW_HIDE;
  startupInfo.hStdOutput = writePipe;
  startupInfo.hStdError = writePipe;
  startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION processInfo = {};
  std::wstring mutableCommandLine = commandLine;
  const wchar_t *workingDir = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
  if (!CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, workingDir,
                      &startupInfo, &processInfo)) {
    result.launchError = GetLastError();
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return result;
  }

  CloseHandle(writePipe);

  char buffer[4096] = {};
  DWORD bytesRead = 0;
  while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) &&
         bytesRead > 0) {
    result.output.append(buffer, buffer + bytesRead);
  }
  CloseHandle(readPipe);

  WaitForSingleObject(processInfo.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  result.exitCode = static_cast<int>(exitCode);
  return result;
}

std::string GetAnimationExportProgressText() {
  if (!g_renderAnimationExport.active) {
    return g_renderExportStatus;
  }

  const ULONGLONG nowMs = GetTickCount64();
  const double elapsedSeconds =
      (g_renderAnimationExport.startedTickMs > 0 &&
       nowMs >= g_renderAnimationExport.startedTickMs)
          ? static_cast<double>(nowMs - g_renderAnimationExport.startedTickMs) /
                1000.0
          : 0.0;

  if (g_renderAnimationExport.encoding) {
    const double encodingElapsedSeconds =
        (g_renderAnimationExport.encodingStartedTickMs > 0 &&
         nowMs >= g_renderAnimationExport.encodingStartedTickMs)
            ? static_cast<double>(nowMs -
                                  g_renderAnimationExport.encodingStartedTickMs) /
                  1000.0
            : 0.0;
    return "Encoding MP4 | elapsed " + FormatDurationHms(elapsedSeconds) +
           " | encode " + FormatDurationHms(encodingElapsedSeconds);
  }

  const int totalFrames = (std::max)(1, g_renderAnimationExport.totalFrames);
  const int currentFrame =
      (std::clamp)(g_renderAnimationExport.currentFrameIndex, 1, totalFrames);
  const double averageSecondsPerFrame =
      elapsedSeconds / static_cast<double>((std::max)(1, currentFrame));
  const double estimatedTotalSeconds =
      averageSecondsPerFrame * static_cast<double>(totalFrames);
  const double remainingSeconds =
      (std::max)(0.0, estimatedTotalSeconds - elapsedSeconds);

  return "Rendering animation frame " + std::to_string(currentFrame) + "/" +
         std::to_string(totalFrames) + " | elapsed " +
         FormatDurationHms(elapsedSeconds) + " | left " +
         FormatDurationHms(remainingSeconds) + " | total " +
         FormatDurationHms(estimatedTotalSeconds);
}

static bool StartAnimationMp4Encode() {
  if (!g_renderAnimationExport.active || g_renderAnimationExport.encoding) {
    return false;
  }

  const auto &animationSettings = AnimationSequence::GetExportSettings();
  const std::wstring inputPattern = BuildAnimationFramePattern(
      g_renderAnimationExport.frameOutputDirectory,
      animationSettings.baseName,
      g_renderAnimationExport.totalFrames);
  const std::wstring outputPath = g_renderAnimationExport.finalOutputPath;
  const std::wstring ffmpegPath = g_renderAnimationExport.ffmpegExecutable;
  const int fps = (std::max)(1, g_renderAnimationExport.fps);
  const std::wstring workingDirectory = g_renderAnimationExport.outputDirectory;

  if (ffmpegPath.empty() || inputPattern.empty() || outputPath.empty()) {
    return false;
  }

  g_renderAnimationExport.encoding = true;
  g_renderAnimationExport.encodingStartedTickMs = GetTickCount64();
  g_renderExportStatus = GetAnimationExportProgressText();
  g_animationEncodeMessage.clear();
  g_animationEncodeWorker = std::async(
      std::launch::async,
      [ffmpegPath, inputPattern, outputPath, fps, workingDirectory]() -> HiddenProcessResult {
        const std::wstring commandLine =
            QuoteCommandLineArg(ffmpegPath) +
            L" -y -loglevel error -framerate " + std::to_wstring(fps) +
            L" -start_number 1 -i " + QuoteCommandLineArg(inputPattern) +
            L" -c:v libx264 -pix_fmt yuv420p -movflags +faststart " +
            QuoteCommandLineArg(outputPath);
        HiddenProcessResult result =
            RunHiddenProcessCapture(commandLine, workingDirectory);
        if (result.launchError != 0) {
          result.output = "CreateProcess failed with error " +
                          std::to_string(result.launchError);
        }
        return result;
      });
  return true;
}

bool IsRenderExportActive() {
  return g_renderExportJob.active || g_renderBatchExport.active ||
         g_renderAnimationExport.active;
}

static void UpdateAnimationEncodingJob() {
  if (!g_renderAnimationExport.active || !g_renderAnimationExport.encoding ||
      !g_animationEncodeWorker.valid()) {
    return;
  }

  if (g_animationEncodeWorker.wait_for(std::chrono::milliseconds(0)) !=
      std::future_status::ready) {
    return;
  }

  const HiddenProcessResult processResult = g_animationEncodeWorker.get();
  const bool success = (processResult.exitCode == 0) &&
                       !g_renderAnimationExport.finalOutputPath.empty() &&
                       std::filesystem::exists(g_renderAnimationExport.finalOutputPath);
  const std::wstring finalOutputPath = g_renderAnimationExport.finalOutputPath;
  const std::wstring tempFrameDirectory = g_renderAnimationExport.temporaryFrameDirectory;
  g_animationEncodeMessage = TrimProcessOutput(processResult.output);
  if (success && !tempFrameDirectory.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(tempFrameDirectory, ec);
  }

  g_renderAnimationExport.encoding = false;
  g_renderAnimationExport.active = false;
  if (success) {
    g_renderExportStatus = "Saved MP4: " + WStringToUtf8(finalOutputPath);
  } else {
    g_renderExportStatus = "MP4 encoding failed";
    if (processResult.exitCode >= 0) {
      g_renderExportStatus +=
          " (exit " + std::to_string(processResult.exitCode) + ")";
    }
    if (!g_animationEncodeMessage.empty()) {
      g_renderExportStatus += ": " + g_animationEncodeMessage;
    }
    if (!tempFrameDirectory.empty()) {
      g_renderExportStatus += " | PNG frames kept in: " +
                              WStringToUtf8(tempFrameDirectory);
    }
  }
  g_renderAnimationExport = {};
}

struct RenderExportLaunchSettings {
  int resolutionPreset = 0;
  int maxSpp = 1;
  float noisePercent = 0.0f;
  int denoiserIndex = 0;
  int projectionMode = (int)CameraProjectionMode::Perspective;
  bool allowNoiseThresholdStop = true;
  unsigned int explicitWidth = 0;
  unsigned int explicitHeight = 0;
  bool tileRenderingEnabled = false;
};

static void StartRenderExportJobWithSettings(
    const std::wstring &outputPath,
    const RenderExportLaunchSettings &settings,
    bool isPreview = false);
static bool StartNextAnimationRenderJob();

static bool StartNextBatchRenderJob() {
  if (!g_renderBatchExport.active) {
    return false;
  }

  const auto &views = SavedViews::GetViews();
  while (g_renderBatchExport.currentViewListIndex <
         g_renderBatchExport.viewIndices.size()) {
    const size_t queueIndex = g_renderBatchExport.currentViewListIndex;
    const size_t viewIndex = g_renderBatchExport.viewIndices[queueIndex];
    ++g_renderBatchExport.currentViewListIndex;
    if (viewIndex >= views.size()) {
      continue;
    }

    std::unordered_set<std::wstring> usedStems;
    for (size_t i = 0; i < queueIndex; ++i) {
      const size_t priorViewIndex = g_renderBatchExport.viewIndices[i];
      if (priorViewIndex >= views.size()) {
        continue;
      }
      BuildBatchOutputPath(g_renderBatchExport.outputDirectory,
                           g_renderBatchExport.baseName,
                           views[priorViewIndex].name, i, usedStems);
    }

    g_renderBatchExport.currentViewName = views[viewIndex].name;
    g_renderBatchExport.currentOutputPath = BuildBatchOutputPath(
        g_renderBatchExport.outputDirectory, g_renderBatchExport.baseName,
        g_renderBatchExport.currentViewName, queueIndex, usedStems);

    SavedViews::ApplyView(views[viewIndex]);
    StartRenderExportJob(g_renderBatchExport.currentOutputPath);
    if (!g_renderExportJob.active) {
      g_renderBatchExport.failed = true;
      return false;
    }

    g_renderExportStatus = "Batch rendering " + g_renderBatchExport.currentViewName +
                           " (" + std::to_string(queueIndex + 1) + "/" +
                           std::to_string(g_renderBatchExport.viewIndices.size()) +
                           ")";
    return true;
  }

  return false;
}

static bool StartNextAnimationRenderJob() {
  if (!g_renderAnimationExport.active) {
    return false;
  }
  if (g_renderAnimationExport.encoding) {
    return false;
  }
  if (g_renderAnimationExport.currentFrameIndex >=
      g_renderAnimationExport.totalFrames) {
    return false;
  }

  const int frameIndex = g_renderAnimationExport.currentFrameIndex;
  const int fps = (std::max)(1, g_renderAnimationExport.fps);
  SavedViews::SavedView frameCamera =
      AnimationSequence::EvaluateAtFrame(frameIndex, fps);
  SavedViews::ApplyView(frameCamera);

  const auto &animationSettings = AnimationSequence::GetExportSettings();
  g_renderAnimationExport.currentLabel = frameCamera.name;
  g_renderAnimationExport.currentOutputPath = BuildAnimationOutputPath(
      g_renderAnimationExport.frameOutputDirectory, animationSettings.baseName,
      frameIndex, g_renderAnimationExport.totalFrames);

  RenderExportLaunchSettings launchSettings = {};
  launchSettings.resolutionPreset = g_renderAnimationExport.resolutionPreset;
  launchSettings.maxSpp = g_renderAnimationExport.maxSpp;
  launchSettings.noisePercent = 0.0f;
  launchSettings.denoiserIndex = g_renderExportSettings.denoiserIndex;
  launchSettings.allowNoiseThresholdStop = false;
  StartRenderExportJobWithSettings(g_renderAnimationExport.currentOutputPath,
                                   launchSettings);
  if (!g_renderExportJob.active) {
    g_renderAnimationExport.failed = true;
    return false;
  }

  ++g_renderAnimationExport.currentFrameIndex;
  g_renderExportStatus = GetAnimationExportProgressText();
  return true;
}

// Forward declarations for helpers also used by the export job logic in
// main.cpp

// Minimum width that triggers tiled export on 6 GB GPUs.
static const UINT kTiledExportMinWidth = 2049;
static const UINT kTiledExportMinPixels = 4000000; // ~4 MP
static const UINT kDefaultTileHeight = 256;

static bool ShouldUseTiledExport(const RenderExportJobState &job) {
  // Tiled export for any very large render (panorama or perspective)
  if (job.targetWidth >= kTiledExportMinWidth)
    return true;
  // Also trigger on pixel count for tall narrow renders
  const unsigned long long totalPixels =
      (unsigned long long)job.targetWidth * job.targetHeight;
  return totalPixels >= kTiledExportMinPixels;
}

void SetupTiledExportJob(RenderExportJobState &job) {
  RenderExportTileState &t = job.tileState;
  t.enabled = true;
  t.fullWidth = job.targetWidth;
  t.fullHeight = job.targetHeight;

  // Use horizontal stripes: full width, fixed tile height. The last tile may
  // render a few rows beyond the panorama; compositing clamps the copied rows.
  t.tileWidth = t.fullWidth;
  t.tileHeight = (std::min)(kDefaultTileHeight, t.fullHeight);
  t.tileCountX = 1;
  t.tileCountY = (t.fullHeight + t.tileHeight - 1) / t.tileHeight;
  t.currentTileIndex = 0;
  t.tileOffsetX = 0;
  t.tileOffsetY = 0;
  job.targetWidth = t.tileWidth;
  job.targetHeight = t.tileHeight;

  // Allocate CPU full-frame HDR buffer (R16G16B16A16_FLOAT = 8 bytes/pixel)
  const size_t totalPixels = (size_t)t.fullWidth * t.fullHeight;
  t.cpuBeautyBuffer.resize(totalPixels * 8);
  if (job.targetDenoiserIndex != 0) {
    t.cpuAlbedoGuideBuffer.resize(totalPixels * 8);
    t.cpuNormalGuideBuffer.resize(totalPixels * 8);
  }
  t.guidesCaptured = false;
  t.guideReadbackFailed = false;

  fprintf(stderr,
          "Tiled panorama export: %ux%u -> %u tiles (%ux%u render size)\n",
          t.fullWidth, t.fullHeight,
          t.tileCountX * t.tileCountY,
          t.tileWidth, t.tileHeight);
}

// Update job state for the next tile. Returns false if no more tiles.
bool AdvanceToNextTile(RenderExportJobState &job) {
  RenderExportTileState &t = job.tileState;
  t.currentTileIndex++;
  if (t.currentTileIndex >= t.tileCountX * t.tileCountY) {
    return false; // All tiles done
  }
  // Compute tile offset for current index (row-major, single column)
  const UINT tileY = t.currentTileIndex;
  t.tileOffsetX = 0;
  t.tileOffsetY = tileY * t.tileHeight;
  job.targetWidth = t.tileWidth;
  job.targetHeight = t.tileHeight;
  return true;
}

// Composite a readback tile into a full-frame tightly packed Half4 buffer.
bool CompositeTileToHalf4Buffer(RenderExportTileState &t,
                                const std::vector<uint8_t> &srcData,
                                std::vector<uint8_t> &dstData) {
  if (!t.enabled || t.fullWidth == 0 || t.fullHeight == 0 ||
      t.tileWidth == 0 || t.tileHeight == 0 ||
      t.tileOffsetY >= t.fullHeight) {
    return false;
  }
  const size_t tileRowBytes = t.tileWidth * 8; // R16G16B16A16
  const size_t fullRowBytes = t.fullWidth * 8;
  const UINT rowsToCopy =
      (std::min)(t.tileHeight, t.fullHeight - t.tileOffsetY);
  const size_t bytesToCopy =
      (std::min)(tileRowBytes, fullRowBytes - (size_t)t.tileOffsetX * 8);
  if (srcData.size() < tileRowBytes * (size_t)t.tileHeight ||
      dstData.size() < fullRowBytes * (size_t)t.fullHeight) {
    return false;
  }
  for (UINT row = 0; row < rowsToCopy; ++row) {
    const size_t srcOffset = row * tileRowBytes;
    const size_t dstOffset = ((size_t)(t.tileOffsetY + row) * fullRowBytes) +
                             (size_t)t.tileOffsetX * 8;
    memcpy(dstData.data() + dstOffset, srcData.data() + srcOffset,
           bytesToCopy);
  }
  return true;
}

// Composite a readback tile into the full-frame HDR panorama buffer.
// srcData: tile-size R16G16B16A16_FLOAT data (8 bytes/pixel)
void CompositeTileToHdrPanorama(RenderExportTileState &t,
                                       const std::vector<uint8_t> &srcData) {
  (void)CompositeTileToHalf4Buffer(t, srcData, t.cpuBeautyBuffer);
}

// Simple HDR tonemap + convert to RGBA8 for the full panorama buffer.
// Uses the same ACES-inspired curve as the GPU tonemapper.
void TonemapHdrPanoramaToRgba8(
    const std::vector<uint8_t> &hdrBuffer,
    UINT width, UINT height,
    std::vector<uint8_t> &outRgba) {
  outRgba.resize((size_t)width * height * 4);
  const auto *src = reinterpret_cast<const uint16_t *>(hdrBuffer.data());
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const size_t idx = (size_t)y * width + x;
      const size_t srcOff = idx * 4;
      const size_t dstOff = idx * 4;

      // Convert half to float
      auto h2f = [](uint16_t h) -> float {
        // Simple half-to-float conversion
        const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
        uint32_t exp = (h >> 10) & 0x1Fu;
        uint32_t mant = h & 0x03FFu;
        uint32_t bits;
        if (exp == 0) {
          if (mant == 0) {
            bits = sign;
          } else {
            exp = 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
            mant &= 0x03FFu;
            bits = sign | ((exp + 112u) << 23) | (mant << 13);
          }
        } else if (exp == 31) {
          bits = sign | 0x7F800000u | (mant << 13);
        } else {
          bits = sign | ((exp + 112u) << 23) | (mant << 13);
        }
        float f;
        memcpy(&f, &bits, sizeof(f));
        return f;
      };

      float r = h2f(src[srcOff + 0]);
      float g = h2f(src[srcOff + 1]);
      float b = h2f(src[srcOff + 2]);
      // float a = h2f(src[srcOff + 3]);

      // ACES-inspired tonemap (matches GPU ToneMap)
      auto tonemap = [](float c) -> float {
        const float a = 2.51f, b2 = 0.03f, c2 = 2.43f, d = 0.59f, e = 0.14f;
        c = (std::max)(0.0f, c);
        return (std::min)(1.0f, (c * (a * c + b2)) / (c * (c2 * c + d) + e));
      };

      // Apply exposure (intensity) from camera
      float intensity = g_cameraData.intensity;
      r *= intensity;
      g *= intensity;
      b *= intensity;

      r = tonemap(r);
      g = tonemap(g);
      b = tonemap(b);

      // Linear to sRGB
      auto linearToSrgb = [](float c) -> uint8_t {
        c = (std::max)(0.0f, (std::min)(1.0f, c));
        if (c <= 0.0031308f) {
          c = c * 12.92f;
        } else {
          c = 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
        }
        return (uint8_t)(c * 255.0f + 0.5f);
      };

      outRgba[dstOff + 0] = linearToSrgb(r);
      outRgba[dstOff + 1] = linearToSrgb(g);
      outRgba[dstOff + 2] = linearToSrgb(b);
      outRgba[dstOff + 3] = 255;
    }
  }
}

static bool EnsureExportRenderTarget(UINT width, UINT height) {
  if (!g_device || width == 0 || height == 0) {
    return false;
  }

  if (g_exportRenderTarget && g_exportRtvHeap &&
      g_exportRenderTargetWidth == width &&
      g_exportRenderTargetHeight == height) {
    return true;
  }

  g_exportRenderTarget.Reset();
  g_exportRtvHeap.Reset();
  g_exportRenderTargetWidth = 0;
  g_exportRenderTargetHeight = 0;
  g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(g_device->CreateDescriptorHeap(&rtvHeapDesc,
                                            IID_PPV_ARGS(&g_exportRtvHeap)))) {
    return false;
  }

  D3D12_RESOURCE_DESC rtDesc = {};
  rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rtDesc.Alignment = 0;
  rtDesc.Width = width;
  rtDesc.Height = height;
  rtDesc.DepthOrArraySize = 1;
  rtDesc.MipLevels = 1;
  rtDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  rtDesc.SampleDesc.Count = 1;
  rtDesc.SampleDesc.Quality = 0;
  rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  clearValue.Color[0] = 0.0f;
  clearValue.Color[1] = 0.0f;
  clearValue.Color[2] = 0.0f;
  clearValue.Color[3] = 1.0f;

  if (FAILED(g_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
          D3D12_RESOURCE_STATE_PRESENT, &clearValue,
          IID_PPV_ARGS(&g_exportRenderTarget)))) {
    g_exportRtvHeap.Reset();
    return false;
  }

  if (!g_exportPreviewSrvAllocated) {
    DescriptorAllocation srvAlloc = g_cbvSrvAllocator.AllocatePersistent(1);
    g_exportPreviewSrvCpu = srvAlloc.cpu;
    g_exportPreviewSrvGpu = srvAlloc.gpu;
    g_exportPreviewSrvAllocated = true;
  }
  if (g_exportPreviewSrvAllocated) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    g_device->CreateShaderResourceView(g_exportRenderTarget.Get(), &srvDesc,
                                       g_exportPreviewSrvCpu);
  }

  g_device->CreateRenderTargetView(
      g_exportRenderTarget.Get(), nullptr,
      g_exportRtvHeap->GetCPUDescriptorHandleForHeapStart());
  g_exportRenderTargetWidth = width;
  g_exportRenderTargetHeight = height;
  g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
  return true;
}

  static void StartRenderExportJobWithSettings(
    const std::wstring &outputPath,
    const RenderExportLaunchSettings &settings,
    bool isPreview) {
    if (!g_renderExportJob.active) {
      ReleasePreviewOverlayImage();
    }
    if (g_renderExportJob.active || (outputPath.empty() && !isPreview) || !g_rayTracingSupported) {
    return;
    }

    int presetIndex = settings.resolutionPreset;
    if (presetIndex < 0 || presetIndex >= g_renderResolutionPresetCount) {
    presetIndex = 0;
    }

    g_renderExportJob.active = true;
    g_renderExportJob.isPreview = isPreview;
    g_renderExportJob.previewReadyToLatch = false;
    g_renderExportJob.previewRestorePending = false;
    g_renderExportJob.completionAdvancePending = false;
    g_renderExportJob.completionExportSucceeded = false;
    g_renderExportJob.outputPath = outputPath;
    g_renderExportJob.targetWidth =
      settings.explicitWidth > 0 ? settings.explicitWidth
                                 : g_renderResolutionPresets[presetIndex].width;
    g_renderExportJob.targetHeight =
      settings.explicitHeight > 0 ? settings.explicitHeight
                                  : g_renderResolutionPresets[presetIndex].height;
    const int targetProjectionMode =
        (std::clamp)(settings.projectionMode,
                     (int)CameraProjectionMode::Perspective,
                     (int)CameraProjectionMode::Spherical360);
    if (targetProjectionMode == (int)CameraProjectionMode::Spherical360) {
      g_renderExportJob.targetHeight =
          (std::max)(1u, g_renderExportJob.targetWidth / 2u);
    }
    g_renderExportJob.targetMaxSpp = (settings.maxSpp < 1) ? 1 : settings.maxSpp;
    g_renderExportJob.targetNoiseThreshold =
      (settings.noisePercent <= 0.0f) ? 0.001f : (settings.noisePercent / 100.0f);
    g_renderExportJob.targetDenoiserIndex =
      (std::clamp)(settings.denoiserIndex, 0, 3);
    g_renderExportJob.allowNoiseThresholdStop = settings.allowNoiseThresholdStop;
    g_renderExportJob.minSppBeforeNoiseStop =
      (g_renderExportJob.targetMaxSpp < 32)
        ? (UINT)g_renderExportJob.targetMaxSpp
        : 32u;
    if (g_renderExportJob.minSppBeforeNoiseStop < 8u) {
      g_renderExportJob.minSppBeforeNoiseStop = 8u;
    }

    g_renderExportJob.tileState = {}; // Clear any previous tile state
    if (!isPreview &&
        settings.tileRenderingEnabled &&
        ShouldUseTiledExport(g_renderExportJob)) {
      SetupTiledExportJob(g_renderExportJob);
    }

    g_renderExportJob.completionArmed = false;
    g_renderExportJob.completionFrames = 0;
    g_renderExportJob.settleFramesRemaining = 0;
    g_renderExportJob.startedTickMs = GetTickCount64();
    g_renderExportJob.previousMode = g_currentRenderMode;
    g_renderExportJob.previousMaxSpp = g_cameraData.maxSPP;
    g_renderExportJob.previousNoiseThreshold = g_cameraData.noiseThreshold;
    g_renderExportJob.previousAdaptiveSampling = g_cameraData.useAdaptiveSampling;
    g_renderExportJob.previousDenoiserIndex =
      DenoiserIndexFromMode(DxrRenderer::GetDenoiserMode());
    g_renderExportJob.previousProjectionMode = g_cameraData.projectionMode;

    g_renderExportJob.previousStreamlineEnabled =
      DX12Context::g_streamline.IsEnabled();
    g_renderExportJob.previousStreamlineMode =
      (int)DX12Context::g_streamline.GetMode();
    g_renderExportJob.previousStreamlineQuality =
      (int)DX12Context::g_streamline.GetQuality();
    if (g_renderExportJob.previousStreamlineEnabled) {
    DX12Context::g_streamline.SetEnabled(false);
    }

    g_currentRenderMode = RenderMode::DXR;
    g_cameraData.maxSPP = (float)g_renderExportJob.targetMaxSpp;
    g_cameraData.noiseThreshold = g_renderExportJob.targetNoiseThreshold;
    g_cameraData.useAdaptiveSampling =
      settings.allowNoiseThresholdStop ? 1.0f : 0.0f;
    g_cameraData.exportRendering = 1.0f;
    g_cameraData.projectionMode = (float)targetProjectionMode;
    const int activeDenoiserIndex =
        g_renderExportJob.tileState.enabled
            ? 0
            : g_renderExportJob.targetDenoiserIndex;
    DxrRenderer::SetDenoiserMode(DenoiserModeFromIndex(activeDenoiserIndex));

    if (!EnsureExportRenderTarget(g_renderExportJob.targetWidth,
                  g_renderExportJob.targetHeight)) {
    g_renderExportStatus = "Failed to allocate export render target.";
    g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
    g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
    g_cameraData.useAdaptiveSampling =
      g_renderExportJob.previousAdaptiveSampling;
    g_cameraData.exportRendering = 0.0f;
    g_cameraData.projectionMode = g_renderExportJob.previousProjectionMode;
    DxrRenderer::SetDenoiserMode(
      DenoiserModeFromIndex(g_renderExportJob.previousDenoiserIndex));
    g_currentRenderMode = g_renderExportJob.previousMode;
    DX12Context::g_streamline.SetMode(
      (StreamlineManager::Mode)g_renderExportJob.previousStreamlineMode);
    DX12Context::g_streamline.SetQuality(
      (StreamlineManager::Quality)
        g_renderExportJob.previousStreamlineQuality);
    DX12Context::g_streamline.SetEnabled(
      g_renderExportJob.previousStreamlineEnabled);
    g_renderExportJob.active = false;
    g_renderExportJob.isPreview = false;
    g_renderExportJob.previewReadyToLatch = false;
    g_renderExportJob.previewRestorePending = false;
    g_renderExportJob.completionAdvancePending = false;
    g_renderExportJob.completionExportSucceeded = false;
    UpdateCameraCB();
    return;
    }

    if (!RecreateDxrPipelineSafe(g_renderExportJob.targetWidth,
                   g_renderExportJob.targetHeight,
                   "StartRenderExportJobWithSettings")) {
    g_renderExportStatus = "Failed to create DXR pipeline for export.";
    g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
    g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
    g_cameraData.useAdaptiveSampling =
      g_renderExportJob.previousAdaptiveSampling;
    g_cameraData.exportRendering = 0.0f;
    g_cameraData.projectionMode = g_renderExportJob.previousProjectionMode;
    DxrRenderer::SetDenoiserMode(
      DenoiserModeFromIndex(g_renderExportJob.previousDenoiserIndex));
    g_currentRenderMode = g_renderExportJob.previousMode;
    DX12Context::g_streamline.SetMode(
      (StreamlineManager::Mode)g_renderExportJob.previousStreamlineMode);
    DX12Context::g_streamline.SetQuality(
      (StreamlineManager::Quality)
        g_renderExportJob.previousStreamlineQuality);
    DX12Context::g_streamline.SetEnabled(
      g_renderExportJob.previousStreamlineEnabled);
    g_renderExportJob.active = false;
    g_renderExportJob.isPreview = false;
    g_renderExportJob.previewReadyToLatch = false;
    g_renderExportJob.previewRestorePending = false;
    g_renderExportJob.completionAdvancePending = false;
    g_renderExportJob.completionExportSucceeded = false;
    UpdateCameraCB();
    return;
    }

    DxrRenderer::ResetAccumulation();
    // Set tile constants for tiled panorama export (no-op if not tiled)
    if (g_renderExportJob.tileState.enabled) {
      const auto &t = g_renderExportJob.tileState;
      DxrRenderer::SetExportTileConstants(t.fullWidth, t.fullHeight,
                                          t.tileOffsetX, t.tileOffsetY);
    }
    UpdateCameraCB();
    g_renderExportStatus = isPreview ? "Rendering preview..." : "Rendering...";
  }

void RestoreRenderExportState(bool preservePreviewImage) {
  if (!g_renderExportJob.active) {
    return;
  }

  g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
  g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
  g_cameraData.useAdaptiveSampling = g_renderExportJob.previousAdaptiveSampling;
  g_cameraData.exportRendering = 0.0f;
  g_cameraData.projectionMode = g_renderExportJob.previousProjectionMode;
  DxrRenderer::SetDenoiserMode(
      DenoiserModeFromIndex(g_renderExportJob.previousDenoiserIndex));
  g_currentRenderMode = g_renderExportJob.previousMode;

  // Restore Streamline/DLSS state that was disabled for export so we
  // recompute noise statistics correctly.
  DX12Context::g_streamline.SetMode(
      (StreamlineManager::Mode)g_renderExportJob.previousStreamlineMode);
  DX12Context::g_streamline.SetQuality(
      (StreamlineManager::Quality)g_renderExportJob.previousStreamlineQuality);
  DX12Context::g_streamline.SetEnabled(
      g_renderExportJob.previousStreamlineEnabled);

  DxrRenderer::ResetStreamlineHistory();
  UINT restoreWidth = 1;
  UINT restoreHeight = 1;
  GetCurrentDxrPreviewSize(restoreWidth, restoreHeight);
  if (!RecreateDxrPipelineSafe(restoreWidth, restoreHeight,
                               "RestoreRenderExportState")) {
    g_currentRenderMode = RenderMode::Raster;
  }
  DxrRenderer::ResetAccumulation();
  // Clear tiled export state and disable tile constants
  g_renderExportJob.tileState = {};
  DxrRenderer::SetExportTileConstants(0, 0, 0, 0);
  g_renderExportJob.active = false;
  g_renderExportJob.isPreview = false;
  g_renderExportJob.previewReadyToLatch = false;
  g_renderExportJob.previewRestorePending = false;
  g_renderExportJob.completionAdvancePending = false;
  g_renderExportJob.completionExportSucceeded = false;
  g_renderExportJob.completionArmed = false;
  g_renderExportJob.completionFrames = 0;
  g_renderExportJob.settleFramesRemaining = 0;
  g_renderExportJob.startedTickMs = 0;
  g_renderExportJob.targetDenoiserIndex = 0;
  g_renderExportJob.allowNoiseThresholdStop = true;
  if (!preservePreviewImage) {
    ReleasePreviewOverlayImage();
  }
  UpdateCameraCB();
}

bool StartBatchRenderExportJobs(const std::wstring &outputDirectory,
                                const std::wstring &baseName) {
  if (g_renderExportJob.active || g_renderBatchExport.active ||
      outputDirectory.empty() || baseName.empty() || !g_rayTracingSupported) {
    return false;
  }

  const auto &views = SavedViews::GetViews();
  if (views.empty()) {
    g_renderExportStatus = "No saved views available for batch export.";
    return false;
  }

  g_renderBatchExport = {};
  g_renderBatchExport.active = true;
  g_renderBatchExport.outputDirectory = outputDirectory;
  g_renderBatchExport.baseName = baseName;
  g_renderBatchExport.previousCamera = SavedViews::CaptureCurrentState();
  g_renderBatchExport.previousCameraCaptured = true;
  g_renderBatchExport.startedTickMs = GetTickCount64();
  g_renderBatchExport.viewIndices.resize(views.size());
  for (size_t index = 0; index < views.size(); ++index) {
    g_renderBatchExport.viewIndices[index] = index;
  }

  if (!StartNextBatchRenderJob()) {
    if (g_renderBatchExport.previousCameraCaptured) {
      SavedViews::ApplyView(g_renderBatchExport.previousCamera);
    }
    g_renderBatchExport.active = false;
    g_renderExportStatus = "Failed to start batch render export.";
    return false;
  }

  return true;
}

void AdvanceBatchRenderExport(bool previousExportSucceeded) {
  if (!g_renderBatchExport.active) {
    return;
  }

  if (!previousExportSucceeded) {
    g_renderBatchExport.failed = true;
  }

  if (g_renderBatchExport.failed ||
      !StartNextBatchRenderJob()) {
    const size_t totalCount = g_renderBatchExport.viewIndices.size();
    const bool batchFailed = g_renderBatchExport.failed;
    if (g_renderBatchExport.previousCameraCaptured) {
      SavedViews::ApplyView(g_renderBatchExport.previousCamera);
    }
    g_renderBatchExport = {};
    if (batchFailed) {
      g_renderExportStatus = "Batch render failed.";
    } else {
      g_renderExportStatus = "Batch render finished: " +
                             std::to_string(totalCount) + " images.";
    }
  }
}

void CancelBatchRenderExport() {
  if (!g_renderBatchExport.active) {
    return;
  }

  if (g_renderExportJob.active) {
    RestoreRenderExportState();
  }
  if (g_renderBatchExport.previousCameraCaptured) {
    SavedViews::ApplyView(g_renderBatchExport.previousCamera);
  }
  g_renderBatchExport = {};
  g_renderExportStatus = "Batch render canceled.";
}

static void DrawReGIRDebugPanel() {
  ImGui::SetNextWindowSize(ImVec2(430, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("regir debug", &g_showReGIRDebugWindow,
                    ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  DxrRenderer::ReGIRSettings settings = DxrRenderer::GetReGIRSettings();
  bool settingsChanged = false;
  bool rendererSettingsChanged = false;
  bool enabled = settings.enabled;
  if (ImGui::Checkbox("Enable ReGIR", &enabled)) {
    settings.enabled = enabled;
    settingsChanged = true;
    rendererSettingsChanged = true;
  }

  int gridRes[3] = {static_cast<int>(settings.gridRes[0]),
                    static_cast<int>(settings.gridRes[1]),
                    static_cast<int>(settings.gridRes[2])};
  if (ImGui::DragInt3("Grid cells", gridRes, 1.0f, 1, 64)) {
    settings.gridRes[0] = static_cast<uint32_t>((std::max)(1, gridRes[0]));
    settings.gridRes[1] = static_cast<uint32_t>((std::max)(1, gridRes[1]));
    settings.gridRes[2] = static_cast<uint32_t>((std::max)(1, gridRes[2]));
    settingsChanged = true;
    rendererSettingsChanged = true;
  }

  int candidates = static_cast<int>(settings.candidatesPerCell);
  if (ImGui::SliderInt("Candidates / cell", &candidates, 1, 8)) {
    settings.candidatesPerCell = static_cast<uint32_t>(candidates);
    settingsChanged = true;
    rendererSettingsChanged = true;
  }

  float jitterScale = settings.cellJitterScale;
  if (ImGui::SliderFloat("Cell jitter", &jitterScale, 0.0f, 2.0f, "%.2f")) {
    settings.cellJitterScale = jitterScale;
    settingsChanged = true;
    rendererSettingsChanged = true;
  }

  float reachScale = settings.lightReachScale;
  if (ImGui::SliderFloat("Light reach scale", &reachScale, 0.1f, 8.0f,
                         "%.2f")) {
    settings.lightReachScale = reachScale;
    settingsChanged = true;
    rendererSettingsChanged = true;
  }

  bool readbackEnabled = settings.debugReadbackEnabled;
  if (ImGui::Checkbox("Cell readback", &readbackEnabled)) {
    settings.debugReadbackEnabled = readbackEnabled;
    settingsChanged = true;
  }
  int readbackInterval = static_cast<int>(settings.debugReadbackInterval);
  if (ImGui::SliderInt("Readback interval", &readbackInterval, 1, 120)) {
    settings.debugReadbackInterval =
        static_cast<uint32_t>((std::max)(1, readbackInterval));
    settingsChanged = true;
  }

  if (settingsChanged) {
    DxrRenderer::SetReGIRSettings(settings);
    if (rendererSettingsChanged) {
      DxrRenderer::ResetAccumulation();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Rebuild Grid")) {
    DxrRenderer::MarkReGIRDirty();
    DxrRenderer::ResetAccumulation();
  }

  DxrRenderer::ReGIRStats stats = DxrRenderer::GetReGIRStats();
  ImGui::Separator();
  const bool ready = stats.fallbackReason == 0u;
  ImGui::Text("State: ");
  ImGui::SameLine();
  ImGui::TextColored(ready ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                           : ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                     "%s",
                     DxrRenderer::GetReGIRFallbackReasonName(
                         stats.fallbackReason));
  ImGui::Text("Updates: %u  Frame: %u  Dispatch groups: %u",
              stats.updateCount, stats.lastUpdateFrameIndex,
              stats.lastDispatchGroups);
  ImGui::Text("Cells: %u (%u x %u x %u)", stats.totalCells, stats.gridRes[0],
              stats.gridRes[1], stats.gridRes[2]);
  ImGui::Text("Cell size: %.3f, %.3f, %.3f", stats.cellSize[0],
              stats.cellSize[1], stats.cellSize[2]);
  ImGui::Text("Grid min: %.2f, %.2f, %.2f", stats.gridMin[0],
              stats.gridMin[1], stats.gridMin[2]);
  ImGui::Text("Grid max: %.2f, %.2f, %.2f", stats.gridMax[0],
              stats.gridMax[1], stats.gridMax[2]);
  ImGui::Text("Cell buffer: %.2f MiB active / %.2f MiB allocated",
              BytesToMiB(stats.activeCellBufferBytes),
              BytesToMiB(stats.cellBufferBytes));

  ImGui::Separator();
  const auto &prototypes = Scene::GetLightPrototypes();
  const auto &instances = Scene::GetLightInstances();
  const auto &iesProfiles = Scene::GetIESProfiles();
  uint32_t typeCounts[6] = {};
  uint32_t enabledInstances = 0;
  for (const LightInstance &inst : instances) {
    if (inst.prototypeIndex >= prototypes.size()) {
      continue;
    }
    const LightPrototype &proto = prototypes[inst.prototypeIndex];
    if (proto.type < 6u) {
      ++typeCounts[proto.type];
    }
    if (proto.enabled && inst.enabled) {
      ++enabledInstances;
    }
  }
  ImGui::Text("Scene lights: %zu prototypes, %zu instances, %u enabled",
              prototypes.size(), instances.size(), enabledInstances);
  ImGui::Text("GPU lights: %u  ReGIR bounds: %u  emissive proxies: %u",
              stats.lightCount, stats.lightBoundCount,
              stats.emissiveProxyCount);
  ImGui::Text("Skipped directional / zero power: %u / %u",
              stats.skippedDirectionalLights, stats.skippedZeroPowerLights);
  ImGui::Text("Types P/S/Rect/Disk/IES: %u / %u / %u / %u / %u",
              typeCounts[static_cast<uint32_t>(LightType::Omni)],
              typeCounts[static_cast<uint32_t>(LightType::Spot)],
              typeCounts[static_cast<uint32_t>(LightType::AreaRect)],
              typeCounts[static_cast<uint32_t>(LightType::AreaDisk)],
              typeCounts[static_cast<uint32_t>(LightType::IES)]);
  ImGui::Text("Queue overflow continuation / shadow / bins: %u / %u / %u",
              DxrRenderer::GetWavefrontContinuationOverflowCount(),
              DxrRenderer::GetWavefrontShadowOverflowCount(),
              DxrRenderer::GetWavefrontMaterialBinOverflowCount());

  // Sampling-side diagnostics. The other stats above show that the UPDATE
  // shader is building cells, but they don't prove that shading actually
  // pulls from those cells. These counters tally what happened at the
  // sampling site over the last GPU frame and answer "is ReGIR really
  // affecting the render?" directly.
  ImGui::Separator();
  const uint32_t hits = stats.sampleHits;
  const uint32_t oob = stats.sampleOutOfBounds;
  const uint32_t nocand = stats.sampleNoCandidate;
  const uint32_t clamped = stats.sampleClamped;
  const uint64_t totalAttempts =
      static_cast<uint64_t>(hits) + oob + nocand;
  ImGui::Text("Counter readback: %s",
              stats.sampleCounterReadbackEnabled ? "active" : "unavailable");
  ImGui::Text("Sampler create / ReGIR mode: %u / %u",
              stats.samplerCreateCalls, stats.samplerReGIRMode);
  ImGui::Text("Flat no feature / no cells / compiled out: %u / %u / %u",
              stats.samplerFlatNoFeature, stats.samplerFlatNoCells,
              stats.samplerCompiledOut);
  ImGui::Text("Shader max cells / lights: %u / %u",
              stats.samplerMaxTotalCells, stats.samplerMaxLights);
  ImGui::Text("Sample attempts: %llu",
              static_cast<unsigned long long>(totalAttempts));
  if (!stats.sampleCounterReadbackEnabled) {
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                       "  sampler verdict unavailable in this frame.");
  } else if (totalAttempts > 0) {
    const float hitPct = 100.0f * static_cast<float>(hits) /
                         static_cast<float>(totalAttempts);
    const float oobPct = 100.0f * static_cast<float>(oob) /
                         static_cast<float>(totalAttempts);
    const float nocandPct = 100.0f * static_cast<float>(nocand) /
                            static_cast<float>(totalAttempts);
    ImGui::Text("  hit: %u (%.1f%%)", hits, hitPct);
    ImGui::Text("  out-of-bounds: %u (%.1f%%)", oob, oobPct);
    ImGui::Text("  no candidate: %u (%.1f%%)", nocand, nocandPct);
    const float clampPct = hits > 0u
                               ? 100.0f * static_cast<float>(clamped) /
                                     static_cast<float>(hits)
                               : 0.0f;
    ImGui::Text("  clamped to domain cap: %u (%.1f%% of hits)", clamped,
                clampPct);
  } else {
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                       "  ReGIR sampler was NOT called this frame.");
  }
  // dxrFeatureFlags bit 8 (DXR_FEATURE_REGIR_ENABLED) is the runtime gate the
  // shader checks. If this is 0 while State says Ready, the integration is
  // misconfigured upstream.
  const uint32_t kRegirBit = 1u << 8;
  const bool maskHasRegir = (stats.currentFeatureMask & kRegirBit) != 0u;
  ImGui::Text("dxrFeatureFlags: 0x%08x", stats.currentFeatureMask);
  ImGui::SameLine();
  ImGui::TextColored(maskHasRegir ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                  : ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                     "%s", maskHasRegir ? "(REGIR bit set)" : "(REGIR bit MISSING)");

  ImGui::Separator();
  const int selectedInstance = Scene::GetSelectedLightIndex();
  const UINT selectedFlat = FlattenedLightIndexForInstance(selectedInstance);
  DxrRenderer::SetReGIRDebugSelectedLight(selectedFlat);
  if (selectedInstance >= 0 &&
      selectedInstance < static_cast<int>(instances.size())) {
    const LightInstance &inst = instances[static_cast<size_t>(selectedInstance)];
    const bool hasPrototype = inst.prototypeIndex < prototypes.size();
    ImGui::Text("Selected instance: %d", selectedInstance);
    if (hasPrototype) {
      const LightPrototype &proto = prototypes[inst.prototypeIndex];
      ImGui::Text("Selected prototype: %zu (%s)", inst.prototypeIndex,
                  DebugLightTypeName(proto.type));
      if (selectedFlat == 0xFFFFFFFFu) {
        ImGui::TextUnformatted("Flattened light index: disabled/not emitted");
      } else {
        ImGui::Text("Flattened light index: %u", selectedFlat);
      }
      if (proto.iesProfileIndex >= 0 &&
          proto.iesProfileIndex < static_cast<int>(iesProfiles.size())) {
        const auto &profile =
            iesProfiles[static_cast<size_t>(proto.iesProfileIndex)];
        ImGui::Text("IES: %s  slice=%d  ready=%s",
                    profile.displayName.c_str(), proto.iesAtlasIndex,
                    profile.gpuReady ? "yes" : "no");
      } else {
        ImGui::TextUnformatted("IES: none");
      }
    }
  } else {
    ImGui::TextUnformatted("Selected light: none");
  }

  ImGui::Separator();
  if (!stats.readbackEnabled) {
    ImGui::TextUnformatted("Cell occupancy: readback disabled");
  } else if (!stats.readbackValid) {
    ImGui::TextUnformatted("Cell occupancy: waiting for GPU readback");
  } else {
    ImGui::Text("Readback frame: %u", stats.readbackFrameIndex);
    ImGui::Text("Occupied cells: %u / %u (%.1f%%)", stats.occupiedCells,
                stats.totalCells, stats.occupancyPercent);
    ImGui::Text("Valid slots: %u (%.1f%% fill, %.2f / occupied cell)",
                stats.validSlots, stats.slotFillPercent,
                stats.avgSlotsPerOccupiedCell);
    ImGui::Text("Selected light slots: %u", stats.selectedLightSlotCount);
    ImGui::Text("Candidate weight min / avg / max: %.4g / %.4g / %.4g",
                stats.minCandidateWeight, stats.avgCandidateWeight,
                stats.maxCandidateWeight);
    ImGui::Text("Reservoir W min / avg / max: %.4g / %.4g / %.4g",
                stats.minReservoirWeightSum, stats.avgReservoirWeightSum,
                stats.maxReservoirWeightSum);
    ImGui::Text("Inverse PDF min / avg / max: %.4g / %.4g / %.4g",
                stats.minInversePdf, stats.avgInversePdf,
                stats.maxInversePdf);
    ImGui::Text("M avg / max: %.2f / %u", stats.avgCandidateCountM,
                stats.maxCandidateCountM);
  }

  if (ImGui::CollapsingHeader("IES atlas status")) {
    ImGui::Text("Profiles: %zu", iesProfiles.size());
    for (size_t i = 0; i < iesProfiles.size(); ++i) {
      const auto &profile = iesProfiles[i];
      ImGui::Text("%zu: slice=%d ready=%s %s", i, profile.atlasSlice,
                  profile.gpuReady ? "yes" : "no",
                  profile.displayName.c_str());
    }
  }

  ImGui::End();
}

bool StartAnimationRenderExport(const std::wstring &outputDirectory) {
  if (g_renderExportJob.active || g_renderBatchExport.active ||
      g_renderAnimationExport.active || outputDirectory.empty() ||
      !g_rayTracingSupported) {
    return false;
  }

  const auto &settings = AnimationSequence::GetExportSettings();
  const int totalFrames = AnimationSequence::GetTotalFrameCount(settings.fps);
  if (totalFrames <= 0) {
    g_renderExportStatus = "Animation has no keyframes.";
    return false;
  }

  g_renderAnimationExport = {};
  g_renderAnimationExport.active = true;
  g_renderAnimationExport.outputDirectory = outputDirectory;
  g_renderAnimationExport.totalFrames = totalFrames;
  g_renderAnimationExport.fps = settings.fps;
  g_renderAnimationExport.resolutionPreset = settings.resolutionPreset;
  g_renderAnimationExport.maxSpp = settings.maxSpp;
  g_renderAnimationExport.exportMode = settings.exportMode;
  g_renderAnimationExport.frameDigits = GetAnimationFrameDigits(totalFrames);
  g_renderAnimationExport.startedTickMs = GetTickCount64();
  g_renderAnimationExport.encodingStartedTickMs = 0;
  g_renderAnimationExport.previousCamera = SavedViews::CaptureCurrentState();
  g_renderAnimationExport.previousCameraCaptured = true;
  g_animationEncodeMessage.clear();

  if (!std::filesystem::create_directories(outputDirectory) &&
      !std::filesystem::exists(outputDirectory)) {
    g_renderAnimationExport = {};
    g_renderExportStatus = "Failed to create animation export folder.";
    return false;
  }

  if (settings.exportMode == static_cast<int>(AnimationSequence::ExportMode::Mp4)) {
    g_renderAnimationExport.ffmpegExecutable = FindFfmpegExecutable();
    if (g_renderAnimationExport.ffmpegExecutable.empty()) {
      g_renderAnimationExport = {};
      g_renderExportStatus = "MP4 export requires ffmpeg.exe in PATH or next to the app.";
      return false;
    }
    g_renderAnimationExport.finalOutputPath =
        BuildAnimationVideoOutputPath(outputDirectory, settings.baseName);
    g_renderAnimationExport.temporaryFrameDirectory =
        BuildAnimationTempDirectory(outputDirectory, settings.baseName);
    g_renderAnimationExport.frameOutputDirectory =
        g_renderAnimationExport.temporaryFrameDirectory;
    if (!std::filesystem::create_directories(g_renderAnimationExport.temporaryFrameDirectory) &&
        !std::filesystem::exists(g_renderAnimationExport.temporaryFrameDirectory)) {
      g_renderAnimationExport = {};
      g_renderExportStatus = "Failed to create temporary frame folder for MP4 export.";
      return false;
    }
  } else {
    g_renderAnimationExport.frameOutputDirectory = outputDirectory;
  }

  if (!StartNextAnimationRenderJob()) {
    if (g_renderAnimationExport.previousCameraCaptured) {
      SavedViews::ApplyView(g_renderAnimationExport.previousCamera);
    }
    g_renderAnimationExport = {};
    g_renderExportStatus = "Failed to start animation export.";
    return false;
  }
  return true;
}

void AdvanceAnimationRenderExport(bool previousExportSucceeded) {
  if (!g_renderAnimationExport.active) {
    return;
  }
  if (g_renderAnimationExport.encoding) {
    return;
  }

  if (!previousExportSucceeded) {
    g_renderAnimationExport.failed = true;
  }

  if (g_renderAnimationExport.failed || !StartNextAnimationRenderJob()) {
    const bool failed = g_renderAnimationExport.failed;
    const int totalFrames = g_renderAnimationExport.totalFrames;
    const bool exportMp4 = g_renderAnimationExport.exportMode ==
                           static_cast<int>(AnimationSequence::ExportMode::Mp4);
    const std::wstring tempFrameDirectory = g_renderAnimationExport.temporaryFrameDirectory;
    if (g_renderAnimationExport.previousCameraCaptured) {
      SavedViews::ApplyView(g_renderAnimationExport.previousCamera);
    }
    if (failed) {
      g_renderAnimationExport = {};
      g_renderExportStatus = exportMp4 && !tempFrameDirectory.empty()
                                 ? "Animation export failed. Frames kept in: " +
                                       WStringToUtf8(tempFrameDirectory)
                                 : "Animation export failed.";
    } else if (exportMp4) {
      if (!StartAnimationMp4Encode()) {
        g_renderExportStatus = "Failed to start MP4 encoding.";
        g_renderAnimationExport = {};
      }
    } else {
      g_renderAnimationExport = {};
      g_renderExportStatus = "Animation export finished: " +
                             std::to_string(totalFrames) + " frames.";
    }
  }
}

void CancelAnimationRenderExport() {
  if (!g_renderAnimationExport.active) {
    return;
  }
  const bool exportMp4 = g_renderAnimationExport.exportMode ==
                         static_cast<int>(AnimationSequence::ExportMode::Mp4);
  const int completedFrames = (std::max)(0, g_renderAnimationExport.currentFrameIndex - 1);
  const std::wstring outputDirectory = g_renderAnimationExport.outputDirectory;
  const std::wstring tempFrameDirectory = g_renderAnimationExport.temporaryFrameDirectory;
  if (g_renderAnimationExport.encoding) {
    g_renderExportStatus = "MP4 encoding is already running and will finish with rendered frames.";
    return;
  }

  if (g_renderExportJob.active) {
    RestoreRenderExportState();
  }
  if (g_renderAnimationExport.previousCameraCaptured) {
    SavedViews::ApplyView(g_renderAnimationExport.previousCamera);
  }

  if (exportMp4 && completedFrames > 0) {
    g_renderAnimationExport.totalFrames = completedFrames;
    g_renderAnimationExport.currentFrameIndex = completedFrames;
    g_renderExportStatus = "Cancel requested. Encoding MP4 from " +
                           std::to_string(completedFrames) +
                           " rendered frames.";
    if (!StartAnimationMp4Encode()) {
      g_renderAnimationExport = {};
      g_renderExportStatus = tempFrameDirectory.empty()
                                 ? "Animation export canceled. Failed to start MP4 encoding."
                                 : "Animation export canceled. Failed to start MP4 encoding. PNG frames kept in: " +
                                       WStringToUtf8(tempFrameDirectory);
    }
    return;
  }

  g_renderAnimationExport = {};
  if (exportMp4 && completedFrames <= 0) {
    g_renderExportStatus = "Animation export canceled before any frames were rendered.";
  } else if (!exportMp4 && completedFrames > 0) {
    g_renderExportStatus = "Animation export canceled. Kept " +
                           std::to_string(completedFrames) +
                           " rendered frames in: " +
                           WStringToUtf8(outputDirectory);
  } else {
    g_renderExportStatus = "Animation export canceled.";
  }
}

void StartPreviewRenderJob() {
  if (g_renderExportJob.active) return;
  RenderExportLaunchSettings settings = {};
  settings.resolutionPreset = g_renderExportSettings.resolutionPreset;
  settings.maxSpp = g_renderExportSettings.maxSpp;
  settings.noisePercent = g_renderExportSettings.noisePercent;
  settings.denoiserIndex = g_renderExportSettings.denoiserIndex;
  settings.projectionMode = g_renderExportSettings.projectionMode;
  settings.allowNoiseThresholdStop = true;
  if (g_renderExportSettings.useCustomResolution) {
    settings.explicitWidth = (unsigned int)g_renderExportSettings.customWidth;
    settings.explicitHeight = (unsigned int)g_renderExportSettings.customHeight;
  }
  settings.tileRenderingEnabled = g_renderExportSettings.tileRenderingEnabled;
  StartRenderExportJobWithSettings(L"", settings, true);
  fprintf(stderr,
          "Preview render started: %ux%u, maxSPP=%d, noise=%.3f, "
          "denoiser=%d\n",
          g_renderExportJob.targetWidth, g_renderExportJob.targetHeight,
          g_renderExportJob.targetMaxSpp,
          g_renderExportJob.targetNoiseThreshold,
          g_renderExportSettings.denoiserIndex);
}

void StartRenderExportJob(const std::wstring &outputPath) {
  RenderExportLaunchSettings settings = {};
  settings.resolutionPreset = g_renderExportSettings.resolutionPreset;
  settings.maxSpp = g_renderExportSettings.maxSpp;
  settings.noisePercent = g_renderExportSettings.noisePercent;
  settings.denoiserIndex = g_renderExportSettings.denoiserIndex;
  settings.projectionMode = g_renderExportSettings.projectionMode;
  settings.allowNoiseThresholdStop = true;
  if (g_renderExportSettings.useCustomResolution) {
    settings.explicitWidth = (unsigned int)g_renderExportSettings.customWidth;
    settings.explicitHeight = (unsigned int)g_renderExportSettings.customHeight;
  }
  settings.tileRenderingEnabled = g_renderExportSettings.tileRenderingEnabled;
  StartRenderExportJobWithSettings(outputPath, settings, false);
  fprintf(stderr,
          "Render export started: %ux%u, maxSPP=%d, noise=%.3f, "
          "denoiser=%d\n",
          g_renderExportJob.targetWidth, g_renderExportJob.targetHeight,
          g_renderExportJob.targetMaxSpp,
          g_renderExportJob.targetNoiseThreshold,
          g_renderExportSettings.denoiserIndex);
}

bool IsPreviewRenderActive() {
    return (g_renderExportJob.active && g_renderExportJob.isPreview) ||
           g_previewOverlay.visible;
}

bool HasPreviewRenderImage() {
    return g_previewOverlay.visible && g_exportRenderTarget &&
           g_exportPreviewSrvGpu.ptr != 0;
}

bool PreviewRenderNeedsPresent() {
  return HasPreviewRenderImage() && g_previewOverlay.needsPresent;
}

void MarkPreviewRenderPresented() {
  if (g_previewOverlay.visible) {
    g_previewOverlay.needsPresent = false;
  }
}

void CancelPreviewRender() {
  if (g_renderExportJob.active && g_renderExportJob.isPreview) {
    g_renderExportStatus = "Preview canceled.";
    RestoreRenderExportState();
    return;
  }
  if (g_previewOverlay.visible) {
    ReleasePreviewOverlayImage();
    g_renderExportStatus.clear();
  }
}

std::string WStringToUtf8(const std::wstring &ws) {
  if (ws.empty())
    return {};
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                                        NULL, 0, NULL, NULL);
  std::string s(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0],
                      size_needed, NULL, NULL);
  return s;
}

static bool RecreateDxrPipelineSafe(UINT width, UINT height,
                                    const char *context) {
  if (!g_rayTracingSupported) {
    fprintf(stderr,
            "DXR pipeline recreate skipped (%s): ray tracing not supported.\n",
            context ? context : "unknown");
    return false;
  }

  try {
    DxrRenderer::WaitForAsyncRestirIdle();
    WaitGPUIdle();
    DxrRenderer::CreateRayTracingPipeline(width, height);
    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "DXR pipeline recreate failed (%s): %s\n",
            context ? context : "unknown", e.what());
  } catch (...) {
    fprintf(stderr, "DXR pipeline recreate failed (%s): unknown exception\n",
            context ? context : "unknown");
  }
  return false;
}

// ── Main UI draw function ───────────────────────────────────────────────────

void DrawEditorUI(float fps, float &timeOfDay, float &northOffset,
                  float &latitudeDeg, float &dayOfYear) {
  UpdateSceneIoJob();
  UpdateAnimationEncodingJob();
  UpdatePreviewOverlayState();
  if (!Input::g_imguiEnabled) {
    // Keep a minimal ImGui frame alive so viewport gizmos continue to work
    // even when debug windows are hidden with F5.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (!IsSceneIoJobActive()) {
      ImGuizmo::BeginFrame();
      Scene::DrawGizmo();
      Scene::DrawLightGizmo();
    }
    ImGui::Render();
    return;
  }

  // Start ImGui frame
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();

  // Fullscreen DockSpace root so panels can be docked and rearranged.
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    window_flags |= ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    // Make host window background transparent so the engine's DX12 render
    // output (the swapchain backbuffer) remains visible beneath the dock
    // central node.
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("DockSpaceRoot", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
  }

  DrawSceneIoOverlay();

  if (IsSceneIoJobActive()) {
    // Scene I/O owns the scene state. Keep only the overlay alive until the
    // worker has finished.
    ImGui::Render();
    return;
  }

  // Main menu bar: Window menu + quick panel toggles on the bar for fast
  // access
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (g_sceneIoJob.active) {
        ImGui::BeginDisabled();
      }
      const bool hasCurrentScene = HasCurrentScenePath();
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
        if (hasCurrentScene) {
          StartSceneIoJob(true, GetCurrentScenePath());
        } else {
          std::wstring chosen;
          if (SaveSceneFileDialog(g_hwnd, chosen)) {
            std::string utf8 = WStringToUtf8(chosen);
            StartSceneIoJob(true, utf8);
          }
        }
      }
      if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
        std::wstring chosen;
        if (SaveSceneFileDialog(g_hwnd, chosen)) {
          std::string utf8 = WStringToUtf8(chosen);
          StartSceneIoJob(true, utf8);
        }
      }
      if (ImGui::MenuItem("Load Scene...", "Ctrl+O")) {
        std::wstring chosen;
        if (OpenSceneFileDialog(g_hwnd, chosen)) {
          std::string utf8 = WStringToUtf8(chosen);
          StartSceneIoJob(false, utf8);
        }
      }
      if (g_sceneIoJob.active) {
        ImGui::EndDisabled();
      }
      ImGui::Separator();
      if (IsSceneIoJobActive()) {
        ImGui::BeginDisabled();
      }
      if (ImGui::MenuItem("Exit", "Alt+F4")) {
        g_appClosing = true;
      }
      if (IsSceneIoJobActive()) {
        ImGui::EndDisabled();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      if (!Scene::CanUndoTransform()) {
        ImGui::BeginDisabled();
      }
      if (ImGui::MenuItem("Undo Transform", "Ctrl+Z")) {
        Scene::UndoTransform();
      }
      if (!Scene::CanUndoTransform()) {
        ImGui::EndDisabled();
      }

      if (!Scene::CanRedoTransform()) {
        ImGui::BeginDisabled();
      }
      if (ImGui::MenuItem("Redo Transform", "Ctrl+Y")) {
        Scene::RedoTransform();
      }
      if (!Scene::CanRedoTransform()) {
        ImGui::EndDisabled();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
      ImGui::MenuItem("Scene", nullptr, &g_showAssetsWindow);
      ImGui::MenuItem("Lights", nullptr, &g_showLightsWindow);
      ImGui::MenuItem("Controls", nullptr, &g_showControlsWindow);
      ImGui::MenuItem("Render Mode", nullptr, &g_showRenderModeWindow);
      ImGui::MenuItem("Render Settings", nullptr,
                      &g_showRenderSettingsWindow);
      ImGui::MenuItem("regir debug", nullptr, &g_showReGIRDebugWindow);
      ImGui::MenuItem("Material Editor", nullptr, &g_showMaterialEditor);
      ImGui::EndMenu();
    }

    // Quick access toggles (side-by-side) for panels
    ImGui::SameLine();
    ImGui::Text("Panels:");
    ImGui::SameLine();
    // Use compact spacing for menu bar toggles
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    // Cloud settings have been moved into the Controls window. No dropdown
    // needed.

    ImGui::Checkbox("##AssetsToggle", &g_showAssetsWindow);
    ImGui::SameLine();
    ImGui::Text("Assets");
    ImGui::SameLine();
    ImGui::Checkbox("##LightsToggle", &g_showLightsWindow);
    ImGui::SameLine();
    ImGui::Text("Lights");
    ImGui::SameLine();
    ImGui::Checkbox("##ControlsToggle", &g_showControlsWindow);
    ImGui::SameLine();
    ImGui::Text("Controls");
    ImGui::SameLine();
    ImGui::Checkbox("##RenderModeToggle", &g_showRenderModeWindow);
    ImGui::SameLine();
    ImGui::Text("Render Mode");
    ImGui::SameLine();
    ImGui::Checkbox("##RenderSettingsToggle", &g_showRenderSettingsWindow);
    ImGui::SameLine();
    ImGui::Text("Render Settings");
    ImGui::SameLine();
    ImGui::Checkbox("##ReGIRDebugToggle", &g_showReGIRDebugWindow);
    ImGui::SameLine();
    ImGui::Text("regir debug");
    ImGui::SameLine();
    ImGui::Checkbox("##MaterialEditorToggle", &g_showMaterialEditor);
    ImGui::SameLine();
    ImGui::Text("Material Editor");
    ImGui::PopStyleVar();

    const float renderButtonWidth = 120.0f;
    const float rightPadding = 12.0f;
    const float rightX =
        ImGui::GetWindowWidth() - renderButtonWidth - rightPadding;
    if (rightX > ImGui::GetCursorPosX()) {
      ImGui::SetCursorPosX(rightX);
    }

    if (!g_rayTracingSupported) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(g_renderExportJob.active ? "Rendering..." : "Render",
                      ImVec2(renderButtonWidth, 0))) {
      ImGui::OpenPopup("Render Export");
      fprintf(stderr, "UI: Render button pressed\n");
    }
    if (!g_rayTracingSupported) {
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("DXR is not supported on this device.");
      }
    }

    if (ImGui::BeginPopup("Render Export")) {
      ImGui::TextUnformatted("DXR Render Settings");
      ImGui::Separator();

      const int presetCount = g_renderResolutionPresetCount;
      if (g_renderExportSettings.resolutionPreset < 0 ||
          g_renderExportSettings.resolutionPreset >= presetCount) {
        g_renderExportSettings.resolutionPreset = 0;
      }

      const RenderResolutionPreset &activePreset =
          g_renderResolutionPresets[g_renderExportSettings.resolutionPreset];
      if (ImGui::BeginCombo("Render Resolution", activePreset.label)) {
        for (int i = 0; i < presetCount; ++i) {
          const bool selected = (i == g_renderExportSettings.resolutionPreset);
          if (ImGui::Selectable(g_renderResolutionPresets[i].label, selected)) {
            g_renderExportSettings.resolutionPreset = i;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      const char *projections[] = {"Perspective", "Spherical 360 Panorama"};
      ImGui::Combo("Projection", &g_renderExportSettings.projectionMode,
                   projections, IM_ARRAYSIZE(projections));

      ImGui::SliderInt("Max SPP", &g_renderExportSettings.maxSpp, 16, 4096);
      ImGui::SliderFloat("Noise %", &g_renderExportSettings.noisePercent, 0.1f,
                         30.0f, "%.2f%%");

      const char *denoisers[] = {"Off", "OIDN (CPU)", "OIDN (GPU)", "OptiX"};
      ImGui::Combo("Denoiser", &g_renderExportSettings.denoiserIndex, denoisers,
                   IM_ARRAYSIZE(denoisers));

      if (g_renderExportJob.active) {
        const UINT spp = DxrRenderer::GetDisplayedSampleCount();
        const float noise = DxrRenderer::GetCurrentNoiseLevel();
        const bool hasNoise = DxrRenderer::HasNoiseEstimate();
        const bool denoiserEnabled =
          !g_renderExportJob.tileState.enabled &&
          (g_renderExportJob.targetDenoiserIndex != 0);
        ImGui::Separator();
        ImGui::Text("Progress: %u / %d SPP", spp,
                    g_renderExportJob.targetMaxSpp);
        if (g_renderExportJob.tileState.enabled) {
          const RenderExportTileState &tile = g_renderExportJob.tileState;
          ImGui::Text("Output: %u x %u", tile.fullWidth, tile.fullHeight);
          ImGui::Text("Tile: %u / %u (%u x %u, y=%u)",
                      tile.currentTileIndex + 1,
                      tile.tileCountX * tile.tileCountY, tile.tileWidth,
                      tile.tileHeight, tile.tileOffsetY);
        } else {
          ImGui::Text("Output: %u x %u", g_renderExportJob.targetWidth,
                      g_renderExportJob.targetHeight);
        }
        if (hasNoise) {
          ImGui::Text("Noise: %.2f%% / %.2f%%", noise * 100.0f,
                      g_renderExportJob.targetNoiseThreshold * 100.0f);
        } else {
          ImGui::Text("Noise: Calculating...");
        }
        ImGui::Text("Min SPP before noise-stop: %u",
                    g_renderExportJob.minSppBeforeNoiseStop);
        if (denoiserEnabled) {
          ImGui::Text("Denoiser output: %s",
                      DxrRenderer::HasDenoisedOutput() ? "Ready" : "Waiting");
        }
        if (g_renderExportJob.completionArmed) {
          ImGui::Text("Finalizing... (%d)",
                      g_renderExportJob.settleFramesRemaining);
        }

        if (ImGui::Button("Cancel Render")) {
          g_renderExportStatus = "Render canceled.";
          RestoreRenderExportState();
        }
      } else {
        if (ImGui::Button("Render and Export PNG...")) {
          std::wstring chosenPath;
          if (SaveRenderImageFileDialog(g_hwnd, chosenPath)) {
            StartRenderExportJob(chosenPath);
          }
        }
      }

      if (!g_renderExportStatus.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", g_renderExportStatus.c_str());
      }

      ImGui::EndPopup();
    }

    ImGui::EndMainMenuBar();
  }

  // UI: Camera controls and debug info
  if (g_showControlsWindow) {
    if (ImGui::Begin("Controls", &g_showControlsWindow,
                     ImGuiWindowFlags_NoCollapse)) {
      bool uiChanged = false;

      // Camera Debug Info
      ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", g_cameraData.pos[0],
                  g_cameraData.pos[1], g_cameraData.pos[2]);
      ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)", g_cameraData.forward[0],
                  g_cameraData.forward[1], g_cameraData.forward[2]);
      ImGui::Text("Camera Up: (%.2f, %.2f, %.2f)", g_cameraData.up[0],
                  g_cameraData.up[1], g_cameraData.up[2]);
      {
        float vFov = g_cameraData.fov;
        float aspect = g_cameraData.aspect;
        float vHalfRad = vFov * 0.5f * (3.14159265f / 180.0f);
        float hFov =
            2.0f * atanf(tanf(vHalfRad) * aspect) * (180.0f / 3.14159265f);
        ImGui::Text("FOV V/H: %.1f° / %.1f°, Aspect: %.2f", vFov, hFov, aspect);
      }
      ImGui::Text("Near: %.2f, Far: %.2f", g_cameraData.nearZ,
                  g_cameraData.farZ);
      ImGui::Text("Exposure Scale: %.4f", g_cameraData.intensity);

      ImGui::Separator();

      // Controls
      // Horizontal-FOV slider (UI shows H, shaders use V). Convert H -> V
      // before storing.
      {
        float aspect = g_cameraData.aspect;
        // compute current horizontal FOV from stored vertical FOV
        float curV = g_cameraData.fov;
        float curVHalf = curV * 0.5f * (3.14159265f / 180.0f);
        float curH =
            2.0f * atanf(tanf(curVHalf) * aspect) * (180.0f / 3.14159265f);
        float hFovSlider = curH;
        if (ImGui::SliderFloat("Horizontal FOV", &hFovSlider, 20.0f, 160.0f)) {
          // convert slider H (degrees) back to vertical FOV in degrees
          float hHalfRad = hFovSlider * 0.5f * (3.14159265f / 180.0f);
          float vHalfRadNew = atanf(tanf(hHalfRad) / aspect);
          float vFovNew = 2.0f * vHalfRadNew * (180.0f / 3.14159265f);
          g_cameraData.fov = vFovNew;
          UpdateCameraCB();
          uiChanged = true;
        }
      }
      {
        ImGui::Text("Camera Exposure");
        bool autoExp = DxrRenderer::GetAutoExposure();
        if (ImGui::Checkbox("Auto Exposure", &autoExp)) {
          DxrRenderer::SetAutoExposure(autoExp);
          uiChanged = true;
        }

        bool physicalCam = DxrRenderer::GetPhysicalCameraExposure();
        if (ImGui::Checkbox("Physical Camera (ISO/Shutter/f)", &physicalCam)) {
          DxrRenderer::SetPhysicalCameraExposure(physicalCam);
          uiChanged = true;
        }

        if (!autoExp && physicalCam) {
          if (ImGui::Button("Preset: Engine Daylight")) {
            DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 30.0f, 2.8f);
            uiChanged = true;
          }
          ImGui::SameLine();
          if (ImGui::Button("Preset: Sunny 16")) {
            DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 125.0f,
                                                   16.0f);
            uiChanged = true;
          }
          ImGui::SameLine();
          if (ImGui::Button("Preset: Interior")) {
            DxrRenderer::SetPhysicalCameraSettings(800.0f, 1.0f / 15.0f, 2.8f);
            uiChanged = true;
          }

          if (ImGui::Button("Match to Scene EV")) {
            float sceneEV = DxrRenderer::GetCurrentEV100();
            float iso = 100.0f;
            float aperture = 8.0f;
            float shutterSeconds =
                (aperture * aperture * 100.0f) /
                ((std::max)(0.001f, iso) * powf(2.0f, sceneEV));
            shutterSeconds =
                (std::clamp)(shutterSeconds, 1.0f / 8000.0f, 30.0f);
            DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds,
                                                   aperture);
            uiChanged = true;
          }
          ImGui::TextDisabled(
              "Use Match to Scene EV if image is too dark/bright.");
        }

        if (autoExp) {
          float comp = DxrRenderer::GetExposureCompensation();
          if (ImGui::SliderFloat("Exposure Comp", &comp, 0.1f, 10.0f,
                                 "%.2fx")) {
            DxrRenderer::SetExposureCompensation(comp);
            uiChanged = true;
          }
        }

        if (!autoExp && physicalCam) {
          float iso = 100.0f;
          float shutterSeconds = 1.0f / 125.0f;
          float aperture = 16.0f;
          DxrRenderer::GetPhysicalCameraSettings(iso, shutterSeconds, aperture);

          if (ImGui::SliderFloat("ISO", &iso, 25.0f, 6400.0f, "%.0f")) {
            DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds,
                                                   aperture);
            uiChanged = true;
          }

          if (ImGui::SliderFloat("Shutter (seconds)", &shutterSeconds,
                                 1.0f / 8000.0f, 30.0f, "%.4f s",
                                 ImGuiSliderFlags_Logarithmic)) {
            DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds,
                                                   aperture);
            uiChanged = true;
          }

          if (shutterSeconds <= 1.0f) {
            ImGui::Text("~ 1/%.0f s",
                        1.0f / (std::max)(shutterSeconds, 1.0e-6f));
          }

          if (ImGui::SliderFloat("Aperture (f/N)", &aperture, 1.0f, 22.0f,
                                 "f/%.1f")) {
            DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds,
                                                   aperture);
            uiChanged = true;
          }

          ImGui::Text("Camera EV100: %.2f",
                      DxrRenderer::GetPhysicalCameraEV100());
        } else {
          if (autoExp) {
            ImGui::BeginDisabled();
          }
          if (ImGui::SliderFloat("Manual Exposure Scale",
                                 &g_cameraData.intensity, 0.0001f, 2.0f, "%.4f",
                                 ImGuiSliderFlags_Logarithmic)) {
            UpdateCameraCB();
            uiChanged = true;
          }
          ImGui::TextDisabled("Affects only camera exposure (post-lighting "
                              "multiplier), not sun/sky light power.");
          if (autoExp) {
            ImGui::EndDisabled();
          }
        }
      }

      {
        auto &rs = RasterRenderer::GetRenderSettings();
        ImGui::Separator();
        ImGui::Text("Tone Map");
        if (ImGui::SliderFloat("Vignette", &rs.tonemapVignette, 0.0f, 1.0f,
                               "%.2f")) {
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Saturation", &rs.tonemapSaturation, 0.0f,
                               2.0f, "%.2f")) {
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Contrast", &rs.tonemapContrast, 0.0f, 2.0f,
                               "%.2f")) {
          uiChanged = true;
        }
        if (ImGui::SliderFloat("White Balance", &rs.tonemapWhiteBalance,
                               -1.0f, 1.0f, "%.2f")) {
          uiChanged = true;
        }

        if (g_currentRenderMode == RenderMode::DXR) {
          static const char *kAoModeLabels[] = {"Inward", "Outward", "Both"};
          int aoMode =
              static_cast<int>(DxrRenderer::GetTonemapAmbientOcclusionMode());
          if (ImGui::Combo("DXR AO Mode", &aoMode, kAoModeLabels,
                           IM_ARRAYSIZE(kAoModeLabels))) {
            DxrRenderer::SetTonemapAmbientOcclusionMode(
                static_cast<DxrRenderer::TonemapAmbientOcclusionMode>(
                    std::clamp(aoMode, 0, 2)));
            uiChanged = true;
          }

          float aoIntensity =
              DxrRenderer::GetTonemapAmbientOcclusionIntensity();
          if (ImGui::SliderFloat("DXR AO Intensity", &aoIntensity, 0.0f, 2.0f,
                                 "%.2f")) {
            DxrRenderer::SetTonemapAmbientOcclusionIntensity(aoIntensity);
            uiChanged = true;
          }
          float aoLengthMm = DxrRenderer::GetTonemapAmbientOcclusionLengthMm();
          if (ImGui::SliderFloat("DXR AO Length (mm)", &aoLengthMm, 0.0f,
                                 5000.0f, "%.0f mm")) {
            DxrRenderer::SetTonemapAmbientOcclusionLengthMm(aoLengthMm);
            uiChanged = true;
          }
        } else {
          ImGui::TextDisabled("DXR AO is available only in DXR mode.");
        }
      }

      if (ImGui::Button("Reset Camera")) {
        ResetCamera();
        UpdateCameraCB();
        uiChanged = true;
      }

      // Camera movement & mouse sensitivity controls
      ImGui::Spacing();
      if (ImGui::SliderFloat("Move Speed", &g_camSpeed, 0.1f, 20.0f)) {
        // no additional action required; movement uses g_camSpeed immediately
        uiChanged = true;
      }
      if (ImGui::SliderFloat("Mouse Sensitivity", &g_mouseSensitivity, 0.001f,
                             0.05f)) {
        // sensitivity applied next frame via g_mouseSensitivity
        uiChanged = true;
      }

      ImGui::Separator();

      // Manual Sun Control removed as Prague Model is default

      ImGui::Text("Environment / Sky Model");

      float iblRotationDeg = IBLManager::Get().GetIblRotationDegrees();
      if (ImGui::SliderFloat("IBL Rotation (deg)", &iblRotationDeg, 0.0f,
                             360.0f, "%.1f")) {
        IBLManager::Get().SetIblRotationDegrees(iblRotationDeg);
        UpdateCameraCB();
        uiChanged = true;
      }

      // analytic sun intensity. For file IBL, this drives the extracted
      // analytic sun created from the HDR; for Prague sky it drives the
      // procedural sun.
      {
        const bool usingFileIBL =
            (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
        const bool hasFileSun = usingFileIBL && IBLManager::Get().HasFileSun();
        float analyticSunInt =
            hasFileSun ? IBLManager::Get().GetFileSunIntensity()
                       : IBLManager::Get().GetSunIntensity();
        if (ImGui::SliderFloat("Analytic Sun Intensity", &analyticSunInt, 0.0f,
                               150000.0f, "%.0f Lux")) {
          if (hasFileSun) {
            IBLManager::Get().SetFileSunIntensity(analyticSunInt);
          } else {
            IBLManager::Get().SetSunIntensity(analyticSunInt);
          }
          uiChanged = true;
        }
      }

      // Environment sampling is physically locked to solid-angle measure; the
      // disabled control remains as a diagnostic readout for old scenes/tools.
      bool sampleSolid = true;
      ImGui::BeginDisabled();
      ImGui::Checkbox("Solid-angle env sampling", &sampleSolid);
      ImGui::EndDisabled();
      g_cameraData.sampleEnvSolidAngle = 1.0f;
      IBLManager::Get().SetEnvSolidAngleSampling(true);

      bool fileIblEnabled =
          (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
      if (ImGui::Checkbox("Enable File IBL", &fileIblEnabled)) {
        if (fileIblEnabled) {
          IBLManager::Get().SetIBLSource(IBLManager::IBLSource::File);
        } else {
          IBLManager::Get().SetIBLSource(IBLManager::IBLSource::PragueSkyModel);
          IBLManager::Get().UpdateSkyModel();
        }
        uiChanged = true;
      }

      static int iblSource = 0;
      iblSource = (int)IBLManager::Get().GetIBLSource();
      if (ImGui::RadioButton("File IBL", &iblSource, 0)) {
        IBLManager::Get().SetIBLSource(IBLManager::IBLSource::File);
        uiChanged = true;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Prague Sky", &iblSource, 1)) {
        IBLManager::Get().SetIBLSource(IBLManager::IBLSource::PragueSkyModel);
        IBLManager::Get().UpdateSkyModel();
        uiChanged = true;
      }

      if (iblSource == 1) {
        bool uiParamChanged = false;

        bool physicalSky = IBLManager::Get().IsPhysicalCalibrationEnabled();
        if (ImGui::Checkbox("Physical Calibration", &physicalSky)) {
          IBLManager::Get().SetPhysicalCalibrationEnabled(physicalSky);
          uiParamChanged = true;
          uiChanged = true;
        }
        if (physicalSky) {
          ImGui::TextDisabled(
              "Physical mode: sky gain=1.0, sun=110000 lux, sun size=0.53 deg");
        }

        float vis = IBLManager::Get().GetSkyVisibility();
        if (ImGui::SliderFloat("Visibility (km)", &vis, 10.0f, 120.0f)) {
          IBLManager::Get().SetSkyVisibility(vis);
          uiParamChanged = true;
          uiChanged = true;
        }
        float albedo = IBLManager::Get().GetSkyAlbedo();
        if (ImGui::SliderFloat("Earth Albedo", &albedo, 0.0f, 1.0f)) {
          IBLManager::Get().SetSkyAlbedo(albedo);
          uiParamChanged = true;
          uiChanged = true;
        }
        float altitude = IBLManager::Get().GetObserverAltitude();
        if (ImGui::SliderFloat("Altitude (m)", &altitude, 0.0f, 15000.0f)) {
          IBLManager::Get().SetObserverAltitude(altitude);
          uiParamChanged = true;
          uiChanged = true;
        }

        // Intensity Controls
        if (physicalSky) {
          ImGui::BeginDisabled();
        }
        float skyInt = IBLManager::Get().GetSkyIntensity();
        if (ImGui::SliderFloat("Sky Intensity", &skyInt, 0.0f, 5.0f, "%.3f")) {
          IBLManager::Get().SetSkyIntensity(skyInt);
          uiParamChanged = true;
          uiChanged = true;
        }

        float sunInt = IBLManager::Get().GetSunIntensity();
        if (ImGui::SliderFloat("Sun Intensity (Lux)", &sunInt, 0.0f,
                               150000.0f, "%.0f Lux")) {
          IBLManager::Get().SetSunIntensity(sunInt);
          // Changes analytic light intensity
          uiChanged = true;
        }
        float sunSize = IBLManager::Get().GetSunSize();
        if (ImGui::SliderFloat("Sun Size (deg)", &sunSize, 0.1f, 5.0f)) {
          IBLManager::Get().SetSunSize(sunSize);
          // Changes analytic light radius
          uiChanged = true;
        }
        if (physicalSky) {
          ImGui::EndDisabled();
        }

        // float elev = IBLManager::Get().GetSolarAltitude(); // Not used
        // directly, driven by Time

        // GUI State for solar model parameters (controlled by main loop refs)

        if (ImGui::SliderFloat("Time of Day", &timeOfDay, 6.0f, 18.0f)) {
          uiParamChanged = true;
          uiChanged = true;
        }
        if (ImGui::SliderFloat("North Offset", &northOffset, 0.0f, 360.0f)) {
          uiParamChanged = true;
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Latitude (deg)", &latitudeDeg, -66.5f, 66.5f)) {
          uiParamChanged = true;
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Day of Year", &dayOfYear, 1.0f, 365.0f,
                               "%.0f")) {
          uiParamChanged = true;
          uiChanged = true;
        }

        ImGui::Text("Sky Avg Y: %.0f cd/m²",
                    IBLManager::Get().GetSkyAvgLuminanceCdM2());
        ImGui::Text("Sky Horizon Y: %.0f cd/m²",
                    IBLManager::Get().GetSkyHorizonLuminanceCdM2());
        ImGui::Text("Sky Max Y: %.0f cd/m²",
                    IBLManager::Get().GetSkyMaxLuminanceCdM2());

        // Logic moved to PopulateCommandList to ensure update even when UI is
        // closed

        // If UI changed non-light parameters (texture content), force logical
        // reset
        if (uiParamChanged) {
          DxrRenderer::ResetAccumulation();
        }

        UpdateCameraCB(); // automatically resets accumulation if
                          // lightDir/Color changed
      }

      ImGui::Spacing();
      ImGui::Checkbox("Show Grid", &g_drawGrid);

      ImGui::Separator();

      {
        static float s_offsetX = 0.2f;
        if (ImGui::SliderFloat("Offset X", &s_offsetX, -1.0f, 1.0f)) {
          struct AlignConstants {
            float offset[4];
          } constants;
          constants.offset[0] = s_offsetX;
          constants.offset[1] = 0.0f;
          constants.offset[2] = 0.0f;
          constants.offset[3] = 0.0f;

          extern void *g_constantCbMappedData;
          if (g_constantCbMappedData) {
            memcpy(g_constantCbMappedData, &constants, sizeof(constants));
          } else {
            UINT8 *pCbData = nullptr;
            D3D12_RANGE readRange = {0, 0};
            if (SUCCEEDED(g_constantBuffer->Map(
                    0, &readRange, reinterpret_cast<void **>(&pCbData)))) {
              memcpy(pCbData, &constants, sizeof(constants));
              g_constantBuffer->Unmap(0, nullptr);
            }
          }
          uiChanged = true;
        }
      }
      if (ImGui::Checkbox("Verbose Render Logs", &g_verboseRenderLogs)) {
        fprintf(stderr, "Verbose Render Logs set=%d\n", g_verboseRenderLogs);
        uiChanged = true;
      }

      // Cloud settings section moved to the bottom of the controls window
      ImGui::Separator();
      ImGui::Text("Cloud Settings");
      {
        CloudParams &cp = g_cloudManager.GetParams();
        bool cloudChanged = false;

        if (ImGui::Checkbox("Enable Cloud Rendering",
                            &g_cloudRenderingEnabled)) {
          cloudChanged = true;
        }
        if (ImGui::Button("Reset to Defaults")) {
          g_cloudManager.ResetToDefaults();
          cloudChanged = true;
        }

        cloudChanged |= ImGui::SliderFloat("Density", &cp.density, 0.0f, 5.0f);
        cloudChanged |=
            ImGui::SliderFloat("Absorption", &cp.absorption, 0.0f, 2.0f);
        cloudChanged |=
            ImGui::SliderFloat("Coverage", &cp.coverage, 0.0f, 1.0f);
        cloudChanged |=
            ImGui::SliderFloat("Scattering (g)", &cp.scattering, -0.99f, 0.99f);
        cloudChanged |=
            ImGui::SliderFloat("Sun Intensity", &cp.sunIntensity, 0.0f, 5.0f);
        cloudChanged |=
            ImGui::SliderFloat("Top Height", &cp.cloudTop, 500.0f, 12000.0f);
        cloudChanged |=
            ImGui::SliderFloat("Start Height", &cp.cloudBottom, 100.0f, 6000.0f);
        if (cp.cloudTop < cp.cloudBottom + 100.0f) {
          cp.cloudTop = cp.cloudBottom + 100.0f;
          cloudChanged = true;
        }
        cloudChanged |=
            ImGui::SliderFloat("Wind Speed", &cp.windSpeed, 0.0f, 50.0f);

        ImGui::Separator();
        cloudChanged |=
            ImGui::SliderFloat("Base Scale", &cp.baseScale, 0.0001f, 0.0020f,
                               "%.5f", ImGuiSliderFlags_Logarithmic);
        cloudChanged |=
            ImGui::SliderFloat("Detail Scale", &cp.detailScale, 0.0005f, 0.01f,
                               "%.5f", ImGuiSliderFlags_Logarithmic);
        cloudChanged |=
            ImGui::SliderFloat("Coverage Scale", &cp.coverageScale, 0.00005f,
                               0.0010f, "%.5f", ImGuiSliderFlags_Logarithmic);
        cloudChanged |= ImGui::SliderFloat("Variety",
                                           &cp.coverageVariation, 0.0f, 1.0f);
        cloudChanged |= ImGui::SliderFloat("Erosion", &cp.erosion, 0.0f, 1.0f);
        cloudChanged |=
            ImGui::SliderFloat("Warp Strength", &cp.warpStrength, 0.0f, 2.0f);
        cloudChanged |=
            ImGui::SliderFloat("Shape Power", &cp.shapePower, 0.4f, 3.0f);
        cloudChanged |= ImGui::SliderFloat("Powder Strength",
                                           &cp.powderStrength, 0.0f, 1.5f);
        cloudChanged |= ImGui::SliderFloat("Cirrus Amount", &cp.cirrusAmount,
                                           0.0f, 1.0f);
        cloudChanged |= ImGui::SliderFloat("Ground Shadows",
                                           &cp.cloudShadowStrength, 0.0f, 1.0f);

        ImGui::Separator();
        cloudChanged |=
            ImGui::SliderInt("Shadow Steps", &cp.shadowSteps, 1, 24);
        cloudChanged |= ImGui::SliderFloat("Shadow Step Size",
                                           &cp.shadowStepSize, 10.0f, 500.0f);
        cloudChanged |=
            ImGui::SliderFloat("Shadow LOD", &cp.shadowLod, 0.0f, 5.0f);
        cloudChanged |=
            ImGui::SliderFloat("Shadow Softness", &cp.shadowSoftness, 0.0f, 1.0f);

        ImGui::Separator();
        cloudChanged |=
            ImGui::SliderInt("Preview Bake Samples", &cp.previewBakeSamples, 1, 8);
        cloudChanged |=
            ImGui::SliderInt("Final Bake Samples", &cp.finalBakeSamples, 1, 32);
        cloudChanged |= ImGui::SliderFloat("Bake Jitter",
                                           &cp.bakeJitterStrength, 0.0f, 2.0f);
        cloudChanged |= ImGui::SliderFloat("Multi-Scatter Boost",
                                           &cp.multiScatterBoost, 0.0f, 2.0f);
        cloudChanged |= ImGui::SliderFloat("Silver Lining",
                                           &cp.silverLiningStrength, 0.0f, 2.0f);
        cloudChanged |= ImGui::SliderFloat("Cloud Type",
                                           &cp.cloudType, 0.0f, 2.0f);
        cloudChanged |= ImGui::SliderFloat("Ground Bounce",
                                           &cp.groundBounceStrength, 0.0f, 2.0f);

        if (cloudChanged) {
          g_cloudManager.RequestPreviewBake();
          DxrRenderer::ResetAccumulation();
        }
      }
    }
    ImGui::End();
  }

  ImGui::SetNextWindowSize(ImVec2(360, 460), ImGuiCond_FirstUseEver);
  if (g_showRenderSettingsWindow) {
    if (ImGui::Begin("Render Settings", &g_showRenderSettingsWindow,
                     ImGuiWindowFlags_NoCollapse)) {
      auto &rs = RasterRenderer::GetRenderSettings();

      if (g_currentRenderMode != RenderMode::Raster) {
        ImGui::TextWrapped(
            "These controls affect the raster renderer and will apply the next "
            "time Raster mode is active.");
        ImGui::Separator();
      }

      if (ImGui::Button("Reset Raster Settings")) {
        RasterRenderer::ResetRenderSettings();
      }

      ImGui::Separator();
      ImGui::Text("Reflections (SSR)");
      ImGui::Checkbox("Enable SSR", &rs.enableSSR);
      if (rs.enableSSR) {
        ImGui::SliderFloat("SSR Step Size", &rs.ssrStepSize, 0.02f, 2.0f,
                           "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("SSR Thickness", &rs.ssrThickness, 0.001f, 1.0f,
                           "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("SSR Intensity", &rs.ssrIntensity, 0.0f, 2.0f,
                           "%.2f");
        ImGui::SliderFloat("SSR Min Smoothness", &rs.ssrMinSmoothness, 0.0f,
                           1.0f, "%.2f");
        ImGui::SliderInt("SSR Max Steps", &rs.ssrMaxSteps, 1, 256);
      }

      ImGui::Separator();
      ImGui::Text("Ambient Occlusion (SSAO)");
      ImGui::Checkbox("Enable SSAO", &rs.enableSSAO);
      if (rs.enableSSAO) {
        ImGui::SliderFloat("SSAO Radius", &rs.ssaoRadius, 0.01f, 5.0f,
                           "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("SSAO Bias", &rs.ssaoBias, 0.0001f, 0.25f,
                           "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("SSAO Strength", &rs.ssaoStrength, 0.0f, 4.0f,
                           "%.2f");
        ImGui::SliderInt("SSAO Samples", &rs.ssaoSamples, 1, 32);
        ImGui::SliderFloat("SSAO Composite", &rs.ssaoCompositeWeight, 0.0f,
                           1.0f, "%.2f");
      }

      ImGui::Separator();
      ImGui::Text("Bloom");
      ImGui::Checkbox("Enable Bloom", &rs.enableBloom);
      if (rs.enableBloom) {
        ImGui::SliderFloat("Bloom Threshold", &rs.bloomThreshold, 0.0f,
                           10.0f, "%.2f");
        ImGui::SliderFloat("Bloom Intensity", &rs.bloomIntensity, 0.0f,
                           4.0f, "%.2f");
      }

      ImGui::Separator();
      ImGui::Text("Tonemap");
      ImGui::SliderFloat("Vignette", &rs.tonemapVignette, 0.0f, 1.0f,
                         "%.2f");
      ImGui::SliderFloat("Saturation", &rs.tonemapSaturation, 0.0f, 2.0f,
                         "%.2f");
      ImGui::SliderFloat("Contrast", &rs.tonemapContrast, 0.0f, 2.0f,
                         "%.2f");
      ImGui::SliderFloat("White Balance", &rs.tonemapWhiteBalance, -1.0f,
                         1.0f, "%.2f");
    }
    ImGui::End();
  }

  // Render Mode Selector
  ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
  if (g_showRenderModeWindow) {
    if (ImGui::Begin("Render Mode", &g_showRenderModeWindow,
                     ImGuiWindowFlags_NoCollapse)) {
      bool uiChanged = false;
      ImGui::Text("Current Mode: %s",
                  g_currentRenderMode == RenderMode::Raster ? "Raster" : "DXR");

      if (g_currentRenderMode == RenderMode::DXR) {
        ImGui::Text("Samples: %u", DxrRenderer::GetDisplayedSampleCount());

        float currentNoise = DxrRenderer::GetCurrentNoiseLevel();
        if (DxrRenderer::HasNoiseEstimate()) {
          ImGui::Text("Noise Level: %.2f%%", currentNoise * 100.0f);
        } else {
          ImGui::Text("Noise Level: Calculating...");
        }

        float avgLum = DxrRenderer::GetCurrentAvgLuminance();
        float ev100 = DxrRenderer::GetCurrentEV100();
        if (avgLum > 0.0f) {
          ImGui::Text("Avg Luminance: %.2f cd/m²", avgLum);
          ImGui::Text("EV100: %.2f", ev100);
        }

      } else {
        float avgLum = RasterRenderer::GetCurrentAvgLuminance();
        float ev100 = RasterRenderer::GetCurrentEV100();
        if (avgLum > 0.0f) {
          ImGui::Text("Avg Luminance: %.2f cd/m²", avgLum);
          ImGui::Text("EV100: %.2f", ev100);
        }
      }

      if (g_currentRenderMode == RenderMode::DXR) {
        if (ImGui::SliderFloat("Reflection Bounces",
                               &g_cameraData.maxSpecularBounces, 0.0f, 16.0f,
                               "%.0f")) {
          UpdateCameraCB();
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Refraction Bounces",
                               &g_cameraData.maxRefractiveBounces, 0.0f, 16.0f,
                               "%.0f")) {
          UpdateCameraCB();
          uiChanged = true;
        }
        if (ImGui::SliderFloat("GI Bounces", &g_cameraData.maxGIBounces, 0.0f,
                               16.0f, "%.0f")) {
          UpdateCameraCB();
          uiChanged = true;
        }

        int maxSpp = (int)g_cameraData.maxSPP;
        if (maxSpp < 10)
          maxSpp = 10;
        if (maxSpp > 1000)
          maxSpp = 1000;
        if (ImGui::SliderInt("Max SPP", &maxSpp, 10, 1000)) {
          g_cameraData.maxSPP = (float)maxSpp;
          UpdateCameraCB();
          uiChanged = true;
        }

        bool clayMode = g_cameraData.debugVisualizationMode > 1.5f &&
                        g_cameraData.debugVisualizationMode < 2.5f;
        if (ImGui::Checkbox("Clay Material Override", &clayMode)) {
          if (clayMode) {
            g_cameraData.debugVisualizationMode = 2.0f;
          } else if (g_cameraData.debugVisualizationMode > 1.5f &&
                     g_cameraData.debugVisualizationMode < 2.5f) {
            g_cameraData.debugVisualizationMode = 0.0f;
          }
          UpdateCameraCB();
          uiChanged = true;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(
              "Overrides scene materials with opaque matte 50%% grey for "
              "lighting checks");
        }
        ImGui::BeginDisabled(!clayMode);
        uint32_t clayFeatureFlags =
            static_cast<uint32_t>(g_cameraData.dxrFeatureFlags);
        bool clayPreserveTransparency =
            (clayFeatureFlags & kDxrFeatureClayPreserveTransparency) != 0;
        if (ImGui::Checkbox("Clay Preserve Transparency",
                            &clayPreserveTransparency)) {
          if (clayPreserveTransparency) {
            clayFeatureFlags |= kDxrFeatureClayPreserveTransparency;
          } else {
            clayFeatureFlags &= ~kDxrFeatureClayPreserveTransparency;
          }
          g_cameraData.dxrFeatureFlags = static_cast<float>(clayFeatureFlags);
          UpdateCameraCB();
          uiChanged = true;
        }
        bool clayPreserveEmission =
            (clayFeatureFlags & kDxrFeatureClayPreserveEmission) != 0;
        if (ImGui::Checkbox("Clay Preserve Emission", &clayPreserveEmission)) {
          if (clayPreserveEmission) {
            clayFeatureFlags |= kDxrFeatureClayPreserveEmission;
          } else {
            clayFeatureFlags &= ~kDxrFeatureClayPreserveEmission;
          }
          g_cameraData.dxrFeatureFlags = static_cast<float>(clayFeatureFlags);
          UpdateCameraCB();
          uiChanged = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Adaptive Sampling");
        bool adaptive = g_cameraData.useAdaptiveSampling > 0.5f;
        if (ImGui::Checkbox("Enable Adaptive Sampling", &adaptive)) {
          g_cameraData.useAdaptiveSampling = adaptive ? 1.0f : 0.0f;
          // Ensure threshold is valid when enabling
          if (g_cameraData.noiseThreshold <= 0.0f) {
            g_cameraData.noiseThreshold = 0.05f; // Default 5%
          }
          UpdateCameraCB();
          uiChanged = true;
        }
        if (adaptive) {
          // If adaptive is on, we might want to increase maxSPP effectively
          // to infinity or let user control it. User said "Max SPP or Noise,
          // whichever first". So we keep Max SPP control.

          float nVal = g_cameraData.noiseThreshold * 100.0f;
          if (ImGui::SliderFloat("Target Noise %", &nVal, 1.0f, 30.0f,
                                 "%.1f%%")) {
            g_cameraData.noiseThreshold = nVal / 100.0f;
            UpdateCameraCB();
            uiChanged = true;
          }

#ifdef _DEBUG
          bool viz = g_cameraData.debugVisualizationMode == 1.0f;
          if (ImGui::Checkbox("Show Noise Map (Debug)", &viz)) {
            if (viz) {
              g_cameraData.debugVisualizationMode = 1.0f;
            } else if (g_cameraData.debugVisualizationMode == 1.0f) {
              g_cameraData.debugVisualizationMode = 0.0f;
            }
            UpdateCameraCB();
            uiChanged = true;
          }

          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("White = High Noise (10%+), Black = Low Noise");
#else
          if (g_cameraData.debugVisualizationMode == 1.0f) {
            g_cameraData.debugVisualizationMode = 0.0f;
          }
#endif
        }

        ImGui::Separator();
        ImGui::Text("ReGIR Light Sampling");
        bool regirEnabled = DxrRenderer::GetReGIREnabled();
        if (ImGui::Checkbox("Enable ReGIR", &regirEnabled)) {
          DxrRenderer::SetReGIREnabled(regirEnabled);
          DxrRenderer::ResetAccumulation();
          uiChanged = true;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Spatially-aware light selection via grid importance "
              "sampling.\nDisable to fall back to uniform random light "
              "sampling.");

        ImGui::Separator();
        ImGui::Text("Streamline / DLSS");
        bool dlssEnabled = DX12Context::g_streamline.IsEnabled();
        if (ImGui::Checkbox("Enable", &dlssEnabled)) {
          DX12Context::g_streamline.SetEnabled(dlssEnabled);
          DxrRenderer::ResetAccumulation();
          // DLSS uses a different internal render resolution; recreate
          // resources.
          RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                  "DLSS enable toggle");
          uiChanged = true;
        }

        const char *dlssModes[] = {"Off", "DLSS Super Resolution",
                                   "DLSS Ray Reconstruction"};
        int modeIdx = 0;
        switch (DX12Context::g_streamline.GetMode()) {
        case StreamlineManager::Mode::Off:
          modeIdx = 0;
          break;
        case StreamlineManager::Mode::DLSS_SuperResolution:
          modeIdx = 1;
          break;
        case StreamlineManager::Mode::DLSS_RayReconstruction:
          modeIdx = 2;
          break;
        }
        if (ImGui::Combo("Mode", &modeIdx, dlssModes,
                         IM_ARRAYSIZE(dlssModes))) {
          StreamlineManager::Mode newMode = StreamlineManager::Mode::Off;
          if (modeIdx == 1)
            newMode = StreamlineManager::Mode::DLSS_SuperResolution;
          if (modeIdx == 2)
            newMode = StreamlineManager::Mode::DLSS_RayReconstruction;
          DX12Context::g_streamline.SetMode(newMode);
          if (newMode == StreamlineManager::Mode::DLSS_RayReconstruction) {
            // RR shimmer is often worst at silhouettes/screen edges.
            // Default to a more stable jitter amplitude when entering RR.
            DxrRenderer::SetRrJitterScale(0.5f);
          }
          DxrRenderer::ResetAccumulation();
          RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                  "DLSS mode change");
          uiChanged = true;
        }

        const char *qualities[] = {"Max Performance", "Balanced", "Max Quality",
                                   "Ultra Performance", "DLAA"};
        int qIdx = 1;
        switch (DX12Context::g_streamline.GetQuality()) {
        case StreamlineManager::Quality::MaxPerformance:
          qIdx = 0;
          break;
        case StreamlineManager::Quality::Balanced:
          qIdx = 1;
          break;
        case StreamlineManager::Quality::MaxQuality:
          qIdx = 2;
          break;
        case StreamlineManager::Quality::UltraPerformance:
          qIdx = 3;
          break;
        case StreamlineManager::Quality::DLAA:
          qIdx = 4;
          break;
        }
        if (ImGui::Combo("Quality", &qIdx, qualities,
                         IM_ARRAYSIZE(qualities))) {
          StreamlineManager::Quality newQ =
              StreamlineManager::Quality::Balanced;
          if (qIdx == 0)
            newQ = StreamlineManager::Quality::MaxPerformance;
          if (qIdx == 1)
            newQ = StreamlineManager::Quality::Balanced;
          if (qIdx == 2)
            newQ = StreamlineManager::Quality::MaxQuality;
          if (qIdx == 3)
            newQ = StreamlineManager::Quality::UltraPerformance;
          if (qIdx == 4)
            newQ = StreamlineManager::Quality::DLAA;
          DX12Context::g_streamline.SetQuality(newQ);
          DxrRenderer::ResetAccumulation();
          RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                  "DLSS quality change");
        }

        if (ImGui::Button("Reset DLSS History")) {
          DxrRenderer::ResetStreamlineHistory();
        }
        ImGui::SameLine();
        ImGui::Text("SL: %s / %s",
                    DX12Context::g_streamline.IsInitialized() ? "Init" : "Off",
                    DX12Context::g_streamline.IsDeviceSet() ? "Device"
                                                            : "NoDevice");

        // Show recommended render (input) size and output (swapchain) size
        {
          auto rec = DX12Context::g_streamline.GetRecommendedRenderSize(
              g_windowWidth, g_windowHeight);
          ImGui::Text("Render (in): %u x %u    Output (out): %u x %u",
                      (unsigned)rec.renderWidth, (unsigned)rec.renderHeight,
                      (unsigned)g_windowWidth, (unsigned)g_windowHeight);
        }

        if (DX12Context::g_streamline.GetMode() ==
            StreamlineManager::Mode::DLSS_RayReconstruction) {
          float rrJitterScale = DxrRenderer::GetRrJitterScale();
          if (ImGui::SliderFloat("RR Jitter Scale", &rrJitterScale, 0.0f, 1.0f,
                                 "%.2f")) {
            DxrRenderer::SetRrJitterScale(rrJitterScale);
            DxrRenderer::ResetStreamlineHistory();
            uiChanged = true;
          }
          ImGui::TextWrapped("Lowering jitter can reduce edge/silhouette "
                             "shimmer (especially near screen borders) but "
                             "may reduce DLSS-RR reconstruction/AA quality.");
        }

        ImGui::Separator();
        ImGui::Text("Final / Export Denoiser");
        const char *denoisers[] = {"Off", "OIDN (CPU)", "OIDN (GPU)", "OptiX"};
        int denoiserIdx = 0;
        switch (DxrRenderer::GetDenoiserMode()) {
        case DxrRenderer::DenoiserMode::Off:
          denoiserIdx = 0;
          break;
        case DxrRenderer::DenoiserMode::OIDN_CPU:
          denoiserIdx = 1;
          break;
        case DxrRenderer::DenoiserMode::OIDN_GPU:
          denoiserIdx = 2;
          break;
        case DxrRenderer::DenoiserMode::OptiX:
          denoiserIdx = 3;
          break;
        }
        if (ImGui::Combo("Denoiser##Final", &denoiserIdx, denoisers,
                         IM_ARRAYSIZE(denoisers))) {
          DxrRenderer::DenoiserMode newMode = DxrRenderer::DenoiserMode::Off;
          if (denoiserIdx == 1)
            newMode = DxrRenderer::DenoiserMode::OIDN_CPU;
          if (denoiserIdx == 2)
            newMode = DxrRenderer::DenoiserMode::OIDN_GPU;
          if (denoiserIdx == 3)
            newMode = DxrRenderer::DenoiserMode::OptiX;
          DxrRenderer::SetDenoiserMode(newMode);
          // Recreate pipeline/resources to account for any mode-specific
          // resources and reset accumulation for stable rendering.
          DxrRenderer::ResetAccumulation();
          RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                  "Denoiser mode change");
        }

        if (DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_CPU || 
            DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_GPU) {
          const char *oidnQualities[] = {"Fast", "Balanced", "High"};
          int qualIdx = (int)DxrRenderer::GetOidnQuality();
          if (ImGui::Combo("OIDN Quality", &qualIdx, oidnQualities,
                           IM_ARRAYSIZE(oidnQualities))) {
            DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)qualIdx);
            uiChanged = true;
          }
        }

        ImGui::Separator();
        ImGui::Text("Path Tracing Backend");
        const char *pathTracingBackends[] = {"Wavefront Optimized",
                                             "Wavefront Surface Diagnostics"};
        int backendIdx =
            DxrRenderer::GetPathTracingBackend() ==
                    DxrRenderer::PathTracingBackend::WavefrontParity
                ? 1
                : 0;
        if (ImGui::Combo("Backend", &backendIdx, pathTracingBackends,
                         IM_ARRAYSIZE(pathTracingBackends))) {
          DxrRenderer::SetPathTracingBackend(
              backendIdx == 1
                  ? DxrRenderer::PathTracingBackend::WavefrontParity
                  : DxrRenderer::PathTracingBackend::WavefrontOptimized);
          uiChanged = true;
        }
        ImGui::TextWrapped(
          "Wavefront Optimized is the production path. Surface Diagnostics is "
          "the primary-surface slice: queued "
          "primary rays are traced and resolved into the existing output and "
          "AOV surfaces without secondary, shadow, or ReSTIR scheduling.");
        {
          ImGui::Text("Bootstrap paths: %u",
                      DxrRenderer::GetWavefrontBootstrapPathCount());
          ImGui::Text("Bootstrap dispatch groups: %u",
                      DxrRenderer::GetWavefrontBootstrapDispatchGroups());
          ImGui::Text("Primary visibility records: %u",
                      DxrRenderer::GetWavefrontPrimaryRecordCount());
          ImGui::Text("Primary visibility hits / misses: %u / %u",
                      DxrRenderer::GetWavefrontPrimaryHitCount(),
                      DxrRenderer::GetWavefrontPrimaryMissCount());
          ImGui::Text("Primary resolve records / surfaces / sky: %u / %u / %u",
                      DxrRenderer::GetWavefrontResolveRecordCount(),
                      DxrRenderer::GetWavefrontResolveSurfaceCount(),
                      DxrRenderer::GetWavefrontResolveSkyCount());
          ImGui::Text("Primary resolve diffuse / specular / transmission: %u / %u / %u",
                      DxrRenderer::GetWavefrontResolveDiffuseCount(),
                      DxrRenderer::GetWavefrontResolveSpecularCount(),
                      DxrRenderer::GetWavefrontResolveTransmissionCount());
            ImGui::Text(
              "Primary bins diff / glossy / conductor / delta: %u / %u / %u / %u",
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Diffuse),
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::GlossyDielectric),
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Conductor),
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::DeltaReflection));
            ImGui::Text(
              "Primary bins refract / emissive / translucent: %u / %u / %u",
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Refraction),
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Emissive),
              DxrRenderer::GetWavefrontPrimaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Translucent));
            ImGui::Text("Continuation paths / shadow tasks: %u / %u",
                  DxrRenderer::GetWavefrontSecondaryPathCount(),
                  DxrRenderer::GetWavefrontShadowTaskCount());
            ImGui::Text("Continuation diffuse / specular / transmission: %u / %u / %u",
                  DxrRenderer::GetWavefrontSecondaryDiffuseCount(),
                  DxrRenderer::GetWavefrontSecondarySpecularCount(),
                  DxrRenderer::GetWavefrontSecondaryTransmissionCount());
            ImGui::Text("Indirect visibility records / hits / misses: %u / %u / %u",
              DxrRenderer::GetWavefrontSecondaryVisibilityRecordCount(),
              DxrRenderer::GetWavefrontSecondaryVisibilityHitCount(),
              DxrRenderer::GetWavefrontSecondaryVisibilityMissCount());
            ImGui::Text("Indirect visibility diffuse lane / spec lane: %u / %u",
              DxrRenderer::GetWavefrontSecondaryVisibilityDiffuseLaneCount(),
              DxrRenderer::GetWavefrontSecondaryVisibilitySpecularLaneCount());
            ImGui::Text("Indirect resolve records / surfaces / sky: %u / %u / %u",
              DxrRenderer::GetWavefrontSecondaryResolveRecordCount(),
              DxrRenderer::GetWavefrontSecondaryResolveSurfaceCount(),
              DxrRenderer::GetWavefrontSecondaryResolveSkyCount());
            ImGui::Text(
              "Indirect bins diff / glossy / conductor / delta: %u / %u / %u / %u",
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Diffuse),
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::GlossyDielectric),
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Conductor),
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::DeltaReflection));
            ImGui::Text(
              "Indirect bins refract / emissive / translucent: %u / %u / %u",
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Refraction),
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Emissive),
              DxrRenderer::GetWavefrontSecondaryMaterialBinCount(
                DxrRenderer::WavefrontMaterialBin::Translucent));
            ImGui::Text("Shadow visibility tasks / visible / occluded: %u / %u / %u",
              DxrRenderer::GetWavefrontShadowVisibilityTaskCount(),
              DxrRenderer::GetWavefrontShadowVisibleCount(),
              DxrRenderer::GetWavefrontShadowOccludedCount());
          const UINT overflowCount =
              DxrRenderer::GetWavefrontBootstrapOverflowCount();
          if (overflowCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                               "Bootstrap overflow: %u", overflowCount);
          }
          const UINT continuationOverflowCount =
              DxrRenderer::GetWavefrontContinuationOverflowCount();
          const UINT shadowOverflowCount =
              DxrRenderer::GetWavefrontShadowOverflowCount();
          const UINT materialBinOverflowCount =
              DxrRenderer::GetWavefrontMaterialBinOverflowCount();
          if (continuationOverflowCount > 0 || shadowOverflowCount > 0 ||
              materialBinOverflowCount > 0) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                "Queue overflow continuation / shadow / bins: %u / %u / %u",
                continuationOverflowCount, shadowOverflowCount,
                materialBinOverflowCount);
          }
        }

        // Denoising is automatically triggered once when Max SPP is reached
        // (only if a denoiser is selected).

        ImGui::Text("NGX AppId: %u",
                    DX12Context::g_streamline.GetApplicationId());
        if (DX12Context::g_streamline.IsEnabled() &&
            DX12Context::g_streamline.GetMode() !=
                StreamlineManager::Mode::Off &&
            (!DX12Context::g_streamline.IsInitialized() ||
             !DX12Context::g_streamline.IsDeviceSet() ||
             !DX12Context::g_streamline.AreFeatureFunctionsReady())) {
          ImGui::TextWrapped("DLSS plugins may be disabled. If you see "
                             "'Missing NGX context', "
                             "set env SL_APPLICATION_ID (or create "
                             "sl_appid.txt next to the exe) "
                             "to your NVIDIA-provided NGX application id.");
        }

        ImGui::Separator();
        ImGui::Text("Streamline logging (restart required)");
        bool slLogToFile = DX12Context::g_streamline.GetLogToFile();
        if (ImGui::Checkbox("Write sl.log to file", &slLogToFile)) {
          DX12Context::g_streamline.SetLogToFile(slLogToFile);
        }
        bool slMirror = DX12Context::g_streamline.GetMirrorLogsToStderr();
        if (ImGui::Checkbox("Mirror SL logs to console", &slMirror)) {
          DX12Context::g_streamline.SetMirrorLogsToStderr(slMirror);
        }
        if (DX12Context::g_streamline.GetLogToFile()) {
          ImGui::TextWrapped(
              "SL log dir: %ls",
              DX12Context::g_streamline.GetLogDirectory().c_str());
        }
      }

      // Debug Render Pass Dropdown
      const char *debugModes[] = {"None",
                                  "Albedo",
                                  "Normal",
                                  "Emissive",
                                  "Roughness/Glossiness",
                                  "Refl. Color",
                                  "Metalness",
                                  "AO",
                                  "Motion Vectors",
                                  "Spec Hit Distance",
                                  "Spec Motion Vectors",
                                  "Cloud: Slab Mask",
                                  "Cloud: CB Sanity",
                                  "Cloud: Noise Sanity",
                                  "Cloud: Density Sanity",
                                  "Cloud: Opacity (1-T)",
                                  "Cloud: BaseShape Sanity",
                                  "Debug: Accum Samples (N)",
                                  "Debug: History Validity",
                                  "Debug: Per-Pixel Noise",
                                  "Debug: Sample Deficit",
                                  "Debug: Recent Reset Mask",
                                  "Raster: Shadow",
                                  "Raster: Shadow UV/Depth",
                                  "Raster: Shadow Map Depth"};
      if (ImGui::Combo("Debug View", &g_debugMode, debugModes,
                       IM_ARRAYSIZE(debugModes))) {
        // Keep history when switching diagnostics so comparisons are from the
        // same accumulated frame state.
      }

      // Reset accumulation once per window when any UI widget changed
      if (uiChanged) {
        DxrRenderer::ResetAccumulation();
      }

      if (ImGui::RadioButton("Fast Raster",
                             g_currentRenderMode == RenderMode::Raster)) {
        g_currentRenderMode = RenderMode::Raster;
      }
      ImGui::SameLine();
      if (!g_rayTracingSupported) {
        ImGui::BeginDisabled();
      }
      if (ImGui::RadioButton("DXR", g_currentRenderMode == RenderMode::DXR)) {
        g_currentRenderMode = RenderMode::DXR;
      }
      if (!g_rayTracingSupported) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(DXR unsupported)");
      }
#ifdef _DEBUG
      if (ImGui::Checkbox("Raster: Show UV (debug)", &g_rasterDebugUV)) {
        fprintf(stderr, "Raster: ShowUV set=%d\n", g_rasterDebugUV);
        RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                             g_rootSignature.Get());
      }

      if (ImGui::Checkbox("Raster: Wireframe / No Cull (debug)",
                          &g_rasterWireframe)) {
        fprintf(stderr, "Raster: Wireframe set=%d\n", g_rasterWireframe);
        RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                             g_rootSignature.Get());
      }

      if (ImGui::Checkbox("Raster: Debug Depth (shader)",
                          &g_rasterDebugDepth)) {
        fprintf(stderr, "Raster: DebugDepth set=%d\n", g_rasterDebugDepth);
        RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                             g_rootSignature.Get());
      }
#else
      g_rasterDebugUV = false;
      g_rasterWireframe = false;
      g_rasterDebugDepth = false;
#endif

      ImGui::Separator();
      ImGui::TextWrapped(
          "Raster: Fast scene traversal\nDXR: Unified Ray/Path Tracing");
      ImGui::Separator();
      // Display smoothed FPS computed each frame
      if (fps > 0.0f) {
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);
        ImGui::Text("CPU Work Time: %.2f ms", DxrRenderer::GetCPUWorkTimeMs());
        ImGui::Text("GPU Frame Time: %.2f ms",
                    DxrRenderer::GetGPUFrameTimeMs());
        ImGui::Text("Full Frame Time: %.2f ms", DxrRenderer::GetFrameTimeMs());
      } else {
        ImGui::Text("FPS: N/A");
      }

      // Display profiling info
      ImGui::Separator();
      ImGui::Text("Profiling");
      ImGui::Text("FPS: %.1f", DxrRenderer::GetFPS());
      ImGui::Text("SPP/s: %.1f", DxrRenderer::GetSPPPerSec());

      float restirTime, dispatchTime, denoiseTime, noiseTime;
      DxrRenderer::GetGPUTimes(restirTime, dispatchTime, denoiseTime,
                               noiseTime);
      ImGui::Text("GPU Times:");
      ImGui::Text("  ReSTIR: %.2f ms", restirTime);
      ImGui::Text("  DispatchRays: %.2f ms", dispatchTime);
      ImGui::Text("  Denoising: %.2f ms", denoiseTime);
      ImGui::Text("  Noise Calc: %.2f ms", noiseTime);

      // Shader instrumentation counters (if available)
      {
        UINT shaderCounters[16] = {0};
        DxrRenderer::GetShaderCounters(shaderCounters,
                                       _countof(shaderCounters));
        ImGui::Text("Shader counters (last frame):");
        ImGui::Text("  TraceRays=%u  Shadow=%u  Spec=%u", shaderCounters[0],
                    shaderCounters[1], shaderCounters[2]);
        ImGui::Text(
            "  TexSamples=%u  VertexFetches=%u  ResReads=%u  ResWrites=%u",
            shaderCounters[5], shaderCounters[4], shaderCounters[6],
            shaderCounters[7]);
      }
    }
    ImGui::End();
  }
  // (default ground plane will be added at startup)

  // Asset loader UI
  // Provide a persistent 'open' toggle and ensure the window starts
  // a reasonable size. Previously we forced windows to be un-collapsed on
  // first use which prevented honoring imgui.ini; instead read the
  // ini on first frame to derive an initial open/closed state.
  ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);

  static bool s_iniRead = false;
  if (!s_iniRead) {
    s_iniRead = true;
    // First prefer our saved visibility file (written on exit). If it's
    // present, honour it. Otherwise fall back to parsing ImGui's ini file
    // for a best-effort collapsed/open state.
    {
      std::ifstream panelsFile("panels_state.ini");
      if (panelsFile) {
        std::string line;
        while (std::getline(panelsFile, line)) {
          if (line.rfind("Scene=", 0) == 0) {
            g_showAssetsWindow = (atoi(line.c_str() + 6) != 0);
          } else if (line.rfind("RenderMode=", 0) == 0) {
            g_showRenderModeWindow = (atoi(line.c_str() + 11) != 0);
          } else if (line.rfind("RenderSettings=", 0) == 0) {
            g_showRenderSettingsWindow = (atoi(line.c_str() + 15) != 0);
          } else if (line.rfind("MaterialEditor=", 0) == 0) {
            g_showMaterialEditor = (atoi(line.c_str() + 15) != 0);
          } else if (line.rfind("Controls=", 0) == 0) {
            g_showControlsWindow = (atoi(line.c_str() + 9) != 0);
          } else if (line.rfind("Lights=", 0) == 0) {
            g_showLightsWindow = (atoi(line.c_str() + 7) != 0);
          } else if (line.rfind("ReGIRDebug=", 0) == 0) {
            g_showReGIRDebugWindow = (atoi(line.c_str() + 11) != 0);
          }
        }
      } else {
        const char *iniName = io.IniFilename ? io.IniFilename : "imgui.ini";
        std::ifstream ini(iniName);
        if (ini) {
          auto readCollapsed = [&](const std::string &section) -> int {
            ini.clear();
            ini.seekg(0);
            std::string line;
            std::string header = "[Window][" + section + "]";
            while (std::getline((std::istream &)ini, line)) {
              if (line == header) {
                while (std::getline((std::istream &)ini, line)) {
                  if (line.empty() || line[0] == '[')
                    break;
                  const std::string key = "Collapsed=";
                  if (line.rfind(key, 0) == 0) {
                    return atoi(line.c_str() + key.size());
                  }
                }
                break;
              }
            }
            return -1; // not found
          };

          int sceneCollapsed = readCollapsed("Scene");
          if (sceneCollapsed >= 0) {
            g_showAssetsWindow = (sceneCollapsed == 0);
          }
          int renderCollapsed = readCollapsed("Render Mode");
          if (renderCollapsed >= 0) {
            g_showRenderModeWindow = (renderCollapsed == 0);
          }
          int renderSettingsCollapsed = readCollapsed("Render Settings");
          if (renderSettingsCollapsed >= 0) {
            g_showRenderSettingsWindow = (renderSettingsCollapsed == 0);
          }
          int lightsCollapsed = readCollapsed("Global Lights");
          if (lightsCollapsed >= 0) {
            g_showLightsWindow = (lightsCollapsed == 0);
          }
          int regirCollapsed = readCollapsed("regir debug");
          if (regirCollapsed >= 0) {
            g_showReGIRDebugWindow = (regirCollapsed == 0);
          }
        }
      }
    }
  }

  // (SavePanelVisibility implemented below at file scope)

  if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
    g_showMaterialEditor = !g_showMaterialEditor;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    if (IsPreviewRenderActive()) {
        CancelPreviewRender();
    } else {
        Scene::SelectNode((size_t)-1);
    }
  }

  if (g_showAssetsWindow) {
    // Scene panel handled by Scene module
    Scene::DrawScenePanel(g_hwnd, g_showAssetsWindow);
  }
  if (g_showLightsWindow) {
    Scene::DrawLightsPanel(g_showLightsWindow);
  }
  if (g_showMaterialEditor) {
    MaterialEditor::Draw(g_hwnd, g_showMaterialEditor);
  }
  if (g_showReGIRDebugWindow) {
    DrawReGIRDebugPanel();
  }

  Scene::DrawGizmo();
  Scene::DrawLightGizmo();

  // fprintf(stderr, "MainLoop: ImGui::Render start\n");
  ImGui::Render();
  // fprintf(stderr, "MainLoop: ImGui::Render done\n");
}

// Save panel visibility to a small file so we can restore whether windows
// were open or closed across runs. ImGui does not persist an explicit
// "visible" flag, so we maintain this separately.
void SavePanelVisibility() {
  const char *fileName = "panels_state.ini";
  FILE *f = fopen(fileName, "w");
  if (!f)
    return;
  fprintf(f, "[PanelVisibility]\n");
  fprintf(f, "Scene=%d\n", g_showAssetsWindow ? 1 : 0);
  fprintf(f, "RenderMode=%d\n", g_showRenderModeWindow ? 1 : 0);
  fprintf(f, "RenderSettings=%d\n", g_showRenderSettingsWindow ? 1 : 0);
  fprintf(f, "MaterialEditor=%d\n", g_showMaterialEditor ? 1 : 0);
  fprintf(f, "Controls=%d\n", g_showControlsWindow ? 1 : 0);
  fprintf(f, "Lights=%d\n", g_showLightsWindow ? 1 : 0);
  fprintf(f, "ReGIRDebug=%d\n", g_showReGIRDebugWindow ? 1 : 0);
  fclose(f);
}
