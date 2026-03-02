#include "scene_io.h"
#include "camera.h"
#include "dxr_renderer.h"
#include "ibl_manager.h"
#include "scene.h"
#include <algorithm>
#include <atomic>
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
extern int g_debugMode;
#include "dx12_context.h"
#include "streamline_manager.h"

#include "clouds.h"
extern CloudManager g_cloudManager;

namespace fs = std::filesystem;
using json = nlohmann::json;

// ============================================================================
// PRS Binary Format v1  —  Project Render Scene (.prs)
// ============================================================================
// Header (16 bytes):  "PRS1" | version:u32 | uncompressedSize:u64
// Payload:  LZMS-compressed binary blob containing:
//   - msgpack metadata (compact scene settings)
//   - raw mesh vertex/index data
//   - raw texture pixel data
// ============================================================================

static const char PRS_MAGIC[4] = {'P', 'R', 'S', '1'};
static const uint32_t PRS_VERSION = 1;
static const char PRS_CHUNK_MAGIC[4] = {'P', 'R', 'S', 'C'};
static constexpr size_t PRS_CHUNK_SIZE = 4ull * 1024ull * 1024ull; // 4 MB

static std::mutex g_sceneIoProgressMutex;
static SceneIO::ProgressCallback g_sceneIoProgressCb = nullptr;

static void ReportProgress(float progress01, const char *stage) {
  SceneIO::ProgressCallback cb = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_sceneIoProgressMutex);
    cb = g_sceneIoProgressCb;
  }
  if (!cb) return;
  progress01 = (std::clamp)(progress01, 0.0f, 1.0f);
  cb(progress01, stage ? stage : "");
}

// ---------------------------------------------------------------------------
// Binary buffer helpers
// ---------------------------------------------------------------------------
class BinaryWriter {
  std::vector<uint8_t> &buf;
public:
  BinaryWriter(std::vector<uint8_t> &b) : buf(b) {}
  void writeU32(uint32_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 4);
  }
  void writeI32(int32_t v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 4);
  }
  void writeF32(float v) {
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&v),
               reinterpret_cast<uint8_t*>(&v) + 4);
  }
  void writeBytes(const void *data, size_t size) {
    auto p = reinterpret_cast<const uint8_t*>(data);
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
  uint32_t readU32() { uint32_t v; memcpy(&v, data + pos, 4); pos += 4; return v; }
  int32_t  readI32() { int32_t  v; memcpy(&v, data + pos, 4); pos += 4; return v; }
  float    readF32() { float    v; memcpy(&v, data + pos, 4); pos += 4; return v; }
  const uint8_t *readBytes(size_t n) { auto p = data + pos; pos += n; return p; }
};

// ---------------------------------------------------------------------------
// LZMS compression / decompression  (Windows Compression API — no deps)
// ---------------------------------------------------------------------------
static bool CompressLZMSBlock(const uint8_t* input, size_t inputSize,
                              std::vector<uint8_t> &output) {
  COMPRESSOR_HANDLE h = nullptr;
  if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) {
    fprintf(stderr, "PRS: CreateCompressor failed (%lu)\n", GetLastError());
    return false;
  }
  SIZE_T needed = 0;
  Compress(h, input, inputSize, nullptr, 0, &needed);
  output.resize(needed);
  SIZE_T actual = 0;
  BOOL ok = Compress(h, input, inputSize,
                     output.data(), output.size(), &actual);
  CloseCompressor(h);
  if (!ok) { fprintf(stderr, "PRS: Compress failed (%lu)\n", GetLastError()); return false; }
  output.resize(actual);
  return true;
}

static bool DecompressLZMSBlock(const uint8_t *comp, size_t compSize,
                                uint8_t* out, size_t outSize, size_t& actualOut) {
  DECOMPRESSOR_HANDLE h = nullptr;
  if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) {
    fprintf(stderr, "PRS: CreateDecompressor failed (%lu)\n", GetLastError());
    return false;
  }
  SIZE_T actual = 0;
  BOOL ok = Decompress(h, comp, compSize, out, outSize, &actual);
  CloseDecompressor(h);
  if (!ok) { fprintf(stderr, "PRS: Decompress failed (%lu)\n", GetLastError()); return false; }
  actualOut = static_cast<size_t>(actual);
  return true;
}

static bool DecompressLZMSLegacy(const uint8_t *comp, size_t compSize,
                                 std::vector<uint8_t> &output, size_t uncompSize) {
  output.resize(uncompSize);
  size_t actual = 0;
  if (!DecompressLZMSBlock(comp, compSize, output.data(), output.size(), actual)) {
    return false;
  }
  output.resize(actual);
  return true;
}

static inline void WriteU32(std::vector<uint8_t>& out, uint32_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
             reinterpret_cast<const uint8_t*>(&v) + sizeof(v));
}

static inline void WriteU64(std::vector<uint8_t>& out, uint64_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v),
             reinterpret_cast<const uint8_t*>(&v) + sizeof(v));
}

static inline bool ReadU32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
  if (pos + sizeof(uint32_t) > size) return false;
  memcpy(&out, data + pos, sizeof(uint32_t));
  pos += sizeof(uint32_t);
  return true;
}

static inline bool ReadU64(const uint8_t* data, size_t size, size_t& pos, uint64_t& out) {
  if (pos + sizeof(uint64_t) > size) return false;
  memcpy(&out, data + pos, sizeof(uint64_t));
  pos += sizeof(uint64_t);
  return true;
}

static size_t GetWorkerCount(size_t tasks) {
  if (tasks == 0) return 1;
  unsigned int hw = std::thread::hardware_concurrency();
  size_t workers = hw == 0 ? 4 : static_cast<size_t>(hw);
  return (std::min)(workers, tasks);
}

