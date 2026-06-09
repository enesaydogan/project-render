#include "asset_registry.h"

#include "asset_metadata_json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

using json = nlohmann::json;

namespace assetlib {
namespace {

constexpr int kRegistrySchemaVersion = 1;
constexpr int kFavoritesSchemaVersion = 1;

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool ContainsCI(const std::string &haystack, const std::string &needleLower) {
  if (needleLower.empty())
    return true;
  return ToLower(haystack).find(needleLower) != std::string::npos;
}

// Normalize a virtual folder path: strip leading/trailing '/', collapse empty
// segments. Returns "" for the root.
std::string NormalizeFolder(const std::string &path) {
  std::string out;
  size_t i = 0;
  while (i < path.size()) {
    while (i < path.size() && path[i] == '/')
      ++i;
    size_t start = i;
    while (i < path.size() && path[i] != '/')
      ++i;
    if (i > start) {
      if (!out.empty())
        out += '/';
      out.append(path, start, i - start);
    }
  }
  return out;
}

std::string ParentFolder(const std::string &path) {
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos)
    return "";
  return path.substr(0, slash);
}

// True if `child` is `prefix` itself or nested beneath it.
bool IsUnder(const std::string &child, const std::string &prefix) {
  if (prefix.empty())
    return true; // root contains everything
  if (child == prefix)
    return true;
  return child.size() > prefix.size() && child.compare(0, prefix.size(), prefix) == 0 &&
         child[prefix.size()] == '/';
}

// AssetMetadata <-> JSON now lives in asset_metadata_json.{h,cpp} (shared with
// the .prpak pack TOC). The registry normalizes virtualPath after parsing.

// Write `text` to `target` atomically: write a sibling temp file, then rename
// over the destination. Returns false on any filesystem error.
bool AtomicWrite(const std::filesystem::path &target, const std::string &text) {
  std::filesystem::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out)
      return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    // Fallback for platforms/filesystems where rename won't replace.
    std::filesystem::remove(target, ec);
    std::filesystem::rename(tmp, target, ec);
  }
  return !ec;
}

} // namespace

AssetRegistry::AssetRegistry(AssetPaths paths) : m_paths(std::move(paths)) {}

void AssetRegistry::RegisterFolderChain(const std::string &path) {
  std::string norm = NormalizeFolder(path);
  if (norm.empty())
    return;
  // Register every ancestor segment so intermediate folders exist.
  size_t pos = 0;
  while (true) {
    size_t slash = norm.find('/', pos);
    std::string chain =
        slash == std::string::npos ? norm : norm.substr(0, slash);
    m_folders.insert(chain);
    if (slash == std::string::npos)
      break;
    pos = slash + 1;
  }
}

bool AssetRegistry::Load() {
  if (!m_paths.EnsureLayout())
    return false;

  m_assets.clear();
  m_folders.clear();
  m_favorites.clear();
  m_recent.clear();

  // Registry file (absent == empty library, not an error).
  std::error_code ec;
  if (std::filesystem::exists(m_paths.registryFile(), ec)) {
    std::ifstream in(m_paths.registryFile(), std::ios::binary);
    if (!in)
      return false;
    json root;
    try {
      in >> root;
    } catch (const json::exception &) {
      return false;
    }
    if (root.contains("folders") && root["folders"].is_array()) {
      for (const auto &f : root["folders"])
        if (f.is_string())
          RegisterFolderChain(f.get<std::string>());
    }
    if (root.contains("assets") && root["assets"].is_array()) {
      for (const auto &ja : root["assets"]) {
        AssetMetadata m;
        if (MetadataFromJson(ja, m)) {
          m.virtualPath = NormalizeFolder(m.virtualPath);
          RegisterFolderChain(m.virtualPath);
          m_assets[m.id] = std::move(m);
        }
      }
    }
  }

  // Favorites/recent file (also optional).
  if (std::filesystem::exists(m_paths.favoritesFile(), ec)) {
    std::ifstream in(m_paths.favoritesFile(), std::ios::binary);
    if (in) {
      json root;
      try {
        in >> root;
        if (root.contains("favorites") && root["favorites"].is_array()) {
          for (const auto &f : root["favorites"]) {
            AssetId id;
            if (f.is_string() && AssetId::FromString(f.get<std::string>(), id))
              m_favorites.push_back(id);
          }
        }
        if (root.contains("recent") && root["recent"].is_array()) {
          for (const auto &r : root["recent"]) {
            AssetId id;
            if (r.is_string() && AssetId::FromString(r.get<std::string>(), id))
              m_recent.push_back(id);
          }
        }
      } catch (const json::exception &) {
        // Corrupt favorites are non-fatal; start with none.
        m_favorites.clear();
        m_recent.clear();
      }
    }
  }

  RefreshSourceStates();
  return true;
}

