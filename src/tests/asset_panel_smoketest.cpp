// Offscreen layout smoke-test for AssetManagerPanel. Reproduces (or rules out)
// the launch-time recursive size-negotiation stack overflow WITHOUT the D3D12 /
// DXGI renderer: it runs under the "offscreen" Qt platform, seeds a populated
// registry, constructs the real panel, and drives the resize events that make
// an IconMode QListView re-lay-out its items. If the layout recurses, this exe
// crashes (stack overflow); if it converges it prints OK and returns 0.
//
// Built as the `asset-panel-smoketest` target. Run with QT_QPA_PLATFORM=offscreen.

#include "../asset_library/asset_metadata.h"
#include "../asset_library/asset_registry.h"
#include "../asset_library/global_registry.h"
#include "../qt/AssetManagerPanel.h"

#include <QApplication>

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

using namespace assetlib;

int main(int argc, char **argv) {
  // Seed a populated registry in a throwaway temp root.
  static std::mt19937_64 rng(12345);
  std::filesystem::path root = std::filesystem::temp_directory_path() /
                               ("prender_paneltest_" + std::to_string(rng()));
  AssetRegistry &reg = InitGlobalRegistry(root);

  const char *names[] = {"Oak Tree",  "Pine",      "Granite Rock",
                         "Boulder",   "Grass",     "Fern Cluster Long Name",
                         "Brass",     "Concrete",  "Oak Bark",
                         "Cloud Bank","Cumulus",   "Meadow Scatter Preset"};
  const char *folders[] = {"Trees", "Trees/Conifers", "Rocks", "Materials",
                           "Textures"};
  for (const char *n : names) {
    AssetMetadata m;
    m.type = AssetType::Model;
    m.displayName = n;
    m.virtualPath = folders[rng() % 5];
    m.tags = {"demo"};
    reg.Add(std::move(m));
  }
  reg.Save();

  QApplication app(argc, argv);

  auto *panel = new AssetManagerPanel();
  panel->resize(800, 500);
  panel->show();
  app.processEvents();

  // Drive the resize sequence that triggers IconMode item reflow, including a
  // very narrow width where a vertical scrollbar appears (the classic
  // oscillation case).
  const int widths[] = {800, 400, 220, 120, 90, 600, 70, 1000};
  for (int w : widths) {
    panel->resize(w, 500);
    app.processEvents();
    app.processEvents();
  }

  std::printf("OK: panel laid out across %zu resizes without crashing\n",
              sizeof(widths) / sizeof(widths[0]));
  delete panel;
  return 0;
}
