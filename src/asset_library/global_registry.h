#pragma once
#include "asset_registry.h"
#include <filesystem>

// Process-wide AssetRegistry accessor, mirroring the free-function global-state
// pattern used by Scene:: . The Qt layer initializes it once at startup with a
// platform-resolved user-data root; panels reach it via GlobalRegistry().
namespace assetlib {

// Creates (once) and loads the global registry rooted at userDataRoot. Repeated
// calls return the existing instance and ignore the argument. Never null.
AssetRegistry &InitGlobalRegistry(const std::filesystem::path &userDataRoot);

// Returns the global registry, or nullptr if InitGlobalRegistry has not run.
AssetRegistry *GlobalRegistry();

} // namespace assetlib
