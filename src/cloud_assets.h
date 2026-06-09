#pragma once
#include "asset_library/asset_id.h"
#include <string>

// Bridge between the procedural cloud system (g_cloudManager / CloudParams) and
// the asset library: a CloudPreset asset snapshots the current cloud look so it
// can be saved, shared (.prpak), and re-applied. Renderer-coupled (touches
// g_cloudManager), so it lives outside the renderer-free asset_library core.
namespace CloudAssets {

// Snapshot the current cloud parameters as a CloudPreset library asset. Returns
// the new asset id (invalid if the registry is unavailable).
assetlib::AssetId SaveCurrentAsPreset(const std::string &name);

// Apply a CloudPreset asset to the live cloud system and request a re-bake.
// Returns false if the id is not a resolvable CloudPreset.
bool ApplyPreset(const assetlib::AssetId &id);

} // namespace CloudAssets
