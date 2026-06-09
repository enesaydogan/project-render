#pragma once
#include "asset_id.h"
#include "asset_paths.h"
#include "prpak_format.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Manages mounted .prpak archives and resolves asset payloads from them at
// runtime (random access, no unpacking). Mounted assets are registered into the
// AssetRegistry as read-only (fromPack) so they appear in the browser but are
// excluded from the user registry file. See notes/asset-menagement.md
// "Mounted Packs".
namespace assetlib {

class AssetRegistry;
class PrPakReader;

class PackMounts {
public:
  static PackMounts &Get();

  // Opens the pack and registers its assets into `registry` (read-only). No-op
  // if already mounted. Returns false (and sets *error) on open failure.
  bool Mount(const std::filesystem::path &packPath, AssetRegistry &registry,
             std::string *error = nullptr);
  // Unmounts and removes the pack's read-only assets from the registry.
  bool Unmount(const std::filesystem::path &packPath, AssetRegistry &registry);

  std::vector<std::filesystem::path> mountedPaths() const;
  bool isMounted(const std::filesystem::path &packPath) const;

  // Read a cooked payload for an asset from any mounted pack (checksum-verified).
  bool ResolvePayload(const AssetId &id, PayloadKind kind,
                      std::vector<uint8_t> &out) const;

  // Persist the mounted-pack list and re-mount it on startup.
  void SaveMountList(const AssetPaths &paths) const;
  void LoadAndMountSaved(const AssetPaths &paths, AssetRegistry &registry);

private:
  PackMounts() = default;
  std::vector<std::unique_ptr<PrPakReader>> m_packs;
};

// Resolve a cooked payload for an asset: a mounted pack first, then the user
// cache file. The single entry point used by asset_runtime / asset_cooker so
// pack-provided assets resolve transparently.
bool ResolveCookedPayload(const AssetPaths &paths, const AssetId &id,
                          PayloadKind kind, std::vector<uint8_t> &out);

} // namespace assetlib
