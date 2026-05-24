#include "scene_io.h"
#include "camera.h"
#include "dxr_renderer.h"
#include "ibl_manager.h"
#include "livelink/livelink_scene_sync.h"
#include "material/material_io.h"
#include "animation_sequence.h"
#include "saved_views.h"
#include "scene.h"
#include "raster_renderer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

// Windows Compression API for LZMS
#include <Windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")

// Globals from main.cpp
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Texture> g_loadedTextures;
extern RenderMode g_currentRenderMode;
extern float g_camYaw;
extern float g_camPitch;
extern bool g_cloudRenderingEnabled;
extern float g_camSpeed;
extern float g_mouseSensitivity;
extern bool g_drawGrid;
extern float g_timeOfDay;
extern float g_northOffset;
extern float g_latitudeDeg;
extern float g_dayOfYear;
extern float g_iblIntensity;
extern float g_iblIndirectBoost;
extern int g_debugMode;
#include "dx12_context.h"
#include "streamline_manager.h"

#include "clouds.h"
extern CloudManager g_cloudManager;

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char PRS_MAGIC[4] = {'P', 'R', 'S', '1'};
static const uint32_t PRS_VERSION = 4;
static const char PRS_CHUNK_MAGIC[4] = {'P', 'R', 'S', 'C'};
static const char PRS_CHUNK_V3_MAGIC[4] = {'P', 'R', 'S', '3'};
static constexpr size_t PRS_CHUNK_SIZE = 4ull * 1024ull * 1024ull;

enum class PrsCompressionAlgorithm : uint32_t {
  Lzms = 1,
  XpressHuff = 2,
};

static std::mutex g_sceneIoProgressMutex;
static SceneIO::ProgressCallback g_sceneIoProgressCb = nullptr;

static int PathTracingBackendToSceneValue() {
  return DxrRenderer::GetPathTracingBackend() ==
                 DxrRenderer::PathTracingBackend::WavefrontParity
             ? 1
             : 2;
}

static DxrRenderer::PathTracingBackend PathTracingBackendFromSceneValue(
    int value) {
  return value == 1 ? DxrRenderer::PathTracingBackend::WavefrontParity
                    : DxrRenderer::PathTracingBackend::WavefrontOptimized;
}

static void ReportProgress(float progress01, const char *stage) {
  SceneIO::ProgressCallback cb = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_sceneIoProgressMutex);
    cb = g_sceneIoProgressCb;
  }
  if (!cb)
    return;
  progress01 = (std::clamp)(progress01, 0.0f, 1.0f);
  cb(progress01, stage ? stage : "");
}

static fs::path NativePathFromUtf8(const std::string &path) {
  if (path.empty()) {
    return {};
  }

#ifdef _WIN32
  const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            path.c_str(), -1, nullptr, 0);
  if (wideCount > 0) {
    std::wstring wide(static_cast<size_t>(wideCount), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                              path.c_str(), -1, wide.data(),
                                              wideCount);
    if (converted == wideCount) {
      if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
      }
      return fs::path(wide);
    }
  }
#endif

  return fs::path(path);
}

static fs::path NormalizeSceneSavePath(fs::path path) {
  const fs::path extension = path.extension();
  if (extension.empty() || extension == L".") {
    path += L".prs";
  }
  return path;
}

class BinaryWriter {
  std::vector<uint8_t> &buf;

public:
  BinaryWriter(std::vector<uint8_t> &b) : buf(b) {}
  void writeU32(uint32_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t *>(&v),
               reinterpret_cast<uint8_t *>(&v) + 4);
  }
  void writeI32(int32_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t *>(&v),
               reinterpret_cast<uint8_t *>(&v) + 4);
  }
  void writeF32(float v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t *>(&v),
               reinterpret_cast<uint8_t *>(&v) + 4);
  }
  void writeBytes(const void *data, size_t size) {
    auto p = reinterpret_cast<const uint8_t *>(data);
    buf.insert(buf.end(), p, p + size);
  }
};

class BinaryReader {
  const uint8_t *data;
  size_t size;
  size_t pos = 0;

public:
  BinaryReader(const uint8_t *d, size_t s) : data(d), size(s) {}
  bool hasRemaining(size_t n) const { return pos + n <= size; }
  uint32_t readU32() {
    uint32_t v;
    memcpy(&v, data + pos, 4);
    pos += 4;
    return v;
  }
  int32_t readI32() {
    int32_t v;
    memcpy(&v, data + pos, 4);
    pos += 4;
    return v;
  }
  float readF32() {
    float v;
    memcpy(&v, data + pos, 4);
    pos += 4;
    return v;
  }
  const uint8_t *readBytes(size_t n) {
    auto p = data + pos;
    pos += n;
    return p;
  }
};

static DWORD WindowsCompressionAlgorithm(PrsCompressionAlgorithm algorithm) {
  switch (algorithm) {
  case PrsCompressionAlgorithm::XpressHuff:
    return COMPRESS_ALGORITHM_XPRESS_HUFF;
  case PrsCompressionAlgorithm::Lzms:
  default:
    return COMPRESS_ALGORITHM_LZMS;
  }
}

static const char *CompressionAlgorithmName(
    PrsCompressionAlgorithm algorithm) {
  switch (algorithm) {
  case PrsCompressionAlgorithm::XpressHuff:
    return "XPRESS_HUFF";
  case PrsCompressionAlgorithm::Lzms:
  default:
    return "LZMS";
  }
}

static PrsCompressionAlgorithm CompressionAlgorithmFromStoredValue(
    uint32_t value) {
  switch (static_cast<PrsCompressionAlgorithm>(value)) {
  case PrsCompressionAlgorithm::XpressHuff:
    return PrsCompressionAlgorithm::XpressHuff;
  case PrsCompressionAlgorithm::Lzms:
    return PrsCompressionAlgorithm::Lzms;
  default:
    return PrsCompressionAlgorithm::Lzms;
  }
}

static bool IsKnownCompressionAlgorithmValue(uint32_t value) {
  return value == static_cast<uint32_t>(PrsCompressionAlgorithm::Lzms) ||
         value == static_cast<uint32_t>(PrsCompressionAlgorithm::XpressHuff);
}

static PrsCompressionAlgorithm GetSaveCompressionAlgorithm() {
  char value[32] = {};
  DWORD length = GetEnvironmentVariableA("PROJECT_RENDER_PRS_COMPRESSION",
                                         value, static_cast<DWORD>(sizeof(value)));
  if (length > 0 && length < sizeof(value)) {
    std::string mode(value, value + length);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (mode == "lzms" || mode == "small") {
      return PrsCompressionAlgorithm::Lzms;
    }
    if (mode == "legacy" || mode == "v2") {
      return PrsCompressionAlgorithm::Lzms;
    }
  }
  return PrsCompressionAlgorithm::XpressHuff;
}

static bool ShouldSaveLegacyPrsV2() {
  char value[32] = {};
  DWORD length = GetEnvironmentVariableA("PROJECT_RENDER_PRS_COMPRESSION",
                                         value, static_cast<DWORD>(sizeof(value)));
  if (length == 0 || length >= sizeof(value)) {
    return false;
  }
  std::string mode(value, value + length);
  std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return mode == "legacy" || mode == "v2";
}

static bool CompressBlock(PrsCompressionAlgorithm algorithm,
                          const uint8_t *input, size_t inputSize,
                          std::vector<uint8_t> &output) {
  COMPRESSOR_HANDLE handle = nullptr;
  const DWORD windowsAlgorithm = WindowsCompressionAlgorithm(algorithm);
  if (!CreateCompressor(windowsAlgorithm, nullptr, &handle)) {
    fprintf(stderr, "PRS: CreateCompressor failed (%lu)\n", GetLastError());
    return false;
  }
  SIZE_T needed = 0;
  Compress(handle, input, inputSize, nullptr, 0, &needed);
  output.resize(needed);
  SIZE_T actual = 0;
  BOOL ok = Compress(handle, input, inputSize, output.data(), output.size(),
                     &actual);
  CloseCompressor(handle);
  if (!ok) {
    fprintf(stderr, "PRS: Compress failed (%lu)\n", GetLastError());
    return false;
  }
  output.resize(actual);
  return true;
}

static bool DecompressBlock(PrsCompressionAlgorithm algorithm,
                            const uint8_t *comp, size_t compSize,
                            uint8_t *out, size_t outSize,
                            size_t &actualOut) {
  DECOMPRESSOR_HANDLE handle = nullptr;
  const DWORD windowsAlgorithm = WindowsCompressionAlgorithm(algorithm);
  if (!CreateDecompressor(windowsAlgorithm, nullptr, &handle)) {
    fprintf(stderr, "PRS: CreateDecompressor failed (%lu)\n", GetLastError());
    return false;
  }
  SIZE_T actual = 0;
  BOOL ok = Decompress(handle, comp, compSize, out, outSize, &actual);
  CloseDecompressor(handle);
  if (!ok) {
    fprintf(stderr, "PRS: Decompress failed (%lu)\n", GetLastError());
    return false;
  }
  actualOut = static_cast<size_t>(actual);
  return true;
}

static bool CompressLZMSBlock(const uint8_t *input, size_t inputSize,
                              std::vector<uint8_t> &output) {
  return CompressBlock(PrsCompressionAlgorithm::Lzms, input, inputSize, output);
}

static bool DecompressLZMSBlock(const uint8_t *comp, size_t compSize,
                                uint8_t *out, size_t outSize,
                                size_t &actualOut) {
  return DecompressBlock(PrsCompressionAlgorithm::Lzms, comp, compSize, out,
                         outSize, actualOut);
}

static bool DecompressLZMSLegacy(const uint8_t *comp, size_t compSize,
                                 std::vector<uint8_t> &output,
                                 size_t uncompSize) {
  output.resize(uncompSize);
  size_t actual = 0;
  if (!DecompressLZMSBlock(comp, compSize, output.data(), output.size(), actual))
    return false;
  output.resize(actual);
  return true;
}

static inline void WriteU32(std::vector<uint8_t> &out, uint32_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&v),
             reinterpret_cast<const uint8_t *>(&v) + sizeof(v));
}

static inline void WriteU64(std::vector<uint8_t> &out, uint64_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&v),
             reinterpret_cast<const uint8_t *>(&v) + sizeof(v));
}

static inline bool ReadU32(const uint8_t *data, size_t size, size_t &pos,
                           uint32_t &out) {
  if (pos + sizeof(uint32_t) > size)
    return false;
  memcpy(&out, data + pos, sizeof(uint32_t));
  pos += sizeof(uint32_t);
  return true;
}

static inline bool ReadU64(const uint8_t *data, size_t size, size_t &pos,
                           uint64_t &out) {
  if (pos + sizeof(uint64_t) > size)
    return false;
  memcpy(&out, data + pos, sizeof(uint64_t));
  pos += sizeof(uint64_t);
  return true;
}

static size_t GetWorkerCount(size_t tasks) {
  if (tasks == 0)
    return 1;
  unsigned int hw = std::thread::hardware_concurrency();
  size_t workers = hw == 0 ? 4 : static_cast<size_t>(hw);
  return (std::min)(workers, tasks);
}

