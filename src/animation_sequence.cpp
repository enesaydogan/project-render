#include "animation_sequence.h"
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace AnimationSequence {
namespace {

std::vector<Keyframe> g_keyframes;
ExportSettings g_exportSettings;
std::string g_lastStatus;

float Clamp01(float value) {
  return (std::clamp)(value, 0.0f, 1.0f);
}

float NormalizeAngle(float angle) {
  while (angle > 3.1415926535f) {
    angle -= 6.283185307f;
  }
  while (angle < -3.1415926535f) {
    angle += 6.283185307f;
  }
  return angle;
}

float Lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float LerpAngle(float a, float b, float t) {
  return a + NormalizeAngle(b - a) * t;
}

void LerpVec3(const float *a, const float *b, float t, float *out) {
  out[0] = Lerp(a[0], b[0], t);
  out[1] = Lerp(a[1], b[1], t);
  out[2] = Lerp(a[2], b[2], t);
}

void NormalizeVec3(float *value) {
  const float lengthSq = value[0] * value[0] + value[1] * value[1] +
                         value[2] * value[2];
  if (lengthSq <= 1e-8f) {
    value[0] = 0.0f;
    value[1] = 0.0f;
    value[2] = -1.0f;
    return;
  }
  const float invLength = 1.0f / std::sqrt(lengthSq);
  value[0] *= invLength;
  value[1] *= invLength;
  value[2] *= invLength;
}

float ApplyEasing(float t, int easing) {
  const float clamped = Clamp01(t);
  switch (static_cast<EasingMode>(easing)) {
  case EasingMode::Ease:
    return clamped * clamped;
  case EasingMode::Linear:
  default:
    return clamped;
  }
}

float ApplyIncomingEasing(float t, int easing) {
  const float clamped = Clamp01(t);
  switch (static_cast<EasingMode>(easing)) {
  case EasingMode::Ease:
    return 1.0f - (1.0f - clamped) * (1.0f - clamped);
  case EasingMode::Linear:
  default:
    return clamped;
  }
}

float ApplyOutgoingEasing(float t, int easing) {
  return ApplyEasing(t, easing);
}

float ApplySegmentEasing(float t, int easeIn, int easeOut) {
  const float clamped = Clamp01(t);
  if (clamped <= 0.5f) {
    return 0.5f * ApplyOutgoingEasing(clamped * 2.0f, easeIn);
  }
  return 0.5f + 0.5f * ApplyIncomingEasing((clamped - 0.5f) * 2.0f, easeOut);
}

SavedViews::SavedView InterpolateView(const Keyframe &from, const Keyframe &to,
                                      float t) {
  SavedViews::SavedView result = from.camera;
  result.name = to.label.empty() ? to.camera.name : to.label;
  LerpVec3(from.camera.pos, to.camera.pos, t, result.pos);
  LerpVec3(from.camera.forward, to.camera.forward, t, result.forward);
  LerpVec3(from.camera.up, to.camera.up, t, result.up);
  NormalizeVec3(result.forward);
  NormalizeVec3(result.up);
  result.fov = Lerp(from.camera.fov, to.camera.fov, t);
  result.nearZ = Lerp(from.camera.nearZ, to.camera.nearZ, t);
  result.farZ = Lerp(from.camera.farZ, to.camera.farZ, t);
  result.intensity = Lerp(from.camera.intensity, to.camera.intensity, t);
  result.maxSpecularBounces =
      Lerp(from.camera.maxSpecularBounces, to.camera.maxSpecularBounces, t);
  result.maxRefractiveBounces =
      Lerp(from.camera.maxRefractiveBounces, to.camera.maxRefractiveBounces, t);
  result.maxGIBounces =
      Lerp(from.camera.maxGIBounces, to.camera.maxGIBounces, t);
  result.maxSPP = Lerp(from.camera.maxSPP, to.camera.maxSPP, t);
  result.yaw = LerpAngle(from.camera.yaw, to.camera.yaw, t);
  result.pitch = LerpAngle(from.camera.pitch, to.camera.pitch, t);
  result.useAdaptiveSampling =
      Lerp(from.camera.useAdaptiveSampling, to.camera.useAdaptiveSampling, t);
  result.noiseThreshold =
      Lerp(from.camera.noiseThreshold, to.camera.noiseThreshold, t);
  result.debugVisualizationMode =
      Lerp(from.camera.debugVisualizationMode,
           to.camera.debugVisualizationMode, t);
  result.sampleEnvSolidAngle = 1.0f;
  result.verticalTiltCorrection =
      (t < 0.5f) ? from.camera.verticalTiltCorrection
                 : to.camera.verticalTiltCorrection;
  result.autoExposure = (t < 0.5f) ? from.camera.autoExposure : to.camera.autoExposure;
  result.physicalCameraExposure =
      (t < 0.5f) ? from.camera.physicalCameraExposure
                 : to.camera.physicalCameraExposure;
  result.safeFrameEnabled =
      (t < 0.5f) ? from.camera.safeFrameEnabled : to.camera.safeFrameEnabled;
  result.exposureCompensation =
      Lerp(from.camera.exposureCompensation,
           to.camera.exposureCompensation, t);
  result.iso = Lerp(from.camera.iso, to.camera.iso, t);
  result.shutterSeconds =
      Lerp(from.camera.shutterSeconds, to.camera.shutterSeconds, t);
  result.aperture = Lerp(from.camera.aperture, to.camera.aperture, t);
  result.tonemapAoIntensity =
      Lerp(from.camera.tonemapAoIntensity, to.camera.tonemapAoIntensity, t);
  result.tonemapAoLengthMm =
      Lerp(from.camera.tonemapAoLengthMm, to.camera.tonemapAoLengthMm, t);
  result.tonemapAoMode =
      (t < 0.5f) ? from.camera.tonemapAoMode : to.camera.tonemapAoMode;
  result.tonemapVignette =
      Lerp(from.camera.tonemapVignette, to.camera.tonemapVignette, t);
  result.tonemapSaturation =
      Lerp(from.camera.tonemapSaturation, to.camera.tonemapSaturation, t);
  result.tonemapContrast =
      Lerp(from.camera.tonemapContrast, to.camera.tonemapContrast, t);
  result.tonemapWhiteBalance =
      Lerp(from.camera.tonemapWhiteBalance, to.camera.tonemapWhiteBalance, t);
  result.tonemapSettingsCaptured =
      from.camera.tonemapSettingsCaptured &&
      to.camera.tonemapSettingsCaptured;
  result.thumbnailWidth = 0;
  result.thumbnailHeight = 0;
  result.thumbnailRgba.clear();
  return result;
}

std::string DefaultLabelForView(const SavedViews::SavedView &view,
                                size_t index) {
  if (!view.name.empty()) {
    return view.name;
  }
  return "Keyframe " + std::to_string(index + 1);
}

} // namespace

const std::vector<Keyframe> &GetKeyframes() { return g_keyframes; }

const ExportSettings &GetExportSettings() { return g_exportSettings; }

const std::string &GetLastStatus() { return g_lastStatus; }

void SetExportSettings(const ExportSettings &settings) {
  g_exportSettings = settings;
  g_exportSettings.fps = (std::max)(1, g_exportSettings.fps);
  g_exportSettings.maxSpp = (std::max)(1, g_exportSettings.maxSpp);
  g_exportSettings.exportMode =
      (std::clamp)(g_exportSettings.exportMode, 0, GetExportModeCount() - 1);
  if (g_exportSettings.baseName.empty()) {
    g_exportSettings.baseName = "final";
  }
}

void SetKeyframes(std::vector<Keyframe> keyframes) {
  g_keyframes = std::move(keyframes);
  g_lastStatus = g_keyframes.empty() ? "No animation keyframes"
                                     : "Animation loaded";
}

void Clear() {
  g_keyframes.clear();
  g_lastStatus = "Animation cleared";
}

size_t AddKeyframeFromView(const SavedViews::SavedView &view,
                          const std::string &label) {
  Keyframe keyframe;
  keyframe.label = label.empty() ? DefaultLabelForView(view, g_keyframes.size())
                                 : label;
  keyframe.camera = view;
  keyframe.camera.thumbnailRgba.clear();
  keyframe.camera.thumbnailWidth = 0;
  keyframe.camera.thumbnailHeight = 0;
  g_keyframes.push_back(std::move(keyframe));
  g_lastStatus = "Added keyframe: " + g_keyframes.back().label;
  return g_keyframes.size() - 1;
}

bool AddKeyframeFromSavedView(size_t savedViewIndex) {
  const auto &views = SavedViews::GetViews();
  if (savedViewIndex >= views.size()) {
    return false;
  }
  AddKeyframeFromView(views[savedViewIndex], views[savedViewIndex].name);
  return true;
}

bool RemoveKeyframe(size_t index) {
  if (index >= g_keyframes.size()) {
    return false;
  }
  g_lastStatus = "Removed keyframe: " + g_keyframes[index].label;
  g_keyframes.erase(g_keyframes.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool MoveKeyframe(size_t index, int direction) {
  if (index >= g_keyframes.size()) {
    return false;
  }
  const int target = static_cast<int>(index) + direction;
  if (target < 0 || target >= static_cast<int>(g_keyframes.size())) {
    return false;
  }
  std::swap(g_keyframes[index], g_keyframes[static_cast<size_t>(target)]);
  g_lastStatus = "Reordered animation keyframes";
  return true;
}

bool UpdateKeyframe(size_t index, float durationToNextSeconds, int easeIn, int easeOut) {
  if (index >= g_keyframes.size()) {
    return false;
  }
  g_keyframes[index].durationToNextSeconds =
      (std::max)(0.0f, durationToNextSeconds);
  g_keyframes[index].easeIn =
      (std::clamp)(easeIn, 0, GetEasingModeCount() - 1);
  g_keyframes[index].easeOut =
      (std::clamp)(easeOut, 0, GetEasingModeCount() - 1);
  g_lastStatus = "Updated animation transition";
  return true;
}

bool RenameKeyframe(size_t index, const std::string &label) {
  if (index >= g_keyframes.size()) {
    return false;
  }
  g_keyframes[index].label = label.empty()
                                 ? DefaultLabelForView(g_keyframes[index].camera,
                                                       index)
                                 : label;
  g_lastStatus = "Renamed keyframe: " + g_keyframes[index].label;
  return true;
}

float GetTotalDurationSeconds() {
  float duration = 0.0f;
  for (size_t index = 0; index + 1 < g_keyframes.size(); ++index) {
    duration += (std::max)(0.0f, g_keyframes[index].durationToNextSeconds);
  }
  return duration;
}

int GetTotalFrameCount(int fps) {
  if (g_keyframes.empty()) {
    return 0;
  }
  if (g_keyframes.size() == 1) {
    return 1;
  }
  const float totalDuration = GetTotalDurationSeconds();
  return (std::max)(2, static_cast<int>(std::ceil(totalDuration * (std::max)(1, fps))) + 1);
}

SavedViews::SavedView EvaluateAtTime(float seconds) {
  if (g_keyframes.empty()) {
    return SavedViews::CaptureCurrentState();
  }
  if (g_keyframes.size() == 1) {
    return g_keyframes.front().camera;
  }

  const float totalDuration = GetTotalDurationSeconds();
  const float clampedTime = (std::clamp)(seconds, 0.0f, totalDuration);
  float elapsed = 0.0f;
  for (size_t index = 0; index + 1 < g_keyframes.size(); ++index) {
    const float segmentDuration =
        (std::max)(0.0001f, g_keyframes[index].durationToNextSeconds);
    if (clampedTime <= elapsed + segmentDuration ||
        index + 2 == g_keyframes.size()) {
      const float localT = (clampedTime - elapsed) / segmentDuration;
      return InterpolateView(g_keyframes[index], g_keyframes[index + 1],
                             ApplySegmentEasing(localT,
                                                g_keyframes[index].easeIn,
                                                g_keyframes[index + 1].easeOut));
    }
    elapsed += segmentDuration;
  }
  return g_keyframes.back().camera;
}

SavedViews::SavedView EvaluateAtFrame(int frameIndex, int fps) {
  if (g_keyframes.empty()) {
    return SavedViews::CaptureCurrentState();
  }
  const int safeFps = (std::max)(1, fps);
  const int totalFrames = GetTotalFrameCount(safeFps);
  if (totalFrames <= 1) {
    return g_keyframes.front().camera;
  }
  const int clampedFrame = (std::clamp)(frameIndex, 0, totalFrames - 1);
  return EvaluateAtTime(static_cast<float>(clampedFrame) /
                        static_cast<float>(safeFps));
}

bool ApplyAtTime(float seconds) {
  if (g_keyframes.empty()) {
    return false;
  }
  Scene::SetVolumeTimelineTime(seconds);
  return SavedViews::ApplyView(EvaluateAtTime(seconds));
}

bool ApplyAtFrame(int frameIndex, int fps) {
  if (g_keyframes.empty()) {
    return false;
  }
  Scene::SetVolumeTimelineTime(
      static_cast<float>((std::max)(frameIndex, 0)) /
      static_cast<float>((std::max)(fps, 1)));
  return SavedViews::ApplyView(EvaluateAtFrame(frameIndex, fps));
}

int GetEasingModeCount() { return 2; }

const char *GetEasingModeLabel(int index) {
  switch (static_cast<EasingMode>(index)) {
  case EasingMode::Ease:
    return "Ease";
  case EasingMode::Linear:
  default:
    return "Linear";
  }
}

int GetExportModeCount() { return 2; }

const char *GetExportModeLabel(int index) {
  switch (static_cast<ExportMode>(index)) {
  case ExportMode::Mp4:
    return "MP4";
  case ExportMode::Frames:
  default:
    return "Frames";
  }
}

} // namespace AnimationSequence
