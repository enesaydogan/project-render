#include "cooked_payload.h"

#include <cstring>
#include <fstream>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Windows Compression API (LZMS), linked via Cabinet.lib — same algorithm the
// .prs writer uses.
#include <compressapi.h>

namespace assetlib {
namespace {

constexpr uint8_t kMagicModel[4] = {'P', 'R', 'C', 'M'};
constexpr uint8_t kMagicTexture[4] = {'P', 'R', 'C', 'T'};
constexpr uint8_t kMagicVolume[4] = {'P', 'R', 'C', 'V'};
constexpr uint32_t kCompressNone = 0;
constexpr uint32_t kCompressLzms = 1;

// --- Append-only writer (host endianness; matches scene_io.cpp) -------------
struct Writer {
  std::vector<uint8_t> &buf;
  explicit Writer(std::vector<uint8_t> &b) : buf(b) {}
  void u32(uint32_t v) { raw(&v, 4); }
  void i32(int32_t v) { raw(&v, 4); }
  void f32(float v) { raw(&v, 4); }
  void bytes(const void *p, size_t n) {
    if (n)
      raw(p, n);
  }
  void raw(const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    buf.insert(buf.end(), b, b + n);
  }
};

// --- Bounds-checked reader (returns false on overrun) -----------------------
struct Reader {
  const uint8_t *data;
  size_t size;
  size_t pos = 0;
  bool ok = true;
  Reader(const uint8_t *d, size_t s) : data(d), size(s) {}
  bool need(size_t n) {
    if (pos + n > size) {
      ok = false;
      return false;
    }
    return true;
  }
  uint32_t u32() {
    uint32_t v = 0;
    if (need(4)) {
      std::memcpy(&v, data + pos, 4);
      pos += 4;
    }
    return v;
  }
  int32_t i32() {
    int32_t v = 0;
    if (need(4)) {
      std::memcpy(&v, data + pos, 4);
      pos += 4;
    }
    return v;
  }
  float f32() {
    float v = 0;
    if (need(4)) {
      std::memcpy(&v, data + pos, 4);
      pos += 4;
    }
    return v;
  }
  // Copies n bytes into out. Fails (and leaves out empty) on overrun.
  void bytes(std::vector<uint8_t> &out, size_t n) {
    out.clear();
    if (n == 0)
      return;
    if (!need(n))
      return;
    out.assign(data + pos, data + pos + n);
    pos += n;
  }
};

bool CompressLzms(const std::vector<uint8_t> &src, std::vector<uint8_t> &out) {
  if (src.empty()) {
    out.clear();
    return true;
  }
  COMPRESSOR_HANDLE h = nullptr;
  if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h))
    return false;
  SIZE_T needed = 0;
  Compress(h, src.data(), src.size(), nullptr, 0, &needed);
  out.resize(needed);
  SIZE_T actual = 0;
  BOOL okc = Compress(h, src.data(), src.size(), out.data(), out.size(),
                      &actual);
  CloseCompressor(h);
  if (!okc)
    return false;
  out.resize(actual);
  return true;
}

bool DecompressLzms(const uint8_t *src, size_t srcSize, size_t uncompressedSize,
                    std::vector<uint8_t> &out) {
  out.clear();
  if (uncompressedSize == 0)
    return true;
  DECOMPRESSOR_HANDLE h = nullptr;
  if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h))
    return false;
  out.resize(uncompressedSize);
  SIZE_T actual = 0;
  BOOL okd =
      Decompress(h, src, srcSize, out.data(), out.size(), &actual);
  CloseDecompressor(h);
  if (!okd || actual != uncompressedSize)
    return false;
  return true;
}

// Wrap a raw payload in a header (magic + version + compression + size) and
// compress the body. Falls back to store if compression doesn't help.
bool Finalize(const uint8_t magic[4], uint32_t version,
              const std::vector<uint8_t> &payload, std::vector<uint8_t> &out) {
  std::vector<uint8_t> compressed;
  uint32_t method = kCompressLzms;
  if (!CompressLzms(payload, compressed) ||
      compressed.size() >= payload.size()) {
    method = kCompressNone;
    compressed = payload;
  }
  out.clear();
  Writer w(out);
  w.bytes(magic, 4);
  w.u32(version);
  w.u32(method);
  w.u32(static_cast<uint32_t>(payload.size())); // uncompressed size
  w.bytes(compressed.data(), compressed.size());
  return true;
}