static bool CompressLZMSChunked(const std::vector<uint8_t> &input,
                                std::vector<uint8_t> &output) {
  if (input.empty()) {
    output.clear();
    output.insert(output.end(), PRS_CHUNK_MAGIC, PRS_CHUNK_MAGIC + 4);
    WriteU32(output, 0);
    return true;
  }

  const size_t chunkCount = (input.size() + PRS_CHUNK_SIZE - 1) / PRS_CHUNK_SIZE;
  std::vector<std::vector<uint8_t>> compressedChunks(chunkCount);
  std::vector<uint64_t> uncompSizes(chunkCount, 0);
  std::vector<uint64_t> compSizes(chunkCount, 0);

  std::atomic<size_t> nextChunk{0};
  std::atomic<size_t> doneChunks{0};
  std::atomic<bool> failed{false};
  const size_t workerCount = GetWorkerCount(chunkCount);
  std::vector<std::thread> workers;
  workers.reserve(workerCount);

  for (size_t w = 0; w < workerCount; ++w) {
    workers.emplace_back([&]() {
      while (true) {
        size_t ci = nextChunk.fetch_add(1);
        if (ci >= chunkCount || failed.load()) break;

        const size_t offset = ci * PRS_CHUNK_SIZE;
        const size_t blockSize = (std::min)(PRS_CHUNK_SIZE, input.size() - offset);
        uncompSizes[ci] = static_cast<uint64_t>(blockSize);
        if (!CompressLZMSBlock(input.data() + offset, blockSize, compressedChunks[ci])) {
          failed.store(true);
          break;
        }
        compSizes[ci] = static_cast<uint64_t>(compressedChunks[ci].size());
        const size_t done = doneChunks.fetch_add(1) + 1;
        const float p = 0.30f + 0.55f * ((float)done / (float)chunkCount);
        ReportProgress(p, "Compressing scene data");
      }
    });
  }
  for (auto& t : workers) t.join();

  if (failed.load()) {
    return false;
  }

  size_t totalCompBytes = 0;
  for (const auto& chunk : compressedChunks) totalCompBytes += chunk.size();

  output.clear();
  output.reserve(8 + chunkCount * 16 + totalCompBytes);
  output.insert(output.end(), PRS_CHUNK_MAGIC, PRS_CHUNK_MAGIC + 4);
  WriteU32(output, static_cast<uint32_t>(chunkCount));
  for (size_t i = 0; i < chunkCount; ++i) {
    WriteU64(output, uncompSizes[i]);
    WriteU64(output, compSizes[i]);
  }
  for (const auto& chunk : compressedChunks) {
    output.insert(output.end(), chunk.begin(), chunk.end());
  }
  return true;
}

static bool DecompressLZMSAny(const uint8_t *comp, size_t compSize,
                              std::vector<uint8_t> &output, size_t uncompSize) {
  if (compSize >= 8 && memcmp(comp, PRS_CHUNK_MAGIC, 4) == 0) {
    size_t pos = 4;
    uint32_t chunkCount = 0;
    if (!ReadU32(comp, compSize, pos, chunkCount)) return false;

    struct ChunkDesc {
      uint64_t uncompSize;
      uint64_t compSize;
      size_t compOffset;
      size_t outOffset;
    };
    std::vector<ChunkDesc> chunks;
    chunks.resize(chunkCount);

    uint64_t totalUncomp = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) {
      uint64_t usz = 0;
      uint64_t csz = 0;
      if (!ReadU64(comp, compSize, pos, usz) || !ReadU64(comp, compSize, pos, csz)) return false;
      chunks[i].uncompSize = usz;
      chunks[i].compSize = csz;
      chunks[i].outOffset = static_cast<size_t>(totalUncomp);
      totalUncomp += usz;
    }

    if (totalUncomp != uncompSize) {
      fprintf(stderr, "PRS: Chunked decompression size mismatch (header=%zu, chunks=%llu)\n",
              uncompSize, static_cast<unsigned long long>(totalUncomp));
      return false;
    }

    size_t compOffset = pos;
    for (uint32_t i = 0; i < chunkCount; ++i) {
      if (chunks[i].compSize > (std::numeric_limits<size_t>::max)()) return false;
      size_t csz = static_cast<size_t>(chunks[i].compSize);
      if (compOffset + csz > compSize) return false;
      chunks[i].compOffset = compOffset;
      compOffset += csz;
    }
    if (compOffset != compSize) return false;

    output.resize(uncompSize);
    std::atomic<size_t> nextChunk{0};
    std::atomic<size_t> doneChunks{0};
    std::atomic<bool> failed{false};
    const size_t workerCount = GetWorkerCount(chunkCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (size_t w = 0; w < workerCount; ++w) {
      workers.emplace_back([&]() {
        while (true) {
          size_t ci = nextChunk.fetch_add(1);
          if (ci >= chunkCount || failed.load()) break;

          const ChunkDesc& cd = chunks[ci];
          if (cd.uncompSize > (std::numeric_limits<size_t>::max)()) {
            failed.store(true);
            break;
          }

          size_t blockActual = 0;
          size_t outSize = static_cast<size_t>(cd.uncompSize);
          if (!DecompressLZMSBlock(comp + cd.compOffset,
                                   static_cast<size_t>(cd.compSize),
                                   output.data() + cd.outOffset,
                                   outSize,
                                   blockActual) || blockActual != outSize) {
            failed.store(true);
            break;
          }
          const size_t done = doneChunks.fetch_add(1) + 1;
          const float p = 0.20f + 0.40f * ((float)done / (float)chunkCount);
          ReportProgress(p, "Decompressing scene data");
        }
      });
    }
    for (auto& t : workers) t.join();

    return !failed.load();
  }

  return DecompressLZMSLegacy(comp, compSize, output, uncompSize);
}

