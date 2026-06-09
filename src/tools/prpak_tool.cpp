// Command-line .prpak validator. Usage: prpak-tool <pack.prpak>
// Opens the pack, runs a full integrity check, prints pack metadata and the
// asset list, and returns 0 if valid, non-zero otherwise. For release packaging
// and automated testing (notes/asset-menagement.md "Reliability and
// Diagnostics").

#include "../asset_library/prpak_reader.h"

#include <cstdio>
#include <string>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: prpak-tool <pack.prpak>\n");
    return 2;
  }
  assetlib::PrPakReader reader;
  std::string err;
  if (!reader.Open(argv[1], &err)) {
    std::printf("FAILED to open: %s\n", err.c_str());
    return 1;
  }

  const auto &meta = reader.meta();
  std::printf("Pack: %s\n", meta.name.empty() ? "(unnamed)" : meta.name.c_str());
  if (!meta.attribution.empty())
    std::printf("  Attribution: %s\n", meta.attribution.c_str());
  if (!meta.license.empty())
    std::printf("  License: %s\n", meta.license.c_str());
  std::printf("  Assets: %zu\n", reader.assets().size());
  for (const auto &a : reader.assets()) {
    std::printf("   - [%s] %s  (%s)\n",
                assetlib::AssetTypeDisplayName(a.meta.type),
                a.meta.displayName.c_str(), a.meta.id.ToString().c_str());
  }

  std::string report;
  const bool ok = reader.Validate(report);
  std::printf("\n%s", report.c_str());
  return ok ? 0 : 1;
}
