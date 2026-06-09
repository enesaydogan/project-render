#include "pack_mounts.h"

#include "asset_registry.h"
#include "cooked_payload.h" // ReadCookedFile
#include "prpak_reader.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace assetlib {

PackMounts &PackMounts::Get() {
  static PackMounts instance;
  return instance;
}

bool PackMounts::isMounted(const std::filesystem::path &packPath) const {
  std::error_code ec;
  for (const auto &p : m_packs)
    if (std::filesystem::equivalent(p->path(), packPath, ec))
      return true;
  return false;
}

bool PackMounts::Mount(const std::filesystem::path &packPath,
                       AssetRegistry &registry, std::string *error) {
  if (isMounted(packPath))
    return true;
  auto reader = std::make_unique<PrPakReader>();
  if (!reader->Open(packPath, error))
    return false;

  const std::string packStr = packPath.string();
  for (const PackedAsset &pa : reader->assets()) {
    // Skip ids that already exist (user asset or another pack wins).
    if (registry.Get(pa.meta.id))
      continue;
    AssetMetadata meta = pa.meta;
    meta.fromPack = true;
    meta.packPath = packStr;
    meta.sourceState = SourceState::None;
    registry.Add(std::move(meta));
  }
  m_packs.push_back(std::move(reader));
  return true;
}

bool PackMounts::Unmount(const std::filesystem::path &packPath,
                         AssetRegistry &registry) {
  std::error_code ec;
  auto it = m_packs.begin();
  for (; it != m_packs.end(); ++it)
    if (std::filesystem::equivalent((*it)->path(), packPath, ec))
      break;
  if (it == m_packs.end())
    return false;

  // Remove this pack's read-only assets from the registry.
  for (const PackedAsset &pa : (*it)->assets()) {
    const AssetMetadata *m = registry.Get(pa.meta.id);
    if (m && m->fromPack && m->packPath == (*it)->path().string())
      registry.Remove(pa.meta.id);
  }
  m_packs.erase(it);
  return true;
}

std::vector<std::filesystem::path> PackMounts::mountedPaths() const {
  std::vector<std::filesystem::path> out;
  out.reserve(m_packs.size());
  for (const auto &p : m_packs)
    out.push_back(p->path());
  return out;
}

bool PackMounts::ResolvePayload(const AssetId &id, PayloadKind kind,
                                std::vector<uint8_t> &out) const {
  for (const auto &p : m_packs)
    if (p->ReadPayload(id, kind, out))
      return true;
  return false;
}

void PackMounts::SaveMountList(const AssetPaths &paths) const {
  nlohmann::json root;
  root["schemaVersion"] = 1;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &p : m_packs)
    arr.push_back(p->path().string());
  root["mounts"] = std::move(arr);
  std::ofstream out(paths.mountsFile(), std::ios::binary | std::ios::trunc);
  if (out)
    out << root.dump(2);
}

void PackMounts::LoadAndMountSaved(const AssetPaths &paths,
                                   AssetRegistry &registry) {
  std::error_code ec;
  if (!std::filesystem::exists(paths.mountsFile(), ec))
    return;
  std::ifstream in(paths.mountsFile(), std::ios::binary);
  if (!in)
    return;
  nlohmann::json root;
  try {
    in >> root;
  } catch (const nlohmann::json::exception &) {
    return;
  }
  if (root.contains("mounts") && root["mounts"].is_array()) {
    for (const auto &m : root["mounts"]) {
      if (!m.is_string())
        continue;
      std::filesystem::path packPath = m.get<std::string>();
      if (std::filesystem::exists(packPath, ec))
        Mount(packPath, registry, nullptr);
    }
  }
}

bool ResolveCookedPayload(const AssetPaths &paths, const AssetId &id,
                          PayloadKind kind, std::vector<uint8_t> &out) {
  if (PackMounts::Get().ResolvePayload(id, kind, out))
    return true;
  std::filesystem::path file;
  switch (kind) {
  case PayloadKind::Mesh:
    file = paths.cookedMeshPath(id);
    break;
  case PayloadKind::Texture:
    file = paths.cookedTexturePath(id);
    break;
  case PayloadKind::Material:
    file = paths.cookedMaterialPath(id);
    break;
  case PayloadKind::Volume:
    file = paths.cookedVolumePath(id);
    break;
  }
  return ReadCookedFile(file, out);
}

} // namespace assetlib
