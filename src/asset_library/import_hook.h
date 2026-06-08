#pragma once
#include "../assets/asset_loader.h"
#include "asset_id.h"
#include <string>
#include <vector>

// High-level entry point the Scene import path calls after a successful import.
// Registers the imported model (+ materials/textures) in the global registry
// and kicks off background cooking, deduplicating by source path + timestamp so
// re-importing the same unchanged file does not recook. No-op (returns invalid
// id) when the global registry is not initialized. Bridge layer.
namespace assetlib {

AssetId RegisterImportedModel(const std::string &displayName,
                              const std::string &sourcePath,
                              const std::vector<Asset::GpuMesh> &meshes,
                              const std::vector<Asset::Material> &materials,
                              const std::vector<Asset::Texture> &textures);

} // namespace assetlib