// ---------------------------------------------------------------------------
// Build compact metadata (msgpack-friendly short keys for smallest size)
// ---------------------------------------------------------------------------
static json BuildMetadata() {
  json j;

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
  j["cam"]["sea"] = g_cameraData.sampleEnvSolidAngle;
  j["cam"]["ae"]  = DxrRenderer::GetAutoExposure();
  j["cam"]["pce"] = DxrRenderer::GetPhysicalCameraExposure();
  j["cam"]["ec"]  = DxrRenderer::GetExposureCompensation();
  float iso, shut, apt;
  DxrRenderer::GetPhysicalCameraSettings(iso, shut, apt);
  j["cam"]["iso"] = iso;
  j["cam"]["ss"]  = shut;
  j["cam"]["apt"] = apt;

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

  // Clouds
  auto &cp = g_cloudManager.GetParams();
  j["cld"] = {
    {"den",cp.density},{"abs",cp.absorption},{"cov",cp.coverage},{"sca",cp.scattering},
    {"stp",cp.steps},{"si",cp.sunIntensity},{"ct",cp.cloudTop},{"cb",cp.cloudBottom},
    {"ws",cp.windSpeed},{"bs",cp.baseScale},{"ds",cp.detailScale},{"cs",cp.coverageScale},
    {"cv",cp.coverageVariation},{"er",cp.erosion},{"wst",cp.warpStrength},{"sp",cp.shapePower},
    {"ps",cp.powderStrength},{"ssh",cp.shadowSteps},{"sss",cp.shadowStepSize},
    {"sl",cp.shadowLod},{"ms2",cp.maxSteps},{"vsm",cp.verticalStepMeters},
    {"se",cp.shadowEvery},{"sdt",cp.shadowDensityThreshold}
  };

  // Streamline
  j["stl"] = {
    {"en", DX12Context::g_streamline.IsEnabled()},
    {"mo", (int)DX12Context::g_streamline.GetMode()},
    {"qu", (int)DX12Context::g_streamline.GetQuality()}
  };

  // DXR
  j["dxr"] = {
    {"dm", (int)DxrRenderer::GetDenoiserMode()},
    {"oq", (int)DxrRenderer::GetOidnQuality()},
    {"rj", DxrRenderer::GetRrJitterScale()}
  };

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
    {"esa", ibl.GetEnvSolidAngleSampling()},
    {"fsi", ibl.GetFileSunIntensity()}, {"fsr", ibl.GetFileSunRadiusDeg()}
  };

  // Materials
  for (const auto &mat : g_loadedMaterials) {
    j["mat"].push_back({
      {"n",   std::string(mat.name)},
      {"dc",  {mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2], mat.diffuseColor[3]}},
      {"rc",  {mat.reflectionColor[0], mat.reflectionColor[1], mat.reflectionColor[2], mat.reflectionColor[3]}},
      {"rg",  mat.reflectionGlossiness}, {"mt", mat.metalness},
      {"rfc", {mat.refractionColor[0], mat.refractionColor[1], mat.refractionColor[2], mat.refractionColor[3]}},
      {"rfg", mat.refractionGlossiness}, {"ior", mat.ior},
      {"ec",  {mat.emissiveColor[0], mat.emissiveColor[1], mat.emissiveColor[2], mat.emissiveColor[3]}},
      {"ei",  mat.emissiveIntensity},
      {"cc",  mat.clearcoat}, {"ccr", mat.clearcoatRoughness},
      {"tw",  mat.thinWalled}, {"tl", mat.translucency},
      {"uvs", {mat.uvScale[0], mat.uvScale[1]}},
      {"uvo", {mat.uvOffset[0], mat.uvOffset[1]}},
      {"tpe", mat.triPlanarEnabled}, {"tps", mat.triPlanarScale},
      {"tpsh",mat.triPlanarSharpness}, {"tpns",mat.triPlanarNormalStrength},
      {"dt",  mat.diffuseTexture},   {"rft", mat.reflectionTexture},
      {"rfrt",mat.refractionTexture},{"nt2", mat.normalTexture},
      {"et",  mat.emissiveTexture},  {"ot",  mat.occlusionTexture},
      {"mrt", mat.metalRoughTexture},
      {"ds",  mat.doubleSided}, {"am", mat.alphaMode},
      {"ig",  mat.isGrass},
      {"gc",  {mat.grassColor[0], mat.grassColor[1], mat.grassColor[2]}},
      {"gbs", mat.grassBladeSize}, {"gbc", mat.grassBladeCount},
      {"gbv", mat.grassBladeVariation}
    });
  }

  // Nodes
  for (const auto &node : Scene::GetNodes()) {
    std::vector<float> xf(16);
    for (int i = 0; i < 16; ++i) xf[i] = node.transform[i];
    j["nod"].push_back({
      {"n", node.name}, {"sp", node.sourcePath},
      {"v", node.visible}, {"s", node.selected},
      {"mi", node.meshIndices}, {"t", xf}
    });
  }

  // Lights
  for (const auto &lt : Scene::GetLights()) {
    j["lgt"].push_back({
      {"ty", lt.type},
      {"p",  {lt.position[0], lt.position[1], lt.position[2]}},
      {"e",  {lt.emission[0], lt.emission[1], lt.emission[2]}},
      {"d",  {lt.direction[0], lt.direction[1], lt.direction[2]}},
      {"r",  lt.radius}, {"ica", lt.innerConeAngle}, {"oca", lt.outerConeAngle},
      {"ae", {lt.areaExtents[0], lt.areaExtents[1]}},
      {"iai",lt.iesAtlasIndex}
    });
  }

  return j;
}

