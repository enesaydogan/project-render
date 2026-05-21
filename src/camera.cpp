#include "camera.h"
#include "d3d12_helpers.h"
#include "editor_ui.h"
#include <algorithm>
#include <math.h>

// Forward declare for accumulation reset
namespace DxrRenderer {
void ResetAccumulation();
}

// Initialize camera defaults (kept consistent with previous main.cpp values)
CameraCB g_initialCameraData = {
    {-3.75f, 3.43f, 3.78f},         // pos
    0.0f,                           // debugMode
    {0.64f, -0.40f, -0.65f},        // forward
    0.0f,                           // _pad1
    {0.0f, 1.0f, 0.0f},             // up
    0.0f,                           // _pad2
    45.0f,                          // fov
    1.86f,                          // aspect
    0.1f,                           // nearZ
    1000.0f,                        // farZ
    0.02f,                          // intensity (manual exposure scale default)
    0.0f,                           // frameCount
    0.0f,                           // lightCount
    3.0f,                           // maxSpecularBounces
    3.0f,                           // maxRefractiveBounces
    2.0f,                           // maxGIBounces
    200.0f,                         // maxSPP
    0.0f,                           // accumulationCount
    {0.707f, 0.707f, 0.0f, 0.0f},   // lightDir (45 deg)
    {1.0f, 0.95f, 0.8f, 100000.0f}, // lightColor (rgb + intensity in .w)
    {0.0f, 0.0f, 0.0f},             // prevPos
    0.0f,                           // prevValid
    {0.0f, 0.0f, 0.0f},             // prevForward
    0.0f,                           // dlssEnabled
    {0.0f, 0.0f, 0.0f},             // prevUp
    0.0f,                           // dlssRayReconstruction
    0.0f,                           // prevFov
    0.0f,                           // prevAspect
    0.0f,                           // prevNearZ
    0.0f,                           // prevFarZ
    0.05f,                          // noiseThreshold (5%)
    1.0f,                           // useAdaptiveSampling (default ON)
    0.0f,                           // debugVisualizationMode
    1.0f,                           // cloudRenderingEnabled (default ON)
    0.0f,                           // iblRotationDegrees
    1.0f,                           // sampleEnvSolidAngle (default true)
    0.0f,                           // exportRendering
    1.0f,                           // dxrProceduralSkyBoost
    1.0f,                           // iblIndirectBoost
    0.0f,                           // tonemapAoIntensity
    0.25f,                          // tonemapAoRadiusMeters
    2.0f,                           // tonemapAoMode (Both)
    0.0f,                           // triPlanarWorldRotationDegrees
    0.0f                            // dxrFeatureFlags
};
CameraCB g_cameraData = g_initialCameraData;
ComPtr<ID3D12Resource> g_cameraConstantBuffer;
float g_camYaw = 0.0f;
float g_camPitch = 0.0f;
float g_camSpeed = 0.8f;           // units/sec
float g_mouseSensitivity = 0.002f; // radians per pixel
POINT g_prevMousePos = {0, 0};
bool g_mouseCaptured = false;
bool g_safeFrameEnabled = false;
float g_cameraTarget[3] = {0.0f, 0.0f, 0.0f};
float g_cameraTargetDistance = 1.0f;

static CameraCB s_lastCameraData = {};
static CameraCB s_prevFrameCameraData = {};
static bool s_prevFrameValid = false;

static void ResolveSafeFrameTargetSize(UINT &width, UINT &height) {
  if (g_renderExportJob.active && g_renderExportJob.targetWidth > 0 &&
      g_renderExportJob.targetHeight > 0) {
    width = g_renderExportJob.targetWidth;
    height = g_renderExportJob.targetHeight;
    return;
  }

  if (g_renderResolutionPresetCount > 0) {
    const int presetIndex =
        (std::clamp)(g_renderExportSettings.resolutionPreset, 0,
                     g_renderResolutionPresetCount - 1);
    width = g_renderResolutionPresets[presetIndex].width;
    height = g_renderResolutionPresets[presetIndex].height;
    return;
  }

  width = 1920;
  height = 1080;
}

static bool CameraChanged(const CameraCB &a, const CameraCB &b) {
  if (a.pos[0] != b.pos[0] || a.pos[1] != b.pos[1] || a.pos[2] != b.pos[2])
    return true;
  if (a.forward[0] != b.forward[0] || a.forward[1] != b.forward[1] ||
      a.forward[2] != b.forward[2])
    return true;
  if (a.up[0] != b.up[0] || a.up[1] != b.up[1] || a.up[2] != b.up[2])
    return true;
  const bool ignoreAspectChange = g_renderExportJob.active;
  if (a.fov != b.fov || (!ignoreAspectChange && a.aspect != b.aspect) ||
      a.nearZ != b.nearZ ||
      a.farZ != b.farZ)
    return true;
  if (a.debugMode != b.debugMode ||
      a.debugVisualizationMode != b.debugVisualizationMode ||
      a.maxSpecularBounces != b.maxSpecularBounces ||
      a.maxRefractiveBounces != b.maxRefractiveBounces ||
      a.maxGIBounces != b.maxGIBounces || a.maxSPP != b.maxSPP)
    return true;
  for (int i = 0; i < 4; ++i) {
    if (a.lightDir[i] != b.lightDir[i])
      return true;
    if (a.lightColor[i] != b.lightColor[i])
      return true;
  }
  if (a.iblRotationDegrees != b.iblRotationDegrees)
    return true;
  if (a.sampleEnvSolidAngle != b.sampleEnvSolidAngle)
    return true;
  if (a.dxrProceduralSkyBoost != b.dxrProceduralSkyBoost)
    return true;
  if (a.iblIndirectBoost != b.iblIndirectBoost)
    return true;
  if (a.tonemapAoIntensity != b.tonemapAoIntensity ||
      a.tonemapAoRadiusMeters != b.tonemapAoRadiusMeters ||
      a.tonemapAoMode != b.tonemapAoMode)
    return true;
  if (a.triPlanarWorldRotationDegrees != b.triPlanarWorldRotationDegrees)
    return true;
  if (a.dxrFeatureFlags != b.dxrFeatureFlags)
    return true;
  return false;
}

