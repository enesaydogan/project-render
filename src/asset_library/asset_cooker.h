#pragma once
#include "../assets/asset_loader.h"
#include "asset_id.h"
#include "asset_paths.h"
#include "cooked_payload.h"
#include <string>
#include <vector>

// Bridge layer between the renderer's Asset:: loader types and the renderer-free
// cooked payloads / registry. This is the only asset_library translation unit
// that depends on D3D12 (via asset_loader.h). It registers imported content as
// AssetIds, writes versioned cooked mesh/texture/material payloads to the cache,
// and wires dependencies by AssetId. See notes/asset-menagement.md Phase 2.
namespace assetlib {

class AssetRegistry;

// AssetIds produced by registering one model import. Parallel arrays line up
// with the input materials/textures vectors.
struct ImportedAssetSet {
  AssetId modelId;
  std::vector<AssetId> materialIds; // index-aligned with input materials[]
  std::vector<AssetId> textureIds;  // index-aligned with input textures[]
};

// --- Conversions (no registry/cache side effects) ---------------------------
CookedModel ToCookedModel(const std::vector<Asset::GpuMesh> &meshes);
CookedTexture ToCookedTexture(const Asset::Texture &tex);

// Reconstruct CPU mesh geometry from a cooked model. The caller must run
// Asset::UploadMeshes() afterwards to create GPU buffers.
void FromCookedModel(const CookedModel &cooked,
                     std::vector<Asset::GpuMesh> &outMeshes);
// Reconstruct a GPU texture from a cooked texture payload.
Asset::Texture FromCookedTexture(const CookedTexture &cooked);

// --- Cook + register --------------------------------------------------------
// Registers a freshly imported model with its materials and textures, writes
// cooked payloads to the cache, and records dependencies by AssetId. Already
// has decoded data in hand (from the import), so it does not re-decode the
// source. Marks registry dirty; caller decides when to Save().
ImportedAssetSet
RegisterAndCookImport(AssetRegistry &registry, const AssetPaths &paths,
                      const std::string &displayName,
                      const std::string &sourcePath,
                      const std::vector<Asset::GpuMesh> &meshes,
                      const std::vector<Asset::Material> &materials,
                      const std::vector<Asset::Texture> &textures);

// Registers and cooks a single standalone texture asset (e.g. the Assets panel
// "Add Asset…" on an image file). Records the source path for relinking. Marks
// the registry dirty; caller decides when to Save().
AssetId RegisterAndCookTexture(AssetRegistry &registry, const AssetPaths &paths,
                               const std::string &displayName,
                               const std::string &sourcePath,
                               const Asset::Texture &tex);

// Re-decode a model asset's source file and rewrite its cooked payloads. Used
// by background recook jobs when a source changes. Returns false if the source
// is missing or fails to load. Requires the Asset:: loader to be initialized.
bool RecookModelFromSource(AssetRegistry &registry, const AssetPaths &paths,
                           const AssetId &modelId);

// True if a current, version-matching cooked mesh payload exists for the asset.
bool HasCurrentCookedModel(const AssetRegistry &registry,
                           const AssetPaths &paths, const AssetId &modelId);

} // namespace assetlib
