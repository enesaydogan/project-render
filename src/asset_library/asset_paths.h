#pragma once
#include "asset_id.h"
#include <filesystem>

// Resolves the on-disk layout of a Project Render user-data root, per
// notes/asset-menagement.md "Proposed Directory Layout". This class is
// platform/UI-agnostic: callers (the Qt layer) supply the writable root, which
// keeps the core testable without Qt or a real user profile.
namespace assetlib {

class AssetPaths {
public:
  // root is the "Project Render User Data" directory. Subdirectories are
  // created lazily by EnsureLayout().
  explicit AssetPaths(std::filesystem::path root) : m_root(std::move(root)) {}

  const std::filesystem::path &root() const { return m_root; }

  std::filesystem::path assetsDir() const { return m_root / "Assets"; }
  std::filesystem::path metadataDir() const { return m_root / "Metadata"; }
  std::filesystem::path cacheDir() const { return m_root / "Cache"; }
  std::filesystem::path packsDir() const { return m_root / "Packs"; }

  std::filesystem::path registryFile() const {
    return metadataDir() / "asset-registry.json";
  }
  std::filesystem::path favoritesFile() const {
    return metadataDir() / "favorites.json";
  }
  std::filesystem::path mountsFile() const {
    return metadataDir() / "packs.json";
  }
  std::filesystem::path thumbnailsDir() const {
    return cacheDir() / "Thumbnails";
  }
  std::filesystem::path pendingCookDir() const {
    return cacheDir() / "Pending";
  }
  std::filesystem::path pendingCookPath(const AssetId &id) const {
    return pendingCookDir() / (id.ToString() + ".prcook");
  }

  // Cooked runtime payload locations, keyed by AssetId.
  std::filesystem::path cookedMeshPath(const AssetId &id) const {
    return cacheDir() / "Meshes" / (id.ToString() + ".prmesh");
  }
  std::filesystem::path cookedTexturePath(const AssetId &id) const {
    return cacheDir() / "Textures" / (id.ToString() + ".prtex");
  }
  std::filesystem::path cookedMaterialPath(const AssetId &id) const {
    return cacheDir() / "Materials" / (id.ToString() + ".prmat");
  }
  std::filesystem::path cookedVolumePath(const AssetId &id) const {
    return cacheDir() / "Volumes" / (id.ToString() + ".prvol");
  }
  std::filesystem::path cookedVolumeFramePath(const AssetId &id,
                                              uint32_t frame) const {
    return cacheDir() / "Volumes" /
           (id.ToString() + "_" + std::to_string(frame) + ".prvol");
  }

  // Creates the full directory tree if missing. Returns false on filesystem
  // error (e.g. unwritable location).
  bool EnsureLayout() const;

private:
  std::filesystem::path m_root;
};

// Convert a stored source-path string (UTF-8, with legacy ANSI fallback) to a
// usable filesystem path. On Windows, fs::path(std::string) interprets narrow
// strings in the local code page, which silently breaks non-ASCII UTF-8 paths:
// exists() returns false, timestamps/hashes read as 0. Every assetlib site
// that touches AssetMetadata::sourcePath must go through this.
std::filesystem::path NativeSourcePath(const std::string &storedPath);

} // namespace assetlib
