#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// Versioned, standalone cooked runtime payloads for meshes and textures. The
// binary field layout mirrors the per-mesh / per-texture encoding already used
// by scene_io.cpp's .prs writer (vertex/index streams + texture cpuData), so a
// cooked cache entry and a .prs embed describe geometry identically. This
// module is deliberately renderer-free (no D3D12 types) so it can be unit
// tested headlessly; the asset_cooker bridge converts to/from Asset:: types.
namespace assetlib {

// Bump when the on-disk layout or cooking semantics change; stale entries are
// then detected and recooked.
constexpr uint32_t kCookerVersionMesh = 1;
constexpr uint32_t kCookerVersionTexture = 1;

struct CookedMesh {
  int32_t materialIndex = -1;
  int32_t materialSlot = -1;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  float minBound[3] = {0, 0, 0};
  float maxBound[3] = {0, 0, 0};
  std::vector<uint8_t> vertexBytes; // interleaved Asset::Vertex stream
  std::vector<uint8_t> indexBytes;  // uint32 indices
};

// A cooked model groups all meshes produced by one source import.
struct CookedModel {
  std::vector<CookedMesh> meshes;
};

struct CookedTexture {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t cpuFormat = 0;   // DXGI_FORMAT value
  uint32_t cpuMipLevels = 1;
  uint32_t usageSemantic = 0; // Asset::TextureUsageSemantic value
  std::vector<uint8_t> data;  // cpuData (possibly GPU-block-compressed)
};

// Serialize to a self-describing, compressed byte blob (header + payload).
// Returns false only on a compression failure.
bool SerializeCookedModel(const CookedModel &model, std::vector<uint8_t> &out);
bool SerializeCookedTexture(const CookedTexture &tex, std::vector<uint8_t> &out);

// Parse a blob produced by the matching Serialize call. Returns false on bad
// magic, version mismatch, truncation, or decompression failure.
bool DeserializeCookedModel(const uint8_t *data, size_t size, CookedModel &out);
bool DeserializeCookedTexture(const uint8_t *data, size_t size,
                              CookedTexture &out);

// Atomic file write (temp + rename) and whole-file read.
bool WriteCookedFile(const std::filesystem::path &path,
                     const std::vector<uint8_t> &bytes);
bool ReadCookedFile(const std::filesystem::path &path,
                    std::vector<uint8_t> &out);

// FNV-1a 64-bit content hashes used for cache keys / change detection.
uint64_t HashBytes(const void *data, size_t size);
// Hashes file contents in a streaming fashion. Returns 0 if unreadable.
uint64_t HashFile(const std::filesystem::path &path);

} // namespace assetlib
