#include "asset_cooker.h"

#include "../material/material_io.h"
#include "asset_registry.h"
#include "cook_jobs.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <system_error>
#include <unordered_map>

using json = nlohmann::json;

namespace assetlib {
namespace {

int64_t FileTimestamp(const std::string &path) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(std::filesystem::path(path), ec);
  if (ec)
    return 0;
  return static_cast<int64_t>(t.time_since_epoch().count());
}

// Cook + register a single texture. Returns its AssetId (already added to the
// registry). cookState is set to Current on success.
AssetId CookAndRegisterTexture(AssetRegistry &registry, const AssetPaths &paths,
                               const Asset::Texture &tex,
                               const std::string &displayName,
                               const std::string &folder) {
  AssetMetadata m;
  m.type = AssetType::Texture;
  m.displayName = displayName;
  m.virtualPath = folder;
  m.cookerVersion = kCookerVersionTexture;
  m.cookState = CookState::Stale; // worker flips to Current/Failed via Pump()
  AssetId id = registry.Add(std::move(m));

  CookService::Get().Enqueue(
      id, paths.cookedTexturePath(id),
      [cooked = ToCookedTexture(tex)]() {
        std::vector<uint8_t> blob;
        SerializeCookedTexture(cooked, blob);
        return blob;
      });
  return id;
}

// Cook + register a single material, recording its texture dependencies by
// AssetId. textureIds is index-aligned with the import's full texture array.
AssetId CookAndRegisterMaterial(AssetRegistry &registry, const AssetPaths &paths,
                                const Asset::Material &material,
                                const std::vector<Asset::Texture> &allTextures,
                                const std::vector<AssetId> &textureIds,
                                const std::string &folder) {
  AssetMetadata m;
  m.type = AssetType::Material;
  m.displayName = material.name;
  m.virtualPath = folder;
  m.cookerVersion = kCookerVersionTexture;

  // Compact remap covering only the textures this material references.
  std::vector<Asset::Material> one = {material};
  std::vector<int> remap = MaterialIO::BuildTextureSaveRemap(allTextures, one);

  // Ordered list of texture AssetIds in compact (saved) order.
  std::vector<AssetId> compactTexIds;
  int maxSaved = -1;
  for (int v : remap)
    maxSaved = (v > maxSaved) ? v : maxSaved;
  compactTexIds.resize(static_cast<size_t>(maxSaved + 1));
  for (size_t i = 0; i < remap.size() && i < textureIds.size(); ++i)
    if (remap[i] >= 0)
      compactTexIds[static_cast<size_t>(remap[i])] = textureIds[i];

  m.dependencies = compactTexIds;
  m.cookState = CookState::Stale;
  AssetId id = registry.Add(std::move(m));

  json doc;
  doc["material"] = MaterialIO::BuildMaterialsMetadata(one, remap);
  json texArr = json::array();
  for (const auto &t : compactTexIds)
    texArr.push_back(t.ToString());
  doc["textures"] = std::move(texArr);

  // Material JSON is cheap; build it now and enqueue only the file write.
  std::string text = doc.dump();
  std::vector<uint8_t> bytes(text.begin(), text.end());
  CookService::Get().Enqueue(id, paths.cookedMaterialPath(id),
                             [bytes = std::move(bytes)]() { return bytes; });
  return id;
}

} // namespace

CookedModel ToCookedModel(const std::vector<Asset::GpuMesh> &meshes) {
  CookedModel out;
  out.meshes.reserve(meshes.size());
  for (const auto &mesh : meshes) {
    CookedMesh cm;
    cm.materialIndex = mesh.materialIndex;
    cm.materialSlot = mesh.materialSlot;
    cm.vertexCount = mesh.vertexCount;
    cm.indexCount = mesh.indexCount;
    std::memcpy(cm.minBound, mesh.minBound, sizeof(cm.minBound));
    std::memcpy(cm.maxBound, mesh.maxBound, sizeof(cm.maxBound));
    const size_t vbytes = mesh.cpuVertices.size() * sizeof(Asset::Vertex);
    cm.vertexBytes.resize(vbytes);
    if (vbytes)
      std::memcpy(cm.vertexBytes.data(), mesh.cpuVertices.data(), vbytes);
    const size_t ibytes = mesh.cpuIndices.size() * sizeof(uint32_t);
    cm.indexBytes.resize(ibytes);
    if (ibytes)
      std::memcpy(cm.indexBytes.data(), mesh.cpuIndices.data(), ibytes);
    out.meshes.push_back(std::move(cm));
  }
  return out;
}

