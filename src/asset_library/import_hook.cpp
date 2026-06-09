#include "import_hook.h"

#include "asset_cooker.h"
#include "asset_paths.h"
#include "asset_registry.h"
#include "cooked_payload.h"
#include "global_registry.h"
#include "vdb_import.h"

#include <cctype>
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

namespace {
std::string LowerExt(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  if (!ext.empty() && ext[0] == '.')
    ext.erase(0, 1);
  for (char &c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}
} // namespace

AssetId ImportFileToLibrary(const std::string &path) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry)
    return {};
  const std::string ext = LowerExt(path);
  const std::string name = std::filesystem::path(path).stem().string();

  const bool isModel = ext == "gltf" || ext == "glb" || ext == "obj" ||
                       ext == "stl" || ext == "fbx";
  if (isModel) {
    // Decode without GPU upload; cooking reads the CPU geometry directly.
    const bool prevDefer = Asset::GetDeferGpuUpload();
    Asset::SetDeferGpuUpload(true);
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;
    const bool ok = Asset::LoadModel(path, meshes, &materials, &textures);
    Asset::SetDeferGpuUpload(prevDefer);
    if (!ok || meshes.empty())
      return {};
    return RegisterImportedModel(name, path, meshes, materials, textures);
  }

  if (ext == "vdb") {
    CookedVolume vol;
    std::string vdbErr;
    if (!VdbImport::ImportVdbToVolume(path, vol, &vdbErr))
      return {};
    AssetId id = RegisterAndCookVolume(*registry, registry->paths(), name, path,
                                       vol);
    registry->TouchRecent(id);
    registry->Save();
    return id;
  }

  const bool isImage = ext == "png" || ext == "jpg" || ext == "jpeg" ||
                       ext == "tga" || ext == "dds" || ext == "exr" ||
                       ext == "hdr" || ext == "bmp";
  if (isImage) {
    const bool hdr = ext == "exr" || ext == "hdr";
    Asset::Texture tex = Asset::LoadTextureFromFile(path, hdr);
    if (tex.cpuData.empty())
      return {};
    AssetId id =
        RegisterAndCookTexture(*registry, registry->paths(), name, path, tex);
    registry->TouchRecent(id);
    registry->Save();
    return id;
  }

  return {}; // unsupported type for cooking
}

} // namespace assetlib