// ---------------------------------------------------------------------------
// Apply PRS metadata (compact keys) to engine state
// ---------------------------------------------------------------------------
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
    g_camYaw = c.value("yaw", g_camYaw);
    g_camPitch = c.value("pit", g_camPitch);
    g_cameraData.useAdaptiveSampling = c.value("as", 0.0f);
    g_cameraData.noiseThreshold = c.value("nt", 0.05f);
    g_cameraData.debugVisualizationMode = c.value("dvm", 0.0f);
    g_cameraData.sampleEnvSolidAngle = c.value("sea", g_cameraData.sampleEnvSolidAngle);
    DxrRenderer::SetAutoExposure(c.value("ae", DxrRenderer::GetAutoExposure()));
    DxrRenderer::SetPhysicalCameraExposure(c.value("pce", DxrRenderer::GetPhysicalCameraExposure()));
    DxrRenderer::SetPhysicalCameraSettings(c.value("iso",100.0f), c.value("ss",1.0f/125.0f), c.value("apt",16.0f));
    DxrRenderer::SetExposureCompensation(c.value("ec", DxrRenderer::GetExposureCompensation()));
  }
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
    cp.powderStrength=c.value("ps",cp.powderStrength); cp.shadowSteps=c.value("ssh",cp.shadowSteps);
    cp.shadowStepSize=c.value("sss",cp.shadowStepSize); cp.shadowLod=c.value("sl",cp.shadowLod);
    cp.maxSteps=c.value("ms2",cp.maxSteps); cp.verticalStepMeters=c.value("vsm",cp.verticalStepMeters);
    cp.shadowEvery=c.value("se",cp.shadowEvery); cp.shadowDensityThreshold=c.value("sdt",cp.shadowDensityThreshold);
  }
  if (j.contains("stl")) {
    auto &s = j["stl"];
    DX12Context::g_streamline.SetEnabled(s.value("en", true));
    DX12Context::g_streamline.SetMode((StreamlineManager::Mode)s.value("mo", (int)StreamlineManager::Mode::Off));
    DX12Context::g_streamline.SetQuality((StreamlineManager::Quality)s.value("qu", (int)StreamlineManager::Quality::Balanced));
  }
  if (j.contains("dxr")) {
    auto &d = j["dxr"];
    DxrRenderer::SetDenoiserMode((DxrRenderer::DenoiserMode)d.value("dm", (int)DxrRenderer::GetDenoiserMode()));
    DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)d.value("oq", (int)DxrRenderer::GetOidnQuality()));
    DxrRenderer::SetRrJitterScale(d.value("rj", DxrRenderer::GetRrJitterScale()));
  }
  if (j.contains("lit")) {
    auto &l = j["lit"];
    if (l.contains("ld") && l["ld"].size()>=4) for (int i=0;i<4;++i) g_cameraData.lightDir[i]=l["ld"][i];
    if (l.contains("lc") && l["lc"].size()>=4) for (int i=0;i<4;++i) g_cameraData.lightColor[i]=l["lc"][i];
  }
  if (j.contains("lgt") && j["lgt"].is_array()) {
    auto &sceneLights = Scene::GetLights();
    sceneLights.clear();
    for (const auto &l : j["lgt"]) {
      Light light = {};
      light.type = l.value("ty", 1u);
      auto p = l.value("p", std::vector<float>{0,0,0}); if (p.size()>=3) { light.position[0]=p[0]; light.position[1]=p[1]; light.position[2]=p[2]; }
      auto e = l.value("e", std::vector<float>{10,10,10}); if (e.size()>=3) { light.emission[0]=e[0]; light.emission[1]=e[1]; light.emission[2]=e[2]; }
      auto d = l.value("d", std::vector<float>{0,-1,0}); if (d.size()>=3) { light.direction[0]=d[0]; light.direction[1]=d[1]; light.direction[2]=d[2]; }
      light.radius = l.value("r", 0.1f);
      light.innerConeAngle = l.value("ica", 1.0f);
      light.outerConeAngle = l.value("oca", 0.5f);
      auto ae = l.value("ae", std::vector<float>{1,1}); if (ae.size()>=2) { light.areaExtents[0]=ae[0]; light.areaExtents[1]=ae[1]; }
      light.iesAtlasIndex = l.value("iai", -1);
      sceneLights.push_back(light);
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
    ibl.SetSkyVisibility(i.value("vis",30.0f)); ibl.SetSkyAlbedo(i.value("alb",0.5f));
    ibl.SetSolarAltitude(i.value("sel",0.5f)); ibl.SetSolarAzimuth(i.value("saz",0.0f));
    ibl.SetObserverAltitude(i.value("alt",200.0f)); ibl.SetSkyIntensity(i.value("ski",1.0f));
    ibl.SetSunIntensity(i.value("sui",1.0f)); ibl.SetSunSize(i.value("sus",2.0f));
    ibl.SetPhysicalCalibrationEnabled(i.value("pc", ibl.IsPhysicalCalibrationEnabled()));
    ibl.SetIblRotationDegrees(i.value("rot", ibl.GetIblRotationDegrees()));
    ibl.SetEnvSolidAngleSampling(i.value("esa", ibl.GetEnvSolidAngleSampling()));
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
      node.visible = n.value("v", true);
      node.meshIndices = n["mi"].get<std::vector<size_t>>();
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
          if (n.contains("t")) for (int i=0;i<16;++i) nodes.back().transform[i]=n["t"][i];
          nodes.back().selected = n.value("s", false);
        }
      }
      continue;
    }
    if (fs::exists(srcPath) && Scene::ImportModel(srcPath)) {
      auto &nodes = const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
      if (!nodes.empty()) {
        nodes.back().name = n.value("n", nodes.back().name);
        nodes.back().visible = n.value("v", true);
        if (n.contains("t")) for (int i=0;i<16;++i) nodes.back().transform[i]=n["t"][i];
        nodes.back().selected = n.value("s", false);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Restore materials from PRS metadata
// ---------------------------------------------------------------------------
static void RestoreMaterialsPRS(const json &j, std::vector<int> &remap) {
  if (!j.contains("mat")) return;
  auto &mats = j["mat"];
  remap.resize(mats.size(), -1);
  for (size_t ji = 0; ji < mats.size(); ++ji) {
    const auto &sm = mats[ji];
    std::string name = sm.value("n", "Material");
    int idx = -1;
    for (size_t mi = 0; mi < g_loadedMaterials.size(); ++mi)
      if (std::string(g_loadedMaterials[mi].name) == name) { idx=(int)mi; break; }
    if (idx == -1) {
      Asset::Material nm; strncpy_s(nm.name, name.c_str(), sizeof(nm.name)-1);
      g_loadedMaterials.push_back(nm); idx=(int)g_loadedMaterials.size()-1;
    }
    remap[ji] = idx;
    auto &mat = g_loadedMaterials[idx];
    if (sm.contains("dc"))  for (int k=0;k<4;k++) mat.diffuseColor[k]=sm["dc"][k];
    if (sm.contains("rc"))  for (int k=0;k<4;k++) mat.reflectionColor[k]=sm["rc"][k];
    mat.reflectionGlossiness = sm.value("rg", mat.reflectionGlossiness);
    mat.metalness = sm.value("mt", mat.metalness);
    if (sm.contains("rfc")) for (int k=0;k<4;k++) mat.refractionColor[k]=sm["rfc"][k];
    mat.refractionGlossiness = sm.value("rfg", mat.refractionGlossiness);
    mat.ior = sm.value("ior", mat.ior);
    if (sm.contains("ec"))  for (int k=0;k<4;k++) mat.emissiveColor[k]=sm["ec"][k];
    mat.emissiveIntensity = sm.value("ei", mat.emissiveIntensity);
    mat.clearcoat = sm.value("cc", mat.clearcoat);
    mat.clearcoatRoughness = sm.value("ccr", mat.clearcoatRoughness);
    mat.thinWalled = sm.value("tw", mat.thinWalled);
    mat.translucency = sm.value("tl", mat.translucency);
    if (sm.contains("uvs")) for (int k=0;k<2;k++) mat.uvScale[k]=sm["uvs"][k];
    if (sm.contains("uvo")) for (int k=0;k<2;k++) mat.uvOffset[k]=sm["uvo"][k];
    mat.triPlanarEnabled = sm.value("tpe", mat.triPlanarEnabled);
    mat.triPlanarScale = sm.value("tps", mat.triPlanarScale);
    mat.triPlanarSharpness = sm.value("tpsh", mat.triPlanarSharpness);
    mat.triPlanarNormalStrength = sm.value("tpns", mat.triPlanarNormalStrength);
    mat.isGrass = sm.value("ig", mat.isGrass);
    if (sm.contains("gc")) for (int k=0;k<3;k++) mat.grassColor[k]=sm["gc"][k];
    else if (mat.isGrass) { mat.grassColor[0]=mat.diffuseColor[0]; mat.grassColor[1]=mat.diffuseColor[1]; mat.grassColor[2]=mat.diffuseColor[2]; }
    mat.grassBladeSize = sm.value("gbs", mat.grassBladeSize);
    mat.grassBladeCount = sm.value("gbc", mat.grassBladeCount);
    mat.grassBladeVariation = sm.value("gbv", mat.grassBladeVariation);
    auto setTex = [&](const char *key, int &field) {
      if (sm.contains(key)) { int t=sm[key]; field=(t>=0 && t<(int)g_loadedTextures.size())?t:-1; }
    };
    setTex("dt",mat.diffuseTexture); setTex("rft",mat.reflectionTexture);
    setTex("rfrt",mat.refractionTexture); setTex("nt2",mat.normalTexture);
    setTex("et",mat.emissiveTexture); setTex("ot",mat.occlusionTexture);
    setTex("mrt",mat.metalRoughTexture);
    mat.doubleSided = sm.value("ds", mat.doubleSided);
    mat.alphaMode = sm.value("am", mat.alphaMode);
  }
}

// ===========================================================================
//  Legacy JSON helpers (backward compatibility with old .json scene files)
// ===========================================================================
static void ApplyMetadataJSON(const json &j) {
  if (j.contains("camera")) {
    auto &c = j["camera"];
    g_cameraData.pos[0]=c["pos"][0]; g_cameraData.pos[1]=c["pos"][1]; g_cameraData.pos[2]=c["pos"][2];
    if (c.contains("forward")) { g_cameraData.forward[0]=c["forward"][0]; g_cameraData.forward[1]=c["forward"][1]; g_cameraData.forward[2]=c["forward"][2]; }
    if (c.contains("up")) { g_cameraData.up[0]=c["up"][0]; g_cameraData.up[1]=c["up"][1]; g_cameraData.up[2]=c["up"][2]; }
    g_cameraData.fov=c.value("fov",60.0f);
    g_cameraData.nearZ=c.value("nearZ",g_cameraData.nearZ);
    g_cameraData.farZ=c.value("farZ",g_cameraData.farZ);
    g_cameraData.intensity=(std::clamp)(c.value("intensity",g_cameraData.intensity),1e-5f,10.0f);
    g_cameraData.maxSPP=c.value("maxSPP",1024.0f);
    g_cameraData.maxSpecularBounces=c.value("maxSpecularBounces",3.0f);
    g_cameraData.maxRefractiveBounces=c.value("maxRefractiveBounces",3.0f);
    g_cameraData.maxGIBounces=c.value("maxGIBounces",2.0f);
    g_camYaw=c.value("yaw",g_camYaw); g_camPitch=c.value("pitch",g_camPitch);
    g_cameraData.useAdaptiveSampling=c.value("useAdaptiveSampling",0.0f);
    g_cameraData.noiseThreshold=c.value("noiseThreshold",0.05f);
    g_cameraData.debugVisualizationMode=c.value("debugVisualizationMode",0.0f);
    g_cameraData.sampleEnvSolidAngle=c.value("sampleEnvSolidAngle",g_cameraData.sampleEnvSolidAngle);
    DxrRenderer::SetAutoExposure(c.value("autoExposure",DxrRenderer::GetAutoExposure()));
    DxrRenderer::SetPhysicalCameraExposure(c.value("physicalCameraExposure",DxrRenderer::GetPhysicalCameraExposure()));
    DxrRenderer::SetPhysicalCameraSettings(c.value("iso",100.0f), c.value("shutterSeconds",1.0f/125.0f), c.value("aperture",16.0f));
    DxrRenderer::SetExposureCompensation(c.value("exposureCompensation",DxrRenderer::GetExposureCompensation()));
  }
  if (j.contains("settings")) {
    auto &s=j["settings"];
    g_cloudRenderingEnabled=s.value("cloudRendering",true); g_camSpeed=s.value("camSpeed",g_camSpeed);
    g_mouseSensitivity=s.value("mouseSensitivity",g_mouseSensitivity); g_drawGrid=s.value("drawGrid",g_drawGrid);
    g_debugMode=s.value("debugView",g_debugMode);
  }
  if (j.contains("sky")) {
    auto &s=j["sky"];
    g_timeOfDay=s.value("timeOfDay",g_timeOfDay); g_northOffset=s.value("northOffset",g_northOffset);
    g_latitudeDeg=s.value("latitudeDeg",g_latitudeDeg); g_dayOfYear=s.value("dayOfYear",g_dayOfYear);
  }
  if (j.contains("clouds")) {
    auto &c=j["clouds"]; auto &cp=g_cloudManager.GetParams();
    cp.density=c.value("density",cp.density); cp.absorption=c.value("absorption",cp.absorption);
    cp.coverage=c.value("coverage",cp.coverage); cp.scattering=c.value("scattering",cp.scattering);
    cp.steps=c.value("steps",cp.steps); cp.sunIntensity=c.value("sunIntensity",cp.sunIntensity);
    cp.cloudTop=c.value("cloudTop",cp.cloudTop); cp.cloudBottom=c.value("cloudBottom",cp.cloudBottom);
    cp.windSpeed=c.value("windSpeed",cp.windSpeed); cp.baseScale=c.value("baseScale",cp.baseScale);
    cp.detailScale=c.value("detailScale",cp.detailScale); cp.coverageScale=c.value("coverageScale",cp.coverageScale);
    cp.coverageVariation=c.value("coverageVariation",cp.coverageVariation); cp.erosion=c.value("erosion",cp.erosion);
    cp.warpStrength=c.value("warpStrength",cp.warpStrength); cp.shapePower=c.value("shapePower",cp.shapePower);
    cp.powderStrength=c.value("powderStrength",cp.powderStrength); cp.shadowSteps=c.value("shadowSteps",cp.shadowSteps);
    cp.shadowStepSize=c.value("shadowStepSize",cp.shadowStepSize); cp.shadowLod=c.value("shadowLod",cp.shadowLod);
    cp.maxSteps=c.value("maxSteps",cp.maxSteps); cp.verticalStepMeters=c.value("verticalStepMeters",cp.verticalStepMeters);
    cp.shadowEvery=c.value("shadowEvery",cp.shadowEvery); cp.shadowDensityThreshold=c.value("shadowDensityThreshold",cp.shadowDensityThreshold);
  }
  if (j.contains("streamline")) {
    auto &sl=j["streamline"];
    DX12Context::g_streamline.SetEnabled(sl.value("enabled",true));
    DX12Context::g_streamline.SetMode((StreamlineManager::Mode)sl.value("mode",(int)StreamlineManager::Mode::Off));
    DX12Context::g_streamline.SetQuality((StreamlineManager::Quality)sl.value("quality",(int)StreamlineManager::Quality::Balanced));
  }
  if (j.contains("dxr")) {
    auto &d=j["dxr"];
    DxrRenderer::SetDenoiserMode((DxrRenderer::DenoiserMode)d.value("denoiserMode",(int)DxrRenderer::GetDenoiserMode()));
    DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)d.value("oidnQuality",(int)DxrRenderer::GetOidnQuality()));
    DxrRenderer::SetRrJitterScale(d.value("rrJitterScale",DxrRenderer::GetRrJitterScale()));
  }
  if (j.contains("lighting")) {
    auto &l=j["lighting"];
    if (l.contains("lightDir")&&l["lightDir"].size()>=4) for(int i=0;i<4;++i) g_cameraData.lightDir[i]=l["lightDir"][i];
    if (l.contains("lightColor")&&l["lightColor"].size()>=4) for(int i=0;i<4;++i) g_cameraData.lightColor[i]=l["lightColor"][i];
  }
  if (j.contains("lights")&&j["lights"].is_array()) {
    auto &sceneLights=Scene::GetLights(); sceneLights.clear();
    for (const auto &l:j["lights"]) {
      Light light={};
      light.type=l.value("type",1u);
      auto p=l.value("position",std::vector<float>{0,0,0}); if(p.size()>=3){light.position[0]=p[0];light.position[1]=p[1];light.position[2]=p[2];}
      auto e=l.value("emission",std::vector<float>{10,10,10}); if(e.size()>=3){light.emission[0]=e[0];light.emission[1]=e[1];light.emission[2]=e[2];}
      auto d=l.value("direction",std::vector<float>{0,-1,0}); if(d.size()>=3){light.direction[0]=d[0];light.direction[1]=d[1];light.direction[2]=d[2];}
      light.radius=l.value("radius",0.1f); light.innerConeAngle=l.value("innerConeAngle",1.0f); light.outerConeAngle=l.value("outerConeAngle",0.5f);
      auto ae=l.value("areaExtents",std::vector<float>{1,1}); if(ae.size()>=2){light.areaExtents[0]=ae[0];light.areaExtents[1]=ae[1];}
      light.iesAtlasIndex=l.value("iesAtlasIndex",-1);
      sceneLights.push_back(light);
    }
    Scene::UpdateLights();
  }
  if (j.contains("renderMode")) g_currentRenderMode=(j["renderMode"]=="DXR")?RenderMode::DXR:RenderMode::Raster;
  if (j.contains("ibl")) {
    auto &ibl=IBLManager::Get(); auto &i=j["ibl"];
    bool wantsFile=(i["source"]=="File");
    if (wantsFile) { std::string p=i.value("filePath",std::string()); if(!p.empty()&&fs::exists(p)) ibl.LoadEnvironmentMap(p); }
    ibl.SetIBLSource(wantsFile?IBLManager::IBLSource::File:IBLManager::IBLSource::PragueSkyModel);
    ibl.SetSkyVisibility(i.value("visibility",30.0f)); ibl.SetSkyAlbedo(i.value("albedo",0.5f));
    ibl.SetSolarAltitude(i.value("solarElevation",0.5f)); ibl.SetSolarAzimuth(i.value("solarAzimuth",0.0f));
    ibl.SetObserverAltitude(i.value("altitude",200.0f)); ibl.SetSkyIntensity(i.value("skyIntensity",1.0f));
    ibl.SetSunIntensity(i.value("sunIntensity",1.0f)); ibl.SetSunSize(i.value("sunSize",2.0f));
    ibl.SetPhysicalCalibrationEnabled(i.value("physicalCalibration",ibl.IsPhysicalCalibrationEnabled()));
    ibl.SetIblRotationDegrees(i.value("iblRotationDegrees",ibl.GetIblRotationDegrees()));
    ibl.SetEnvSolidAngleSampling(i.value("envSolidAngleSampling",ibl.GetEnvSolidAngleSampling()));
    ibl.SetFileSunIntensity(i.value("fileSunIntensity",ibl.GetFileSunIntensity()));
    ibl.SetFileSunRadiusDeg(i.value("fileSunRadiusDeg",ibl.GetFileSunRadiusDeg()));
    ibl.UpdateSkyModel();
  }
}

