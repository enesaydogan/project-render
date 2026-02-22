#include "input_handler.h"
#include "camera.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "material_editor.h"
#include "scene.h"
#include <cmath>


// We need access to these globals to manipulate the camera.
// They are likely declared in main.cpp or camera.cpp.
extern HWND g_hwnd;
extern bool g_mouseCaptured;
extern float g_camYaw;
extern float g_camPitch;
extern float g_mouseSensitivity;
extern POINT g_prevMousePos;
extern float g_camSpeed;
extern RenderMode g_currentRenderMode;
extern CameraCB g_cameraData;
extern bool g_verboseRenderLogs; // from main.cpp for debug logging

// Simple Vec3 helper for CPU-side math
struct Vec3 {
  float x, y, z;
};

namespace Input {

void Update(float dt) {
  // Input handling guarded by application focus
  bool appFocused = (GetForegroundWindow() == g_hwnd);

  // Handle mouse rotation when RMB is pressed (only when the app is focused)
  // Use FPS-style capture: confine cursor and recenter each frame for smooth
  // relative motion
  RECT clientRect;
  GetClientRect(g_hwnd, &clientRect);
  POINT centerScreen = {clientRect.right / 2, clientRect.bottom / 2};
  ClientToScreen(g_hwnd, &centerScreen);

  if (appFocused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
    if (!g_mouseCaptured) {
      // Enter capture mode
      SetCursorPos(centerScreen.x, centerScreen.y);
      ShowCursor(FALSE);
      RECT winRect;
      GetWindowRect(g_hwnd, &winRect);
      ClipCursor(&winRect);
      g_mouseCaptured = true;
    }

    POINT curPos;
    GetCursorPos(&curPos);
    int dx = curPos.x - centerScreen.x;
    int dy = curPos.y - centerScreen.y;

    const float sensitivity = g_mouseSensitivity; // radians per pixel
    // Update yaw/pitch directly (FPS-style mouse look)
    g_camYaw += dx * sensitivity;
    g_camPitch -= dy * sensitivity;

    // Clamp pitch to avoid flipping
    const float maxPitch = 3.14159265f * 0.5f - 0.01f;
    if (g_camPitch > maxPitch)
      g_camPitch = maxPitch;
    if (g_camPitch < -maxPitch)
      g_camPitch = -maxPitch;

    // Compute forward from yaw/pitch
    g_cameraData.forward[0] = cosf(g_camPitch) * sinf(g_camYaw);
    g_cameraData.forward[1] = sinf(g_camPitch);
    g_cameraData.forward[2] = cosf(g_camPitch) * -cosf(g_camYaw);

    // Reset accumulation immediately when the camera orientation changes via
    // mouse
    DxrRenderer::ResetAccumulation();

    // Recenter cursor for next delta
    SetCursorPos(centerScreen.x, centerScreen.y);
  } else {
    if (g_mouseCaptured) {
      ShowCursor(TRUE);
      ClipCursor(NULL);
      g_mouseCaptured = false;
    }
    if (appFocused) {
      GetCursorPos(&g_prevMousePos);
      ScreenToClient(g_hwnd, &g_prevMousePos);
    }
  }

  // Movement: WASD (only when app is focused)
  float moveSpeed = g_camSpeed;
  if (appFocused) {
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
      moveSpeed *= 3.0f;
  }

  // Build forward vector for rendering (uses yaw/pitch) and also compute a
  // horizontal-only forward for FPS movement
  Vec3 camF = {g_cameraData.forward[0], g_cameraData.forward[1],
               g_cameraData.forward[2]};
  Vec3 camU = {g_cameraData.up[0], g_cameraData.up[1], g_cameraData.up[2]};

  // rotate forward by yaw/pitch (used for view/rendering)
  {
    float cp = cosf(g_camPitch);
    float sp = sinf(g_camPitch);
    float cy = cosf(g_camYaw);
    float sy = sinf(g_camYaw);
    camF.x = cp * sy;
    camF.y = sp;
    camF.z = cp * -cy;
  }

  // Horizontal FPS movement basis (yaw-only forward)
  Vec3 worldUp = {0.0f, 1.0f, 0.0f};
  Vec3 moveF = {sinf(g_camYaw), 0.0f, -cosf(g_camYaw)};
  // Right vector = cross(moveF, worldUp)
  Vec3 moveR = {moveF.y * worldUp.z - moveF.z * worldUp.y,
                moveF.z * worldUp.x - moveF.x * worldUp.z,
                moveF.x * worldUp.y - moveF.y * worldUp.x};

  // normalize helper
  auto normalize3 = [](Vec3 &v) {
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 0.00001f) {
      v.x /= l;
      v.y /= l;
      v.z /= l;
    }
  };
  normalize3(camF);
  normalize3(moveR);
  normalize3(moveF);

