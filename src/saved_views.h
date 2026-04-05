#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SavedViews {

struct SavedView {
  std::string name;
  std::string sourceSessionId;
  std::string sourceObjectId;
  bool external = false;
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float forward[3] = {0.0f, 0.0f, -1.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float fov = 45.0f;
  float nearZ = 0.1f;
  float farZ = 1000.0f;
  float intensity = 0.02f;
  float maxSpecularBounces = 3.0f;
  float maxRefractiveBounces = 3.0f;
  float maxGIBounces = 2.0f;
  float maxSPP = 200.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float useAdaptiveSampling = 1.0f;
  float noiseThreshold = 0.05f;
  float debugVisualizationMode = 0.0f;
  float sampleEnvSolidAngle = 1.0f;
  bool autoExposure = false;
  bool physicalCameraExposure = true;
  bool safeFrameEnabled = false;
  float exposureCompensation = 1.0f;
  float iso = 100.0f;
  float shutterSeconds = 1.0f / 30.0f;
  float aperture = 2.8f;
  float tonemapAoIntensity = 0.0f;
  float tonemapAoLengthMm = 250.0f;
  int tonemapAoMode = 2;
  uint32_t thumbnailWidth = 0;
  uint32_t thumbnailHeight = 0;
  std::vector<uint8_t> thumbnailRgba;
};

const std::vector<SavedView> &GetViews();
const std::string &GetLastStatus();
int GetSelectedViewIndex();
void SetSelectedViewIndex(int index);

SavedView CaptureCurrentState();
size_t AddCurrentView(const std::string &preferredName = std::string());
bool RemoveView(size_t index);
bool ApplyView(const SavedView &view);
bool ApplyView(size_t index);
size_t UpsertExternalView(const SavedView &view);
bool RemoveExternalView(const std::string &sessionId,
                        const std::string &objectId);
void RemoveExternalViewsForSession(const std::string &sessionId);
void RemoveAllExternalViews();
void Clear();
void SetViews(std::vector<SavedView> views);

} // namespace SavedViews
