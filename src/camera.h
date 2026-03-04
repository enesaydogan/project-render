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
  float accumulationCount;

  // Global Scene Lighting
  float lightDir[4];     // xyz = direction pointing TO light, w = unused
  float lightColor[4];   // rgb + intensity in .w

  // --- Streamline / DLSS history support ---
  // These fields must NOT participate in accumulation reset decisions.
  float prevPos[3];
  float prevValid;
  float prevForward[3];
  float dlssEnabled;
  float prevUp[3];
  float dlssRayReconstruction;
  float prevFov;
  float prevAspect;
  float prevNearZ;
  float prevFarZ;
  float noiseThreshold;
  float useAdaptiveSampling;
  float debugVisualizationMode;
  float cloudRenderingEnabled;
  float iblRotationDegrees;
  // When true we build/sample the environment CDF using solid-angle weighting
  // (luminance * sin(theta)) rather than raw texel area. Turning this off can
  // be useful for debugging or comparing the two approaches.
  float sampleEnvSolidAngle;
  float nrdEnabled;
  float _cameraPadEnd;
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
extern bool g_cloudRenderingEnabled;
extern float g_cameraTarget[3];
extern float g_cameraTargetDistance;

// Reset camera to initial configuration
void ResetCamera();

// Update the GPU camera constant buffer if present
void UpdateCameraCB();
