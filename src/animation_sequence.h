#pragma once

#include <string>
#include <vector>

#include "saved_views.h"

namespace AnimationSequence {

enum class EasingMode : int {
  Linear = 0,
  EaseIn = 1,
  EaseOut = 2,
  EaseInOut = 3,
  SmoothStep = 4,
};

enum class ExportMode : int {
  Frames = 0,
  Mp4 = 1,
};

struct Keyframe {
  std::string label;
  SavedViews::SavedView camera;
  float durationToNextSeconds = 2.0f;
  int easing = static_cast<int>(EasingMode::EaseInOut);
};

struct ExportSettings {
  int resolutionPreset = 1;
  int fps = 30;
  int maxSpp = 64;
  std::string baseName = "final";
  int exportMode = static_cast<int>(ExportMode::Frames);
};

const std::vector<Keyframe> &GetKeyframes();
const ExportSettings &GetExportSettings();
const std::string &GetLastStatus();

void SetExportSettings(const ExportSettings &settings);
void SetKeyframes(std::vector<Keyframe> keyframes);
void Clear();

size_t AddKeyframeFromView(const SavedViews::SavedView &view,
                          const std::string &label = std::string());
bool AddKeyframeFromSavedView(size_t savedViewIndex);
bool RemoveKeyframe(size_t index);
bool MoveKeyframe(size_t index, int direction);
bool UpdateKeyframe(size_t index, float durationToNextSeconds, int easing);
bool RenameKeyframe(size_t index, const std::string &label);

float GetTotalDurationSeconds();
int GetTotalFrameCount(int fps);

SavedViews::SavedView EvaluateAtTime(float seconds);
SavedViews::SavedView EvaluateAtFrame(int frameIndex, int fps);
bool ApplyAtTime(float seconds);
bool ApplyAtFrame(int frameIndex, int fps);

int GetEasingModeCount();
const char *GetEasingModeLabel(int index);
int GetExportModeCount();
const char *GetExportModeLabel(int index);

} // namespace AnimationSequence