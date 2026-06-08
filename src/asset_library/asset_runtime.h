#pragma once
#include "../assets/asset_loader.h"
#include "asset_id.h"
#include "asset_paths.h"
#include <vector>

// Resolves AssetIds back into renderer-ready Asset:: types by loading cooked
// payloads from the cache. The doc's "AssetRuntime" boundary. Bridge layer
// (depends on D3D12 via asset_loader.h). Meshes are returned with CPU geometry
// only; the caller uploads them (Asset::UploadMeshes / Scene import path).
namespace assetlib {

class AssetRegistry;

struct ResolvedTexture {
  bool valid = false;
  Asset::Texture texture;
};

struct ResolvedMaterial {
  bool valid = false;
  Asset::Material material;            // texture slots index into `textures`
  std::vector<Asset::Texture> textures;
};

// A model resolved into a self-consistent set ready to become a scene node:
// mesh.materialIndex indexes into `materials`, and each material's texture
// slots index into the shared `textures` array.
struct ResolvedModel {
  bool valid = false;
  std::vector<Asset::GpuMesh> meshes; // CPU geometry; caller must upload
  std::vector<Asset::Material> materials;
  std::vector<Asset::Texture> textures;
};

ResolvedTexture ResolveTexture(const AssetRegistry &registry,
                               const AssetPaths &paths, const AssetId &id);
ResolvedMaterial ResolveMaterial(const AssetRegistry &registry,
                                 const AssetPaths &paths, const AssetId &id);
ResolvedModel ResolveModel(const AssetRegistry &registry,
                           const AssetPaths &paths, const AssetId &id);

} // namespace assetlib
