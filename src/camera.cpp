#include "camera.h"
#include <math.h>
#include <stdio.h>

// Initialize camera defaults (kept consistent with previous main.cpp values)
CameraCB g_initialCameraData = {{-0.59f, 0.42f, 0.66f, 0.0f}, {0.62f, -0.49f, -0.62f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, 45.0f, 1.86f, 0.1f, 1000.0f, 1.0f};
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
    UINT8 *pCam = nullptr;
    D3D12_RANGE readRange = {0,0};
    if (SUCCEEDED(g_cameraConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCam)))) {
        memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
        g_cameraConstantBuffer->Unmap(0, nullptr);
    }
}
