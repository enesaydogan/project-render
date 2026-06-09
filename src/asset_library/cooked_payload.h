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
constexpr uint32_t kCookerVersionVolume = 1;

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

// Project Render's sparse, bricked runtime volume (e.g. cooked from a VDB
// density grid). Only non-empty bricks are stored; each brick's density is
// quantized to 8-bit over its own [minVal,maxVal] range (dequantize:
// value = minVal + (b/255)*(maxVal-minVal)). data.size() == brickSize^3.
struct CookedVolumeBrick {
  uint32_t bx = 0, by = 0, bz = 0; // brick coordinate (in bricks)
  float minVal = 0.0f, maxVal = 0.0f;
  std::vector<uint8_t> data; // quantized densities, brickSize^3 bytes
};

struct CookedVolume {
  uint32_t dim[3] = {0, 0, 0}; // voxel dimensions
  uint32_t brickSize = 8;
  float boundsMin[3] = {0, 0, 0};
  float boundsMax[3] = {0, 0, 0};
  uint64_t activeVoxels = 0; // statistics (non-empty voxel count)
  std::vector<CookedVolumeBrick> bricks;
  // Optional fire/heat channel, sampled in the same index domain as density.
  // Kept separate so smoke-only assets retain their compact representation.
  float temperatureMin = 0.0f;
  float temperatureMax = 0.0f;
  std::vector<CookedVolumeBrick> temperatureBricks;
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
bool SerializeCookedVolume(const CookedVolume &vol, std::vector<uint8_t> &out);
bool DeserializeCookedVolume(const uint8_t *data, size_t size,
                             CookedVolume &out);

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
