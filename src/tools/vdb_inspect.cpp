// Headless .vdb inspector: lists the grids in a file and runs the actual
// importer, printing the cooked-volume stats. Used to debug VDB import without
// the GPU. Usage: vdb-inspect <file.vdb>

#include "../asset_library/cooked_payload.h"
#include "../asset_library/vdb_import.h"

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
