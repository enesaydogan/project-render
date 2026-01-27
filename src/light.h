#pragma once
#include <wrl.h>
#include "assets/asset_loader.h"

using Microsoft::WRL::ComPtr;

struct DirectionalLight {
  float dir[4]; // xyz = direction (pointing *towards* light), w = unused
  float color[4]; // rgb + intensity in .w
};

extern DirectionalLight g_defaultLight;

// Create and append a default plane (size 10x10) to the global meshes/materials.
void AddDefaultPlane();
