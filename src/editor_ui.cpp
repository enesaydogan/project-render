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
#include "material_editor.h"
#include "oidn_denoiser.h"
#include "raster_renderer.h"
#include "scene.h"
#include "scene_io.h"
#include "streamline_manager.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
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
extern bool g_dxrDebugUV;
extern bool g_rasterDebugUV;
extern bool g_rasterWireframe;
extern bool g_rasterDebugDepth;
extern ComPtr<ID3D12RootSignature> g_rootSignature;
extern CameraCB g_cameraData;
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
std::string g_renderExportStatus;

bool g_showRenderModeWindow = false;
bool g_showAssetsWindow = false;
bool g_showMaterialEditor = false;
bool g_showControlsWindow = false;
bool g_forceUncollapse = false;

int g_debugMode = 0; // 0=None, 1=Albedo, 2=Normal, 3=Emissive, ...

// ── helper lambdas/functions that were local to WinMain ─────────────────────

static DxrRenderer::DenoiserMode DenoiserModeFromIndex(int idx) {
  if (idx == 1)
    return DxrRenderer::DenoiserMode::OIDN_CPU;
  if (idx == 2)
    return DxrRenderer::DenoiserMode::OIDN_GPU;
  return DxrRenderer::DenoiserMode::Off;
}

static int DenoiserIndexFromMode(DxrRenderer::DenoiserMode mode) {
  switch (mode) {
  case DxrRenderer::DenoiserMode::OIDN_CPU:
    return 1;
  case DxrRenderer::DenoiserMode::OIDN_GPU:
    return 2;
  default:
    return 0;
  }
}

static bool RecreateDxrPipelineSafe(UINT width, UINT height,
                                    const char *context);

// Forward declarations for helpers also used by the export job logic in
// main.cpp

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

void RestoreRenderExportState() {
  if (!g_renderExportJob.active) {
    return;
  }

  g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
  g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
  g_cameraData.useAdaptiveSampling = g_renderExportJob.previousAdaptiveSampling;
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
  if (!RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                               "RestoreRenderExportState")) {
    g_currentRenderMode = RenderMode::Raster;
  }
  DxrRenderer::ResetAccumulation();
  g_renderExportJob.active = false;
  g_renderExportJob.completionArmed = false;
  g_renderExportJob.completionFrames = 0;
  g_renderExportJob.settleFramesRemaining = 0;
  g_exportRenderTarget.Reset();
  g_exportRtvHeap.Reset();
  g_exportRenderTargetWidth = 0;
  g_exportRenderTargetHeight = 0;
  g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
  UpdateCameraCB();
}

