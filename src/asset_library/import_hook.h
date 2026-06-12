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
bool EnsureVdbSequenceCooked(const AssetId &id);

// Startup resume: scans the registry for assets whose cook was interrupted by
// an app exit (cookState stale/not-cooked/failed, cooker version mismatch, or
// a missing cooked payload file) and finishes the work:
//  - cooked file already complete on disk -> adopt it (flip to Current)
//  - source file reachable on disk        -> enqueue a background recook
//  - neither                              -> flag as missing/failed
// Main thread only (mutates the registry). No-op when the registry is not
// initialized.
struct ResumeCookStats {
  size_t adopted = 0;  // payloads finished before exit; registry repaired
  size_t requeued = 0; // background recook jobs enqueued from source files
  size_t missing = 0;  // no payload and no reachable source; flagged
};
ResumeCookStats ResumePendingCooks();

} // namespace assetlib