CookedTexture ToCookedTexture(const Asset::Texture &tex) {
  CookedTexture out;
  out.width = tex.width;
  out.height = tex.height;
  out.cpuFormat = static_cast<uint32_t>(tex.cpuFormat);
  out.cpuMipLevels = tex.cpuMipLevels;
  out.usageSemantic = static_cast<uint32_t>(tex.usageSemantic);
  out.data = tex.cpuData;
  return out;
}

void FromCookedModel(const CookedModel &cooked,
                     std::vector<Asset::GpuMesh> &outMeshes) {
  outMeshes.clear();
  outMeshes.reserve(cooked.meshes.size());
  for (const auto &cm : cooked.meshes) {
    Asset::GpuMesh mesh;
    mesh.materialIndex = cm.materialIndex;
    mesh.materialSlot = cm.materialSlot;
    mesh.vertexCount = cm.vertexCount;
    mesh.indexCount = cm.indexCount;
    std::memcpy(mesh.minBound, cm.minBound, sizeof(mesh.minBound));
    std::memcpy(mesh.maxBound, cm.maxBound, sizeof(mesh.maxBound));
    const size_t vcount = cm.vertexBytes.size() / sizeof(Asset::Vertex);
    mesh.cpuVertices.resize(vcount);
    if (vcount)
      std::memcpy(mesh.cpuVertices.data(), cm.vertexBytes.data(),
                  vcount * sizeof(Asset::Vertex));
    const size_t icount = cm.indexBytes.size() / sizeof(uint32_t);
    mesh.cpuIndices.resize(icount);
    if (icount)
      std::memcpy(mesh.cpuIndices.data(), cm.indexBytes.data(),
                  icount * sizeof(uint32_t));
    outMeshes.push_back(std::move(mesh));
  }
}

Asset::Texture FromCookedTexture(const CookedTexture &cooked) {
  Asset::Texture tex = Asset::LoadTextureFromMemoryMipChain(
      cooked.data.data(), cooked.data.size(), static_cast<int>(cooked.width),
      static_cast<int>(cooked.height),
      static_cast<DXGI_FORMAT>(cooked.cpuFormat), cooked.cpuMipLevels);
  tex.usageSemantic =
      static_cast<Asset::TextureUsageSemantic>(cooked.usageSemantic);
  return tex;
}

ImportedAssetSet
RegisterAndCookImport(AssetRegistry &registry, const AssetPaths &paths,
                      const std::string &displayName,
                      const std::string &sourcePath,
                      const std::vector<Asset::GpuMesh> &meshes,
                      const std::vector<Asset::Material> &materials,
                      const std::vector<Asset::Texture> &textures) {
  ImportedAssetSet set;

  // 1. Textures (index-aligned with input array).
  set.textureIds.reserve(textures.size());
  for (size_t i = 0; i < textures.size(); ++i) {
    std::string texName = displayName + " Tex " + std::to_string(i);
    set.textureIds.push_back(
        CookAndRegisterTexture(registry, paths, textures[i], texName,
                               "Imported/Textures"));
  }

  // 2. Materials (index-aligned), recording texture deps by AssetId.
  set.materialIds.reserve(materials.size());
  for (const auto &mat : materials) {
    set.materialIds.push_back(CookAndRegisterMaterial(
        registry, paths, mat, textures, set.textureIds, "Imported/Materials"));
  }

  // 3. Model: cooked mesh payload + dependency list of material AssetIds in
  // materials[] order (so CookedMesh.materialIndex indexes straight into it).
  AssetMetadata model;
  model.type = AssetType::Model;
  model.displayName = displayName;
  model.virtualPath = "Imported/Models";
  model.sourcePath = sourcePath;
  model.sourceContentHash = sourcePath.empty() ? 0 : HashFile(sourcePath);
  model.sourceTimestamp = sourcePath.empty() ? 0 : FileTimestamp(sourcePath);
  model.cookerVersion = kCookerVersionMesh;
  model.dependencies = set.materialIds;
  model.cookState = CookState::Stale;
  set.modelId = registry.Add(std::move(model));

  CookService::Get().Enqueue(
      set.modelId, paths.cookedMeshPath(set.modelId),
      [cooked = ToCookedModel(meshes)]() {
        std::vector<uint8_t> blob;
        SerializeCookedModel(cooked, blob);
        return blob;
      });
  return set;
}

