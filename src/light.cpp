#include "light.h"
#include "assets/asset_loader.h"
#include "d3d12_helpers.h"
#include <wrl.h>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Externals from main.cpp
extern ComPtr<ID3D12Device> g_device;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;

// Default sun light: Intensity 2.5, pointing down at 45 degree angle (0.7, 0.7, 0)
DirectionalLight g_defaultLight = {{0.707f, 0.707f, 0.0f, 0.0f}, {1.0f, 0.95f, 0.8f, 2.5f}};