static bool CompressChunked(const std::vector<uint8_t> &input,
                            std::vector<uint8_t> &output,
                            PrsCompressionAlgorithm algorithm,
                            bool legacyV2ChunkHeader = false) {
  if (input.empty()) {
    output.clear();
    if (legacyV2ChunkHeader) {
      output.insert(output.end(), PRS_CHUNK_MAGIC, PRS_CHUNK_MAGIC + 4);
    } else {
      output.insert(output.end(), PRS_CHUNK_V3_MAGIC, PRS_CHUNK_V3_MAGIC + 4);
      WriteU32(output, static_cast<uint32_t>(algorithm));
    }
    WriteU32(output, 0);
    return true;
  }

  const size_t chunkCount =
      (input.size() + PRS_CHUNK_SIZE - 1) / PRS_CHUNK_SIZE;
  std::vector<std::vector<uint8_t>> compressedChunks(chunkCount);
  std::vector<uint64_t> uncompSizes(chunkCount, 0);
  std::vector<uint64_t> compSizes(chunkCount, 0);

  std::atomic<size_t> nextChunk{0};
  std::atomic<size_t> doneChunks{0};
  std::atomic<bool> failed{false};
  const size_t workerCount = GetWorkerCount(chunkCount);
  std::vector<std::thread> workers;
  workers.reserve(workerCount);

  for (size_t worker = 0; worker < workerCount; ++worker) {
    workers.emplace_back([&]() {
      while (true) {
        size_t chunkIndex = nextChunk.fetch_add(1);
        if (chunkIndex >= chunkCount || failed.load())
          break;

        const size_t offset = chunkIndex * PRS_CHUNK_SIZE;
        const size_t blockSize =
            (std::min)(PRS_CHUNK_SIZE, input.size() - offset);
        uncompSizes[chunkIndex] = static_cast<uint64_t>(blockSize);
        if (!CompressBlock(algorithm, input.data() + offset, blockSize,
                           compressedChunks[chunkIndex])) {
          failed.store(true);
          break;
        }
        compSizes[chunkIndex] =
            static_cast<uint64_t>(compressedChunks[chunkIndex].size());
        const size_t done = doneChunks.fetch_add(1) + 1;
        const float progress = 0.30f + 0.55f * ((float)done / (float)chunkCount);
        ReportProgress(progress, "Compressing scene data");
      }
    });
  }
  for (auto &thread : workers)
    thread.join();

  if (failed.load())
    return false;

  size_t totalCompBytes = 0;
  for (const auto &chunk : compressedChunks)
    totalCompBytes += chunk.size();

  output.clear();
  output.reserve((legacyV2ChunkHeader ? 8 : 12) + chunkCount * 16 +
                 totalCompBytes);
  if (legacyV2ChunkHeader) {
    output.insert(output.end(), PRS_CHUNK_MAGIC, PRS_CHUNK_MAGIC + 4);
  } else {
    output.insert(output.end(), PRS_CHUNK_V3_MAGIC, PRS_CHUNK_V3_MAGIC + 4);
    WriteU32(output, static_cast<uint32_t>(algorithm));
  }
  WriteU32(output, static_cast<uint32_t>(chunkCount));
  for (size_t i = 0; i < chunkCount; ++i) {
    WriteU64(output, uncompSizes[i]);
    WriteU64(output, compSizes[i]);
  }
  for (const auto &chunk : compressedChunks)
    output.insert(output.end(), chunk.begin(), chunk.end());
  return true;
}

static bool DecompressLZMSAny(const uint8_t *comp, size_t compSize,
                              std::vector<uint8_t> &output, size_t uncompSize) {
  const bool isLegacyChunked =
      compSize >= 8 && memcmp(comp, PRS_CHUNK_MAGIC, 4) == 0;
  const bool isV3Chunked =
      compSize >= 12 && memcmp(comp, PRS_CHUNK_V3_MAGIC, 4) == 0;
  if (isLegacyChunked || isV3Chunked) {
    size_t pos = 4;
    PrsCompressionAlgorithm algorithm = PrsCompressionAlgorithm::Lzms;
    if (isV3Chunked) {
      uint32_t storedAlgorithm = 0;
      if (!ReadU32(comp, compSize, pos, storedAlgorithm))
        return false;
      if (!IsKnownCompressionAlgorithmValue(storedAlgorithm)) {
        fprintf(stderr, "PRS: Unknown compression algorithm %u\n",
                storedAlgorithm);
        return false;
      }
      algorithm = CompressionAlgorithmFromStoredValue(storedAlgorithm);
    }
    uint32_t chunkCount = 0;
    if (!ReadU32(comp, compSize, pos, chunkCount))
      return false;

    struct ChunkDesc {
      uint64_t uncompSize;
      uint64_t compSize;
      size_t compOffset;
      size_t outOffset;
    };
    std::vector<ChunkDesc> chunks(chunkCount);

    uint64_t totalUncomp = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) {
      uint64_t uncompChunkSize = 0;
      uint64_t compChunkSize = 0;
      if (!ReadU64(comp, compSize, pos, uncompChunkSize) ||
          !ReadU64(comp, compSize, pos, compChunkSize))
        return false;
      chunks[i].uncompSize = uncompChunkSize;
      chunks[i].compSize = compChunkSize;
      chunks[i].outOffset = static_cast<size_t>(totalUncomp);
      totalUncomp += uncompChunkSize;
    }

    if (totalUncomp != uncompSize) {
      fprintf(stderr,
              "PRS: Chunked decompression size mismatch (header=%zu, chunks=%llu)\n",
              uncompSize, static_cast<unsigned long long>(totalUncomp));
      return false;
    }

    size_t compOffset = pos;
    for (uint32_t i = 0; i < chunkCount; ++i) {
      if (chunks[i].compSize > (std::numeric_limits<size_t>::max)())
        return false;
      size_t chunkSize = static_cast<size_t>(chunks[i].compSize);
      if (compOffset + chunkSize > compSize)
        return false;
      chunks[i].compOffset = compOffset;
      compOffset += chunkSize;
    }
    if (compOffset != compSize)
      return false;

    output.resize(uncompSize);
    std::atomic<size_t> nextChunk{0};
    std::atomic<size_t> doneChunks{0};
    std::atomic<bool> failed{false};
    const size_t workerCount = GetWorkerCount(chunkCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (size_t worker = 0; worker < workerCount; ++worker) {
      workers.emplace_back([&]() {
        while (true) {
          size_t chunkIndex = nextChunk.fetch_add(1);
          if (chunkIndex >= chunkCount || failed.load())
            break;

          const ChunkDesc &chunk = chunks[chunkIndex];
          if (chunk.uncompSize > (std::numeric_limits<size_t>::max)()) {
            failed.store(true);
            break;
          }

          size_t actualOut = 0;
          size_t outputSize = static_cast<size_t>(chunk.uncompSize);
          if (!DecompressBlock(algorithm, comp + chunk.compOffset,
                               static_cast<size_t>(chunk.compSize),
                               output.data() + chunk.outOffset, outputSize,
                               actualOut) ||
              actualOut != outputSize) {
            failed.store(true);
            break;
          }
          const size_t done = doneChunks.fetch_add(1) + 1;
          const float progress = 0.20f + 0.40f * ((float)done / (float)chunkCount);
          ReportProgress(progress, "Decompressing scene data");
        }
      });
    }
    for (auto &thread : workers)
      thread.join();

    return !failed.load();
  }

  return DecompressLZMSLegacy(comp, compSize, output, uncompSize);
}