AssetId RegisterAndCookTexture(AssetRegistry &registry, const AssetPaths &paths,
                               const std::string &displayName,
                               const std::string &sourcePath,
                               const Asset::Texture &tex) {
  AssetMetadata m;
  m.type = AssetType::Texture;
  m.displayName = displayName;
  m.virtualPath = "Imported/Textures";
  m.sourcePath = sourcePath;
  m.sourceContentHash = sourcePath.empty() ? 0 : HashFile(sourcePath);
  m.sourceTimestamp = sourcePath.empty() ? 0 : FileTimestamp(sourcePath);
  m.cookerVersion = kCookerVersionTexture;
  m.cookState = CookState::Stale;
  AssetId id = registry.Add(std::move(m));

  CookService::Get().Enqueue(
      id, paths.cookedTexturePath(id), [cooked = ToCookedTexture(tex)]() {
        std::vector<uint8_t> blob;
        SerializeCookedTexture(cooked, blob);
        return blob;
      });
  return id;
}

AssetId RegisterAndCookVolume(AssetRegistry &registry, const AssetPaths &paths,
                              const std::string &displayName,
                              const std::string &sourcePath,
                              const CookedVolume &volume) {
  AssetMetadata m;
  m.type = AssetType::Volume;
  m.displayName = displayName;
  m.virtualPath = "Imported/Volumes";
  m.sourcePath = sourcePath;
  m.sourceContentHash = sourcePath.empty() ? 0 : HashFile(sourcePath);
  m.sourceTimestamp = sourcePath.empty() ? 0 : FileTimestamp(sourcePath);
  m.cookerVersion = kCookerVersionVolume;
  m.cookState = CookState::Stale;
  // Statistics for the inspector / diagnostics.
  json stats;
  stats["dim"] = {volume.dim[0], volume.dim[1], volume.dim[2]};
  stats["activeVoxels"] = volume.activeVoxels;
  stats["bricks"] = volume.bricks.size();
  stats["temperatureBricks"] = volume.temperatureBricks.size();
  stats["hasTemperature"] = !volume.temperatureBricks.empty();
  stats["brickSize"] = volume.brickSize;
  m.importSettingsJson = stats.dump();
  AssetId id = registry.Add(std::move(m));

  CookService::Get().Enqueue(id, paths.cookedVolumePath(id),
                             [volume]() {
                               std::vector<uint8_t> blob;
                               SerializeCookedVolume(volume, blob);
                               return blob;
                             });
  return id;
}

bool HasCurrentCookedModel(const AssetRegistry &registry,
                           const AssetPaths &paths, const AssetId &modelId) {
  const AssetMetadata *m = registry.Get(modelId);
  if (!m || m->type != AssetType::Model)
    return false;
  if (m->cookState != CookState::Current ||
      m->cookerVersion != kCookerVersionMesh)
    return false;
  std::error_code ec;
  return std::filesystem::exists(paths.cookedMeshPath(modelId), ec);
}

bool RecookModelFromSource(AssetRegistry &registry, const AssetPaths &paths,
                           const AssetId &modelId) {
  const AssetMetadata *meta = registry.Get(modelId);
  if (!meta || meta->type != AssetType::Model || meta->sourcePath.empty())
    return false;
  std::error_code ec;
  if (!std::filesystem::exists(std::filesystem::path(meta->sourcePath), ec))
    return false;

  // Re-decode geometry on the calling (worker) thread without GPU upload.
  const bool prevDefer = Asset::GetDeferGpuUpload();
  Asset::SetDeferGpuUpload(true);
  std::vector<Asset::GpuMesh> meshes;
  const bool loaded = Asset::LoadModel(meta->sourcePath, meshes);
  Asset::SetDeferGpuUpload(prevDefer);
  if (!loaded || meshes.empty()) {
    AssetMetadata fail = *registry.Get(modelId);
    fail.cookState = CookState::Failed;
    registry.Update(fail);
    return false;
  }

  CookedModel cooked = ToCookedModel(meshes);
  std::vector<uint8_t> blob;
  bool ok = SerializeCookedModel(cooked, blob) &&
            WriteCookedFile(paths.cookedMeshPath(modelId), blob);

  AssetMetadata updated = *registry.Get(modelId);
  updated.cookState = ok ? CookState::Current : CookState::Failed;
  updated.cookerVersion = kCookerVersionMesh;
  updated.sourceContentHash = HashFile(meta->sourcePath);
  updated.sourceTimestamp = FileTimestamp(meta->sourcePath);
  if (ok && !blob.empty())
    updated.cookedPayloadHash = HashBytes(blob.data(), blob.size());
  registry.Update(updated);
  return ok;
}

} // namespace assetlib
