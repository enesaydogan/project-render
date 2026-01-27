#pragma once
#include <d3d12.h>
#include <windows.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;

struct CameraCB {
  float pos[4];
  float forward[4];
  float up[4];
  float fov;
  float aspect;
  float nearZ;
  float farZ;
  float intensity;
};

// Camera state (defined in camera.cpp)
extern CameraCB g_initialCameraData;
extern CameraCB g_cameraData;
extern ComPtr<ID3D12Resource> g_cameraConstantBuffer;
extern float g_camYaw;
extern float g_camPitch;
extern float g_camSpeed;
extern float g_mouseSensitivity;
extern POINT g_prevMousePos;
extern bool g_mouseCaptured;
extern float g_cameraTarget[3];
extern float g_cameraTargetDistance;

// Reset camera to initial configuration
void ResetCamera();

// Update the GPU camera constant buffer if present
void UpdateCameraCB();
