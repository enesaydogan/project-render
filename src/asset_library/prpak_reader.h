#pragma once
#include "prpak_format.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace assetlib {

// Read-only view over a .prpak. Open() parses and integrity-checks the footer
// and TOC; payloads are read on demand by absolute offset (random access, no
// full unpack) and verified against their stored checksum.
class PrPakReader {
public:
  bool Open(const std::filesystem::path &path, std::string *error = nullptr);

  bool isOpen() const { return m_open; }
  const std::filesystem::path &path() const { return m_path; }
  const PackMeta &meta() const { return m_meta; }
  const std::vector<PackedAsset> &assets() const { return m_assets; }

  bool has(const AssetId &id) const;
  bool hasPayload(const AssetId &id, PayloadKind kind) const;

  // Random-access read of one payload, verifying its checksum.
  bool ReadPayload(const AssetId &id, PayloadKind kind,
                   std::vector<uint8_t> &out) const;

  // Full integrity sweep: every chunk checksum + every asset's chunk refs.
  // Appends human-readable findings to `report`; returns true if valid.
  bool Validate(std::string &report) const;

private:
  bool m_open = false;
  std::filesystem::path m_path;
  PackMeta m_meta;
  std::vector<PackChunk> m_chunks;
  std::vector<PackedAsset> m_assets;
};

} // namespace assetlib
