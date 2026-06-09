#include "prpak_reader.h"

#include "asset_metadata_json.h"
#include "cooked_payload.h" // HashBytes

#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace assetlib {
namespace {

uint64_t ReadU64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<uint64_t>(p[i]) << (i * 8);
  return v;
}

bool ReadFileRange(const std::filesystem::path &path, uint64_t offset,
                   uint64_t size, std::vector<uint8_t> &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  in.seekg(static_cast<std::streamoff>(offset));
  if (!in)
    return false;
  out.resize(static_cast<size_t>(size));
  if (size > 0)
    in.read(reinterpret_cast<char *>(out.data()),
            static_cast<std::streamsize>(size));
  return static_cast<bool>(in);
}

} // namespace

bool PrPakReader::Open(const std::filesystem::path &path, std::string *error) {
  auto fail = [&](const char *msg) {
    if (error)
      *error = msg;
    return false;
  };
  m_open = false;
  m_chunks.clear();
  m_assets.clear();
  m_meta = PackMeta{};

  std::error_code ec;
  const uint64_t fileSize =
      static_cast<uint64_t>(std::filesystem::file_size(path, ec));
  if (ec || fileSize < sizeof(kPrPakMagic) + kPrPakFooterSize)
    return fail("file too small or unreadable");

  // Verify file magic.
  std::vector<uint8_t> magic;
  if (!ReadFileRange(path, 0, sizeof(kPrPakMagic), magic) ||
      std::memcmp(magic.data(), kPrPakMagic, sizeof(kPrPakMagic)) != 0)
    return fail("bad pack magic");

  // Footer.
  std::vector<uint8_t> footer;
  if (!ReadFileRange(path, fileSize - kPrPakFooterSize, kPrPakFooterSize,
                     footer))
    return fail("could not read footer");
  if (std::memcmp(footer.data(), kPrPakFooterMagic, sizeof(kPrPakFooterMagic)) !=
      0)
    return fail("bad footer magic");
  const uint64_t tocOffset = ReadU64(footer.data() + 8);
  const uint64_t tocSize = ReadU64(footer.data() + 16);
  const uint64_t tocHash = ReadU64(footer.data() + 24);
  if (tocOffset + tocSize > fileSize - kPrPakFooterSize)
    return fail("TOC bounds invalid");

  // TOC + integrity.
  std::vector<uint8_t> tocBytes;
  if (!ReadFileRange(path, tocOffset, tocSize, tocBytes))
    return fail("could not read TOC");
  if (HashBytes(tocBytes.data(), tocBytes.size()) != tocHash)
    return fail("TOC checksum mismatch (corrupt pack)");

  json toc;
  try {
    toc = json::from_msgpack(tocBytes);
  } catch (const json::exception &) {
    return fail("TOC parse failed");
  }
  if (toc.value("schemaVersion", 0u) != kPrPakSchemaVersion)
    return fail("unsupported pack schema version");

  if (toc.contains("pack")) {
    const auto &p = toc["pack"];
    m_meta.name = p.value("name", std::string());
    m_meta.attribution = p.value("attribution", std::string());
    m_meta.license = p.value("license", std::string());
    m_meta.created = p.value("created", int64_t{0});
  }
  if (toc.contains("chunks") && toc["chunks"].is_array()) {
    for (const auto &jc : toc["chunks"]) {
      PackChunk c;
      c.hash = jc.value("hash", uint64_t{0});
      c.offset = jc.value("offset", uint64_t{0});
      c.size = jc.value("size", uint64_t{0});
      m_chunks.push_back(c);
    }
  }
  if (toc.contains("assets") && toc["assets"].is_array()) {
    for (const auto &ja : toc["assets"]) {
      PackedAsset pa;
      if (!MetadataFromJson(ja, pa.meta))
        continue;
      if (ja.contains("payloads")) {
        const auto &pl = ja["payloads"];
        pa.meshChunk = pl.value("mesh", -1);
        pa.textureChunk = pl.value("texture", -1);
        pa.materialChunk = pl.value("material", -1);
      }
      m_assets.push_back(std::move(pa));
    }
  }

  m_path = path;
  m_open = true;
  return true;
}

bool PrPakReader::has(const AssetId &id) const {
  for (const auto &a : m_assets)
    if (a.meta.id == id)
      return true;
  return false;
}

bool PrPakReader::hasPayload(const AssetId &id, PayloadKind kind) const {
  for (const auto &a : m_assets)
    if (a.meta.id == id)
      return a.chunkFor(kind) >= 0;
  return false;
}

bool PrPakReader::ReadPayload(const AssetId &id, PayloadKind kind,
                              std::vector<uint8_t> &out) const {
  if (!m_open)
    return false;
  const PackedAsset *asset = nullptr;
  for (const auto &a : m_assets)
    if (a.meta.id == id) {
      asset = &a;
      break;
    }
  if (!asset)
    return false;
  const int idx = asset->chunkFor(kind);
  if (idx < 0 || idx >= static_cast<int>(m_chunks.size()))
    return false;
  const PackChunk &c = m_chunks[static_cast<size_t>(idx)];
  if (!ReadFileRange(m_path, c.offset, c.size, out))
    return false;
  if (HashBytes(out.data(), out.size()) != c.hash) {
    out.clear();
    return false; // corrupt chunk
  }
  return true;
}

bool PrPakReader::Validate(std::string &report) const {
  if (!m_open) {
    report += "pack not open\n";
    return false;
  }
  bool ok = true;
  // Every chunk's stored bytes must hash to its recorded value.
  for (size_t i = 0; i < m_chunks.size(); ++i) {
    std::vector<uint8_t> bytes;
    if (!ReadFileRange(m_path, m_chunks[i].offset, m_chunks[i].size, bytes)) {
      report += "chunk " + std::to_string(i) + ": unreadable\n";
      ok = false;
      continue;
    }
    if (HashBytes(bytes.data(), bytes.size()) != m_chunks[i].hash) {
      report += "chunk " + std::to_string(i) + ": checksum mismatch\n";
      ok = false;
    }
  }
  // Every asset's chunk references must be in range.
  for (const auto &a : m_assets) {
    const int refs[] = {a.meshChunk, a.textureChunk, a.materialChunk};
    for (int r : refs) {
      if (r >= static_cast<int>(m_chunks.size())) {
        report += "asset " + a.meta.id.ToString() +
                  ": dangling chunk reference\n";
        ok = false;
      }
    }
  }
  report += ok ? "pack OK: " + std::to_string(m_assets.size()) + " assets, " +
                     std::to_string(m_chunks.size()) + " chunks\n"
               : "pack INVALID\n";
  return ok;
}

} // namespace assetlib
