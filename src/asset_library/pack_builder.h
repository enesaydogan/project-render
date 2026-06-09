#pragma once
#include "asset_id.h"
#include "asset_paths.h"
#include "prpak_format.h"
#include <filesystem>
#include <string>
#include <vector>

// Builds a .prpak from registry assets + their cooked cache payloads. Pure
// (registry + filesystem only). See notes/asset-menagement.md "User Pack
// Creation".
namespace assetlib {

class AssetRegistry;

// Returns `roots` plus all transitively-referenced dependency AssetIds present
// in the registry (deduplicated).
std::vector<AssetId> CollectWithDependencies(const AssetRegistry &registry,
                                             const std::vector<AssetId> &roots);

// Gathers metadata + cooked payloads for `assetIds` (and writes them) into a
// .prpak at `out`. Missing payloads/dependencies are reported in `warnings` but
// do not abort the build. Returns false (and sets *error) only on hard failure.
bool BuildPack(const AssetRegistry &registry, const AssetPaths &paths,
               const std::vector<AssetId> &assetIds, const PackMeta &meta,
               const std::filesystem::path &out, std::string *error,
               std::vector<std::string> *warnings);

} // namespace assetlib
