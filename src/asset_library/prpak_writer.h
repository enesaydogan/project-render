#pragma once
#include "prpak_format.h"
#include <filesystem>
#include <string>
#include <vector>

namespace assetlib {

// Writes a .prpak bundling the given assets and their cooked payloads. Payloads
// are deduplicated by content hash. Writes atomically (temp + rename). Returns
// false and sets *error on failure.
bool WritePack(const std::filesystem::path &path, const PackMeta &meta,
               const std::vector<PackAssetInput> &assets,
               std::string *error = nullptr);

} // namespace assetlib