// Validate header and return the decompressed body in `body`.
bool OpenBlob(const uint8_t *data, size_t size, const uint8_t magic[4],
              uint32_t expectedVersion, std::vector<uint8_t> &body) {
  if (size < 16 || std::memcmp(data, magic, 4) != 0)
    return false;
  Reader r(data, size);
  r.bytes(body, 4); // skip magic (already checked)
  uint32_t version = r.u32();
  uint32_t method = r.u32();
  uint32_t uncompressed = r.u32();
  if (!r.ok || version != expectedVersion)
    return false;
  const uint8_t *bodyPtr = data + r.pos;
  size_t bodyLen = size - r.pos;
  if (method == kCompressNone) {
    body.assign(bodyPtr, bodyPtr + bodyLen);
    return body.size() == uncompressed;
  }
  if (method == kCompressLzms)
    return DecompressLzms(bodyPtr, bodyLen, uncompressed, body);
  return false;
}

} // namespace

bool SerializeCookedModel(const CookedModel &model, std::vector<uint8_t> &out) {
  std::vector<uint8_t> payload;
  Writer w(payload);
  w.u32(static_cast<uint32_t>(model.meshes.size()));
  for (const auto &m : model.meshes) {
    w.i32(m.materialIndex);
    w.i32(m.materialSlot);
    w.u32(m.vertexCount);
    w.u32(m.indexCount);
    for (float b : m.minBound)
      w.f32(b);
    for (float b : m.maxBound)
      w.f32(b);
    w.u32(static_cast<uint32_t>(m.vertexBytes.size()));
    w.bytes(m.vertexBytes.data(), m.vertexBytes.size());
    w.u32(static_cast<uint32_t>(m.indexBytes.size()));
    w.bytes(m.indexBytes.data(), m.indexBytes.size());
  }
  return Finalize(kMagicModel, kCookerVersionMesh, payload, out);
}

bool DeserializeCookedModel(const uint8_t *data, size_t size,
                            CookedModel &out) {
  std::vector<uint8_t> body;
  if (!OpenBlob(data, size, kMagicModel, kCookerVersionMesh, body))
    return false;
  out.meshes.clear();
  Reader r(body.data(), body.size());
  uint32_t count = r.u32();
  if (!r.ok || count > (1u << 24))
    return false;
  out.meshes.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    CookedMesh m;
    m.materialIndex = r.i32();
    m.materialSlot = r.i32();
    m.vertexCount = r.u32();
    m.indexCount = r.u32();
    for (float &b : m.minBound)
      b = r.f32();
    for (float &b : m.maxBound)
      b = r.f32();
    uint32_t vb = r.u32();
    r.bytes(m.vertexBytes, vb);
    uint32_t ib = r.u32();
    r.bytes(m.indexBytes, ib);
    if (!r.ok)
      return false;
    out.meshes.push_back(std::move(m));
  }
  return true;
}

bool SerializeCookedTexture(const CookedTexture &tex,
                            std::vector<uint8_t> &out) {
  std::vector<uint8_t> payload;
  Writer w(payload);
  w.u32(tex.width);
  w.u32(tex.height);
  w.u32(tex.cpuFormat);
  w.u32(tex.cpuMipLevels);
  w.u32(tex.usageSemantic);
  w.u32(static_cast<uint32_t>(tex.data.size()));
  w.bytes(tex.data.data(), tex.data.size());
  return Finalize(kMagicTexture, kCookerVersionTexture, payload, out);
}

bool DeserializeCookedTexture(const uint8_t *data, size_t size,
                              CookedTexture &out) {
  std::vector<uint8_t> body;
  if (!OpenBlob(data, size, kMagicTexture, kCookerVersionTexture, body))
    return false;
  Reader r(body.data(), body.size());
  out.width = r.u32();
  out.height = r.u32();
  out.cpuFormat = r.u32();
  out.cpuMipLevels = r.u32();
  out.usageSemantic = r.u32();
  uint32_t db = r.u32();
  r.bytes(out.data, db);
  return r.ok;
}

