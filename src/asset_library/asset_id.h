#pragma once
#include <cstdint>
#include <functional>
#include <random>
#include <string>

// Stable 128-bit identity for a registered asset. Independent of display name
// and folder path: moving or renaming an asset must not change its AssetId
// (see notes/asset-menagement.md "Asset Identity").
namespace assetlib {

struct AssetId {
  uint64_t hi = 0;
  uint64_t lo = 0;

  bool valid() const { return hi != 0 || lo != 0; }

  bool operator==(const AssetId &other) const {
    return hi == other.hi && lo == other.lo;
  }
  bool operator!=(const AssetId &other) const { return !(*this == other); }
  bool operator<(const AssetId &other) const {
    return hi != other.hi ? hi < other.hi : lo < other.lo;
  }

  // Generate a fresh random (v4-style) identity. Not cryptographically
  // strong; collision probability across a user library is negligible.
  static AssetId Generate() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    AssetId id;
    do {
      id.hi = rng();
      id.lo = rng();
    } while (!id.valid());
    return id;
  }

  // 32 lowercase hex characters (hi then lo), no separators.
  std::string ToString() const {
    static const char *kHex = "0123456789abcdef";
    std::string out(32, '0');
    uint64_t parts[2] = {hi, lo};
    for (int p = 0; p < 2; ++p) {
      uint64_t v = parts[p];
      for (int i = 0; i < 16; ++i) {
        out[p * 16 + (15 - i)] = kHex[v & 0xF];
        v >>= 4;
      }
    }
    return out;
  }

  // Parse a 32-hex-character string. Returns false (and leaves out unchanged)
  // on any malformed input.
  static bool FromString(const std::string &s, AssetId &out) {
    if (s.size() != 32)
      return false;
    uint64_t parts[2] = {0, 0};
    for (int i = 0; i < 32; ++i) {
      char c = s[i];
      uint64_t nibble;
      if (c >= '0' && c <= '9')
        nibble = static_cast<uint64_t>(c - '0');
      else if (c >= 'a' && c <= 'f')
        nibble = static_cast<uint64_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        nibble = static_cast<uint64_t>(c - 'A' + 10);
      else
        return false;
      parts[i / 16] = (parts[i / 16] << 4) | nibble;
    }
    out.hi = parts[0];
    out.lo = parts[1];
    return true;
  }
};

} // namespace assetlib

namespace std {
template <> struct hash<assetlib::AssetId> {
  size_t operator()(const assetlib::AssetId &id) const noexcept {
    // Mix the two halves; std::hash<uint64_t> is identity on many stdlibs.
    size_t h = std::hash<uint64_t>{}(id.hi);
    h ^= std::hash<uint64_t>{}(id.lo) + 0x9e3779b97f4a7c15ULL + (h << 6) +
         (h >> 2);
    return h;
  }
};
} // namespace std
