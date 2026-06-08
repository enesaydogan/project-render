#pragma once
#include "asset_metadata.h"
#include "asset_paths.h"
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// In-memory asset library backed by a versioned JSON file. Owns identity,
// metadata, virtual-folder organization, tags, favorites, and search. It does
// not decode authoring formats (that stays in the Asset:: loader) and does not
// touch the renderer. See notes/asset-menagement.md "Implementation
// Boundaries".
namespace assetlib {

// Filter for SearchAssets. Empty/none fields are ignored.
struct AssetQuery {
  std::string text;                 // case-insensitive substring match
  std::optional<AssetType> type;    // exact type
  std::string tag;                  // exact tag membership
  std::optional<std::string> folder; // exact virtualPath (not recursive)
  bool favoritesOnly = false;
};

class AssetRegistry {
public:
  using ChangeListener = std::function<void()>;

  explicit AssetRegistry(AssetPaths paths);

  const AssetPaths &paths() const { return m_paths; }

  // Loads the registry + favorites from disk, creating the directory layout if
  // needed. Returns false only on a hard filesystem/parse failure; a missing
  // registry file is treated as an empty library (success).
  bool Load();
  // Atomically writes the registry (and favorites) to disk.
  bool Save();

  // --- CRUD ---------------------------------------------------------------
  // Adds a record. If meta.id is invalid a fresh id is generated. Returns the
  // final id. Registers the asset's folder path. Marks the registry dirty and
  // notifies listeners.
  AssetId Add(AssetMetadata meta);
  bool Remove(const AssetId &id);
  // Replaces the record for meta.id. Returns false if it does not exist.
  bool Update(const AssetMetadata &meta);
  const AssetMetadata *Get(const AssetId &id) const;

  std::vector<AssetId> AllAssets() const;
  size_t AssetCount() const { return m_assets.size(); }

  // --- Folders (virtual) --------------------------------------------------
  const std::set<std::string> &Folders() const { return m_folders; }
  void CreateFolder(const std::string &path);
  // Renames a folder and re-parents every asset and sub-folder under it.
  bool RenameFolder(const std::string &oldPath, const std::string &newPath);
  // Removes a folder. Assets directly within it move to its parent unless
  // removeContainedAssets is set, in which case they are deleted.
  bool DeleteFolder(const std::string &path, bool removeContainedAssets);

  // --- Tags ---------------------------------------------------------------
  bool AddTag(const AssetId &id, const std::string &tag);
  bool RemoveTag(const AssetId &id, const std::string &tag);
  std::set<std::string> AllTags() const;

  // --- Favorites (user-local; never mutate asset records or packs) --------
  bool IsFavorite(const AssetId &id) const;
  void SetFavorite(const AssetId &id, bool favorite);
  // Favorited ids in insertion order, including ids whose asset is currently
  // missing from the registry (so they display as unavailable).
  const std::vector<AssetId> &Favorites() const { return m_favorites; }

  // --- Recent -------------------------------------------------------------
  void TouchRecent(const AssetId &id);
  const std::vector<AssetId> &Recent() const { return m_recent; }

  // --- Search / state -----------------------------------------------------
  std::vector<AssetId> SearchAssets(const AssetQuery &query) const;
  // Recomputes SourceState for every asset by checking sourcePath existence.
  void RefreshSourceStates();
  // Ids whose source is recorded but missing, or whose cook failed/corrupt.
  std::vector<AssetId> MissingOrFailed() const;

  // --- Change notification ------------------------------------------------
  size_t AddChangeListener(ChangeListener cb);
  void RemoveChangeListener(size_t id);

private:
  void RegisterFolderChain(const std::string &path);
  void NotifyChanged();
  bool SaveRegistryFile();
  bool SaveFavoritesFile();

  AssetPaths m_paths;
  std::map<AssetId, AssetMetadata> m_assets;
  std::set<std::string> m_folders;
  std::vector<AssetId> m_favorites;
  std::vector<AssetId> m_recent;
  static constexpr size_t kMaxRecent = 50;

  std::map<size_t, ChangeListener> m_listeners;
  size_t m_nextListenerId = 1;
};

} // namespace assetlib