bool SerializeCookedVolume(const CookedVolume &vol, std::vector<uint8_t> &out) {
  std::vector<uint8_t> payload;
  Writer w(payload);
  for (int i = 0; i < 3; ++i)
    w.u32(vol.dim[i]);
  w.u32(vol.brickSize);
  for (float b : vol.boundsMin)
    w.f32(b);
  for (float b : vol.boundsMax)
    w.f32(b);
  w.u32(static_cast<uint32_t>(vol.activeVoxels & 0xFFFFFFFF));
  w.u32(static_cast<uint32_t>(vol.activeVoxels >> 32));
  w.u32(static_cast<uint32_t>(vol.bricks.size()));
  for (const auto &b : vol.bricks) {
    w.u32(b.bx);
    w.u32(b.by);
    w.u32(b.bz);
    w.f32(b.minVal);
    w.f32(b.maxVal);
    w.u32(static_cast<uint32_t>(b.data.size()));
    w.bytes(b.data.data(), b.data.size());
  }
  w.f32(vol.temperatureMin);
  w.f32(vol.temperatureMax);
  w.u32(static_cast<uint32_t>(vol.temperatureBricks.size()));
  for (const auto &b : vol.temperatureBricks) {
    w.u32(b.bx);
    w.u32(b.by);
    w.u32(b.bz);
    w.f32(b.minVal);
    w.f32(b.maxVal);
    w.u32(static_cast<uint32_t>(b.data.size()));
    w.bytes(b.data.data(), b.data.size());
  }
  return Finalize(kMagicVolume, kCookerVersionVolume, payload, out);
}

bool DeserializeCookedVolume(const uint8_t *data, size_t size,
                             CookedVolume &out) {
  std::vector<uint8_t> body;
  if (!OpenBlob(data, size, kMagicVolume, kCookerVersionVolume, body))
    return false;
  Reader r(body.data(), body.size());
  for (int i = 0; i < 3; ++i)
    out.dim[i] = r.u32();
  out.brickSize = r.u32();
  for (float &b : out.boundsMin)
    b = r.f32();
  for (float &b : out.boundsMax)
    b = r.f32();
  const uint32_t lo = r.u32();
  const uint32_t hi = r.u32();
  out.activeVoxels = (static_cast<uint64_t>(hi) << 32) | lo;
  uint32_t brickCount = r.u32();
  if (!r.ok || brickCount > (1u << 24))
    return false;
  out.bricks.clear();
  out.bricks.reserve(brickCount);
  for (uint32_t i = 0; i < brickCount; ++i) {
    CookedVolumeBrick b;
    b.bx = r.u32();
    b.by = r.u32();
    b.bz = r.u32();
    b.minVal = r.f32();
    b.maxVal = r.f32();
    uint32_t len = r.u32();
    r.bytes(b.data, len);
    if (!r.ok)
      return false;
    out.bricks.push_back(std::move(b));
  }
  out.temperatureMin = 0.0f;
  out.temperatureMax = 0.0f;
  out.temperatureBricks.clear();
  // Temperature data was appended compatibly; legacy density-only payloads
  // end here and remain valid.
  if (r.pos == r.size)
    return true;
  out.temperatureMin = r.f32();
  out.temperatureMax = r.f32();
  const uint32_t temperatureBrickCount = r.u32();
  if (!r.ok || temperatureBrickCount > (1u << 24))
    return false;
  out.temperatureBricks.reserve(temperatureBrickCount);
  for (uint32_t i = 0; i < temperatureBrickCount; ++i) {
    CookedVolumeBrick b;
    b.bx = r.u32();
    b.by = r.u32();
    b.bz = r.u32();
    b.minVal = r.f32();
    b.maxVal = r.f32();
    const uint32_t len = r.u32();
    r.bytes(b.data, len);
    if (!r.ok)
      return false;
    out.temperatureBricks.push_back(std::move(b));
  }
  return true;
}

bool WriteCookedFile(const std::filesystem::path &path,
                     const std::vector<uint8_t> &bytes) {
  std::error_code ec;
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    if (!bytes.empty())
      out.write(reinterpret_cast<const char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out)
      return false;
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
  }
  return !ec;
}

bool ReadCookedFile(const std::filesystem::path &path,
                    std::vector<uint8_t> &out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return false;
  std::streamoff len = in.tellg();
  if (len < 0)
    return false;
  in.seekg(0);
  out.resize(static_cast<size_t>(len));
  if (len > 0)
    in.read(reinterpret_cast<char *>(out.data()), len);
  return static_cast<bool>(in);
}

uint64_t HashBytes(const void *data, size_t size) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint64_t h = 1469598103934665603ULL; // FNV offset basis
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ULL; // FNV prime
  }
  return h;
}

uint64_t HashFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return 0;
  uint64_t h = 1469598103934665603ULL;
  char buf[64 * 1024];
  while (in) {
    in.read(buf, sizeof(buf));
    std::streamsize got = in.gcount();
    for (std::streamsize i = 0; i < got; ++i) {
      h ^= static_cast<uint8_t>(buf[i]);
      h *= 1099511628211ULL;
    }
  }
  return h;
}

} // namespace assetlib