static json BuildMetadata(const std::vector<int> &textureSaveRemap) {
  json j;
  j["matv"] = Asset::Material::kSchemaVersionCoronaArchviz;
  j["matModel"] = "corona-archviz-wavefront";

  // Camera
  j["cam"]["p"]   = {g_cameraData.pos[0], g_cameraData.pos[1], g_cameraData.pos[2]};
  j["cam"]["f"]   = {g_cameraData.forward[0], g_cameraData.forward[1], g_cameraData.forward[2]};
  j["cam"]["u"]   = {g_cameraData.up[0], g_cameraData.up[1], g_cameraData.up[2]};
  j["cam"]["fov"] = g_cameraData.fov;
  j["cam"]["nz"]  = g_cameraData.nearZ;
  j["cam"]["fz"]  = g_cameraData.farZ;
  j["cam"]["int"] = g_cameraData.intensity;
  j["cam"]["spp"] = g_cameraData.maxSPP;
  j["cam"]["msb"] = g_cameraData.maxSpecularBounces;
  j["cam"]["mrb"] = g_cameraData.maxRefractiveBounces;
  j["cam"]["mgb"] = g_cameraData.maxGIBounces;
  j["cam"]["yaw"] = g_camYaw;
  j["cam"]["pit"] = g_camPitch;
  j["cam"]["as"]  = g_cameraData.useAdaptiveSampling;
  j["cam"]["nt"]  = g_cameraData.noiseThreshold;
  j["cam"]["dvm"] = g_cameraData.debugVisualizationMode;
  j["cam"]["vtc"] = g_cameraData.verticalTiltCorrection > 0.5f;
  j["cam"]["sea"] = 1.0f;
  j["cam"]["ae"]  = DxrRenderer::GetAutoExposure();
  j["cam"]["pce"] = DxrRenderer::GetPhysicalCameraExposure();
  j["cam"]["ec"]  = DxrRenderer::GetExposureCompensation();
  float iso, shut, apt;
  DxrRenderer::GetPhysicalCameraSettings(iso, shut, apt);
  j["cam"]["iso"] = iso;
  j["cam"]["ss"]  = shut;
  j["cam"]["apt"] = apt;
  j["cam"]["tai"] = DxrRenderer::GetTonemapAmbientOcclusionIntensity();
  j["cam"]["tal"] = DxrRenderer::GetTonemapAmbientOcclusionLengthMm();
  j["cam"]["tam"] =
      static_cast<int>(DxrRenderer::GetTonemapAmbientOcclusionMode());
  j["cam"]["wb"] = RasterRenderer::GetRenderSettings().tonemapWhiteBalance;

  j["vws"] = json::array();
  for (const auto &view : SavedViews::GetViews()) {
    json entry = {
        {"n", view.name},
        {"p", {view.pos[0], view.pos[1], view.pos[2]}},
        {"f", {view.forward[0], view.forward[1], view.forward[2]}},
        {"u", {view.up[0], view.up[1], view.up[2]}},
        {"fov", view.fov},
        {"nz", view.nearZ},
        {"fz", view.farZ},
        {"int", view.intensity},
        {"msb", view.maxSpecularBounces},
        {"mrb", view.maxRefractiveBounces},
        {"mgb", view.maxGIBounces},
        {"spp", view.maxSPP},
        {"yaw", view.yaw},
        {"pit", view.pitch},
        {"as", view.useAdaptiveSampling},
        {"nt", view.noiseThreshold},
        {"dvm", view.debugVisualizationMode},
        {"sea", 1.0f},
        {"vtc", view.verticalTiltCorrection},
        {"ae", view.autoExposure},
        {"pce", view.physicalCameraExposure},
        {"sf", view.safeFrameEnabled},
        {"ec", view.exposureCompensation},
        {"iso", view.iso},
        {"ss", view.shutterSeconds},
        {"apt", view.aperture},
        {"tai", view.tonemapAoIntensity},
        {"tal", view.tonemapAoLengthMm},
        {"tam", view.tonemapAoMode},
        {"tvg", view.tonemapVignette},
        {"tsa", view.tonemapSaturation},
        {"tco", view.tonemapContrast},
        {"twb", view.tonemapWhiteBalance},
        {"tms", view.tonemapSettingsCaptured},
    };
    if (!view.thumbnailRgba.empty() && view.thumbnailWidth > 0 &&
        view.thumbnailHeight > 0) {
      entry["tw"] = view.thumbnailWidth;
      entry["th"] = view.thumbnailHeight;
      entry["tb"] = json::binary(view.thumbnailRgba);
    }
    j["vws"].push_back(std::move(entry));
  }

  j["anm"]["set"] = {
      {"rp", AnimationSequence::GetExportSettings().resolutionPreset},
      {"fps", AnimationSequence::GetExportSettings().fps},
      {"spp", AnimationSequence::GetExportSettings().maxSpp},
      {"bn", AnimationSequence::GetExportSettings().baseName},
      {"em", AnimationSequence::GetExportSettings().exportMode},
  };
  j["anm"]["kfs"] = json::array();
  for (const auto &keyframe : AnimationSequence::GetKeyframes()) {
    j["anm"]["kfs"].push_back({
        {"n", keyframe.label},
        {"dur", keyframe.durationToNextSeconds},
        {"ein", keyframe.easeIn},
        {"eout", keyframe.easeOut},
        {"p", {keyframe.camera.pos[0], keyframe.camera.pos[1], keyframe.camera.pos[2]}},
        {"f", {keyframe.camera.forward[0], keyframe.camera.forward[1], keyframe.camera.forward[2]}},
        {"u", {keyframe.camera.up[0], keyframe.camera.up[1], keyframe.camera.up[2]}},
        {"fov", keyframe.camera.fov},
        {"nz", keyframe.camera.nearZ},
        {"fz", keyframe.camera.farZ},
        {"int", keyframe.camera.intensity},
        {"msb", keyframe.camera.maxSpecularBounces},
        {"mrb", keyframe.camera.maxRefractiveBounces},
        {"mgb", keyframe.camera.maxGIBounces},
        {"spp", keyframe.camera.maxSPP},
        {"yaw", keyframe.camera.yaw},
        {"pit", keyframe.camera.pitch},
        {"as", keyframe.camera.useAdaptiveSampling},
        {"nt", keyframe.camera.noiseThreshold},
        {"dvm", keyframe.camera.debugVisualizationMode},
        {"sea", 1.0f},
        {"vtc", keyframe.camera.verticalTiltCorrection},
        {"ae", keyframe.camera.autoExposure},
        {"pce", keyframe.camera.physicalCameraExposure},
        {"sf", keyframe.camera.safeFrameEnabled},
        {"ec", keyframe.camera.exposureCompensation},
        {"iso", keyframe.camera.iso},
        {"ss", keyframe.camera.shutterSeconds},
        {"apt", keyframe.camera.aperture},
        {"tai", keyframe.camera.tonemapAoIntensity},
        {"tal", keyframe.camera.tonemapAoLengthMm},
        {"tam", keyframe.camera.tonemapAoMode},
        {"tvg", keyframe.camera.tonemapVignette},
        {"tsa", keyframe.camera.tonemapSaturation},
        {"tco", keyframe.camera.tonemapContrast},
        {"twb", keyframe.camera.tonemapWhiteBalance},
        {"tms", keyframe.camera.tonemapSettingsCaptured},
    });
  }

  // Settings
  j["set"]["cr"] = g_cloudRenderingEnabled;
  j["set"]["cs"] = g_camSpeed;
  j["set"]["ms"] = g_mouseSensitivity;
  j["set"]["dg"] = g_drawGrid;
  j["set"]["dv"] = g_debugMode;

  // Sky
  j["sky"]["tod"] = g_timeOfDay;
  j["sky"]["no"]  = g_northOffset;
  j["sky"]["lat"] = g_latitudeDeg;
  j["sky"]["doy"] = g_dayOfYear;
  j["sky"]["ibli"] = g_iblIntensity;
  j["sky"]["iblib"] = g_iblIndirectBoost;

  // Clouds
  auto &cp = g_cloudManager.GetParams();
  j["cld"] = {
    {"den",cp.density},{"abs",cp.absorption},{"cov",cp.coverage},{"sca",cp.scattering},
    {"stp",cp.steps},{"si",cp.sunIntensity},{"ct",cp.cloudTop},{"cb",cp.cloudBottom},
    {"ws",cp.windSpeed},{"bs",cp.baseScale},{"ds",cp.detailScale},{"cs",cp.coverageScale},
    {"cv",cp.coverageVariation},{"er",cp.erosion},{"wst",cp.warpStrength},{"sp",cp.shapePower},
    {"ps",cp.powderStrength},{"ca",cp.cirrusAmount},{"gsh",cp.cloudShadowStrength},
    {"ssh",cp.shadowSteps},{"sss",cp.shadowStepSize},
    {"sl",cp.shadowLod},{"ms2",cp.maxSteps},{"vsm",cp.verticalStepMeters},
    {"se",cp.shadowEvery},{"sdt",cp.shadowDensityThreshold},
    {"pbs",cp.previewBakeSamples},{"fbs",cp.finalBakeSamples},
    {"bjs",cp.bakeJitterStrength},{"msb",cp.multiScatterBoost},
    {"sls",cp.silverLiningStrength},{"cty",cp.cloudType},
    {"gbs",cp.groundBounceStrength},{"sso",cp.shadowSoftness}
  };

  // Streamline
  j["stl"] = {
    {"en", DX12Context::g_streamline.IsEnabled()},
    {"mo", (int)DX12Context::g_streamline.GetMode()},
    {"qu", (int)DX12Context::g_streamline.GetQuality()}
  };

  // DXR
  j["dxr"] = {
    {"fdm", (int)DxrRenderer::GetDenoiserMode()},
    {"oq", (int)DxrRenderer::GetOidnQuality()},
    {"rj", DxrRenderer::GetRrJitterScale()},
    {"ptb", PathTracingBackendToSceneValue()}
  };

  // Raster post-process
  {
    const auto &rs = RasterRenderer::GetRenderSettings();
    j["rps"] = {
      {"esr", rs.enableSSR},
      {"sss", rs.ssrStepSize},
      {"sst", rs.ssrThickness},
      {"ssi", rs.ssrIntensity},
      {"sms", rs.ssrMinSmoothness},
      {"smx", rs.ssrMaxSteps},
      {"eao", rs.enableSSAO},
      {"sar", rs.ssaoRadius},
      {"sab", rs.ssaoBias},
      {"sas", rs.ssaoStrength},
      {"san", rs.ssaoSamples},
      {"sac", rs.ssaoCompositeWeight},
      {"ebl", rs.enableBloom},
      {"bth", rs.bloomThreshold},
      {"bin", rs.bloomIntensity},
      {"vig", rs.tonemapVignette},
      {"sat", rs.tonemapSaturation},
      {"con", rs.tonemapContrast},
      {"wb", rs.tonemapWhiteBalance}
    };
  }

  // Lighting
  j["lit"]["ld"] = {g_cameraData.lightDir[0], g_cameraData.lightDir[1],
                    g_cameraData.lightDir[2], g_cameraData.lightDir[3]};
  j["lit"]["lc"] = {g_cameraData.lightColor[0], g_cameraData.lightColor[1],
                    g_cameraData.lightColor[2], g_cameraData.lightColor[3]};

  // Render mode (1=DXR, 0=Raster)
  j["rm"] = (g_currentRenderMode == RenderMode::DXR) ? 1 : 0;

  // IBL
  auto &ibl = IBLManager::Get();
  j["ibl"] = {
    {"src", (ibl.GetIBLSource() == IBLManager::IBLSource::File) ? 1 : 0},
    {"fp",  ibl.GetEnvironmentMapPath()},
    {"vis", ibl.GetSkyVisibility()},   {"alb", ibl.GetSkyAlbedo()},
    {"sel", ibl.GetSolarAltitude()},   {"saz", ibl.GetSolarAzimuth()},
    {"alt", ibl.GetObserverAltitude()}, {"ski", ibl.GetSkyIntensity()},
    {"sui", ibl.GetSunIntensity()},    {"sus", ibl.GetSunSize()},
    {"pc",  ibl.IsPhysicalCalibrationEnabled()},
    {"rot", ibl.GetIblRotationDegrees()},
    {"esa", true},
    {"fsi", ibl.GetFileSunIntensity()}, {"fsr", ibl.GetFileSunRadiusDeg()}
  };

  // Materials
  j["mat"] = MaterialIO::BuildMaterialsMetadata(g_loadedMaterials,
                                                 textureSaveRemap);

  // Nodes
  for (const auto &node : Scene::GetNodes()) {
    std::vector<float> xf(16);
    for (int i = 0; i < 16; ++i) xf[i] = node.transform[i];
    j["nod"].push_back({
      {"n", node.name}, {"sp", node.sourcePath},
      {"igk", node.importGroupKey}, {"igr", node.importGroupRoot},
      {"v", node.visible}, {"s", node.selected},
      {"sl", node.selectionLocked},
      {"ll", node.liveLinkManaged}, {"pi", node.parentIndex},
      {"mi", node.meshIndices}, {"lmi", node.linkedMaterialIndices},
      {"lmn", node.linkedMaterialSourceNames}, {"t", xf}
    });
  }

  j["sct"] = json::array();
  for (const Scene::ScatterModel &model : Scene::GetScatterModels()) {
    json sm;
    sm["n"] = model.name;
    sm["sd"] = model.seed;
    sm["en"] = model.enabled;
    sm["pds"] = model.previewDensityScale;
    sm["pib"] = model.previewInstanceBudget;
    sm["tg"] = json::array();
    for (const Scene::ScatterTarget &target : model.targets) {
      sm["tg"].push_back({
          {"ni", target.nodeIndex},
          {"mi", target.meshIndex},
          {"nn", target.nodeName},
          {"sp", target.sourcePath},
          {"igk", target.importGroupKey},
          {"mat", target.materialIndex},
          {"ms", target.materialSlot},
          {"w", target.weight},
          {"en", target.enabled},
      });
    }
    sm["ob"] = json::array();
    for (const Scene::ScatterObject &object : model.objects) {
      json meshTransforms = json::array();
      for (const auto &transform : object.meshLocalTransforms) {
        std::vector<float> savedTransform(16);
        for (int i = 0; i < 16; ++i) {
          savedTransform[i] = transform[static_cast<size_t>(i)];
        }
        meshTransforms.push_back(std::move(savedTransform));
      }
      sm["ob"].push_back({
          {"n", object.name},
          {"sn", object.sourceNodeName},
          {"sp", object.sourcePath},
          {"mi", object.meshIndices},
          {"mt", meshTransforms},
          {"d", object.densityPerSquareMeter},
          {"w", object.weight},
          {"mx", object.maxInstances},
          {"pmx", object.previewMaxInstances},
          {"s0", object.minScale},
          {"s1", object.maxScale},
          {"yw", object.randomYawDegrees},
          {"pt", object.randomPitchDegrees},
          {"rl", object.randomRollDegrees},
          {"na", object.normalAlign},
          {"sl0", object.slopeMinDegrees},
          {"sl1", object.slopeMaxDegrees},
          {"h0", object.heightMin},
          {"h1", object.heightMax},
          {"jt", object.jitterMeters},
          {"md", object.minDistance},
          {"xd", object.maxDistance},
          {"cs", object.clumpScale},
          {"cl", object.clumpStrength},
          {"ea", object.edgeAvoidance},
          {"ca", object.collisionAvoidanceRadius},
          {"lhs", object.librarySourceHidden},
          {"en", object.enabled},
      });
    }
    j["sct"].push_back(std::move(sm));
  }

  // Lights — new prototype + instance schema
  j["lgp"] = json::array();
  for (const auto &proto : Scene::GetLightPrototypes()) {
    j["lgp"].push_back({
      {"ty",  proto.type},
      {"en",  proto.enabled},
      {"c",   {proto.color[0], proto.color[1], proto.color[2]}},
      {"i",   proto.intensity},
      {"r",   proto.radius},
      {"ica", proto.innerConeAngle},
      {"oca", proto.outerConeAngle},
      {"ae",  {proto.areaExtents[0], proto.areaExtents[1]}},
      {"iai", proto.iesAtlasIndex}
    });
  }
  j["lgi"] = json::array();
  for (const auto &inst : Scene::GetLightInstances()) {
    j["lgi"].push_back({
      {"pi", inst.prototypeIndex},
      {"p",  {inst.position[0], inst.position[1], inst.position[2]}},
      {"d",  {inst.direction[0], inst.direction[1], inst.direction[2]}},
      {"en", inst.enabled}
    });
  }

  const auto liveLinkBindings = LiveLink::GetSceneSync().ExportPersistedBindings();
  for (const auto &binding : liveLinkBindings) {
    const int64_t handleIndex =
        binding.handleIndex == static_cast<size_t>(-1)
            ? -1
            : static_cast<int64_t>(binding.handleIndex);
    j["llb"].push_back({
        {"sa", binding.objectId.sourceApp},
        {"di", binding.objectId.documentId},
        {"oi", binding.objectId.objectId},
        {"ot", static_cast<int>(binding.objectId.objectType)},
        {"hk", static_cast<int>(binding.handleKind)},
        {"hi", handleIndex},
    });
  }

  return j;
}

