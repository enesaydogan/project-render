#include "global_registry.h"
#include "pack_mounts.h"
#include <memory>

namespace assetlib {
namespace {
std::unique_ptr<AssetRegistry> g_registry;
}

AssetRegistry &InitGlobalRegistry(const std::filesystem::path &userDataRoot) {
  if (!g_registry) {
    g_registry = std::make_unique<AssetRegistry>(AssetPaths(userDataRoot));
    g_registry->Load();
    // Re-mount previously mounted .prpak archives (read-only) on startup.
    PackMounts::Get().LoadAndMountSaved(g_registry->paths(), *g_registry);
  }
  return *g_registry;
}

AssetRegistry *GlobalRegistry() { return g_registry.get(); }

} // namespace assetlib
