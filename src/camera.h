#pragma once
#include <d3d12.h>
#include <windows.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;

struct CameraCB {
  float pos[3];
  float debugMode; // used to be _pad0
  float forward[3];
  float _pad1;
  float up[3];
  float _pad2;
  float fov;
  float aspect;
  float nearZ;
  float farZ;
  float intensity;
  float frameCount;
  float lightCount;
  float maxSpecularBounces;
  float maxRefractiveBounces;
  float maxGIBounces;
  float maxSPP;
  float _pad3;

  // Global Scene Lighting
  float lightDir[4];     // xyz = direction pointing TO light, w = unused
  float lightColor[4];   // rgb + intensity in .w
  float ambientColor[4]; // rgb + weight in .w
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