bool AssetRegistry::SaveRegistryFile() {
  json root;
  root["schemaVersion"] = kRegistrySchemaVersion;
  json folders = json::array();
  for (const auto &f : m_folders)
    folders.push_back(f);
  root["folders"] = std::move(folders);
  json assets = json::array();
  for (const auto &kv : m_assets)
    if (!kv.second.fromPack) // mounted-pack assets live in the pack, not here
      assets.push_back(MetadataToJson(kv.second));
  root["assets"] = std::move(assets);
  return AtomicWrite(m_paths.registryFile(), root.dump(2));
}

bool AssetRegistry::SaveFavoritesFile() {
  json root;
  root["schemaVersion"] = kFavoritesSchemaVersion;
  json favs = json::array();
  for (const auto &id : m_favorites)
    favs.push_back(id.ToString());
  root["favorites"] = std::move(favs);
  json recent = json::array();
  for (const auto &id : m_recent)
    recent.push_back(id.ToString());
  root["recent"] = std::move(recent);
  return AtomicWrite(m_paths.favoritesFile(), root.dump(2));
}

bool AssetRegistry::Save() {
  if (!m_paths.EnsureLayout())
    return false;
  bool ok = SaveRegistryFile();
  ok = SaveFavoritesFile() && ok;
  return ok;
}

AssetId AssetRegistry::Add(AssetMetadata meta) {
  if (!meta.id.valid())
    meta.id = AssetId::Generate();
  meta.virtualPath = NormalizeFolder(meta.virtualPath);
  RegisterFolderChain(meta.virtualPath);
  AssetId id = meta.id;
  m_assets[id] = std::move(meta);
  NotifyChanged();
  return id;
}

bool AssetRegistry::Remove(const AssetId &id) {
  auto it = m_assets.find(id);
  if (it == m_assets.end())
    return false;
  m_assets.erase(it);
  // Favorites for a removed asset are intentionally kept (display as missing).
  NotifyChanged();
  return true;
}

bool AssetRegistry::Update(const AssetMetadata &meta) {
  auto it = m_assets.find(meta.id);
  if (it == m_assets.end())
    return false;
  AssetMetadata copy = meta;
  copy.virtualPath = NormalizeFolder(copy.virtualPath);
  RegisterFolderChain(copy.virtualPath);
  it->second = std::move(copy);
  NotifyChanged();
  return true;
}

const AssetMetadata *AssetRegistry::Get(const AssetId &id) const {
  auto it = m_assets.find(id);
  return it == m_assets.end() ? nullptr : &it->second;
}

