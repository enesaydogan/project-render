#include "import_hook.h"

#include "asset_cooker.h"
#include "asset_paths.h"
#include "asset_registry.h"
#include "cooked_payload.h"
#include "global_registry.h"

#include <filesystem>
#include <system_error>

namespace assetlib {
namespace {

// Find an existing Model asset for this source whose cooked payload is current
// and whose source has not changed (same timestamp). Returns invalid if none.
AssetId FindUpToDateModel(const AssetRegistry &registry, const AssetPaths &paths,
                          const std::string &sourcePath) {
  if (sourcePath.empty())
    return {};
  std::error_code ec;
  auto ts = std::filesystem::last_write_time(
      std::filesystem::path(sourcePath), ec);
  int64_t timestamp = ec ? 0 : static_cast<int64_t>(ts.time_since_epoch().count());
  for (const AssetId &id : registry.AllAssets()) {
    const AssetMetadata *m = registry.Get(id);
    if (!m || m->type != AssetType::Model || m->sourcePath != sourcePath)
      continue;
    if (timestamp != 0 && m->sourceTimestamp == timestamp &&
        HasCurrentCookedModel(registry, paths, id))
      return id;
  }
  return {};
}

} // namespace

AssetId RegisterImportedModel(const std::string &displayName,
                              const std::string &sourcePath,
                              const std::vector<Asset::GpuMesh> &meshes,
                              const std::vector<Asset::Material> &materials,
                              const std::vector<Asset::Texture> &textures) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry || meshes.empty())
    return {};
  const AssetPaths &paths = registry->paths();

  AssetId existing = FindUpToDateModel(*registry, paths, sourcePath);
  if (existing.valid())
    return existing; // already cooked & unchanged — skip recook

  ImportedAssetSet set = RegisterAndCookImport(
      *registry, paths, displayName, sourcePath, meshes, materials, textures);
  registry->TouchRecent(set.modelId);
  registry->Save(); // persist catalog now; cookState flips to Current via Pump
  return set.modelId;
}

} // namespace assetlib
