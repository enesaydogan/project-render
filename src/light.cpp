#include "light.h"
#include "assets/asset_loader.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include <cstdio>
#include <vector>
#include <wrl.h>


using Microsoft::WRL::ComPtr;
using namespace DX12Context;

// Externals from main.cpp
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;

// Default sun light removed