static void RestoreNodesJSON(const json &j, bool hasEmbedded) {
  if (!j.contains("nodes")) return;
  for (const auto &n : j["nodes"]) {
    std::string sp = n.value("sourcePath","");
    if (hasEmbedded && n.contains("meshIndices")) {
      Scene::Node node; node.name=n.value("name","EmbeddedNode"); node.sourcePath=sp;
      node.visible=n.value("visible",true); node.meshIndices=n["meshIndices"].get<std::vector<size_t>>();
      if (n.contains("transform")) for(int i=0;i<16;++i) node.transform[i]=n["transform"][i];
      node.selected=n.value("selected",false);
      const_cast<std::vector<Scene::Node>&>(Scene::GetNodes()).push_back(node);
      continue;
    }
    if (sp.empty()) {
      if (n.value("name","") == "Ground Plane") {
        Scene::AddDefaultPlane(0.0f);
        auto &nodes=const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
        if (!nodes.empty()) { nodes.back().visible=n.value("visible",true);
          if(n.contains("transform")) for(int i=0;i<16;++i) nodes.back().transform[i]=n["transform"][i];
          nodes.back().selected=n.value("selected",false); }
      }
      continue;
    }
    if (fs::exists(sp) && Scene::ImportModel(sp)) {
      auto &nodes=const_cast<std::vector<Scene::Node>&>(Scene::GetNodes());
      if (!nodes.empty()) { nodes.back().name=n.value("name",nodes.back().name);
        nodes.back().visible=n.value("visible",true);
        if(n.contains("transform")) for(int i=0;i<16;++i) nodes.back().transform[i]=n["transform"][i];
        nodes.back().selected=n.value("selected",false); }
    }
  }
}

