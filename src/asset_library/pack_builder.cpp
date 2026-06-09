#include "pack_builder.h"

#include "asset_registry.h"
#include "cooked_payload.h" // ReadCookedFile
#include "prpak_writer.h"

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace assetlib {

std::vector<AssetId> CollectWithDependencies(const AssetRegistry &registry,
                                             const std::vector<AssetId> &roots) {
  std::vector<AssetId> ordered;
  std::unordered_set<AssetId> seen;
  std::deque<AssetId> queue(roots.begin(), roots.end());
  while (!queue.empty()) {
    AssetId id = queue.front();
    queue.pop_front();
    if (!id.valid() || seen.count(id))
      continue;
    const AssetMetadata *m = registry.Get(id);
    if (!m)
      continue; // missing dependency — skipped (reported by BuildPack)
    seen.insert(id);
    ordered.push_back(id);
    for (const AssetId &dep : m->dependencies)
      if (!seen.count(dep))
        queue.push_back(dep);
  }
  return ordered;
}

bool BuildPack(const AssetRegistry &registry, const AssetPaths &paths,
               const std::vector<AssetId> &assetIds, const PackMeta &meta,
               const std::filesystem::path &out, std::string *error,
               std::vector<std::string> *warnings) {
  const std::vector<AssetId> all = CollectWithDependencies(registry, assetIds);
  if (all.empty()) {
    if (error)
      *error = "no assets to pack";
    return false;
  }

  std::vector<PackAssetInput> inputs;
  inputs.reserve(all.size());
  for (const AssetId &id : all) {
    const AssetMetadata *m = registry.Get(id);
    if (!m)
      continue;
    PackAssetInput in;
    in.meta = *m;
    // The payload kind to pull depends on the asset type. Scatter / preset
    // assets carry their data in importSettingsJson (metadata) — no payload.
    switch (m->type) {
    case AssetType::Model:
      if (!ReadCookedFile(paths.cookedMeshPath(id), in.meshPayload) &&
          warnings)
        warnings->push_back("missing cooked mesh for " + m->displayName);
      break;
    case AssetType::Texture:
    case AssetType::Hdri:
      if (!ReadCookedFile(paths.cookedTexturePath(id), in.texturePayload) &&
          warnings)
        warnings->push_back("missing cooked texture for " + m->displayName);
      break;
    case AssetType::Material:
      if (!ReadCookedFile(paths.cookedMaterialPath(id), in.materialPayload) &&
          warnings)
        warnings->push_back("missing cooked material for " + m->displayName);
      break;
    case AssetType::Volume:
      if (!ReadCookedFile(paths.cookedVolumePath(id), in.volumePayload) &&
          warnings)
        warnings->push_back("missing cooked volume for " + m->displayName);
      break;
    default:
      break; // scatter/preset/etc: metadata-only
    }
    // Flag missing dependencies for the user.
    if (warnings)
      for (const AssetId &dep : m->dependencies)
        if (!registry.Get(dep))
          warnings->push_back(m->displayName + " references a missing asset");
    inputs.push_back(std::move(in));
  }

  return WritePack(out, meta, inputs, error);
}

} // namespace assetlib
