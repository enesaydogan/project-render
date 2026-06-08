#pragma once
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
  std::filesystem::path thumbnailsDir() const {
    return cacheDir() / "Thumbnails";
  }

  // Creates the full directory tree if missing. Returns false on filesystem
  // error (e.g. unwritable location).
  bool EnsureLayout() const;

private:
  std::filesystem::path m_root;
};

} // namespace assetlib