static void RestoreMaterialsJSON(const json &j, std::vector<int> &remap) {
  if (!j.contains("materials")) return;
  auto &savedMats=j["materials"]; remap.resize(savedMats.size(),-1);
  for (size_t ji=0;ji<savedMats.size();++ji) {
    const auto &sm=savedMats[ji]; std::string name=sm.value("name","Material");
    int idx=-1;
    for (size_t mi=0;mi<g_loadedMaterials.size();++mi)
      if (std::string(g_loadedMaterials[mi].name)==name){idx=(int)mi;break;}
    if (idx==-1) { Asset::Material nm; strncpy_s(nm.name,name.c_str(),sizeof(nm.name)-1); g_loadedMaterials.push_back(nm); idx=(int)g_loadedMaterials.size()-1; }
    remap[ji]=idx; auto &mat=g_loadedMaterials[idx];
    if(sm.contains("diffuseColor")) for(int k=0;k<4;k++) mat.diffuseColor[k]=sm["diffuseColor"][k];
    if(sm.contains("reflectionColor")) for(int k=0;k<4;k++) mat.reflectionColor[k]=sm["reflectionColor"][k];
    mat.reflectionGlossiness=sm.value("reflectionGlossiness",mat.reflectionGlossiness);
    mat.metalness=sm.value("metalness",mat.metalness);
    if(sm.contains("refractionColor")) for(int k=0;k<4;k++) mat.refractionColor[k]=sm["refractionColor"][k];
    mat.refractionGlossiness=sm.value("refractionGlossiness",mat.refractionGlossiness);
    mat.ior=sm.value("ior",mat.ior);
    if(sm.contains("emissiveColor")) for(int k=0;k<4;k++) mat.emissiveColor[k]=sm["emissiveColor"][k];
    mat.emissiveIntensity=sm.value("emissiveIntensity",mat.emissiveIntensity);
    mat.clearcoat=sm.value("clearcoat",mat.clearcoat); mat.clearcoatRoughness=sm.value("clearcoatRoughness",mat.clearcoatRoughness);
    mat.thinWalled=sm.value("thinWalled",mat.thinWalled); mat.translucency=sm.value("translucency",mat.translucency);
    if(sm.contains("uvScale")) for(int k=0;k<2;k++) mat.uvScale[k]=sm["uvScale"][k];
    if(sm.contains("uvOffset")) for(int k=0;k<2;k++) mat.uvOffset[k]=sm["uvOffset"][k];
    mat.triPlanarEnabled=sm.value("triPlanarEnabled",mat.triPlanarEnabled);
    mat.triPlanarScale=sm.value("triPlanarScale",mat.triPlanarScale);
    mat.triPlanarSharpness=sm.value("triPlanarSharpness",mat.triPlanarSharpness);
    mat.triPlanarNormalStrength=sm.value("triPlanarNormalStrength",mat.triPlanarNormalStrength);
    mat.isGrass=sm.value("isGrass",mat.isGrass);
    if(sm.contains("grassColor")) for(int k=0;k<3;k++) mat.grassColor[k]=sm["grassColor"][k];
    else if(mat.isGrass){mat.grassColor[0]=mat.diffuseColor[0];mat.grassColor[1]=mat.diffuseColor[1];mat.grassColor[2]=mat.diffuseColor[2];}
    mat.grassBladeSize=sm.value("grassBladeSize",mat.grassBladeSize);
    mat.grassBladeCount=sm.value("grassBladeCount",mat.grassBladeCount);
    mat.grassBladeVariation=sm.value("grassBladeVariation",mat.grassBladeVariation);
    auto setTex=[&](const char*key,int&field){ if(sm.contains(key)){int t=sm[key];field=(t>=0&&t<(int)g_loadedTextures.size())?t:-1;} };
    setTex("diffuseTexture",mat.diffuseTexture); setTex("reflectionTexture",mat.reflectionTexture);
    setTex("refractionTexture",mat.refractionTexture); setTex("normalTexture",mat.normalTexture);
    setTex("emissiveTexture",mat.emissiveTexture); setTex("occlusionTexture",mat.occlusionTexture);
    setTex("metalRoughTexture",mat.metalRoughTexture);
    mat.doubleSided=sm.value("doubleSided",mat.doubleSided); mat.alphaMode=sm.value("alphaMode",mat.alphaMode);
  }
}

