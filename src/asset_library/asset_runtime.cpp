#include "asset_runtime.h"

#include "../material/material_io.h"
#include "asset_cooker.h"
#include "asset_registry.h"
#include "cooked_payload.h"
#include "pack_mounts.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace assetlib {
namespace {

// Every texture-slot member of Asset::Material (each indexes a textures array).
void ForEachTextureSlot(Asset::Material &m, void (*fn)(int &, void *),
                        void *ctx) {
  int *slots[] = {
      &m.diffuseTexture,        &m.normalTexture,
      &m.opacityTexture,        &m.emissiveTexture,
      &m.occlusionTexture,      &m.metalRoughTexture,
      &m.runtimeMetalRoughTexture, &m.metalnessTexture,
      &m.roughnessGlossTexture, &m.specularColorTexture,
      &m.thicknessTexture,      &m.coatNormalTexture,
      &m.parallaxTexture};
  for (int *s : slots)
    fn(*s, ctx);
}

void OffsetSlot(int &slot, void *ctx) {
  int base = *static_cast<int *>(ctx);
  if (slot >= 0)
    slot += base;
}

} // namespace

ResolvedTexture ResolveTexture(const AssetRegistry &registry,
                               const AssetPaths &paths, const AssetId &id) {
  ResolvedTexture out;
  const AssetMetadata *meta = registry.Get(id);
  if (!meta || meta->type != AssetType::Texture)
    return out;
  std::vector<uint8_t> blob;
  if (!ResolveCookedPayload(paths, id, PayloadKind::Texture, blob))
    return out;
  CookedTexture cooked;
  if (!DeserializeCookedTexture(blob.data(), blob.size(), cooked))
    return out;
  out.texture = FromCookedTexture(cooked);
  out.valid = true;
  return out;
}

ResolvedMaterial ResolveMaterial(const AssetRegistry &registry,
                                 const AssetPaths &paths, const AssetId &id) {
  ResolvedMaterial out;
  const AssetMetadata *meta = registry.Get(id);
  if (!meta || meta->type != AssetType::Material)
    return out;

  std::vector<uint8_t> bytes;
  if (!ResolveCookedPayload(paths, id, PayloadKind::Material, bytes))
    return out;
  json doc;
  try {
    doc = json::parse(bytes.begin(), bytes.end());
  } catch (const json::exception &) {
    return out;
  }
  if (!doc.contains("material"))
    return out;

  // Resolve this material's textures in saved (compact) order first.
  if (doc.contains("textures") && doc["textures"].is_array()) {
    for (const auto &t : doc["textures"]) {
      AssetId texId;
      ResolvedTexture rt;
      if (t.is_string() && AssetId::FromString(t.get<std::string>(), texId))
        rt = ResolveTexture(registry, paths, texId);
      // Preserve slot ordering even if one texture failed to resolve.
      out.textures.push_back(rt.valid ? rt.texture : Asset::Texture{});
    }
  }

  std::vector<Asset::Material> mats;
  std::vector<int> remap;
  MaterialIO::RestoreMaterialsFromMetadata(doc["material"], &mats, out.textures,
                                           &remap);
  if (mats.empty())
    return out;
  out.material = mats.front();
  out.valid = true;
  return out;
}

ResolvedModel ResolveModel(const AssetRegistry &registry,
                           const AssetPaths &paths, const AssetId &id) {
  ResolvedModel out;
  const AssetMetadata *meta = registry.Get(id);
  if (!meta || meta->type != AssetType::Model)
    return out;

  std::vector<uint8_t> blob;
  if (!ResolveCookedPayload(paths, id, PayloadKind::Mesh, blob))
    return out;
  CookedModel cooked;
  if (!DeserializeCookedModel(blob.data(), blob.size(), cooked))
    return out;
  FromCookedModel(cooked, out.meshes);

  // dependencies are the material AssetIds in mesh.materialIndex order. Resolve
  // each, appending its textures to the shared array and offsetting the
  // material's slot indices so the assembled set is self-consistent.
  for (const AssetId &matId : meta->dependencies) {
    ResolvedMaterial rm = ResolveMaterial(registry, paths, matId);
    int base = static_cast<int>(out.textures.size());
    if (rm.valid) {
      ForEachTextureSlot(rm.material, OffsetSlot, &base);
      for (auto &t : rm.textures)
        out.textures.push_back(std::move(t));
      out.materials.push_back(rm.material);
    } else {
      out.materials.push_back(Asset::Material{}); // keep index alignment
    }
  }

  // Clamp any mesh material index that points outside the resolved set.
  for (auto &mesh : out.meshes) {
    if (mesh.materialIndex >= static_cast<int>(out.materials.size()))
      mesh.materialIndex = out.materials.empty() ? -1 : 0;
  }

  out.valid = true;
  return out;
}

} // namespace assetlib
