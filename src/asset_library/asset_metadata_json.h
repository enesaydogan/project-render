#pragma once
#include "asset_metadata.h"
#include <nlohmann/json.hpp>

// Shared, renderer-free AssetMetadata <-> JSON codec used by both the registry
// file (asset-registry.json) and the .prpak pack TOC, so the two never drift.
// sourceState is derived at runtime and intentionally not serialized.
namespace assetlib {

nlohmann::json MetadataToJson(const AssetMetadata &m);
// Returns false on missing/invalid id. Other fields fall back to defaults.
bool MetadataFromJson(const nlohmann::json &j, AssetMetadata &out);

} // namespace assetlib