// ===========================================================================
//  Base64 decode (legacy JSON only)
// ===========================================================================
static const std::string b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<unsigned char> Base64Decode(const std::string &enc) {
  size_t in_len = enc.size(); int i=0, j2=0, in_=0;
  unsigned char c4[4], c3[3]; std::vector<unsigned char> ret;
  while (in_len-- && enc[in_]!='=' && (isalnum(enc[in_])||enc[in_]=='+'||enc[in_]=='/')) {
    c4[i++] = enc[in_++];
    if (i==4) {
      for(i=0;i<4;i++) c4[i]=(unsigned char)b64chars.find(c4[i]);
      c3[0]=(c4[0]<<2)+((c4[1]&0x30)>>4); c3[1]=((c4[1]&0xf)<<4)+((c4[2]&0x3c)>>2); c3[2]=((c4[2]&0x3)<<6)+c4[3];
      for(i=0;i<3;i++) ret.push_back(c3[i]); i=0;
    }
  }
  if (i) {
    for(j2=i;j2<4;j2++) c4[j2]=0;
    for(j2=0;j2<4;j2++) c4[j2]=(unsigned char)b64chars.find(c4[j2]);
    c3[0]=(c4[0]<<2)+((c4[1]&0x30)>>4); c3[1]=((c4[1]&0xf)<<4)+((c4[2]&0x3c)>>2); c3[2]=((c4[2]&0x3)<<6)+c4[3];
    for(j2=0;j2<i-1;j2++) ret.push_back(c3[j2]);
  }
  return ret;
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
    ReportProgress(0.01f, "Preparing scene metadata");
    fprintf(stderr, "PRS: Saving scene to %s\n", path.c_str());

    // 1. Build metadata as msgpack
    json metadata = BuildMetadata();
    std::vector<uint8_t> msgpack = json::to_msgpack(metadata);
    ReportProgress(0.08f, "Packing metadata");
    fprintf(stderr, "PRS: Metadata: %zu bytes (msgpack)\n", msgpack.size());

    // 2. Build uncompressed binary payload
    std::vector<uint8_t> payload;
    {
      // Pre-estimate size
      size_t est = 4 + msgpack.size() + 4 + 4;
      for (auto &m : g_loadedMeshes) est += 40 + m.cpuVertices.size()*sizeof(Asset::Vertex) + m.cpuIndices.size()*sizeof(uint32_t);
      for (auto &t : g_loadedTextures) est += 16 + t.cpuData.size();
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
    for (const auto &mesh : g_loadedMeshes) {
      w.writeI32(mesh.materialIndex);
      w.writeU32((uint32_t)mesh.vertexCount);
      w.writeU32((uint32_t)mesh.indexCount);
      w.writeF32(mesh.minBound[0]); w.writeF32(mesh.minBound[1]); w.writeF32(mesh.minBound[2]);
      w.writeF32(mesh.maxBound[0]); w.writeF32(mesh.maxBound[1]); w.writeF32(mesh.maxBound[2]);
      uint32_t vb = (uint32_t)(mesh.cpuVertices.size() * sizeof(Asset::Vertex));
      w.writeU32(vb);
      if (vb) w.writeBytes(mesh.cpuVertices.data(), vb);
      uint32_t ib = (uint32_t)(mesh.cpuIndices.size() * sizeof(uint32_t));
      w.writeU32(ib);
      if (ib) w.writeBytes(mesh.cpuIndices.data(), ib);
    }

    // Textures — raw binary
    w.writeU32((uint32_t)g_loadedTextures.size());
    for (const auto &tex : g_loadedTextures) {
      w.writeU32(tex.width);
      w.writeU32(tex.height);
      w.writeU32((uint32_t)tex.format);
      uint32_t db = (uint32_t)tex.cpuData.size();
      w.writeU32(db);
      if (db) w.writeBytes(tex.cpuData.data(), db);
    }
    ReportProgress(0.28f, "Finalizing payload");

    size_t uncompSize = payload.size();
    fprintf(stderr, "PRS: Uncompressed: %.2f MB\n", uncompSize / (1024.0*1024.0));

    // 3. Compress with LZMS
    std::vector<uint8_t> compressed;
    if (!CompressLZMSChunked(payload, compressed)) {
      fprintf(stderr, "PRS: Compression failed\n");
      return false;
    }
    payload.clear(); payload.shrink_to_fit();

    fprintf(stderr, "PRS: Compressed: %.2f MB (%.1f%% ratio)\n",
            compressed.size()/(1024.0*1024.0), 100.0*compressed.size()/uncompSize);

    // 4. Write: header (16 bytes) + compressed payload
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(PRS_MAGIC, 4);
    uint32_t ver = PRS_VERSION;
    file.write(reinterpret_cast<const char*>(&ver), 4);
    uint64_t usz = (uint64_t)uncompSize;
    file.write(reinterpret_cast<const char*>(&usz), 8);
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
        ReportProgress(0.98f, "Writing file");

    fprintf(stderr, "PRS: Saved %.2f MB (was %.2f MB uncompressed)\n",
            (16+compressed.size())/(1024.0*1024.0), uncompSize/(1024.0*1024.0));
        ReportProgress(1.0f, "Save complete");
    return true;
  } catch (const std::exception &e) {
    std::cerr << "SaveScene: " << e.what() << std::endl;
    return false;
  }
}

