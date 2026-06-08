#include "global_registry.h"
#include <memory>

namespace assetlib {
namespace {
std::unique_ptr<AssetRegistry> g_registry;
}

AssetRegistry &InitGlobalRegistry(const std::filesystem::path &userDataRoot) {
  if (!g_registry) {
    g_registry = std::make_unique<AssetRegistry>(AssetPaths(userDataRoot));
    g_registry->Load();
  }
  return *g_registry;
}

AssetRegistry *GlobalRegistry() { return g_registry.get(); }

} // namespace assetlib
