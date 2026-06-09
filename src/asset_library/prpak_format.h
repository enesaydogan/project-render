#pragma once
#include "asset_metadata.h"
#include <cstdint>
#include <string>
#include <vector>

// .prpak — Project Render's unencrypted, distributable asset pack. Bundles a
// set of assets' registry metadata plus their cooked payloads (.prmesh/.prtex/
// .prmat) with random-access offsets, per-chunk checksums, and content-hash
// payload deduplication. Renderer-free (it only moves already-cooked bytes).
//
// On-disk layout:
//   [8]  file magic "PRPAK1\0\0"
//   payload chunks, back to back (deduped by content hash)
//   TOC (msgpack of a JSON object: pack meta + chunk table + asset table)
//   [32] footer: "PRPAKEND"(8) tocOffset(u64) tocSize(u64) tocHash(u64)
// A reader seeks to EOF-32, reads the footer, then the TOC, then random-accesses
// chunks by absolute offset. See notes/asset-menagement.md ".prpak Format".
namespace assetlib {

constexpr uint8_t kPrPakMagic[8] = {'P', 'R', 'P', 'A', 'K', '1', 0, 0};
constexpr uint8_t kPrPakFooterMagic[8] = {'P', 'R', 'P', 'A', 'K',
                                          'E', 'N', 'D'};
constexpr uint32_t kPrPakSchemaVersion = 1;
constexpr size_t kPrPakFooterSize = 32;

enum class PayloadKind { Mesh, Texture, Material };

struct PackMeta {
  std::string name;
  std::string attribution;
  std::string license;
  int64_t created = 0; // unix-ish timestamp, informational
};

// A deduplicated payload blob located within the pack.
struct PackChunk {
  uint64_t hash = 0;   // FNV-1a 64 of the chunk bytes (integrity)
  uint64_t offset = 0; // absolute file offset
  uint64_t size = 0;
};

// An asset entry as stored in the pack: full metadata + chunk indices for each
// optional payload kind (-1 when absent; e.g. scatter assets have no payload
// because their data lives in metadata.importSettingsJson).
struct PackedAsset {
  AssetMetadata meta;
  int meshChunk = -1;
  int textureChunk = -1;
  int materialChunk = -1;

  int chunkFor(PayloadKind k) const {
    switch (k) {
    case PayloadKind::Mesh:
      return meshChunk;
    case PayloadKind::Texture:
      return textureChunk;
    case PayloadKind::Material:
      return materialChunk;
    }
    return -1;
  }
};

// Input to the writer: an asset plus the raw cooked payload bytes it owns.
// Empty payload vectors mean "no payload of that kind".
struct PackAssetInput {
  AssetMetadata meta;
  std::vector<uint8_t> meshPayload;
  std::vector<uint8_t> texturePayload;
  std::vector<uint8_t> materialPayload;
};

} // namespace assetlib