// ---------------------------------------------------------------------------
// LoadScene — auto-detects PRS binary vs legacy JSON
// ---------------------------------------------------------------------------
bool LoadScene(const std::string &path) {
  try {
    std::ifstream probe(path, std::ios::binary);
    if (!probe.is_open()) return false;
    char magic[4] = {};
    probe.read(magic, 4);
    probe.close();

    if (memcmp(magic, PRS_MAGIC, 4) == 0)
      return LoadScenePRS(path);
    else
      return LoadSceneJSON(path);
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

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    size_t fileSize = (size_t)file.tellg();
    if (fileSize < 16) return false;
    file.seekg(0);

    char magic[4]; file.read(magic, 4);
    if (memcmp(magic, PRS_MAGIC, 4) != 0) return false;
    uint32_t version; file.read(reinterpret_cast<char*>(&version), 4);
    if (version != PRS_VERSION) { fprintf(stderr, "PRS: Unknown version %u\n", version); return false; }
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
      uint32_t db = r.readU32();
      if (db) {
        const uint8_t *px = r.readBytes(db);
        g_loadedTextures[ti] = Asset::LoadTextureFromMemory(px, w, h, fmt);
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
    RestoreMaterialsPRS(meta, remap);

    if (hasEmbedded) {
      for (size_t i = 0; i < embMeshCnt; ++i) {
        int old = g_loadedMeshes[i].materialIndex;
        if (old >= 0 && old < (int)remap.size()) g_loadedMeshes[i].materialIndex = remap[old];
      }
      Scene::RebuildAccelerationStructures();
      DxrRenderer::CreateRayTracingPipeline(0, 0);
    }

    UpdateCameraCB();
    DxrRenderer::ResetAccumulation();
    fprintf(stderr, "PRS: Scene loaded OK\n");
    ReportProgress(1.0f, "Load complete");
    return true;
  } catch (const std::exception &e) {
    std::cerr << "LoadScenePRS: " << e.what() << std::endl;
    return false;
  }
}

// ---------------------------------------------------------------------------
// LoadSceneJSON — Legacy .json backward compatibility
// ---------------------------------------------------------------------------
bool LoadSceneJSON(const std::string &path) {
  try {
    fprintf(stderr, "LoadScene: Legacy JSON from %s\n", path.c_str());
    std::ifstream file(path);
    if (!file.is_open()) return false;
    json j; file >> j;

    Scene::ResetScene();

    bool hasEmbedded = false;
    size_t embMeshCnt = 0;

    if (j.contains("embeddedAssets")) {
      hasEmbedded = true;
      auto &ea = j["embeddedAssets"];
      if (ea.contains("textures")) {
        for (const auto &t : ea["textures"]) {
          size_t idx = t.value("index", g_loadedTextures.size());
          if (idx >= g_loadedTextures.size()) g_loadedTextures.resize(idx+1);
          std::string b64 = t.value("data","");
          if (!b64.empty()) {
            auto data = Base64Decode(b64);
            g_loadedTextures[idx] = Asset::LoadTextureFromMemory(data.data(), t["width"], t["height"], (DXGI_FORMAT)t["format"]);
          }
        }
        Scene::RegisterTextures(g_loadedTextures);
      }
      if (ea.contains("meshes")) {
        for (const auto &m : ea["meshes"]) {
          auto vData = Base64Decode(m["vertices"]);
          auto iData = Base64Decode(m["indices"]);
          std::vector<Asset::Vertex> verts(m["vertexCount"].get<size_t>());
          if (vData.size()==verts.size()*sizeof(Asset::Vertex)) memcpy(verts.data(),vData.data(),vData.size());
          std::vector<uint32_t> inds(m["indexCount"].get<size_t>());
          if (iData.size()==inds.size()*sizeof(uint32_t)) memcpy(inds.data(),iData.data(),iData.size());
          Asset::GpuMesh mesh=Asset::LoadMeshFromMemory(verts,inds);
          mesh.materialIndex=m.value("materialIndex",-1);
          if(m.contains("minBound")&&m["minBound"].size()>=3) for(int k=0;k<3;k++) mesh.minBound[k]=m["minBound"][k];
          if(m.contains("maxBound")&&m["maxBound"].size()>=3) for(int k=0;k<3;k++) mesh.maxBound[k]=m["maxBound"][k];
          g_loadedMeshes.push_back(mesh);
        }
        embMeshCnt = g_loadedMeshes.size();
      }
    }

    ApplyMetadataJSON(j);
    RestoreNodesJSON(j, hasEmbedded);

    std::vector<int> remap;
    RestoreMaterialsJSON(j, remap);

    if (hasEmbedded) {
      for (size_t i=0;i<embMeshCnt;++i) {
        int old=g_loadedMeshes[i].materialIndex;
        if (old>=0 && old<(int)remap.size()) g_loadedMeshes[i].materialIndex=remap[old];
      }
      Scene::RebuildAccelerationStructures();
      DxrRenderer::CreateRayTracingPipeline(0,0);
    }

    UpdateCameraCB();
    DxrRenderer::ResetAccumulation();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "LoadSceneJSON: " << e.what() << std::endl;
    return false;
  }
}

} // namespace SceneIO