void StartRenderExportJob(const std::wstring &outputPath) {
  if (g_renderExportJob.active || outputPath.empty() ||
      !g_rayTracingSupported) {
    return;
  }

  int presetIndex = g_renderExportSettings.resolutionPreset;
  if (presetIndex < 0 || presetIndex >= g_renderResolutionPresetCount) {
    presetIndex = 0;
  }

  g_renderExportJob.active = true;
  g_renderExportJob.outputPath = outputPath;
  g_renderExportJob.targetWidth = g_renderResolutionPresets[presetIndex].width;
  g_renderExportJob.targetHeight =
      g_renderResolutionPresets[presetIndex].height;
  g_renderExportJob.targetMaxSpp =
      (g_renderExportSettings.maxSpp < 1) ? 1 : g_renderExportSettings.maxSpp;
  g_renderExportJob.targetNoiseThreshold =
      (g_renderExportSettings.noisePercent <= 0.0f)
          ? 0.001f
          : (g_renderExportSettings.noisePercent / 100.0f);
  g_renderExportJob.minSppBeforeNoiseStop =
      (g_renderExportJob.targetMaxSpp < 32)
          ? (UINT)g_renderExportJob.targetMaxSpp
          : 32u;
  if (g_renderExportJob.minSppBeforeNoiseStop < 8u) {
    g_renderExportJob.minSppBeforeNoiseStop = 8u;
  }
  g_renderExportJob.completionArmed = false;
  g_renderExportJob.completionFrames = 0;
  g_renderExportJob.settleFramesRemaining = 0;
  if (g_renderExportSettings.denoiserIndex < 0 ||
      g_renderExportSettings.denoiserIndex > 2) {
    g_renderExportSettings.denoiserIndex = 0;
  }
  g_renderExportJob.previousMode = g_currentRenderMode;
  g_renderExportJob.previousMaxSpp = g_cameraData.maxSPP;
  g_renderExportJob.previousNoiseThreshold = g_cameraData.noiseThreshold;
  g_renderExportJob.previousAdaptiveSampling = g_cameraData.useAdaptiveSampling;
  g_renderExportJob.previousDenoiserIndex =
      DenoiserIndexFromMode(DxrRenderer::GetDenoiserMode());

  // Save Streamline/DLSS state so we can disable it for the export (this
  // allows noise statistics to be computed even when DLSS-RR would normally
  // bypass accumulation). We'll restore these in RestoreRenderExportState().
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
  g_cameraData.useAdaptiveSampling = 1.0f;
  DxrRenderer::SetDenoiserMode(
      DenoiserModeFromIndex(g_renderExportSettings.denoiserIndex));

  if (!EnsureExportRenderTarget(g_renderExportJob.targetWidth,
                                g_renderExportJob.targetHeight)) {
    g_renderExportStatus = "Failed to allocate export render target.";
    g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
    g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
    g_cameraData.useAdaptiveSampling =
        g_renderExportJob.previousAdaptiveSampling;
    DxrRenderer::SetDenoiserMode(
        DenoiserModeFromIndex(g_renderExportJob.previousDenoiserIndex));
    g_currentRenderMode = g_renderExportJob.previousMode;
    // Restore Streamline/DLSS state if we disabled it earlier
    DX12Context::g_streamline.SetMode(
        (StreamlineManager::Mode)g_renderExportJob.previousStreamlineMode);
    DX12Context::g_streamline.SetQuality(
        (StreamlineManager::Quality)
            g_renderExportJob.previousStreamlineQuality);
    DX12Context::g_streamline.SetEnabled(
        g_renderExportJob.previousStreamlineEnabled);
    g_renderExportJob.active = false;
    UpdateCameraCB();
    return;
  }

    if (!RecreateDxrPipelineSafe(g_renderExportJob.targetWidth,
                   g_renderExportJob.targetHeight,
                   "StartRenderExportJob")) {
    g_renderExportStatus = "Failed to create DXR pipeline for export.";
    g_cameraData.maxSPP = g_renderExportJob.previousMaxSpp;
    g_cameraData.noiseThreshold = g_renderExportJob.previousNoiseThreshold;
    g_cameraData.useAdaptiveSampling =
      g_renderExportJob.previousAdaptiveSampling;
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
    UpdateCameraCB();
    return;
    }
  DxrRenderer::ResetAccumulation();
  UpdateCameraCB();

  g_renderExportStatus = "Rendering...";
  fprintf(stderr,
          "Render export started: %ux%u, maxSPP=%d, noise=%.3f, "
          "denoiser=%d\n",
          g_renderExportJob.targetWidth, g_renderExportJob.targetHeight,
          g_renderExportJob.targetMaxSpp,
          g_renderExportJob.targetNoiseThreshold,
          g_renderExportSettings.denoiserIndex);
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
    WaitGPUIdle();
    DxrRenderer::CreateRayTracingPipeline(width, height);
    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "DXR pipeline recreate failed (%s): %s\n",
            context ? context : "unknown", e.what());
  } catch (...) {
    fprintf(stderr,
            "DXR pipeline recreate failed (%s): unknown exception\n",
            context ? context : "unknown");
  }
  return false;
}

// ── Main UI draw function ───────────────────────────────────────────────────

