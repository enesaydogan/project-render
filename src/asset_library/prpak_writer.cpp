#include "prpak_writer.h"

#include "asset_metadata_json.h"
#include "cooked_payload.h" // HashBytes

#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_map>

using json = nlohmann::json;

namespace assetlib {
namespace {

void AppendU64(std::vector<uint8_t> &buf, uint64_t v) {
  for (int i = 0; i < 8; ++i)
    buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

} // namespace

bool WritePack(const std::filesystem::path &path, const PackMeta &meta,
               const std::vector<PackAssetInput> &assets, std::string *error) {
  auto fail = [&](const char *msg) {
    if (error)
      *error = msg;
    return false;
  };

  // Build the payload region and chunk table in memory (dedup by content hash).
  std::vector<uint8_t> payloadRegion;
  std::vector<PackChunk> chunks;
  std::unordered_map<uint64_t, int> chunkByHash;
  const uint64_t payloadBase = sizeof(kPrPakMagic); // payloads start after magic

  auto addChunk = [&](const std::vector<uint8_t> &bytes) -> int {
    if (bytes.empty())
      return -1;
    const uint64_t h = HashBytes(bytes.data(), bytes.size());
    auto it = chunkByHash.find(h);
    if (it != chunkByHash.end())
      return it->second; // deduplicated
    PackChunk c;
    c.hash = h;
    c.offset = payloadBase + payloadRegion.size();
    c.size = bytes.size();
    const int idx = static_cast<int>(chunks.size());
    chunks.push_back(c);
    chunkByHash[h] = idx;
    payloadRegion.insert(payloadRegion.end(), bytes.begin(), bytes.end());
    return idx;
  };

  json assetArr = json::array();
  for (const auto &a : assets) {
    const int meshIdx = addChunk(a.meshPayload);
    const int texIdx = addChunk(a.texturePayload);
    const int matIdx = addChunk(a.materialPayload);
    const int volIdx = addChunk(a.volumePayload);
    json ja = MetadataToJson(a.meta);
    json payloads = json::object();
    if (meshIdx >= 0)
      payloads["mesh"] = meshIdx;
    if (texIdx >= 0)
      payloads["texture"] = texIdx;
    if (matIdx >= 0)
      payloads["material"] = matIdx;
    if (volIdx >= 0)
      payloads["volume"] = volIdx;
    ja["payloads"] = std::move(payloads);
    assetArr.push_back(std::move(ja));
  }

  json chunkArr = json::array();
  for (const auto &c : chunks) {
    json jc;
    jc["hash"] = c.hash;
    jc["offset"] = c.offset;
    jc["size"] = c.size;
    chunkArr.push_back(std::move(jc));
  }

  json toc;
  toc["schemaVersion"] = kPrPakSchemaVersion;
  toc["pack"] = {{"name", meta.name},
                 {"attribution", meta.attribution},
                 {"license", meta.license},
                 {"created", meta.created}};
  toc["chunks"] = std::move(chunkArr);
  toc["assets"] = std::move(assetArr);
  std::vector<uint8_t> tocBytes = json::to_msgpack(toc);

  const uint64_t tocOffset = payloadBase + payloadRegion.size();
  const uint64_t tocHash = HashBytes(tocBytes.data(), tocBytes.size());

  std::vector<uint8_t> footer;
  footer.insert(footer.end(), kPrPakFooterMagic,
                kPrPakFooterMagic + sizeof(kPrPakFooterMagic));
  AppendU64(footer, tocOffset);
  AppendU64(footer, static_cast<uint64_t>(tocBytes.size()));
  AppendU64(footer, tocHash);

  // Atomic write.
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      return fail("could not open output file");
    out.write(reinterpret_cast<const char *>(kPrPakMagic), sizeof(kPrPakMagic));
    if (!payloadRegion.empty())
      out.write(reinterpret_cast<const char *>(payloadRegion.data()),
                static_cast<std::streamsize>(payloadRegion.size()));
    out.write(reinterpret_cast<const char *>(tocBytes.data()),
              static_cast<std::streamsize>(tocBytes.size()));
    out.write(reinterpret_cast<const char *>(footer.data()),
              static_cast<std::streamsize>(footer.size()));
    out.flush();
    if (!out)
      return fail("write failed");
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
  }
  if (ec)
    return fail("rename failed");
  return true;
}

} // namespace assetlib
