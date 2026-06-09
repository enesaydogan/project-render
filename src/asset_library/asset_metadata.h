#pragma once
#include "asset_id.h"
#include "asset_types.h"
#include <cstdint>
#include <string>
#include <vector>

// Per-asset record stored in the registry. Mirrors the "Asset Identity" list in
// notes/asset-menagement.md. Cooking-related fields (cookerVersion,
// cookedPayloadHash, cookState) are present so the JSON schema is stable, but
// they are not populated until Phase 2 (Model/Material/Texture cooking).
namespace assetlib {

// Whether the original authoring/source file backing this asset is reachable.
enum class SourceState : uint32_t {
  None = 0,    // asset has no external source (authored in-engine)
  Available,   // source file exists on disk
  Missing,     // source path recorded but file not found
};

// Whether a usable cooked runtime payload exists. Unused in Phase 1; every
// asset reports NotCooked until the cooker lands.
enum class CookState : uint32_t {
  NotCooked = 0,
  Current,
  Stale,
  Corrupt,
  Failed,
};

inline const char *SourceStateToString(SourceState s) {
  switch (s) {
  case SourceState::Available:
    return "available";
  case SourceState::Missing:
    return "missing";
  case SourceState::None:
  default:
    return "none";
  }
}

inline const char *CookStateToString(CookState s) {
  switch (s) {
  case CookState::Current:
    return "current";
  case CookState::Stale:
    return "stale";
  case CookState::Corrupt:
    return "corrupt";
  case CookState::Failed:
    return "failed";
  case CookState::NotCooked:
  default:
    return "not_cooked";
  }
}

inline CookState CookStateFromString(const std::string &s) {
  if (s == "current")
    return CookState::Current;
  if (s == "stale")
    return CookState::Stale;
  if (s == "corrupt")
    return CookState::Corrupt;
  if (s == "failed")
    return CookState::Failed;
  return CookState::NotCooked;
}

struct AssetMetadata {
  AssetId id;
  AssetType type = AssetType::Unknown;

  std::string displayName;
  // Virtual library path using '/' separators, e.g. "Trees/Oak". Does not
  // include the display name. Empty == library root.
  std::string virtualPath;

  // Original authoring file, when one exists. May be absent for in-engine
  // authored assets.
  std::string sourcePath;
  // 64-bit content hash + last-modified time of the source, used to detect
  // changes that should drive recooking (Phase 2). 0 == unknown.
  uint64_t sourceContentHash = 0;
  int64_t sourceTimestamp = 0;

  // Cooking metadata (Phase 2+). Retained in the schema from day one.
  uint32_t cookerVersion = 0;
  uint64_t cookedPayloadHash = 0;
  CookState cookState = CookState::NotCooked;

  // Stable ids of assets this one depends on (e.g. a material's textures).
  std::vector<AssetId> dependencies;

  std::vector<std::string> tags;

  // Relative path (under Cache/Thumbnails) of a generated preview, if any.
  std::string thumbnailRef;

  // Opaque, importer-defined settings serialized as a JSON object string.
  // Treated as a blob by the registry in Phase 1.
  std::string importSettingsJson;

  std::string license;
  std::string attribution;

  // Derived at runtime from sourcePath; not persisted authoritatively.
  SourceState sourceState = SourceState::None;

  // Runtime-only (never serialized): set for assets provided by a mounted
  // read-only .prpak. Such assets are excluded from the user registry file and
  // are not editable; their payloads resolve from the pack, not the cache.
  bool fromPack = false;
  std::string packPath;
};

} // namespace assetlib
