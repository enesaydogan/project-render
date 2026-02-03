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

// Default sun light removed

