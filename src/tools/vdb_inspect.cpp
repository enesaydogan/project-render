// Headless .vdb inspector: lists the grids in a file and runs the actual
// importer, printing the cooked-volume stats. Used to debug VDB import without
// the GPU. Usage: vdb-inspect <file.vdb>

#include "../asset_library/cooked_payload.h"
#include "../asset_library/vdb_import.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static void PrintVolumeStats(const assetlib::CookedVolume &vol) {
  std::printf("dim = %u x %u x %u\n", vol.dim[0], vol.dim[1], vol.dim[2]);
  std::printf("activeVoxels = %llu\n",
              static_cast<unsigned long long>(vol.activeVoxels));
  std::printf("density bricks = %zu\n", vol.bricks.size());
  std::printf("bounds = (%.3f, %.3f, %.3f) .. (%.3f, %.3f, %.3f)\n",
              vol.boundsMin[0], vol.boundsMin[1], vol.boundsMin[2],
              vol.boundsMax[0], vol.boundsMax[1], vol.boundsMax[2]);
  std::printf("temperature bricks = %zu, range = %.4f .. %.4f\n",
              vol.temperatureBricks.size(), vol.temperatureMin,
              vol.temperatureMax);
  if (!vol.temperatureBricks.empty() &&
      vol.temperatureMax > vol.temperatureMin) {
    constexpr float kHeatLow = 0.02f;
    constexpr float kHeatHigh = 0.98f;
    constexpr float kHeatGamma = 1.5f;
    uint64_t hotVoxelCount = 0;
    double integratedFireWeight = 0.0;
    const float invTemperatureRange =
        1.0f / (vol.temperatureMax - vol.temperatureMin);
    for (const auto &brick : vol.temperatureBricks) {
      const float brickRange = brick.maxVal - brick.minVal;
      for (uint8_t q : brick.data) {
        const float temperature =
            brick.minVal + (static_cast<float>(q) / 255.0f) * brickRange;
        const float rawHeat = std::clamp(
            (temperature - vol.temperatureMin) * invTemperatureRange, 0.0f,
            1.0f);
        const float fireMask = std::clamp(
            (rawHeat - kHeatLow) / (kHeatHigh - kHeatLow), 0.0f, 1.0f);
        if (fireMask <= 1.0e-4f)
          continue;
        ++hotVoxelCount;
        integratedFireWeight += std::pow(fireMask, kHeatGamma);
      }
    }
    const uint64_t totalVoxelCount =
        static_cast<uint64_t>(vol.dim[0]) * vol.dim[1] * vol.dim[2];
    std::printf(
        "default fire mask: %llu hot voxels (%.4f%%), integrated weight %.3f, "
        "mean %.8f\n",
        static_cast<unsigned long long>(hotVoxelCount),
        totalVoxelCount > 0
            ? 100.0 * static_cast<double>(hotVoxelCount) /
                  static_cast<double>(totalVoxelCount)
            : 0.0,
        integratedFireWeight,
        totalVoxelCount > 0
            ? integratedFireWeight / static_cast<double>(totalVoxelCount)
            : 0.0);
  }
  // Peek a few non-zero temperature samples to confirm real data survived.
  int shown = 0;
  for (const auto &b : vol.temperatureBricks) {
    for (uint8_t q : b.data) {
      if (q > 0) {
        const float range = b.maxVal - b.minVal;
        std::printf("  temp sample: q=%u -> %.3f\n", q,
                    b.minVal + (q / 255.0f) * range);
        if (++shown >= 5)
          return;
        break;
      }
    }
  }
  if (shown == 0)
    std::printf("  (no non-zero temperature voxels found!)\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: vdb-inspect <file.vdb | file.prvol>\n");
    return 2;
  }
  const std::string arg = argv[1];
  // Inspect an already-cooked volume payload (what the renderer loads).
  if (arg.size() > 6 && arg.substr(arg.size() - 6) == ".prvol") {
    std::ifstream in(arg, std::ios::binary | std::ios::ate);
    if (!in) {
      std::printf("cannot open %s\n", arg.c_str());
      return 1;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    std::printf("=== cooked .prvol (%zu bytes) ===\n", bytes.size());
    assetlib::CookedVolume vol;
    if (!assetlib::DeserializeCookedVolume(bytes.data(), bytes.size(), vol)) {
      std::printf("DESERIALIZE FAILED\n");
      return 1;
    }
    PrintVolumeStats(vol);
    return 0;
  }

  std::printf("=== grids in %s ===\n", argv[1]);
  std::vector<VdbImport::GridInfo> gridInfos;
  std::string listError;
  if (!VdbImport::ListGrids(argv[1], gridInfos, &listError)) {
    std::printf("GRID LIST FAILED: %s\n", listError.c_str());
    return 1;
  }
  for (const auto &grid : gridInfos) {
    std::printf("  selectable '%s' value=%s scalar=%s vector=%s\n",
                grid.name.c_str(), grid.valueType.c_str(),
                grid.scalar ? "yes" : "no", grid.vector ? "yes" : "no");
  }
  std::printf("%s", VdbImport::DescribeGrids(argv[1]).c_str());

  std::printf("\n=== import result ===\n");
  assetlib::CookedVolume vol;
  std::string err;
  const bool ok = VdbImport::ImportVdbToVolume(argv[1], vol, &err);
  if (!ok) {
    std::printf("IMPORT FAILED: %s\n", err.c_str());
    return 1;
  }
  PrintVolumeStats(vol);
  return 0;
}
