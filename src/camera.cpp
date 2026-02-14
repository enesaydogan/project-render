#include "camera.h"
#include <math.h>

// Forward declare for accumulation reset
namespace DxrRenderer {
void ResetAccumulation();
}

// Initialize camera defaults (kept consistent with previous main.cpp values)
CameraCB g_initialCameraData = {
    {-3.75f, 3.43f, 3.78f},       // pos
    0.0f,                         // debugMode
    {0.64f, -0.40f, -0.65f},      // forward
    0.0f,                         // _pad1
    {0.0f, 1.0f, 0.0f},           // up
    0.0f,                         // _pad2
    45.0f,                        // fov
    1.86f,                        // aspect
    0.1f,                         // nearZ
    1000.0f,                      // farZ
    1.0f,                         // intensity
    0.0f,                         // frameCount
    0.0f,                         // lightCount
    3.0f,                         // maxSpecularBounces
    3.0f,                         // maxRefractiveBounces
    2.0f,                         // maxGIBounces
    200.0f,                       // maxSPP
    0.0f,                         // _pad3
    {0.707f, 0.707f, 0.0f, 0.0f}, // lightDir (45 deg)
    {1.0f, 0.95f, 0.8f, 2.5f},    // lightColor
    {0.2f, 0.3f, 0.4f, 0.15f},    // ambientColor
    {0.0f, 0.0f, 0.0f},           // prevPos
    0.0f,                         // prevValid
    {0.0f, 0.0f, 0.0f},           // prevForward
    0.0f,                         // dlssEnabled
    {0.0f, 0.0f, 0.0f},           // prevUp
    0.0f,                         // dlssRayReconstruction
    0.0f,                         // prevFov
    0.0f,                         // prevAspect
    0.0f,                         // prevNearZ
    0.0f,                         // prevFarZ
    0.05f,                        // noiseThreshold (5%)
    1.0f,                         // useAdaptiveSampling (default ON)
    0.0f,                         // debugVisualizationMode
    1.0f                          // cloudRenderingEnabled (default ON)
};
CameraCB g_cameraData = g_initialCameraData;
ComPtr<ID3D12Resource> g_cameraConstantBuffer;
float g_camYaw = 0.0f;
float g_camPitch = 0.0f;
float g_camSpeed = 0.8f;           // units/sec
float g_mouseSensitivity = 0.002f; // radians per pixel
POINT g_prevMousePos = {0, 0};
bool g_mouseCaptured = false;
float g_cameraTarget[3] = {0.0f, 0.0f, 0.0f};
float g_cameraTargetDistance = 1.0f;

static CameraCB s_lastCameraData = {};
static CameraCB s_prevFrameCameraData = {};
static bool s_prevFrameValid = false;

static bool CameraChanged(const CameraCB &a, const CameraCB &b) {
  if (a.pos[0] != b.pos[0] || a.pos[1] != b.pos[1] || a.pos[2] != b.pos[2])
    return true;
  if (a.forward[0] != b.forward[0] || a.forward[1] != b.forward[1] ||
      a.forward[2] != b.forward[2])
    return true;
  if (a.up[0] != b.up[0] || a.up[1] != b.up[1] || a.up[2] != b.up[2])
    return true;
  if (a.fov != b.fov || a.aspect != b.aspect || a.nearZ != b.nearZ ||
      a.farZ != b.farZ)
    return true;
  if (a.intensity != b.intensity || a.debugMode != b.debugMode ||
      a.maxSpecularBounces != b.maxSpecularBounces ||
      a.maxRefractiveBounces != b.maxRefractiveBounces ||
      a.maxGIBounces != b.maxGIBounces || a.maxSPP != b.maxSPP)
    return true;
  for (int i = 0; i < 4; ++i) {
    if (a.lightDir[i] != b.lightDir[i])
      return true;
    if (a.lightColor[i] != b.lightColor[i])
      return true;
    if (a.ambientColor[i] != b.ambientColor[i])
      return true;
  }
  return false;
}

void ResetCamera() {
  g_cameraData = g_initialCameraData;
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

  // ALWAYS update the GPU buffer because frameCount changes every frame
  UINT8 *pCam = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (SUCCEEDED(g_cameraConstantBuffer->Map(
          0, &readRange, reinterpret_cast<void **>(&pCam)))) {
    memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
    g_cameraConstantBuffer->Unmap(0, nullptr);
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