// ---------------------------------------------------------------------------
// Apply PRS metadata (compact keys) to engine state
// ---------------------------------------------------------------------------
static void SyncLoadedCameraInputBasis(bool loadedYaw, bool loadedPitch) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kMaxPitch = kPi * 0.5f - 0.01f;

  const float fx = g_cameraData.forward[0];
  const float fy = g_cameraData.forward[1];
  const float fz = g_cameraData.forward[2];
  const float forwardLenSq = fx * fx + fy * fy + fz * fz;
  if ((!loadedYaw || !loadedPitch) && forwardLenSq > 1.0e-8f) {
    const float invLen = 1.0f / std::sqrt(forwardLenSq);
    const float nfx = fx * invLen;
    const float nfy = (std::clamp)(fy * invLen, -1.0f, 1.0f);
    const float nfz = fz * invLen;
    g_camYaw = std::atan2(nfx, -nfz);
    g_camPitch = std::asin(nfy);
  }

  if (!std::isfinite(g_camYaw)) {
    g_camYaw = 0.0f;
  }
  if (!std::isfinite(g_camPitch)) {
    g_camPitch = 0.0f;
  }
  g_camPitch = (std::clamp)(g_camPitch, -kMaxPitch, kMaxPitch);

  const float cp = std::cos(g_camPitch);
  g_cameraData.forward[0] = cp * std::sin(g_camYaw);
  g_cameraData.forward[1] = std::sin(g_camPitch);
  g_cameraData.forward[2] = cp * -std::cos(g_camYaw);
}

