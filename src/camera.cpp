#include "camera.h"
#include <math.h>
#include <stdio.h>

// Forward declare for accumulation reset
namespace DxrRenderer { void ResetAccumulation(); }

// Initialize camera defaults (kept consistent with previous main.cpp values)
CameraCB g_initialCameraData = {
    {-3.75f, 3.43f, 3.78f}, 0.0f,
    {0.64f, -0.40f, -0.65f}, 0.0f,
    {0.0f, 1.0f, 0.0f}, 0.0f,
    45.0f, 1.86f, 0.1f, 1000.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    {0.707f, 0.707f, 0.0f, 0.0f}, // lightDir (45 deg)
    {1.0f, 0.95f, 0.8f, 2.5f},    // lightColor
    {0.2f, 0.3f, 0.4f, 0.15f}     // ambientColor
};
CameraCB g_cameraData = g_initialCameraData;
ComPtr<ID3D12Resource> g_cameraConstantBuffer;
float g_camYaw = 0.0f;
float g_camPitch = 0.0f;
float g_camSpeed = 0.8f; // units/sec
float g_mouseSensitivity = 0.002f; // radians per pixel
POINT g_prevMousePos = {0,0};
bool g_mouseCaptured = false;
float g_cameraTarget[3] = {0.0f, 0.0f, 0.0f};
float g_cameraTargetDistance = 1.0f;

static CameraCB s_lastCameraData = {};

static bool CameraChanged(const CameraCB& a, const CameraCB& b) {
    if (a.pos[0] != b.pos[0] || a.pos[1] != b.pos[1] || a.pos[2] != b.pos[2]) return true;
    if (a.forward[0] != b.forward[0] || a.forward[1] != b.forward[1] || a.forward[2] != b.forward[2]) return true;
    if (a.up[0] != b.up[0] || a.up[1] != b.up[1] || a.up[2] != b.up[2]) return true;
    if (a.fov != b.fov || a.aspect != b.aspect || a.nearZ != b.nearZ || a.farZ != b.farZ) return true;
    if (a.intensity != b.intensity || a.debugMode != b.debugMode) return true;
    for(int i=0; i<4; ++i) {
        if (a.lightDir[i] != b.lightDir[i]) return true;
        if (a.lightColor[i] != b.lightColor[i]) return true;
        if (a.ambientColor[i] != b.ambientColor[i]) return true;
    }
    return false;
}

void ResetCamera() {
    g_cameraData = g_initialCameraData;
    // Initialize yaw/pitch from forward so mouse-look feels consistent after reset
    float fx = g_cameraData.forward[0]; float fy = g_cameraData.forward[1]; float fz = g_cameraData.forward[2];
    // yaw = atan2(fx, -fz) ; pitch = asin(fy)
    g_camYaw = atan2f(fx, -fz);
    g_camPitch = asinf(fy);
}

void UpdateCameraCB() {
    if (!g_cameraConstantBuffer) return;
    
    if (CameraChanged(g_cameraData, s_lastCameraData)) {
        UINT8 *pCam = nullptr;
        D3D12_RANGE readRange = {0,0};
        if (SUCCEEDED(g_cameraConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCam)))) {
            memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
            g_cameraConstantBuffer->Unmap(0, nullptr);
        }
        // Any camera update should reset path tracing accumulation
        DxrRenderer::ResetAccumulation();
        s_lastCameraData = g_cameraData;
    }
}