size_t AssetRegistry::ClearUserAssets() {
  size_t removed = 0;
  for (auto it = m_assets.begin(); it != m_assets.end();) {
    if (!it->second.fromPack) {
      it = m_assets.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  m_favorites.clear();
  m_recent.clear();
  // Rebuild folder set from the assets that remain (mounted-pack assets).
  m_folders.clear();
  for (const auto &kv : m_assets)
    RegisterFolderChain(kv.second.virtualPath);
  NotifyChanged();
  return removed;
}

std::vector<AssetId> AssetRegistry::AllAssets() const {
  std::vector<AssetId> out;
  out.reserve(m_assets.size());
  for (const auto &kv : m_assets)
    out.push_back(kv.first);
  return out;
}

void AssetRegistry::CreateFolder(const std::string &path) {
  std::string norm = NormalizeFolder(path);
  if (norm.empty())
    return;
  RegisterFolderChain(norm);
  NotifyChanged();
}

bool AssetRegistry::RenameFolder(const std::string &oldPath,
                                 const std::string &newPath) {
  std::string from = NormalizeFolder(oldPath);
  std::string to = NormalizeFolder(newPath);
  if (from.empty() || to.empty() || from == to)
    return false;
  if (m_folders.find(from) == m_folders.end())
    return false;
  // Disallow renaming a folder into its own subtree.
  if (IsUnder(to, from))
    return false;

  // Re-parent matching folders.
  std::set<std::string> updated;
  for (const auto &f : m_folders) {
    if (IsUnder(f, from))
      updated.insert(to + f.substr(from.size()));
    else
      updated.insert(f);
  }
  m_folders = std::move(updated);
  RegisterFolderChain(to);

  // Re-parent matching assets.
  for (auto &kv : m_assets) {
    if (IsUnder(kv.second.virtualPath, from))
      kv.second.virtualPath = to + kv.second.virtualPath.substr(from.size());
  }
  NotifyChanged();
  return true;
}

bool AssetRegistry::DeleteFolder(const std::string &path,
                                 bool removeContainedAssets) {
  std::string target = NormalizeFolder(path);
  if (target.empty())
    return false;
  if (m_folders.find(target) == m_folders.end())
    return false;
  std::string parent = ParentFolder(target);

  // Handle contained assets first.
  for (auto it = m_assets.begin(); it != m_assets.end();) {
    if (IsUnder(it->second.virtualPath, target)) {
      if (removeContainedAssets) {
        it = m_assets.erase(it);
        continue;
      }
      it->second.virtualPath = parent;
    }
    ++it;
  }

  // Drop the folder and all descendants.
  for (auto it = m_folders.begin(); it != m_folders.end();) {
    if (IsUnder(*it, target))
      it = m_folders.erase(it);
    else
      ++it;
  }
  NotifyChanged();
  return true;
}

bool AssetRegistry::AddTag(const AssetId &id, const std::string &tag) {
  auto it = m_assets.find(id);
  if (it == m_assets.end() || tag.empty())
    return false;
  auto &tags = it->second.tags;
  if (std::find(tags.begin(), tags.end(), tag) != tags.end())
    return false;
  tags.push_back(tag);
  NotifyChanged();
  return true;
}

bool AssetRegistry::RemoveTag(const AssetId &id, const std::string &tag) {
  auto it = m_assets.find(id);
  if (it == m_assets.end())
    return false;
  auto &tags = it->second.tags;
  auto found = std::find(tags.begin(), tags.end(), tag);
  if (found == tags.end())
    return false;
  tags.erase(found);
  NotifyChanged();
  return true;
}

std::set<std::string> AssetRegistry::AllTags() const {
  std::set<std::string> out;
  for (const auto &kv : m_assets)
    for (const auto &t : kv.second.tags)
      out.insert(t);
  return out;
}

bool AssetRegistry::IsFavorite(const AssetId &id) const {
  return std::find(m_favorites.begin(), m_favorites.end(), id) !=
         m_favorites.end();
}

void AssetRegistry::SetFavorite(const AssetId &id, bool favorite) {
  auto it = std::find(m_favorites.begin(), m_favorites.end(), id);
  bool changed = false;
  if (favorite && it == m_favorites.end()) {
    m_favorites.push_back(id);
    changed = true;
  } else if (!favorite && it != m_favorites.end()) {
    m_favorites.erase(it);
    changed = true;
  }
  if (changed)
    NotifyChanged();
}

void AssetRegistry::TouchRecent(const AssetId &id) {
  auto it = std::find(m_recent.begin(), m_recent.end(), id);
  if (it != m_recent.end())
    m_recent.erase(it);
  m_recent.insert(m_recent.begin(), id);
  if (m_recent.size() > kMaxRecent)
    m_recent.resize(kMaxRecent);
  NotifyChanged();
}

std::vector<AssetId> AssetRegistry::SearchAssets(const AssetQuery &query) const {
  const std::string textLower = ToLower(query.text);
  std::vector<AssetId> out;
  for (const auto &kv : m_assets) {
    const AssetMetadata &m = kv.second;
    if (query.type && m.type != *query.type)
      continue;
    if (query.folder && m.virtualPath != *query.folder)
      continue;
    if (!query.tag.empty() &&
        std::find(m.tags.begin(), m.tags.end(), query.tag) == m.tags.end())
      continue;
    if (query.favoritesOnly && !IsFavorite(m.id))
      continue;
    if (!textLower.empty()) {
      bool match = ContainsCI(m.displayName, textLower) ||
                   ContainsCI(AssetTypeDisplayName(m.type), textLower) ||
                   ContainsCI(m.virtualPath, textLower) ||
                   ContainsCI(m.attribution, textLower);
      if (!match) {
        for (const auto &t : m.tags) {
          if (ContainsCI(t, textLower)) {
            match = true;
            break;
          }
        }
      }
      if (!match)
        continue;
    }
    out.push_back(m.id);
  }
  return out;
}

void AssetRegistry::RefreshSourceStates() {
  std::error_code ec;
  for (auto &kv : m_assets) {
    AssetMetadata &m = kv.second;
    if (m.sourcePath.empty()) {
      m.sourceState = SourceState::None;
    } else if (std::filesystem::exists(std::filesystem::path(m.sourcePath), ec)) {
      m.sourceState = SourceState::Available;
    } else {
      m.sourceState = SourceState::Missing;
    }
  }
}

std::vector<AssetId> AssetRegistry::MissingOrFailed() const {
  std::vector<AssetId> out;
  for (const auto &kv : m_assets) {
    const AssetMetadata &m = kv.second;
    if (m.sourceState == SourceState::Missing ||
        m.cookState == CookState::Failed || m.cookState == CookState::Corrupt)
      out.push_back(m.id);
  }
  return out;
}

size_t AssetRegistry::AddChangeListener(ChangeListener cb) {
  size_t id = m_nextListenerId++;
  m_listeners[id] = std::move(cb);
  return id;
}

void AssetRegistry::RemoveChangeListener(size_t id) { m_listeners.erase(id); }

void AssetRegistry::NotifyChanged() {
  for (const auto &kv : m_listeners)
    if (kv.second)
      kv.second();
}

} // namespace assetlib
