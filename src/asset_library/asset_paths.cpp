#include "asset_paths.h"
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace assetlib {

std::filesystem::path NativeSourcePath(const std::string &storedPath) {
#ifdef _WIN32
  if (storedPath.empty())
    return {};
  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  int wideCount = MultiByteToWideChar(codePage, flags, storedPath.data(),
                                      static_cast<int>(storedPath.size()),
                                      nullptr, 0);
  if (wideCount <= 0) {
    // Legacy registries may contain ANSI-encoded paths; keep those usable
    // while treating new paths as UTF-8.
    codePage = CP_ACP;
    flags = 0;
    wideCount = MultiByteToWideChar(codePage, flags, storedPath.data(),
                                    static_cast<int>(storedPath.size()),
                                    nullptr, 0);
  }
  if (wideCount <= 0)
    return std::filesystem::path(storedPath);
  std::wstring widePath(static_cast<size_t>(wideCount), L'\0');
  MultiByteToWideChar(codePage, flags, storedPath.data(),
                      static_cast<int>(storedPath.size()), widePath.data(),
                      wideCount);
  return std::filesystem::path(widePath);
#else
  return std::filesystem::path(storedPath);
#endif
}

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
      cacheDir() / "Materials",
      cacheDir() / "Volumes",
      pendingCookDir(),
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