void DrawEditorUI(float fps, float &timeOfDay, float &northOffset,
                  float &latitudeDeg, float &dayOfYear) {
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

  // Draw export preview directly into the main viewport (scaled to fit),
  // so users can track render progress without opening a separate panel.
  if (g_renderExportJob.active && g_exportPreviewSrvGpu.ptr != 0 &&
      g_renderExportJob.targetHeight > 0) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    if (vp) {
      const float srcAspect = (float)g_renderExportJob.targetWidth /
                              (float)g_renderExportJob.targetHeight;
      float drawW = vp->Size.x;
      float drawH = drawW / srcAspect;
      if (drawH > vp->Size.y) {
        drawH = vp->Size.y;
        drawW = drawH * srcAspect;
      }
      if (drawW > 0.0f && drawH > 0.0f) {
        const ImVec2 pMin(vp->Pos.x + (vp->Size.x - drawW) * 0.5f,
                          vp->Pos.y + (vp->Size.y - drawH) * 0.5f);
        const ImVec2 pMax(pMin.x + drawW, pMin.y + drawH);
        ImGui::GetBackgroundDrawList()->AddImage(
            (ImTextureID)g_exportPreviewSrvGpu.ptr, pMin, pMax);
      }
    }
  }

  // Main menu bar: Window menu + quick panel toggles on the bar for fast
  // access
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save Scene...")) {
        std::wstring chosen;
        if (SaveSceneFileDialog(g_hwnd, chosen)) {
          std::string utf8 = WStringToUtf8(chosen);
          if (SceneIO::SaveScene(utf8)) {
            fprintf(stderr, "Scene saved to %s\n", utf8.c_str());
          } else {
            fprintf(stderr, "Failed to save scene to %s\n", utf8.c_str());
          }
        }
      }
      if (ImGui::MenuItem("Load Scene...")) {
        std::wstring chosen;
        if (OpenSceneFileDialog(g_hwnd, chosen)) {
          std::string utf8 = WStringToUtf8(chosen);
          if (SceneIO::LoadScene(utf8)) {
            fprintf(stderr, "Scene loaded from %s\n", utf8.c_str());
          } else {
            fprintf(stderr, "Failed to load scene from %s\n", utf8.c_str());
          }
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit", "Alt+F4")) {
        g_appClosing = true;
      }
      ImGui::EndMenu();
    }

    // Keep Window menu for non-toggle commands

    // Quick access toggles (side-by-side) for panels
    ImGui::SameLine();
    ImGui::Text("Panels:");
    ImGui::SameLine();
    // Use compact spacing for menu bar toggles
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    if (ImGui::BeginMenu("Clouds")) {
      CloudParams &cp = g_cloudManager.GetParams();
      bool changed = false;

      if (ImGui::Checkbox("Enable Cloud Rendering", &g_cloudRenderingEnabled)) {
        changed = true;
      }
      ImGui::Separator();

      if (ImGui::Button("Reset to Defaults")) {
        g_cloudManager.ResetToDefaults();
        changed = true;
      }
      ImGui::Separator();

      changed |= ImGui::SliderFloat("Density", &cp.density, 0.0f, 5.0f);
      changed |= ImGui::SliderFloat("Absorption", &cp.absorption, 0.0f, 2.0f);
      changed |= ImGui::SliderFloat("Coverage", &cp.coverage, 0.0f, 1.0f);
      changed |=
          ImGui::SliderFloat("Scattering (g)", &cp.scattering, -0.99f, 0.99f);
      changed |= ImGui::SliderInt("Steps", &cp.steps, 16, 128);
      changed |=
          ImGui::SliderFloat("Sun Intensity", &cp.sunIntensity, 0.0f, 20.0f);
      changed |= ImGui::SliderFloat("Cloud Top", &cp.cloudTop, 200.0f, 1000.0f);
      changed |=
          ImGui::SliderFloat("Cloud Bottom", &cp.cloudBottom, 50.0f, 300.0f);
      changed |= ImGui::SliderFloat("Wind Speed", &cp.windSpeed, 0.0f, 50.0f);

      ImGui::Separator();
      changed |=
          ImGui::SliderFloat("Base Scale", &cp.baseScale, 0.0001f, 0.0020f,
                             "%.5f", ImGuiSliderFlags_Logarithmic);
      changed |=
          ImGui::SliderFloat("Detail Scale", &cp.detailScale, 0.0005f, 0.01f,
                             "%.5f", ImGuiSliderFlags_Logarithmic);
      changed |=
          ImGui::SliderFloat("Coverage Scale", &cp.coverageScale, 0.00005f,
                             0.0010f, "%.5f", ImGuiSliderFlags_Logarithmic);
      changed |= ImGui::SliderFloat("Erosion", &cp.erosion, 0.0f, 1.0f);
      changed |=
          ImGui::SliderFloat("Warp Strength", &cp.warpStrength, 0.0f, 2.0f);

      ImGui::Separator();
      changed |= ImGui::SliderInt("Shadow Steps", &cp.shadowSteps, 1, 16);
      changed |= ImGui::SliderFloat("Shadow Step Size", &cp.shadowStepSize,
                                    10.0f, 500.0f);
      changed |= ImGui::SliderFloat("Shadow LOD", &cp.shadowLod, 0.0f, 5.0f);
      changed |= ImGui::SliderInt("Max Ray Steps", &cp.maxSteps, 64, 2048);
      changed |= ImGui::SliderFloat("Vertical Step (m)", &cp.verticalStepMeters,
                                    2.0f, 80.0f);
      changed |=
          ImGui::SliderInt("Shadow Every N Steps", &cp.shadowEvery, 1, 16);
      changed |= ImGui::SliderFloat("Shadow Density Threshold",
                                    &cp.shadowDensityThreshold, 0.0f, 0.5f);

      if (changed) {
        DxrRenderer::ResetAccumulation();
      }
      ImGui::EndMenu();
    }

    ImGui::Checkbox("##AssetsToggle", &g_showAssetsWindow);
    ImGui::SameLine();
    ImGui::Text("Assets");
    ImGui::SameLine();
    ImGui::Checkbox("##ControlsToggle", &g_showControlsWindow);
    ImGui::SameLine();
    ImGui::Text("Controls");
    ImGui::SameLine();
    ImGui::Checkbox("##RenderModeToggle", &g_showRenderModeWindow);
    ImGui::SameLine();
    ImGui::Text("Render Mode");
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

      ImGui::SliderInt("Max SPP", &g_renderExportSettings.maxSpp, 16, 4096);
      ImGui::SliderFloat("Noise %", &g_renderExportSettings.noisePercent, 0.1f,
                         30.0f, "%.2f%%");

      const char *denoisers[] = {"Off", "OIDN (CPU)", "OIDN (GPU)"};
      ImGui::Combo("Denoiser", &g_renderExportSettings.denoiserIndex, denoisers,
                   IM_ARRAYSIZE(denoisers));

      if (g_renderExportJob.active) {
        const UINT spp = DxrRenderer::GetDisplayedSampleCount();
        const float noise = DxrRenderer::GetCurrentNoiseLevel();
        const bool denoiserEnabled =
            (g_renderExportSettings.denoiserIndex != 0);
        ImGui::Separator();
        ImGui::Text("Progress: %u / %d SPP", spp,
                    g_renderExportJob.targetMaxSpp);
        ImGui::Text("Output: %u x %u", g_renderExportJob.targetWidth,
                    g_renderExportJob.targetHeight);
        if (noise > 0.0f) {
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
            DxrRenderer::SetPhysicalCameraSettings(400.0f, 1.0f / 30.0f,
                                                   2.8f);
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
            shutterSeconds = (std::clamp)(shutterSeconds, 1.0f / 8000.0f, 30.0f);
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
            ImGui::Text("~ 1/%.0f s", 1.0f / (std::max)(shutterSeconds, 1.0e-6f));
          }

          if (ImGui::SliderFloat("Aperture (f/N)", &aperture, 1.0f, 22.0f,
                                 "f/%.1f")) {
            DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds,
                                                   aperture);
            uiChanged = true;
          }

          ImGui::Text("Camera EV100: %.2f", DxrRenderer::GetPhysicalCameraEV100());
        } else {
          if (autoExp) {
            ImGui::BeginDisabled();
          }
          if (ImGui::SliderFloat("Manual Exposure Scale", &g_cameraData.intensity,
                                 0.0001f, 2.0f, "%.4f",
                                 ImGuiSliderFlags_Logarithmic)) {
            UpdateCameraCB();
            uiChanged = true;
          }
          ImGui::TextDisabled(
              "Affects only camera exposure (post-lighting multiplier), not sun/sky light power.");
          if (autoExp) {
            ImGui::EndDisabled();
          }
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
        if (ImGui::SliderFloat("Sky Intensity", &skyInt, 0.0f, 10.0f)) {
          IBLManager::Get().SetSkyIntensity(skyInt);
          uiParamChanged = true;
          uiChanged = true;
        }
        float sunInt = IBLManager::Get().GetSunIntensity();
        if (ImGui::SliderFloat("Sun Intensity (Lux)", &sunInt, 0.0f, 150000.0f,
                               "%.0f Lux")) {
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
        if (ImGui::SliderFloat("Latitude (deg)", &latitudeDeg, -66.5f,
                               66.5f)) {
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
      if (ImGui::ColorEdit3("Ambient Color", g_cameraData.ambientColor)) {
        UpdateCameraCB();
        uiChanged = true;
      }
      if (ImGui::SliderFloat("Ambient Weight", &g_cameraData.ambientColor[3],
                             0.0f, 1.0f)) {
        UpdateCameraCB();
        uiChanged = true;
      }
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
        if (currentNoise > 0.0f) {
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
          bool viz = g_cameraData.debugVisualizationMode > 0.5f;
          if (ImGui::Checkbox("Show Noise Map (Debug)", &viz)) {
            g_cameraData.debugVisualizationMode = viz ? 1.0f : 0.0f;
            UpdateCameraCB();
            uiChanged = true;
          }

          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("White = High Noise (10%+), Black = Low Noise");
#else
          g_cameraData.debugVisualizationMode = 0.0f;
#endif
        }

        ImGui::Separator();
        ImGui::Text("Streamline / DLSS");
        bool dlssEnabled = DX12Context::g_streamline.IsEnabled();
        if (ImGui::Checkbox("Enable", &dlssEnabled)) {
          DX12Context::g_streamline.SetEnabled(dlssEnabled);
          DxrRenderer::ResetStreamlineHistory();
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
          DxrRenderer::ResetStreamlineHistory();
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
          DxrRenderer::ResetStreamlineHistory();
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

        // Denoiser selection
        const char *denoisers[] = {"Off", "OIDN (CPU)", "OIDN (GPU)"};
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
        }
        if (ImGui::Combo("Denoiser", &denoiserIdx, denoisers,
                         IM_ARRAYSIZE(denoisers))) {
          DxrRenderer::DenoiserMode newMode = DxrRenderer::DenoiserMode::Off;
          if (denoiserIdx == 1)
            newMode = DxrRenderer::DenoiserMode::OIDN_CPU;
          if (denoiserIdx == 2)
            newMode = DxrRenderer::DenoiserMode::OIDN_GPU;
          DxrRenderer::SetDenoiserMode(newMode);
          // Recreate pipeline/resources to account for any mode-specific
          // resources and reset accumulation for stable rendering.
          DxrRenderer::ResetAccumulation();
          RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                  "Denoiser mode change");
        }

        if (DxrRenderer::GetDenoiserMode() != DxrRenderer::DenoiserMode::Off) {
          const char *oidnQualities[] = {"Fast", "Balanced", "High"};
          int qualIdx = (int)DxrRenderer::GetOidnQuality();
          if (ImGui::Combo("OIDN Quality", &qualIdx, oidnQualities,
                           IM_ARRAYSIZE(oidnQualities))) {
            DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)qualIdx);
            uiChanged = true;
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
                                  "Debug: Recent Reset Mask"};
#ifdef _DEBUG
      if (ImGui::Combo("Debug View", &g_debugMode, debugModes,
                       IM_ARRAYSIZE(debugModes))) {
        // Keep history when switching diagnostics so comparisons are from the
        // same accumulated frame state.
      }
#else
      g_debugMode = 0;
#endif

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
      // DXR debug: show UV output from RayGen
      if (ImGui::Checkbox("DXR: Show RayGen UV (debug)", &g_dxrDebugUV)) {
        // Recreate pipeline with debug define; reinitializing RT pipeline
        RecreateDxrPipelineSafe(g_windowWidth, g_windowHeight,
                                "DXR debug UV toggle");
      }
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
      g_dxrDebugUV = false;
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
          } else if (line.rfind("MaterialEditor=", 0) == 0) {
            g_showMaterialEditor = (atoi(line.c_str() + 15) != 0);
          } else if (line.rfind("Controls=", 0) == 0) {
            g_showControlsWindow = (atoi(line.c_str() + 9) != 0);
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
        }
      }
    }
  }

  // (SavePanelVisibility implemented below at file scope)

  if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
    g_showMaterialEditor = !g_showMaterialEditor;
  }

  if (g_showAssetsWindow) {
    // Scene panel handled by Scene module
    Scene::DrawScenePanel(g_hwnd, g_showAssetsWindow);
  }
  if (g_showMaterialEditor) {
    MaterialEditor::Draw(g_hwnd, g_showMaterialEditor);
  }

  Scene::DrawGizmo();

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
  fprintf(f, "MaterialEditor=%d\n", g_showMaterialEditor ? 1 : 0);
  fprintf(f, "Controls=%d\n", g_showControlsWindow ? 1 : 0);
  fclose(f);
}
