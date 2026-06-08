#pragma once
#include "asset_id.h"
#include "asset_paths.h"
#include <cstdint>
#include <filesystem>
#include <vector>

// Phase 1 scaffolding for cached asset previews. It only manages thumbnail file
// locations under Cache/Thumbnails and stores raw bytes a caller provides. Real
// GPU-rendered thumbnails are an open decision (see notes/asset-menagement.md)
// and land in a later phase; until then the Asset Manager draws typed
// placeholders for assets without a cached preview.
namespace assetlib {

class ThumbnailCache {
public:
  explicit ThumbnailCache(AssetPaths paths) : m_paths(std::move(paths)) {}

  // Deterministic on-disk location for an asset's thumbnail (PNG).
  std::filesystem::path PathFor(const AssetId &id) const;

  bool Has(const AssetId &id) const;

  // Atomically writes pre-encoded image bytes (PNG) for an asset. Returns false
  // on filesystem error.
  bool Store(const AssetId &id, const std::vector<uint8_t> &pngBytes) const;

  void Remove(const AssetId &id) const;

private:
  AssetPaths m_paths;
};

} // namespace assetlib
