#pragma once

#include <string>
#include <vector>

#include "saved_views.h"

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
  bool batchSavedViews = false;
  std::string batchBaseName = "final";
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

struct RenderBatchExportState {
  bool active = false;
  bool failed = false;
  std::wstring outputDirectory;
  std::wstring baseName;
  std::vector<size_t> viewIndices;
  size_t currentViewListIndex = 0;
  SavedViews::SavedView previousCamera;
  bool previousCameraCaptured = false;
  std::wstring currentOutputPath;
  std::string currentViewName;
};

// Resolution presets (defined in editor_ui.cpp)
extern const RenderResolutionPreset g_renderResolutionPresets[];
extern const int g_renderResolutionPresetCount;

// Editor UI state (defined in editor_ui.cpp)
extern RenderExportSettings g_renderExportSettings;
extern RenderExportJobState g_renderExportJob;
extern RenderBatchExportState g_renderBatchExport;
extern std::string g_renderExportStatus;

// Panel visibility toggles (defined in editor_ui.cpp)
extern bool g_showRenderModeWindow;
extern bool g_showAssetsWindow;
extern bool g_showMaterialEditor;
extern bool g_showControlsWindow;
extern bool g_showRenderSettingsWindow;
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
bool StartBatchRenderExportJobs(const std::wstring &outputDirectory,
                                const std::wstring &baseName);
void AdvanceBatchRenderExport(bool previousExportSucceeded);
void CancelBatchRenderExport();

// Utility
std::string WStringToUtf8(const std::wstring &ws);

// Scene I/O job helpers (shared by ImGui + Qt)
void StartSceneIoJob(bool isSave, const std::string &utf8Path);
bool IsSceneIoJobActive();
bool IsSceneIoSaveJob();
float GetSceneIoProgress();
std::string GetSceneIoStage();
bool HasCurrentScenePath();
std::string GetCurrentScenePath();
void SetCurrentScenePath(const std::string &utf8Path);

bool IsSceneLoadInProgress();

// Persist panel visibility across runs (written to panels_state.ini)
void SavePanelVisibility();