static void ApplyMetadataPRS(const json &j) {
  if (j.contains("cam")) {
    auto &c = j["cam"];
    if (c.contains("p"))  { g_cameraData.pos[0]=c["p"][0]; g_cameraData.pos[1]=c["p"][1]; g_cameraData.pos[2]=c["p"][2]; }
    if (c.contains("f"))  { g_cameraData.forward[0]=c["f"][0]; g_cameraData.forward[1]=c["f"][1]; g_cameraData.forward[2]=c["f"][2]; }
    if (c.contains("u"))  { g_cameraData.up[0]=c["u"][0]; g_cameraData.up[1]=c["u"][1]; g_cameraData.up[2]=c["u"][2]; }
    g_cameraData.fov = c.value("fov", 60.0f);
    g_cameraData.nearZ = c.value("nz", g_cameraData.nearZ);
    g_cameraData.farZ = c.value("fz", g_cameraData.farZ);
    g_cameraData.intensity = (std::clamp)(c.value("int", g_cameraData.intensity), 1e-5f, 10.0f);
    g_cameraData.maxSPP = c.value("spp", 1024.0f);
    g_cameraData.maxSpecularBounces = c.value("msb", 3.0f);
    g_cameraData.maxRefractiveBounces = c.value("mrb", 3.0f);
    g_cameraData.maxGIBounces = c.value("mgb", 2.0f);
    const bool loadedYaw = c.contains("yaw");
    const bool loadedPitch = c.contains("pit");
    g_camYaw = c.value("yaw", g_camYaw);
    g_camPitch = c.value("pit", g_camPitch);
    SyncLoadedCameraInputBasis(loadedYaw, loadedPitch);
    g_cameraData.useAdaptiveSampling = c.value("as", 0.0f);
    g_cameraData.noiseThreshold = c.value("nt", 0.05f);
    g_cameraData.debugVisualizationMode = c.value("dvm", 0.0f);
    g_cameraData.sampleEnvSolidAngle = 1.0f;
    g_cameraData.verticalTiltCorrection =
        c.value("vtc", false) ? 1.0f : 0.0f;
    DxrRenderer::SetAutoExposure(c.value("ae", DxrRenderer::GetAutoExposure()));
    DxrRenderer::SetPhysicalCameraExposure(c.value("pce", DxrRenderer::GetPhysicalCameraExposure()));
    DxrRenderer::SetPhysicalCameraSettings(c.value("iso",100.0f), c.value("ss",1.0f/125.0f), c.value("apt",16.0f));
    DxrRenderer::SetExposureCompensation(c.value("ec", DxrRenderer::GetExposureCompensation()));
    DxrRenderer::SetTonemapAmbientOcclusionIntensity(
        c.value("tai", DxrRenderer::GetTonemapAmbientOcclusionIntensity()));
    DxrRenderer::SetTonemapAmbientOcclusionLengthMm(
        c.value("tal", DxrRenderer::GetTonemapAmbientOcclusionLengthMm()));
    DxrRenderer::SetTonemapAmbientOcclusionMode(
        static_cast<DxrRenderer::TonemapAmbientOcclusionMode>(
            std::clamp(c.value("tam",
                               static_cast<int>(
                                   DxrRenderer::GetTonemapAmbientOcclusionMode())),
                       0, 2)));
    if (c.contains("wb")) {
      auto &rs = RasterRenderer::GetRenderSettings();
      rs.tonemapWhiteBalance =
          (std::clamp)(c.value("wb", rs.tonemapWhiteBalance), -1.0f, 1.0f);
    }
  }
  std::vector<SavedViews::SavedView> loadedViews;
  if (j.contains("vws") && j["vws"].is_array()) {
    for (const auto &entry : j["vws"]) {
      SavedViews::SavedView view;
      view.name = entry.value("n", std::string("View"));
      if (entry.contains("p") && entry["p"].size() >= 3) {
        for (int i = 0; i < 3; ++i) view.pos[i] = entry["p"][i];
      }
      if (entry.contains("f") && entry["f"].size() >= 3) {
        for (int i = 0; i < 3; ++i) view.forward[i] = entry["f"][i];
      }
      if (entry.contains("u") && entry["u"].size() >= 3) {
        for (int i = 0; i < 3; ++i) view.up[i] = entry["u"][i];
      }
      view.fov = entry.value("fov", view.fov);
      view.nearZ = entry.value("nz", view.nearZ);
      view.farZ = entry.value("fz", view.farZ);
      view.intensity = entry.value("int", view.intensity);
      view.maxSpecularBounces = entry.value("msb", view.maxSpecularBounces);
      view.maxRefractiveBounces = entry.value("mrb", view.maxRefractiveBounces);
      view.maxGIBounces = entry.value("mgb", view.maxGIBounces);
      view.maxSPP = entry.value("spp", view.maxSPP);
      view.yaw = entry.value("yaw", view.yaw);
      view.pitch = entry.value("pit", view.pitch);
      view.useAdaptiveSampling = entry.value("as", view.useAdaptiveSampling);
      view.noiseThreshold = entry.value("nt", view.noiseThreshold);
      view.debugVisualizationMode = entry.value("dvm", view.debugVisualizationMode);
      view.sampleEnvSolidAngle = 1.0f;
      view.verticalTiltCorrection =
          entry.value("vtc", view.verticalTiltCorrection);
      view.autoExposure = entry.value("ae", view.autoExposure);
      view.physicalCameraExposure = entry.value("pce", view.physicalCameraExposure);
      view.safeFrameEnabled = entry.value("sf", view.safeFrameEnabled);
      view.exposureCompensation = entry.value("ec", view.exposureCompensation);
      view.iso = entry.value("iso", view.iso);
      view.shutterSeconds = entry.value("ss", view.shutterSeconds);
      view.aperture = entry.value("apt", view.aperture);
      view.tonemapAoIntensity = entry.value("tai", view.tonemapAoIntensity);
      view.tonemapAoLengthMm = entry.value("tal", view.tonemapAoLengthMm);
      view.tonemapAoMode = std::clamp(entry.value("tam", view.tonemapAoMode), 0, 2);
      view.tonemapVignette = std::clamp(entry.value("tvg", view.tonemapVignette), 0.0f, 1.0f);
      view.tonemapSaturation = std::clamp(entry.value("tsa", view.tonemapSaturation), 0.0f, 2.0f);
      view.tonemapContrast = std::clamp(entry.value("tco", view.tonemapContrast), 0.0f, 2.0f);
      view.tonemapWhiteBalance = std::clamp(entry.value("twb", view.tonemapWhiteBalance), -1.0f, 1.0f);
      view.tonemapSettingsCaptured =
          entry.value("tms", entry.contains("tvg") || entry.contains("tsa") ||
                                 entry.contains("tco") || entry.contains("twb"));
      view.thumbnailWidth = entry.value("tw", 0u);
      view.thumbnailHeight = entry.value("th", 0u);
      if (entry.contains("tb") && entry["tb"].is_binary()) {
        const auto &thumbnailBinary = entry["tb"].get_binary();
        view.thumbnailRgba.assign(thumbnailBinary.begin(),
                                  thumbnailBinary.end());
      }
      loadedViews.push_back(std::move(view));
    }
  }
  SavedViews::SetViews(std::move(loadedViews));
  AnimationSequence::ExportSettings animationSettings =
      AnimationSequence::GetExportSettings();
  std::vector<AnimationSequence::Keyframe> animationKeyframes;
  if (j.contains("anm") && j["anm"].is_object()) {
    const auto &animation = j["anm"];
    if (animation.contains("set") && animation["set"].is_object()) {
      const auto &settings = animation["set"];
      animationSettings.resolutionPreset =
          settings.value("rp", animationSettings.resolutionPreset);
      animationSettings.fps = settings.value("fps", animationSettings.fps);
      animationSettings.maxSpp = settings.value("spp", animationSettings.maxSpp);
      animationSettings.baseName = settings.value("bn", animationSettings.baseName);
      animationSettings.exportMode = settings.value("em", animationSettings.exportMode);
    }
    if (animation.contains("kfs") && animation["kfs"].is_array()) {
      for (const auto &entry : animation["kfs"]) {
        AnimationSequence::Keyframe keyframe;
        keyframe.label = entry.value("n", std::string("Keyframe"));
        keyframe.durationToNextSeconds = entry.value("dur", 2.0f);
        const int legacyEase = entry.value("ez", static_cast<int>(AnimationSequence::EasingMode::Ease));
        keyframe.easeIn = entry.value("ein", legacyEase);
        keyframe.easeOut = entry.value("eout", legacyEase);
        if (entry.contains("p") && entry["p"].size() >= 3) {
          for (int i = 0; i < 3; ++i) keyframe.camera.pos[i] = entry["p"][i];
        }
        if (entry.contains("f") && entry["f"].size() >= 3) {
          for (int i = 0; i < 3; ++i) keyframe.camera.forward[i] = entry["f"][i];
        }
        if (entry.contains("u") && entry["u"].size() >= 3) {
          for (int i = 0; i < 3; ++i) keyframe.camera.up[i] = entry["u"][i];
        }
        keyframe.camera.name = keyframe.label;
        keyframe.camera.fov = entry.value("fov", keyframe.camera.fov);
        keyframe.camera.nearZ = entry.value("nz", keyframe.camera.nearZ);
        keyframe.camera.farZ = entry.value("fz", keyframe.camera.farZ);
        keyframe.camera.intensity = entry.value("int", keyframe.camera.intensity);
        keyframe.camera.maxSpecularBounces = entry.value("msb", keyframe.camera.maxSpecularBounces);
        keyframe.camera.maxRefractiveBounces = entry.value("mrb", keyframe.camera.maxRefractiveBounces);
        keyframe.camera.maxGIBounces = entry.value("mgb", keyframe.camera.maxGIBounces);
        keyframe.camera.maxSPP = entry.value("spp", keyframe.camera.maxSPP);
        keyframe.camera.yaw = entry.value("yaw", keyframe.camera.yaw);
        keyframe.camera.pitch = entry.value("pit", keyframe.camera.pitch);
        keyframe.camera.useAdaptiveSampling = entry.value("as", keyframe.camera.useAdaptiveSampling);
        keyframe.camera.noiseThreshold = entry.value("nt", keyframe.camera.noiseThreshold);
        keyframe.camera.debugVisualizationMode = entry.value("dvm", keyframe.camera.debugVisualizationMode);
        keyframe.camera.sampleEnvSolidAngle = 1.0f;
        keyframe.camera.verticalTiltCorrection =
            entry.value("vtc", keyframe.camera.verticalTiltCorrection);
        keyframe.camera.autoExposure = entry.value("ae", keyframe.camera.autoExposure);
        keyframe.camera.physicalCameraExposure = entry.value("pce", keyframe.camera.physicalCameraExposure);
        keyframe.camera.safeFrameEnabled = entry.value("sf", keyframe.camera.safeFrameEnabled);
        keyframe.camera.exposureCompensation = entry.value("ec", keyframe.camera.exposureCompensation);
        keyframe.camera.iso = entry.value("iso", keyframe.camera.iso);
        keyframe.camera.shutterSeconds = entry.value("ss", keyframe.camera.shutterSeconds);
        keyframe.camera.aperture = entry.value("apt", keyframe.camera.aperture);
        keyframe.camera.tonemapAoIntensity = entry.value("tai", keyframe.camera.tonemapAoIntensity);
        keyframe.camera.tonemapAoLengthMm = entry.value("tal", keyframe.camera.tonemapAoLengthMm);
        keyframe.camera.tonemapAoMode = std::clamp(entry.value("tam", keyframe.camera.tonemapAoMode), 0, 2);
        keyframe.camera.tonemapVignette = std::clamp(entry.value("tvg", keyframe.camera.tonemapVignette), 0.0f, 1.0f);
        keyframe.camera.tonemapSaturation = std::clamp(entry.value("tsa", keyframe.camera.tonemapSaturation), 0.0f, 2.0f);
        keyframe.camera.tonemapContrast = std::clamp(entry.value("tco", keyframe.camera.tonemapContrast), 0.0f, 2.0f);
        keyframe.camera.tonemapWhiteBalance = std::clamp(entry.value("twb", keyframe.camera.tonemapWhiteBalance), -1.0f, 1.0f);
        keyframe.camera.tonemapSettingsCaptured =
            entry.value("tms", entry.contains("tvg") ||
                                   entry.contains("tsa") ||
                                   entry.contains("tco") ||
                                   entry.contains("twb"));
        animationKeyframes.push_back(std::move(keyframe));
      }
    }
  }
  AnimationSequence::SetExportSettings(animationSettings);
  AnimationSequence::SetKeyframes(std::move(animationKeyframes));
  if (j.contains("set")) {
    auto &s = j["set"];
    g_cloudRenderingEnabled = s.value("cr", true);
    g_camSpeed = s.value("cs", g_camSpeed);
    g_mouseSensitivity = s.value("ms", g_mouseSensitivity);
    g_drawGrid = s.value("dg", g_drawGrid);
    g_debugMode = s.value("dv", g_debugMode);
  }
  if (j.contains("sky")) {
    auto &s = j["sky"];
    g_timeOfDay = s.value("tod", g_timeOfDay);
    g_northOffset = s.value("no", g_northOffset);
    g_latitudeDeg = s.value("lat", g_latitudeDeg);
    g_dayOfYear = s.value("doy", g_dayOfYear);
    g_iblIntensity = s.value("ibli", g_iblIntensity);
    g_iblIndirectBoost = s.value("iblib", g_iblIndirectBoost);
  }
  if (j.contains("cld")) {
    auto &c = j["cld"];
    auto &cp = g_cloudManager.GetParams();
    cp.density=c.value("den",cp.density); cp.absorption=c.value("abs",cp.absorption);
    cp.coverage=c.value("cov",cp.coverage); cp.scattering=c.value("sca",cp.scattering);
    cp.steps=c.value("stp",cp.steps); cp.sunIntensity=c.value("si",cp.sunIntensity);
    cp.cloudTop=c.value("ct",cp.cloudTop); cp.cloudBottom=c.value("cb",cp.cloudBottom);
    cp.windSpeed=c.value("ws",cp.windSpeed); cp.baseScale=c.value("bs",cp.baseScale);
    cp.detailScale=c.value("ds",cp.detailScale); cp.coverageScale=c.value("cs",cp.coverageScale);
    cp.coverageVariation=c.value("cv",cp.coverageVariation); cp.erosion=c.value("er",cp.erosion);
    cp.warpStrength=c.value("wst",cp.warpStrength); cp.shapePower=c.value("sp",cp.shapePower);
    cp.powderStrength=c.value("ps",cp.powderStrength); cp.cirrusAmount=c.value("ca",cp.cirrusAmount);
    cp.cloudShadowStrength=c.value("gsh",cp.cloudShadowStrength); cp.shadowSteps=c.value("ssh",cp.shadowSteps);
    cp.shadowStepSize=c.value("sss",cp.shadowStepSize); cp.shadowLod=c.value("sl",cp.shadowLod);
    cp.maxSteps=c.value("ms2",cp.maxSteps); cp.verticalStepMeters=c.value("vsm",cp.verticalStepMeters);
    cp.shadowEvery=c.value("se",cp.shadowEvery); cp.shadowDensityThreshold=c.value("sdt",cp.shadowDensityThreshold);
    cp.previewBakeSamples=c.value("pbs",cp.previewBakeSamples); cp.finalBakeSamples=c.value("fbs",cp.finalBakeSamples);
    cp.bakeJitterStrength=c.value("bjs",cp.bakeJitterStrength); cp.multiScatterBoost=c.value("msb",cp.multiScatterBoost);
    cp.silverLiningStrength=c.value("sls",cp.silverLiningStrength); cp.cloudType=c.value("cty",cp.cloudType);
    cp.groundBounceStrength=c.value("gbs",cp.groundBounceStrength); cp.shadowSoftness=c.value("sso",cp.shadowSoftness);
  }
  if (j.contains("stl")) {
    auto &s = j["stl"];
    DX12Context::g_streamline.SetEnabled(
        s.value("en", DX12Context::g_streamline.IsEnabled()));
    DX12Context::g_streamline.SetMode((StreamlineManager::Mode)s.value("mo", (int)DX12Context::g_streamline.GetMode()));
    DX12Context::g_streamline.SetQuality((StreamlineManager::Quality)s.value("qu", (int)StreamlineManager::Quality::Balanced));
  }
  if (j.contains("dxr")) {
    auto &d = j["dxr"];
    const int legacyDm = d.value("dm", 0);
    DxrRenderer::SetDenoiserMode(
        (DxrRenderer::DenoiserMode)d.value("fdm",
                                           (legacyDm <= 2) ? legacyDm
                                                           : (int)DxrRenderer::GetDenoiserMode()));
    DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)d.value("oq", (int)DxrRenderer::GetOidnQuality()));
    DxrRenderer::SetRrJitterScale(d.value("rj", DxrRenderer::GetRrJitterScale()));
    DxrRenderer::SetPathTracingBackend(PathTracingBackendFromSceneValue(
        d.value("ptb", PathTracingBackendToSceneValue())));
  }
  if (j.contains("rps") && j["rps"].is_object()) {
    auto &r = j["rps"];
    auto &rs = RasterRenderer::GetRenderSettings();
    rs.enableSSR = r.value("esr", rs.enableSSR);
    rs.ssrStepSize = (std::clamp)(r.value("sss", rs.ssrStepSize), 0.02f, 2.0f);
    rs.ssrThickness = (std::clamp)(r.value("sst", rs.ssrThickness), 0.001f, 1.0f);
    rs.ssrIntensity = (std::clamp)(r.value("ssi", rs.ssrIntensity), 0.0f, 2.0f);
    rs.ssrMinSmoothness = (std::clamp)(r.value("sms", rs.ssrMinSmoothness), 0.0f, 1.0f);
    rs.ssrMaxSteps = (std::clamp)(r.value("smx", rs.ssrMaxSteps), 1, 256);

    rs.enableSSAO = r.value("eao", rs.enableSSAO);
    rs.ssaoRadius = (std::clamp)(r.value("sar", rs.ssaoRadius), 0.01f, 5.0f);
    rs.ssaoBias = (std::clamp)(r.value("sab", rs.ssaoBias), 0.0001f, 0.25f);
    rs.ssaoStrength = (std::clamp)(r.value("sas", rs.ssaoStrength), 0.0f, 4.0f);
    rs.ssaoSamples = (std::clamp)(r.value("san", rs.ssaoSamples), 1, 32);
    rs.ssaoCompositeWeight =
        (std::clamp)(r.value("sac", rs.ssaoCompositeWeight), 0.0f, 1.0f);

    rs.enableBloom = r.value("ebl", rs.enableBloom);
    rs.bloomThreshold = (std::clamp)(r.value("bth", rs.bloomThreshold), 0.0f, 10.0f);
    rs.bloomIntensity = (std::clamp)(r.value("bin", rs.bloomIntensity), 0.0f, 4.0f);

    rs.tonemapVignette = (std::clamp)(r.value("vig", rs.tonemapVignette), 0.0f, 1.0f);
    rs.tonemapSaturation = (std::clamp)(r.value("sat", rs.tonemapSaturation), 0.0f, 2.0f);
    rs.tonemapContrast = (std::clamp)(r.value("con", rs.tonemapContrast), 0.0f, 2.0f);
    rs.tonemapWhiteBalance = (std::clamp)(r.value("wb", rs.tonemapWhiteBalance), -1.0f, 1.0f);
  }
  if (j.contains("lit")) {
    auto &l = j["lit"];
    if (l.contains("ld") && l["ld"].size()>=4) for (int i=0;i<4;++i) g_cameraData.lightDir[i]=l["ld"][i];
    if (l.contains("lc") && l["lc"].size()>=4) for (int i=0;i<4;++i) g_cameraData.lightColor[i]=l["lc"][i];
  }
  if (j.contains("lgp") && j["lgp"].is_array() && j.contains("lgi") && j["lgi"].is_array()) {
    // New prototype + instance schema
    for (const auto &p : j["lgp"]) {
      LightPrototype proto;
      proto.type = p.value("ty", 1u);
      proto.enabled = p.value("en", true);
      auto c = p.value("c", std::vector<float>{1,1,1});
      if (c.size()>=3) { proto.color[0]=c[0]; proto.color[1]=c[1]; proto.color[2]=c[2]; }
      proto.intensity = p.value("i", 1000.0f);
      proto.radius = p.value("r", 0.1f);
      proto.innerConeAngle = p.value("ica", 0.8660254f);
      proto.outerConeAngle = p.value("oca", 0.7071068f);
      auto ae = p.value("ae", std::vector<float>{1,1});
      if (ae.size()>=2) { proto.areaExtents[0]=ae[0]; proto.areaExtents[1]=ae[1]; }
      proto.iesAtlasIndex = p.value("iai", -1);
      // Add prototype without auto-instance (we'll add instances from lgi)
      size_t protoIdx = Scene::AddLightPrototypeRaw(proto);
      (void)protoIdx;
    }
    for (const auto &i : j["lgi"]) {
      LightInstance inst;
      inst.prototypeIndex = i.value("pi", size_t(0));
      auto ip = i.value("p", std::vector<float>{0,2,0});
      if (ip.size()>=3) { inst.position[0]=ip[0]; inst.position[1]=ip[1]; inst.position[2]=ip[2]; }
      auto id = i.value("d", std::vector<float>{0,-1,0});
      if (id.size()>=3) { inst.direction[0]=id[0]; inst.direction[1]=id[1]; inst.direction[2]=id[2]; }
      inst.enabled = i.value("en", true);
      Scene::AddLightInstanceRaw(inst);
    }
    Scene::UpdateLights();
  } else if (j.contains("lgt") && j["lgt"].is_array()) {
    // Legacy flat light format — migrate each light to one prototype + one instance
    for (const auto &l : j["lgt"]) {
      LightPrototype proto;
      proto.type = l.value("ty", 1u);
      auto e = l.value("e", std::vector<float>{10,10,10});
      float intensity = 1.0f;
      if (e.size()>=3) {
        intensity = (std::max)({e[0], e[1], e[2], 0.001f});
        proto.color[0] = e[0] / intensity;
        proto.color[1] = e[1] / intensity;
        proto.color[2] = e[2] / intensity;
      }
      proto.intensity = intensity;
      proto.radius = l.value("r", 0.1f);
      proto.innerConeAngle = l.value("ica", 1.0f);
      proto.outerConeAngle = l.value("oca", 0.5f);
      auto ae = l.value("ae", std::vector<float>{1,1});
      if (ae.size()>=2) { proto.areaExtents[0]=ae[0]; proto.areaExtents[1]=ae[1]; }
      proto.iesAtlasIndex = l.value("iai", -1);
      size_t protoIdx = Scene::AddLightPrototypeRaw(proto);

      LightInstance inst;
      inst.prototypeIndex = protoIdx;
      auto p = l.value("p", std::vector<float>{0,0,0});
      if (p.size()>=3) { inst.position[0]=p[0]; inst.position[1]=p[1]; inst.position[2]=p[2]; }
      auto d = l.value("d", std::vector<float>{0,-1,0});
      if (d.size()>=3) { inst.direction[0]=d[0]; inst.direction[1]=d[1]; inst.direction[2]=d[2]; }
      Scene::AddLightInstanceRaw(inst);
    }
    Scene::UpdateLights();
  }
  if (j.contains("rm")) g_currentRenderMode = (j["rm"].get<int>()==1) ? RenderMode::DXR : RenderMode::Raster;
  if (j.contains("ibl")) {
    auto &ibl = IBLManager::Get();
    auto &i = j["ibl"];
    bool wantsFile = (i.value("src", 0) == 1);
    if (wantsFile) {
      std::string p = i.value("fp", std::string());
      if (!p.empty() && fs::exists(p)) ibl.LoadEnvironmentMap(p);
    }
    ibl.SetIBLSource(wantsFile ? IBLManager::IBLSource::File : IBLManager::IBLSource::PragueSkyModel);
    ibl.SetSkyVisibility(i.value("vis",60.0f)); ibl.SetSkyAlbedo(i.value("alb",0.3f));
    ibl.SetSolarAltitude(i.value("sel",0.5f)); ibl.SetSolarAzimuth(i.value("saz",0.0f));
    ibl.SetObserverAltitude(i.value("alt",0.0f)); ibl.SetSkyIntensity(i.value("ski",1.0f));
    ibl.SetSunIntensity(i.value("sui",110000.0f)); ibl.SetSunSize(i.value("sus",0.53f));
    ibl.SetPhysicalCalibrationEnabled(i.value("pc", ibl.IsPhysicalCalibrationEnabled()));
    ibl.SetIblRotationDegrees(i.value("rot", ibl.GetIblRotationDegrees()));
    ibl.SetEnvSolidAngleSampling(true);
    ibl.SetFileSunIntensity(i.value("fsi", ibl.GetFileSunIntensity()));
    ibl.SetFileSunRadiusDeg(i.value("fsr", ibl.GetFileSunRadiusDeg()));
    ibl.UpdateSkyModel();
  }
}

