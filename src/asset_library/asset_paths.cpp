#include "asset_paths.h"
#include <system_error>

namespace assetlib {

bool AssetPaths::EnsureLayout() const {
  std::error_code ec;
  const std::filesystem::path dirs[] = {
      m_root,
      assetsDir(),
      assetsDir() / "Models",
      assetsDir() / "Materials",
      assetsDir() / "Textures",
      assetsDir() / "Scatter",
      assetsDir() / "Clouds",
      assetsDir() / "HDRI",
      assetsDir() / "Presets",
      metadataDir(),
      cacheDir(),
      cacheDir() / "Meshes",
      cacheDir() / "Textures",
      cacheDir() / "Volumes",
      thumbnailsDir(),
      packsDir(),
  };
  for (const auto &d : dirs) {
    std::filesystem::create_directories(d, ec);
    // create_directories returns false when the dir already exists, which is
    // not an error; only a populated error_code is a real failure.
    if (ec)
      return false;
  }
  return true;
}

} // namespace assetlib