  Vec3 move = {0, 0, 0};
  if (appFocused) {
    // W/S forward/back (horizontal)
    if (GetAsyncKeyState('W') & 0x8000) {
      move.x += moveF.x;
      move.y += moveF.y;
      move.z += moveF.z;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
      move.x -= moveF.x;
      move.y -= moveF.y;
      move.z -= moveF.z;
    }
    // A/D strafing (standard FPS: A=left, D=right)
    if (GetAsyncKeyState('A') & 0x8000) {
      move.x -= moveR.x;
      move.y -= moveR.y;
      move.z -= moveR.z;
    }
    if (GetAsyncKeyState('D') & 0x8000) {
      move.x += moveR.x;
      move.y += moveR.y;
      move.z += moveR.z;
    }
    // Vertical movement: Q up, E down (world up)
    if (GetAsyncKeyState('Q') & 0x8000) {
      move.x += worldUp.x;
      move.y += worldUp.y;
      move.z += worldUp.z;
    }
    if (GetAsyncKeyState('E') & 0x8000) {
      move.x -= worldUp.x;
      move.y -= worldUp.y;
      move.z -= worldUp.z;
    }

    // TAB: Toggle between Raster and Raytracing modes
    static bool tabDown = false;
    if (GetAsyncKeyState(VK_TAB) & 0x8000) {
      if (!tabDown) {
        if (g_currentRenderMode == RenderMode::Raster) {
          g_currentRenderMode = RenderMode::DXR;
          DX12Context::WaitGPUIdle();
          DxrRenderer::CreateRayTracingPipeline(DX12Context::g_windowWidth,
                                                DX12Context::g_windowHeight);
          // ensure acceleration structures are fresh when we switch modes;
          // sometimes the TLAS can be cleared if the scene was momentarily
          // empty, so rebuild here to guarantee IsReady() will succeed.
          Scene::RebuildAccelerationStructures();
          if (g_verboseRenderLogs)
            fprintf(stderr, "InputHandler: switched to DXR, rebuilt TLAS\n");
        } else {
          g_currentRenderMode = RenderMode::Raster;
        }
        tabDown = true;
      }
    } else {
      tabDown = false;
    }

    // Selection: LBUTTON
    static bool lbtnDown = false;
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
      if (!lbtnDown) {
        // Always run selection logic to allow selecting nodes
        int pickedMaterial =
            Scene::UpdateSelection((float)DX12Context::g_windowWidth,
                                   (float)DX12Context::g_windowHeight);

        if (pickedMaterial != -1) {
          // Only switch the Material Editor's active material if the Picking
          // Tool is explicitly enabled
          if (MaterialEditor::IsPickingEnabled()) {
            MaterialEditor::SelectMaterial(pickedMaterial);
            MaterialEditor::SetPickingEnabled(false);
          }
        }
        lbtnDown = true;
      }
    } else {
      lbtnDown = false;
    }
  }

  if (move.x != 0 || move.y != 0 || move.z != 0) {
    normalize3(move);
    g_cameraData.pos[0] += move.x * moveSpeed * dt;
    g_cameraData.pos[1] += move.y * moveSpeed * dt;
    g_cameraData.pos[2] += move.z * moveSpeed * dt;

    // Reset accumulation immediately when the camera position changes via input
    DxrRenderer::ResetAccumulation();
  }
}

} // namespace Input