void ResetCamera() {
  const float triPlanarWorldRotationDegrees =
      g_cameraData.triPlanarWorldRotationDegrees;
  g_cameraData = g_initialCameraData;
  g_cameraData.triPlanarWorldRotationDegrees = triPlanarWorldRotationDegrees;
  // Initialize yaw/pitch from forward so mouse-look feels consistent after
  // reset
  float fx = g_cameraData.forward[0];
  float fy = g_cameraData.forward[1];
  float fz = g_cameraData.forward[2];
  // yaw = atan2(fx, -fz) ; pitch = asin(fy)
  g_camYaw = atan2f(fx, -fz);
  g_camPitch = asinf(fy);
}

void UpdateCameraCB() {
  if (!g_cameraConstantBuffer)
    return;

  // Provide previous-frame camera info for motion vectors / DLSS, but do not
  // use it for accumulation reset.
  g_cameraData.prevPos[0] = s_prevFrameCameraData.pos[0];
  g_cameraData.prevPos[1] = s_prevFrameCameraData.pos[1];
  g_cameraData.prevPos[2] = s_prevFrameCameraData.pos[2];
  g_cameraData.prevForward[0] = s_prevFrameCameraData.forward[0];
  g_cameraData.prevForward[1] = s_prevFrameCameraData.forward[1];
  g_cameraData.prevForward[2] = s_prevFrameCameraData.forward[2];
  g_cameraData.prevUp[0] = s_prevFrameCameraData.up[0];
  g_cameraData.prevUp[1] = s_prevFrameCameraData.up[1];
  g_cameraData.prevUp[2] = s_prevFrameCameraData.up[2];
  g_cameraData.prevFov = s_prevFrameCameraData.fov;
  g_cameraData.prevAspect = s_prevFrameCameraData.aspect;
  g_cameraData.prevNearZ = s_prevFrameCameraData.nearZ;
  g_cameraData.prevFarZ = s_prevFrameCameraData.farZ;
  g_cameraData.prevValid = s_prevFrameValid ? 1.0f : 0.0f;

  bool changed = CameraChanged(g_cameraData, s_lastCameraData);

  // Persistent mapping for camera CB
  static void *pCam = nullptr;
  if (!pCam && g_cameraConstantBuffer) {
    ThrowIfFailed(g_cameraConstantBuffer->Map(
        0, nullptr, reinterpret_cast<void **>(&pCam)));
  }

  if (pCam) {
    memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
    // No Unmap needed for upload heap persistent mapping.
  }

  if (changed) {
    // Only reset accumulation if movement/settings changed (not just
    // frameCount)
    DxrRenderer::ResetAccumulation();
    s_lastCameraData = g_cameraData;
  }

  // Update prev-frame data for the next frame.
  s_prevFrameCameraData = g_cameraData;
  s_prevFrameValid = true;
}

bool GetSafeFrameTargetSize(UINT &width, UINT &height) {
  ResolveSafeFrameTargetSize(width, height);
  return width > 0 && height > 0;
}

bool GetSafeFramePreviewRect(UINT availableWidth, UINT availableHeight,
                             D3D12_RECT &outRect) {
  outRect.left = 0;
  outRect.top = 0;
  outRect.right = static_cast<LONG>(availableWidth);
  outRect.bottom = static_cast<LONG>(availableHeight);

  if (!g_safeFrameEnabled || g_renderExportJob.active || availableWidth == 0 ||
      availableHeight == 0) {
    return false;
  }

  UINT targetWidth = 0;
  UINT targetHeight = 0;
  if (!GetSafeFrameTargetSize(targetWidth, targetHeight)) {
    return false;
  }

  const double targetAspect = static_cast<double>(targetWidth) /
                              static_cast<double>(targetHeight);
  const double availableAspect = static_cast<double>(availableWidth) /
                                 static_cast<double>(availableHeight);

  UINT previewWidth = availableWidth;
  UINT previewHeight = availableHeight;
  if (availableAspect > targetAspect) {
    previewWidth = (std::max)(1u, static_cast<UINT>(
                                      std::lround(availableHeight * targetAspect)));
  } else if (availableAspect < targetAspect) {
    previewHeight = (std::max)(1u, static_cast<UINT>(
                                       std::lround(availableWidth / targetAspect)));
  }

  const UINT offsetX = (availableWidth - previewWidth) / 2;
  const UINT offsetY = (availableHeight - previewHeight) / 2;
  outRect.left = static_cast<LONG>(offsetX);
  outRect.top = static_cast<LONG>(offsetY);
  outRect.right = static_cast<LONG>(offsetX + previewWidth);
  outRect.bottom = static_cast<LONG>(offsetY + previewHeight);
  return true;
}
