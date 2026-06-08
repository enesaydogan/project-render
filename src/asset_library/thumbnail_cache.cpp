#include "thumbnail_cache.h"
#include <fstream>
#include <system_error>

namespace assetlib {

std::filesystem::path ThumbnailCache::PathFor(const AssetId &id) const {
  return m_paths.thumbnailsDir() / (id.ToString() + ".png");
}

bool ThumbnailCache::Has(const AssetId &id) const {
  std::error_code ec;
  return std::filesystem::exists(PathFor(id), ec);
}

bool ThumbnailCache::Store(const AssetId &id,
                           const std::vector<uint8_t> &pngBytes) const {
  std::error_code ec;
  std::filesystem::create_directories(m_paths.thumbnailsDir(), ec);
  std::filesystem::path target = PathFor(id);
  std::filesystem::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    if (!pngBytes.empty())
      out.write(reinterpret_cast<const char *>(pngBytes.data()),
                static_cast<std::streamsize>(pngBytes.size()));
    out.flush();
    if (!out)
      return false;
  }
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    std::filesystem::remove(target, ec);
    std::filesystem::rename(tmp, target, ec);
  }
  return !ec;
}

void ThumbnailCache::Remove(const AssetId &id) const {
  std::error_code ec;
  std::filesystem::remove(PathFor(id), ec);
}

} // namespace assetlib