// ---------------------------------------------------------------------------
// Restore nodes from PRS metadata
// ---------------------------------------------------------------------------
static void RestoreNodesPRS(const json &j, bool hasEmbedded) {
  if (!j.contains("nod")) return;
  for (const auto &n : j["nod"]) {
    std::string srcPath = n.value("sp", "");
    if (hasEmbedded && n.contains("mi")) {
      Scene::Node node;
      node.name = n.value("n", "EmbeddedNode");
      node.sourcePath = srcPath;
      node.importGroupKey = n.value("igk", std::string());
      node.importGroupRoot = n.value("igr", false);
      node.visible = n.value("v", true);
      node.selectionLocked = n.value("sl", node.importGroupRoot);
      node.liveLinkManaged = n.value("ll", false);
      node.parentIndex = n.value("pi", static_cast<size_t>(-1));
      node.meshIndices = n["mi"].get<std::vector<size_t>>();
      if (n.contains("lmi")) {
        node.linkedMaterialIndices = n["lmi"].get<std::vector<int>>();
      }
      if (n.contains("lmn")) {
        node.linkedMaterialSourceNames =
            n["lmn"].get<std::vector<std::string>>();
      }
      if (n.contains("t")) for (int i=0;i<16;++i) node.transform[i]=n["t"][i];
      node.selected = n.value("s", false);
      const_cast<std::vector<Scene::Node>&>(Scene::GetNodes()).push_back(node);
      continue;
    }
    if (srcPath.empty()) {
      if (n.value("n","") == "Ground Plane") {
        Scene::AddDefaultPlane(0.0f);
        auto &nodes = const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
        if (!nodes.empty()) {
          nodes.back().visible = n.value("v", true);
          nodes.back().selectionLocked = n.value("sl", false);
          nodes.back().liveLinkManaged = n.value("ll", false);
          nodes.back().parentIndex = n.value("pi", static_cast<size_t>(-1));
          if (n.contains("t")) for (int i=0;i<16;++i) nodes.back().transform[i]=n["t"][i];
          nodes.back().selected = n.value("s", false);
        }
      }
      continue;
    }
    if (fs::exists(srcPath) && Scene::ImportModel(srcPath)) {
      auto &nodes = const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
      size_t restoredIndex = static_cast<size_t>(-1);
      if (n.value("igr", false)) {
        for (size_t i = nodes.size(); i-- > 0;) {
          if (nodes[i].sourcePath == srcPath && nodes[i].importGroupRoot) {
            restoredIndex = i;
            break;
          }
        }
      } else if (!nodes.empty()) {
        restoredIndex = nodes.size() - 1;
      }
      if (restoredIndex < nodes.size()) {
        nodes[restoredIndex].name = n.value("n", nodes[restoredIndex].name);
        nodes[restoredIndex].visible = n.value("v", true);
        nodes[restoredIndex].selectionLocked =
            n.value("sl", nodes[restoredIndex].importGroupRoot);
        nodes[restoredIndex].liveLinkManaged = n.value("ll", false);
        nodes[restoredIndex].parentIndex =
            n.value("pi", static_cast<size_t>(-1));
        if (n.contains("lmi")) {
          nodes[restoredIndex].linkedMaterialIndices =
              n["lmi"].get<std::vector<int>>();
        }
        if (n.contains("lmn")) {
          nodes[restoredIndex].linkedMaterialSourceNames =
              n["lmn"].get<std::vector<std::string>>();
        }
        if (n.contains("t")) for (int i=0;i<16;++i) nodes[restoredIndex].transform[i]=n["t"][i];
        nodes[restoredIndex].selected = n.value("s", false);
      }
    }
  }
}

static size_t JsonToSizeT(const json &entry, const char *key,
                          size_t fallback = static_cast<size_t>(-1)) {
  if (!entry.contains(key)) {
    return fallback;
  }
  if (entry[key].is_number_unsigned()) {
    return static_cast<size_t>(entry[key].get<uint64_t>());
  }
  if (entry[key].is_number_integer()) {
    const int64_t value = entry[key].get<int64_t>();
    return value < 0 ? fallback : static_cast<size_t>(value);
  }
  return fallback;
}

static bool ResolveScatterTarget(Scene::ScatterTarget &target) {
  const auto &nodes = Scene::GetNodes();
  if (target.nodeIndex < nodes.size() && target.meshIndex < g_loadedMeshes.size()) {
    return true;
  }

  auto nodeMatches = [&target](const Scene::Node &node) {
    if (!target.importGroupKey.empty() &&
        node.importGroupKey == target.importGroupKey) {
      return true;
    }
    if (!target.sourcePath.empty() && node.sourcePath == target.sourcePath) {
      return true;
    }
    return !target.nodeName.empty() && node.name == target.nodeName;
  };

  for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
    const Scene::Node &node = nodes[nodeIndex];
    if (!nodeMatches(node)) {
      continue;
    }
    for (size_t meshIndex : node.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
      const bool slotMatches =
          target.materialSlot >= 0 && mesh.materialSlot == target.materialSlot;
      const bool materialMatches =
          target.materialIndex >= 0 && mesh.materialIndex == target.materialIndex;
      const bool hasStableMeshHint =
          target.materialSlot >= 0 || target.materialIndex >= 0;
      if (!hasStableMeshHint || slotMatches || materialMatches) {
        target.nodeIndex = nodeIndex;
        target.meshIndex = meshIndex;
        target.materialIndex = mesh.materialIndex;
        target.materialSlot = mesh.materialSlot;
        target.nodeName = node.name;
        target.sourcePath = node.sourcePath;
        target.importGroupKey = node.importGroupKey;
        return true;
      }
    }
  }
  return false;
}

