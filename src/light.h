#pragma once
#include "assets/asset_loader.h"
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
