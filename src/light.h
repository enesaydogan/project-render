#pragma once
#include "assets/asset_loader.h"
#include <cstddef>
#include <utility>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

enum class LightType : uint32_t {
  Directional = 0,
  Omni = 1,
  Spot = 2,
  AreaRect = 3,
  AreaDisk = 4,
  IES = 5
};

// GPU runtime record — 64 bytes, uploaded as StructuredBuffer<Light>
struct Light {
  uint32_t type;
  float position[3];
  float emission[3]; // rgb intensity
  float direction[3];
  float radius;         // for soft shadows / omni radius
  float innerConeAngle; // cos(inner)
  float outerConeAngle; // cos(outer)
  float areaExtents[2]; // width, height
  int iesAtlasIndex;
};

// Ensure it's exactly 64 bytes for GPU alignment
static_assert(sizeof(Light) == 64, "Light struct must be 64 bytes");

// Editor-side prototype: shared properties for a group of light instances.
struct LightPrototype {
  char name[64] = {};
  char stableId[64] = {};           // live-link stable ID for instanced lights
  uint32_t type = 1;                // LightType::Omni
  bool enabled = true;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float intensity = 1000.0f;
  float radius = 0.1f;
  float innerConeAngle = 0.8660254f;  // cos(30°)
  float outerConeAngle = 0.7071068f;  // cos(45°)
  float areaExtents[2] = {1.0f, 1.0f};
  int iesAtlasIndex = -1;
  int iesProfileIndex = -1;  // index into Scene::GetIESProfiles()
};

// Editor-side instance: per-light transform data.
struct LightInstance {
  size_t prototypeIndex = 0;
  float position[3] = {0.0f, 2.0f, 0.0f};
  float direction[3] = {0.0f, -1.0f, 0.0f};
  bool enabled = true;
  bool selected = false;
};

// Rebuild the flattened GPU light array from prototypes + instances.
// Only enabled prototypes and enabled instances produce output.
void FlattenLights(const std::vector<LightPrototype> &prototypes,
                   const std::vector<LightInstance> &instances,
                   std::vector<Light> &outLights,
                   std::vector<std::pair<size_t, size_t>> &outMapping);