static void RestoreScatterPRS(const json &j) {
  std::vector<Scene::ScatterModel> models;
  if (!j.contains("sct") || !j["sct"].is_array()) {
    Scene::SetScatterModels({});
    return;
  }

  const size_t nodeCount = Scene::GetNodes().size();
  const size_t meshCount = g_loadedMeshes.size();
  for (const auto &savedModel : j["sct"]) {
    Scene::ScatterModel model;
    model.name = savedModel.value("n", std::string("Scatter"));
    model.seed = savedModel.value("sd", model.seed);
    model.enabled = savedModel.value("en", model.enabled);
    model.previewDensityScale = savedModel.value("pds", model.previewDensityScale);
    model.previewInstanceBudget = savedModel.value("pib", model.previewInstanceBudget);

    if (savedModel.contains("tg") && savedModel["tg"].is_array()) {
      for (const auto &savedTarget : savedModel["tg"]) {
        Scene::ScatterTarget target;
        target.nodeIndex = JsonToSizeT(savedTarget, "ni");
        target.meshIndex = JsonToSizeT(savedTarget, "mi");
        target.nodeName = savedTarget.value("nn", std::string());
        target.sourcePath = savedTarget.value("sp", std::string());
        target.importGroupKey = savedTarget.value("igk", std::string());
        target.materialIndex = savedTarget.value("mat", target.materialIndex);
        target.materialSlot = savedTarget.value("ms", target.materialSlot);
        target.weight = savedTarget.value("w", target.weight);
        target.enabled = savedTarget.value("en", target.enabled);
        if ((target.nodeIndex >= nodeCount || target.meshIndex >= meshCount) &&
            !ResolveScatterTarget(target)) {
          target.enabled = false;
        }
        model.targets.push_back(target);
      }
    }

    if (savedModel.contains("ob") && savedModel["ob"].is_array()) {
      for (const auto &savedObject : savedModel["ob"]) {
        Scene::ScatterObject object;
        object.name = savedObject.value("n", std::string("Scatter Object"));
        object.sourceNodeName = savedObject.value("sn", object.sourceNodeName);
        object.sourcePath = savedObject.value("sp", object.sourcePath);
        if (savedObject.contains("mi") && savedObject["mi"].is_array()) {
          object.meshIndices.clear();
          for (const auto &savedMeshIndex : savedObject["mi"]) {
            size_t meshIndex = static_cast<size_t>(-1);
            if (savedMeshIndex.is_number_unsigned()) {
              meshIndex = static_cast<size_t>(savedMeshIndex.get<uint64_t>());
            } else if (savedMeshIndex.is_number_integer()) {
              const int64_t value = savedMeshIndex.get<int64_t>();
              if (value >= 0) {
                meshIndex = static_cast<size_t>(value);
              }
            }
            if (meshIndex < meshCount) {
              object.meshIndices.push_back(meshIndex);
            }
          }
        }
        if (savedObject.contains("mt") && savedObject["mt"].is_array()) {
          object.meshLocalTransforms.clear();
          for (const auto &savedTransform : savedObject["mt"]) {
            std::array<float, 16> transform = {};
            transform[0] = 1.0f;
            transform[5] = 1.0f;
            transform[10] = 1.0f;
            transform[15] = 1.0f;
            if (savedTransform.is_array() && savedTransform.size() >= 16) {
              for (int i = 0; i < 16; ++i) {
                transform[static_cast<size_t>(i)] = savedTransform[i];
              }
            }
            object.meshLocalTransforms.push_back(transform);
          }
        }
        while (object.meshLocalTransforms.size() < object.meshIndices.size()) {
          std::array<float, 16> identity = {};
          identity[0] = 1.0f;
          identity[5] = 1.0f;
          identity[10] = 1.0f;
          identity[15] = 1.0f;
          object.meshLocalTransforms.push_back(identity);
        }
        if (object.meshLocalTransforms.size() > object.meshIndices.size()) {
          object.meshLocalTransforms.resize(object.meshIndices.size());
        }
        object.densityPerSquareMeter = savedObject.value("d", object.densityPerSquareMeter);
        object.weight = savedObject.value("w", object.weight);
        object.maxInstances = savedObject.value("mx", object.maxInstances);
        object.previewMaxInstances = savedObject.value("pmx", object.previewMaxInstances);
        object.minScale = savedObject.value("s0", object.minScale);
        object.maxScale = savedObject.value("s1", object.maxScale);
        object.randomYawDegrees = savedObject.value("yw", object.randomYawDegrees);
        object.randomPitchDegrees = savedObject.value("pt", object.randomPitchDegrees);
        object.randomRollDegrees = savedObject.value("rl", object.randomRollDegrees);
        object.normalAlign = savedObject.value("na", object.normalAlign);
        object.slopeMinDegrees = savedObject.value("sl0", object.slopeMinDegrees);
        object.slopeMaxDegrees = savedObject.value("sl1", object.slopeMaxDegrees);
        object.heightMin = savedObject.value("h0", object.heightMin);
        object.heightMax = savedObject.value("h1", object.heightMax);
        object.jitterMeters = savedObject.value("jt", object.jitterMeters);
        object.minDistance = savedObject.value("md", object.minDistance);
        object.maxDistance = savedObject.value("xd", object.maxDistance);
        object.clumpScale = savedObject.value("cs", object.clumpScale);
        object.clumpStrength = savedObject.value("cl", object.clumpStrength);
        object.edgeAvoidance = savedObject.value("ea", object.edgeAvoidance);
        object.collisionAvoidanceRadius =
            savedObject.value("ca", object.collisionAvoidanceRadius);
        object.librarySourceHidden = savedObject.value("lhs", object.librarySourceHidden);
        object.enabled = savedObject.value("en", object.enabled);
        if (object.meshIndices.empty()) {
          object.enabled = false;
        }
        model.objects.push_back(std::move(object));
      }
    }
    models.push_back(std::move(model));
  }

  Scene::SetScatterModels(std::move(models));
}

static void RestoreLiveLinkBindingsPRS(const json &j,
                                       const std::vector<int> &materialRemap) {
  std::vector<LiveLink::LiveLinkSceneSync::PersistedBinding> bindings;
  if (j.contains("llb") && j["llb"].is_array()) {
    for (const auto &entry : j["llb"]) {
      LiveLink::LiveLinkSceneSync::PersistedBinding binding;
      binding.objectId.sourceApp = entry.value("sa", std::string());
      binding.objectId.documentId = entry.value("di", std::string());
      binding.objectId.objectId = entry.value("oi", std::string());
      binding.objectId.objectType = static_cast<LiveLink::ObjectType>(
          entry.value("ot", static_cast<int>(LiveLink::ObjectType::Unknown)));
      binding.handleKind = static_cast<LiveLink::LiveLinkSceneSync::EngineHandleKind>(
          entry.value("hk", static_cast<int>(LiveLink::LiveLinkSceneSync::EngineHandleKind::Unknown)));

      const int64_t savedHandleIndex = entry.value("hi", int64_t(-1));
      binding.handleIndex =
          savedHandleIndex < 0 ? static_cast<size_t>(-1)
                               : static_cast<size_t>(savedHandleIndex);

      switch (binding.handleKind) {
      case LiveLink::LiveLinkSceneSync::EngineHandleKind::SceneNode:
        if (binding.handleIndex >= Scene::GetNodes().size()) {
          continue;
        }
        break;
      case LiveLink::LiveLinkSceneSync::EngineHandleKind::SceneLight:
        if (binding.handleIndex >= Scene::GetLightInstances().size()) {
          continue;
        }
        break;
      case LiveLink::LiveLinkSceneSync::EngineHandleKind::SceneMaterial:
        if (binding.handleIndex == static_cast<size_t>(-1) ||
            binding.handleIndex >= materialRemap.size()) {
          continue;
        }
        if (materialRemap[binding.handleIndex] < 0) {
          continue;
        }
        binding.handleIndex = static_cast<size_t>(materialRemap[binding.handleIndex]);
        break;
      default:
        break;
      }

      if (binding.objectId.Empty()) {
        continue;
      }
      bindings.push_back(std::move(binding));
    }
  }

  LiveLink::GetSceneSync().RestorePersistedBindings(bindings);
}

