#pragma once

#include <string>

// Forward declarations for types used by the editor UI
struct RenderResolutionPreset {
  const char *label;
  unsigned int width;
  unsigned int height;
};

struct RenderExportSettings {
  int resolutionPreset = 1;
  int maxSpp = 200;
  float noisePercent = 5.0f;
  int denoiserIndex = 2; // 0=Off, 1=OIDN CPU, 2=OIDN GPU
};

// NOTE: RenderMode is defined in scene.h
#include "scene.h"

struct RenderExportJobState {
  bool active = false;
  bool completionArmed = false;
  int completionFrames = 0;
  int settleFramesRemaining = 0;
  unsigned int minSppBeforeNoiseStop = 32;
  std::wstring outputPath;
  unsigned int targetWidth = 1920;
  unsigned int targetHeight = 1080;
  int targetMaxSpp = 512;
  float targetNoiseThreshold = 0.05f;
  RenderMode previousMode = RenderMode::Raster;
  float previousMaxSpp = 200.0f;
  float previousNoiseThreshold = 0.05f;
  float previousAdaptiveSampling = 1.0f;
  int previousDenoiserIndex = 0;
  // Streamline (DLSS) state saved/restored during export so noise can be
  // calculated even when DLSS-RR is normally active.
  bool previousStreamlineEnabled = false;
  int previousStreamlineMode = 0;
  int previousStreamlineQuality = 1;
};

// Resolution presets (defined in editor_ui.cpp)
extern const RenderResolutionPreset g_renderResolutionPresets[];
extern const int g_renderResolutionPresetCount;

// Editor UI state (defined in editor_ui.cpp)
extern RenderExportSettings g_renderExportSettings;
extern RenderExportJobState g_renderExportJob;
extern std::string g_renderExportStatus;

// Panel visibility toggles (defined in editor_ui.cpp)
extern bool g_showRenderModeWindow;
extern bool g_showAssetsWindow;
extern bool g_showMaterialEditor;
extern bool g_showControlsWindow;
extern bool g_forceUncollapse;

// Debug mode (defined in editor_ui.cpp)
extern int g_debugMode;


// Draw the complete editor UI for one frame.
// Call between ImGui::NewFrame() and ImGui::Render().
// |fps| is the smoothed FPS value computed in the main loop.
// |timeOfDay|, |northOffset|, |latitudeDeg| and |dayOfYear| are references to
// sky/solar parameters.
void DrawEditorUI(float fps, float &timeOfDay, float &northOffset,
                  float &latitudeDeg, float &dayOfYear);

// Helper: start / restore / cancel a render export job
void StartRenderExportJob(const std::wstring &outputPath);
void RestoreRenderExportState();

// Utility
std::string WStringToUtf8(const std::wstring &ws);

bool IsSceneLoadInProgress();

// Persist panel visibility across runs (written to panels_state.ini)
void SavePanelVisibility();
