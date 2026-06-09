#pragma once
#include "../assets/asset_loader.h"
#include "asset_id.h"
#include "vdb_import.h"
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

// Decode a file (model or image) and register + cook it into the library
// WITHOUT adding it to the scene — backs the Assets panel "Add Asset…" action.
// Returns the created (or existing, for models) AssetId, or invalid on an
// unsupported/undecodable file. Requires the Asset:: loader to be initialized.
// Decodes synchronously on the calling (main) thread; cooking is backgrounded.
AssetId ImportFileToLibrary(const std::string &path);
AssetId ImportVdbFileToLibrary(const std::string &path,
                               const VdbImport::ImportOptions &options);

} // namespace assetlib