// ===========================================================================
//  Public API
// ===========================================================================
namespace SceneIO {

void SetProgressCallback(ProgressCallback cb) {
  std::lock_guard<std::mutex> lock(g_sceneIoProgressMutex);
  g_sceneIoProgressCb = cb;
}

// ---------------------------------------------------------------------------
// SaveScene — Binary compressed .prs format
// ---------------------------------------------------------------------------
bool SaveScene(const std::string &path) {
  try {
    const auto saveStart = std::chrono::steady_clock::now();
    auto stageStart = saveStart;
    const auto elapsedMs = [](std::chrono::steady_clock::time_point start,
                              std::chrono::steady_clock::time_point end) {
      return std::chrono::duration<double, std::milli>(end - start).count();
    };
    double metadataMs = 0.0;
    double serializeMs = 0.0;
    double compressMs = 0.0;
    double writeMs = 0.0;

    fs::path outputPath = NormalizeSceneSavePath(NativePathFromUtf8(path));
    if (outputPath.empty()) {
      fprintf(stderr, "PRS: Save failed: empty output path\n");
      return false;
    }

    ReportProgress(0.01f, "Preparing scene metadata");
    fprintf(stderr, "PRS: Saving scene to %s\n", path.c_str());

    const std::vector<int> textureSaveRemap =
      MaterialIO::BuildTextureSaveRemap(g_loadedTextures, g_loadedMaterials);
    uint32_t savedTextureCount = 0;
    size_t savedTextureBytes = 0;
    for (size_t textureIndex = 0; textureIndex < g_loadedTextures.size();
         ++textureIndex) {
      if (textureSaveRemap[textureIndex] < 0) {
        continue;
      }
      ++savedTextureCount;
      savedTextureBytes += g_loadedTextures[textureIndex].cpuData.size();
    }
    fprintf(stderr,
            "PRS: Texture save set: %u referenced / %zu loaded (%.2f MB)\n",
            savedTextureCount, g_loadedTextures.size(),
            savedTextureBytes / (1024.0 * 1024.0));

    // 1. Build metadata as msgpack
    json metadata = BuildMetadata(textureSaveRemap);
    std::vector<uint8_t> msgpack = json::to_msgpack(metadata);
    ReportProgress(0.08f, "Packing metadata");
    fprintf(stderr, "PRS: Metadata: %zu bytes (msgpack)\n", msgpack.size());
    auto stageEnd = std::chrono::steady_clock::now();
    metadataMs = elapsedMs(stageStart, stageEnd);
    stageStart = stageEnd;

    // 2. Build uncompressed binary payload
    std::vector<uint8_t> payload;
    {
      // Pre-estimate size
      size_t est = 4 + msgpack.size() + 4 + 4;
      for (auto &m : g_loadedMeshes) est += 40 + m.cpuVertices.size()*sizeof(Asset::Vertex) + m.cpuIndices.size()*sizeof(uint32_t);
      for (size_t textureIndex = 0; textureIndex < g_loadedTextures.size();
           ++textureIndex)
        if (textureSaveRemap[textureIndex] >= 0)
          est += 20 + g_loadedTextures[textureIndex].cpuData.size();
      payload.reserve(est);
    }

    BinaryWriter w(payload);

    // Metadata
    w.writeU32((uint32_t)msgpack.size());
    w.writeBytes(msgpack.data(), msgpack.size());
    msgpack.clear(); msgpack.shrink_to_fit();
    ReportProgress(0.12f, "Serializing meshes");

    // Meshes — raw binary (no base64 = saves ~33% size)
    w.writeU32((uint32_t)g_loadedMeshes.size());
    size_t meshBytes = 0;
    for (const auto &mesh : g_loadedMeshes) {
      w.writeI32(mesh.materialIndex);
      w.writeU32((uint32_t)mesh.vertexCount);
      w.writeU32((uint32_t)mesh.indexCount);
      w.writeF32(mesh.minBound[0]); w.writeF32(mesh.minBound[1]); w.writeF32(mesh.minBound[2]);
      w.writeF32(mesh.maxBound[0]); w.writeF32(mesh.maxBound[1]); w.writeF32(mesh.maxBound[2]);
      uint32_t vb = (uint32_t)(mesh.cpuVertices.size() * sizeof(Asset::Vertex));
      meshBytes += vb;
      w.writeU32(vb);
      if (vb) w.writeBytes(mesh.cpuVertices.data(), vb);
      uint32_t ib = (uint32_t)(mesh.cpuIndices.size() * sizeof(uint32_t));
      meshBytes += ib;
      w.writeU32(ib);
      if (ib) w.writeBytes(mesh.cpuIndices.data(), ib);
    }

    // Textures — raw binary
    w.writeU32(savedTextureCount);
    for (size_t textureIndex = 0; textureIndex < g_loadedTextures.size();
         ++textureIndex) {
      if (textureSaveRemap[textureIndex] < 0) {
        continue;
      }
      const auto &tex = g_loadedTextures[textureIndex];
      w.writeU32(tex.width);
      w.writeU32(tex.height);
      w.writeU32((uint32_t)tex.cpuFormat);
      w.writeU32(tex.cpuMipLevels);
      uint32_t db = (uint32_t)tex.cpuData.size();
      w.writeU32(db);
      if (db) w.writeBytes(tex.cpuData.data(), db);
    }
    ReportProgress(0.28f, "Finalizing payload");
    stageEnd = std::chrono::steady_clock::now();
    serializeMs = elapsedMs(stageStart, stageEnd);
    stageStart = stageEnd;

    size_t uncompSize = payload.size();
    fprintf(stderr,
            "PRS: Uncompressed: %.2f MB (meshes %.2f MB, textures %.2f MB)\n",
            uncompSize / (1024.0 * 1024.0),
            meshBytes / (1024.0 * 1024.0),
            savedTextureBytes / (1024.0 * 1024.0));

    // 3. Compress
    const bool saveLegacyV2 = ShouldSaveLegacyPrsV2();
    const PrsCompressionAlgorithm compressionAlgorithm =
        saveLegacyV2 ? PrsCompressionAlgorithm::Lzms
                     : GetSaveCompressionAlgorithm();
    fprintf(stderr, "PRS: Save format: v%u, compression: %s\n",
            saveLegacyV2 ? 2u : PRS_VERSION,
            CompressionAlgorithmName(compressionAlgorithm));
    std::vector<uint8_t> compressed;
    if (!CompressChunked(payload, compressed, compressionAlgorithm,
                         saveLegacyV2)) {
      fprintf(stderr, "PRS: Compression failed\n");
      return false;
    }
    payload.clear(); payload.shrink_to_fit();
    stageEnd = std::chrono::steady_clock::now();
    compressMs = elapsedMs(stageStart, stageEnd);
    stageStart = stageEnd;

    fprintf(stderr, "PRS: Compressed: %.2f MB (%.1f%% ratio)\n",
            compressed.size()/(1024.0*1024.0), 100.0*compressed.size()/uncompSize);

    // 4. Write: header (16 bytes) + compressed payload
    ReportProgress(0.96f, "Writing file");

    if (outputPath.has_parent_path()) {
      std::error_code parentError;
      const fs::path parent = outputPath.parent_path();
      if (!parent.empty() && !fs::exists(parent, parentError)) {
        parentError.clear();
        if (!fs::create_directories(parent, parentError) && parentError) {
          fprintf(stderr, "PRS: Save failed: could not create output directory: %s\n",
                  parentError.message().c_str());
          return false;
        }
      }
    }

    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
      fprintf(stderr, "PRS: Save failed: could not open output file\n");
      return false;
    }

    file.write(PRS_MAGIC, 4);
    uint32_t ver = saveLegacyV2 ? 2u : PRS_VERSION;
    file.write(reinterpret_cast<const char*>(&ver), 4);
    uint64_t usz = (uint64_t)uncompSize;
    file.write(reinterpret_cast<const char*>(&usz), 8);
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    file.close();
    if (!file) {
      fprintf(stderr, "PRS: Save failed: write did not complete\n");
      return false;
    }
    stageEnd = std::chrono::steady_clock::now();
    writeMs = elapsedMs(stageStart, stageEnd);
    ReportProgress(0.98f, "Finalizing file");

    fprintf(stderr, "PRS: Saved %.2f MB (was %.2f MB uncompressed)\n",
            (16+compressed.size())/(1024.0*1024.0), uncompSize/(1024.0*1024.0));
    fprintf(stderr,
            "PRS: Save timings: metadata %.1f ms, serialize %.1f ms, "
            "compress %.1f ms, write %.1f ms, total %.1f ms\n",
            metadataMs, serializeMs, compressMs, writeMs,
            elapsedMs(saveStart, std::chrono::steady_clock::now()));
    ReportProgress(1.0f, "Save complete");
    return true;
  } catch (const std::exception &e) {
    std::cerr << "SaveScene: " << e.what() << std::endl;
    return false;
  }
}

// ---------------------------------------------------------------------------
// LoadScene — PRS binary only
// ---------------------------------------------------------------------------

bool LoadScene(const std::string &path) {
  try {
    std::ifstream probe(NativePathFromUtf8(path), std::ios::binary);
    if (!probe.is_open()) return false;
    char magic[4] = {};
    probe.read(magic, 4);
    probe.close();
    if (memcmp(magic, PRS_MAGIC, 4) != 0) {
      std::cerr << "LoadScene: unsupported scene format (expected .prs)" << std::endl;
      return false;
    }
    return LoadScenePRS(path);
  } catch (const std::exception &e) {
    std::cerr << "LoadScene: " << e.what() << std::endl;
    return false;
  }
}

// ---------------------------------------------------------------------------
// LoadScenePRS — Binary .prs format
// ---------------------------------------------------------------------------
bool LoadScenePRS(const std::string &path) {
  try {
    ReportProgress(0.01f, "Reading scene file");
    fprintf(stderr, "PRS: Loading %s\n", path.c_str());

    std::ifstream file(NativePathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    size_t fileSize = (size_t)file.tellg();
    if (fileSize < 16) return false;
    file.seekg(0);

    char magic[4]; file.read(magic, 4);
    if (memcmp(magic, PRS_MAGIC, 4) != 0) return false;
    uint32_t version; file.read(reinterpret_cast<char*>(&version), 4);
    if (version == 0 || version > PRS_VERSION) { fprintf(stderr, "PRS: Unknown version %u\n", version); return false; }
    uint64_t uncompSize; file.read(reinterpret_cast<char*>(&uncompSize), 8);

    size_t compSize = fileSize - 16;
    std::vector<uint8_t> comp(compSize);
    file.read(reinterpret_cast<char*>(comp.data()), compSize);
    file.close();
    ReportProgress(0.15f, "Read complete");

    fprintf(stderr, "PRS: File %.2f MB, uncompressed %.2f MB\n",
            fileSize/(1024.0*1024.0), uncompSize/(1024.0*1024.0));

    // Decompress
    std::vector<uint8_t> payload;
    if (!DecompressLZMSAny(comp.data(), compSize, payload, (size_t)uncompSize)) return false;
    comp.clear(); comp.shrink_to_fit();
    ReportProgress(0.62f, "Parsing scene data");

    BinaryReader r(payload.data(), payload.size());

    // Metadata (msgpack)
    uint32_t metaSize = r.readU32();
    if (!r.hasRemaining(metaSize)) return false;
    const uint8_t *metaBytes = r.readBytes(metaSize);
    json meta = json::from_msgpack(metaBytes, metaBytes + metaSize);

    // Reset scene
    Scene::ResetScene();

    // Read meshes (binary)
    uint32_t numMeshes = r.readU32();
    fprintf(stderr, "PRS: %u meshes\n", numMeshes);
    struct RawMesh { int32_t matIdx; uint32_t vc, ic; float mn[3], mx[3]; std::vector<Asset::Vertex> verts; std::vector<uint32_t> inds; };
    std::vector<RawMesh> rawMeshes(numMeshes);
    for (uint32_t mi = 0; mi < numMeshes; ++mi) {
      auto &rm = rawMeshes[mi];
      rm.matIdx = r.readI32(); rm.vc = r.readU32(); rm.ic = r.readU32();
      for (int k=0;k<3;k++) rm.mn[k]=r.readF32();
      for (int k=0;k<3;k++) rm.mx[k]=r.readF32();
      uint32_t vb = r.readU32();
      rm.verts.resize(rm.vc);
      if (vb && vb == rm.vc*sizeof(Asset::Vertex)) memcpy(rm.verts.data(), r.readBytes(vb), vb);
      else if (vb) r.readBytes(vb);
      uint32_t ib = r.readU32();
      rm.inds.resize(rm.ic);
      if (ib && ib == rm.ic*sizeof(uint32_t)) memcpy(rm.inds.data(), r.readBytes(ib), ib);
      else if (ib) r.readBytes(ib);
    }

    // Read textures (binary)
    uint32_t numTex = r.readU32();
    fprintf(stderr, "PRS: %u textures\n", numTex);
    g_loadedTextures.resize(numTex);
    for (uint32_t ti = 0; ti < numTex; ++ti) {
      uint32_t w = r.readU32(), h = r.readU32();
      DXGI_FORMAT fmt = (DXGI_FORMAT)r.readU32();
      uint32_t mipLevels = 1;
      if (version >= 2) {
        mipLevels = (std::max)(1u, r.readU32());
      }
      uint32_t db = r.readU32();
      if (db) {
        const uint8_t *px = r.readBytes(db);
        g_loadedTextures[ti] =
            version >= 2
                ? Asset::LoadTextureFromMemoryMipChain(px, db, w, h, fmt,
                                                       mipLevels)
                : Asset::LoadTextureFromMemory(px, w, h, fmt);
      }
    }
    Scene::RegisterTextures(g_loadedTextures);
    ReportProgress(0.82f, "Uploading textures and meshes");

    // Create GPU meshes
    bool hasEmbedded = (numMeshes > 0);
    for (auto &rm : rawMeshes) {
      Asset::GpuMesh gm = Asset::LoadMeshFromMemory(rm.verts, rm.inds);
      gm.materialIndex = rm.matIdx;
      for (int k=0;k<3;k++) { gm.minBound[k]=rm.mn[k]; gm.maxBound[k]=rm.mx[k]; }
      g_loadedMeshes.push_back(gm);
    }
    size_t embMeshCnt = g_loadedMeshes.size();

    // Apply metadata
    ApplyMetadataPRS(meta);
    RestoreNodesPRS(meta, hasEmbedded);

    std::vector<int> remap;
    if (meta.contains("mat")) {
      MaterialIO::RestoreMaterialsFromMetadata(meta["mat"],
                                               &g_loadedMaterials,
                                               g_loadedTextures, &remap);
    }
    if (!remap.empty()) {
      auto &nodes = const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
      for (Scene::Node &node : nodes) {
        for (int &materialIndex : node.linkedMaterialIndices) {
          if (materialIndex >= 0 && materialIndex < (int)remap.size()) {
            materialIndex = remap[materialIndex];
          }
        }
      }
    }
    RestoreScatterPRS(meta);
    Scene::RefreshAllMaterialRuntimeTextures();
    RestoreLiveLinkBindingsPRS(meta, remap);

    if (hasEmbedded) {
      for (size_t i = 0; i < embMeshCnt; ++i) {
        int old = g_loadedMeshes[i].materialIndex;
        if (old >= 0 && old < (int)remap.size()) g_loadedMeshes[i].materialIndex = remap[old];
      }
      Scene::RequestRendererFullRebuild();
    }

    UpdateCameraCB();
    fprintf(stderr, "PRS: Scene loaded OK\n");
    ReportProgress(1.0f, "Load complete");
    return true;
  } catch (const std::exception &e) {
    std::cerr << "LoadScenePRS: " << e.what() << std::endl;
    return false;
  }
}

} // namespace SceneIO
