// Standalone, headless unit tests for the asset_library core. Links only the
// Qt-free / renderer-free registry sources, so it runs without a GPU and serves
// as the primary correctness gate for Phase 1.
//
// Built as the `asset-registry-tests` CMake target. Run the produced exe; it
// prints a summary and returns the number of failed checks (0 == success).

#include "../asset_library/asset_paths.h"
#include "../asset_library/asset_registry.h"
#include "../asset_library/thumbnail_cache.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace assetlib;

namespace {
int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char *expr, const char *file, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::printf("  FAIL: %s  (%s:%d)\n", expr, file, line);
  }
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

// A fresh, unique temp directory for a test, removed when the helper dies.
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    static std::mt19937_64 rng(std::random_device{}());
    path = std::filesystem::temp_directory_path() /
           ("prender_assettest_" + std::to_string(rng()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

AssetMetadata MakeAsset(AssetType type, const std::string &name,
                        const std::string &folder) {
  AssetMetadata m;
  m.type = type;
  m.displayName = name;
  m.virtualPath = folder;
  return m;
}

// --- Tests ---------------------------------------------------------------

void TestAssetId() {
  std::printf("TestAssetId\n");
  AssetId a = AssetId::Generate();
  AssetId b = AssetId::Generate();
  CHECK(a.valid());
  CHECK(a != b); // generation should not collide

  std::string s = a.ToString();
  CHECK(s.size() == 32);
  AssetId parsed;
  CHECK(AssetId::FromString(s, parsed));
  CHECK(parsed == a);

  AssetId bad;
  CHECK(!AssetId::FromString("xyz", bad));
  CHECK(!AssetId::FromString("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", bad));
}

void TestCrudAndRoundTrip() {
  std::printf("TestCrudAndRoundTrip\n");
  TempDir tmp;
  AssetPaths paths(tmp.path);

  AssetId modelId, texId;
  {
    AssetRegistry reg(paths);
    CHECK(reg.Load()); // empty library
    CHECK(reg.AssetCount() == 0);

    AssetMetadata model = MakeAsset(AssetType::Model, "Oak", "Trees/Deciduous");
    model.tags = {"foliage", "tree"};
    model.attribution = "Acme Studios";
    AssetMetadata tex = MakeAsset(AssetType::Texture, "OakBark", "Textures");
    modelId = reg.Add(model);
    texId = reg.Add(tex);
    // Wire a dependency: model depends on texture.
    AssetMetadata withDep = *reg.Get(modelId);
    withDep.dependencies.push_back(texId);
    CHECK(reg.Update(withDep));
    CHECK(reg.Save());
  }

  // Reload into a fresh registry and verify everything round-tripped.
  {
    AssetRegistry reg(paths);
    CHECK(reg.Load());
    CHECK(reg.AssetCount() == 2);
    const AssetMetadata *model = reg.Get(modelId);
    CHECK(model != nullptr);
    if (model) {
      CHECK(model->displayName == "Oak");
      CHECK(model->type == AssetType::Model);
      CHECK(model->virtualPath == "Trees/Deciduous");
      CHECK(model->tags.size() == 2);
      CHECK(model->attribution == "Acme Studios");
      CHECK(model->dependencies.size() == 1);
      CHECK(model->dependencies.size() == 1 &&
            model->dependencies[0] == texId);
    }
    // Folder chain should have been registered (incl. intermediate).
    CHECK(reg.Folders().count("Trees") == 1);
    CHECK(reg.Folders().count("Trees/Deciduous") == 1);
    CHECK(reg.Folders().count("Textures") == 1);

    CHECK(reg.Remove(texId));
    CHECK(reg.AssetCount() == 1);
    CHECK(reg.Get(texId) == nullptr);
  }
}

void TestFolderRename() {
  std::printf("TestFolderRename\n");
  TempDir tmp;
  AssetRegistry reg(AssetPaths(tmp.path));
  reg.Load();

  AssetId a = reg.Add(MakeAsset(AssetType::Model, "A", "Trees/Oak"));
  AssetId b = reg.Add(MakeAsset(AssetType::Model, "B", "Trees"));
  AssetId c = reg.Add(MakeAsset(AssetType::Model, "C", "Rocks"));

  CHECK(reg.RenameFolder("Trees", "Plants"));
  CHECK(reg.Get(a)->virtualPath == "Plants/Oak");
  CHECK(reg.Get(b)->virtualPath == "Plants");
  CHECK(reg.Get(c)->virtualPath == "Rocks"); // untouched
  CHECK(reg.Folders().count("Plants") == 1);
  CHECK(reg.Folders().count("Plants/Oak") == 1);
  CHECK(reg.Folders().count("Trees") == 0);

  // Renaming a folder into its own subtree must fail.
  CHECK(!reg.RenameFolder("Plants", "Plants/Sub"));

  // DeleteFolder without removing assets re-parents them.
  CHECK(reg.DeleteFolder("Plants/Oak", /*removeContainedAssets=*/false));
  CHECK(reg.Get(a)->virtualPath == "Plants");
  CHECK(reg.Folders().count("Plants/Oak") == 0);

  // DeleteFolder with removal deletes contained assets.
  CHECK(reg.DeleteFolder("Plants", /*removeContainedAssets=*/true));
  CHECK(reg.Get(a) == nullptr);
  CHECK(reg.Get(b) == nullptr);
  CHECK(reg.Get(c) != nullptr); // Rocks untouched
}

void TestFavoritesPersistIndependently() {
  std::printf("TestFavoritesPersistIndependently\n");
  TempDir tmp;
  AssetPaths paths(tmp.path);
  AssetId id;
  {
    AssetRegistry reg(paths);
    reg.Load();
    id = reg.Add(MakeAsset(AssetType::Material, "Brass", ""));
    reg.SetFavorite(id, true);
    CHECK(reg.IsFavorite(id));
    CHECK(reg.Save());
    // Removing the asset must keep the favorite entry (displays as missing).
    reg.Remove(id);
    CHECK(reg.IsFavorite(id));
    CHECK(reg.Save());
  }
  {
    AssetRegistry reg(paths);
    reg.Load();
    // Favorite survived restart even though the asset is gone.
    CHECK(reg.IsFavorite(id));
    CHECK(reg.Favorites().size() == 1);
    CHECK(reg.Get(id) == nullptr);
  }
}

void TestSearch() {
  std::printf("TestSearch\n");
  TempDir tmp;
  AssetRegistry reg(AssetPaths(tmp.path));
  reg.Load();

  AssetId oak = reg.Add(MakeAsset(AssetType::Model, "Oak Tree", "Trees"));
  reg.AddTag(oak, "foliage");
  AssetId rock = reg.Add(MakeAsset(AssetType::Model, "Granite Rock", "Rocks"));
  AssetId grass = reg.Add(MakeAsset(AssetType::ScatterObject, "Meadow", "Scatter"));

  // Text match on name.
  AssetQuery q;
  q.text = "oak";
  CHECK(reg.SearchAssets(q).size() == 1);

  // Tag filter.
  AssetQuery qt;
  qt.tag = "foliage";
  CHECK(reg.SearchAssets(qt).size() == 1);

  // Type filter.
  AssetQuery qty;
  qty.type = AssetType::Model;
  CHECK(reg.SearchAssets(qty).size() == 2);

  // Folder filter (exact, non-recursive).
  AssetQuery qf;
  qf.folder = std::string("Rocks");
  CHECK(reg.SearchAssets(qf).size() == 1);

  // Favorites-only.
  reg.SetFavorite(grass, true);
  AssetQuery qfav;
  qfav.favoritesOnly = true;
  CHECK(reg.SearchAssets(qfav).size() == 1);

  // Empty query returns everything.
  CHECK(reg.SearchAssets(AssetQuery{}).size() == 3);
}

void TestMissingState() {
  std::printf("TestMissingState\n");
  TempDir tmp;
  AssetRegistry reg(AssetPaths(tmp.path));
  reg.Load();

  // Create a real source file and point an asset at it.
  std::filesystem::path src = tmp.path / "model.gltf";
  { std::ofstream(src) << "{}"; }

  AssetMetadata m = MakeAsset(AssetType::Model, "Sourced", "");
  m.sourcePath = src.string();
  AssetId id = reg.Add(m);

  reg.RefreshSourceStates();
  CHECK(reg.Get(id)->sourceState == SourceState::Available);
  CHECK(reg.MissingOrFailed().empty());

  // Delete the source; state should flip to Missing.
  std::filesystem::remove(src);
  reg.RefreshSourceStates();
  CHECK(reg.Get(id)->sourceState == SourceState::Missing);
  CHECK(reg.MissingOrFailed().size() == 1);
}

void TestChangeListener() {
  std::printf("TestChangeListener\n");
  TempDir tmp;
  AssetRegistry reg(AssetPaths(tmp.path));
  reg.Load();
  int hits = 0;
  size_t lid = reg.AddChangeListener([&hits]() { ++hits; });
  reg.Add(MakeAsset(AssetType::Model, "X", ""));
  CHECK(hits == 1);
  reg.RemoveChangeListener(lid);
  reg.Add(MakeAsset(AssetType::Model, "Y", ""));
  CHECK(hits == 1); // no longer notified
}

void TestThumbnailCache() {
  std::printf("TestThumbnailCache\n");
  TempDir tmp;
  AssetPaths paths(tmp.path);
  paths.EnsureLayout();
  ThumbnailCache cache(paths);
  AssetId id = AssetId::Generate();
  CHECK(!cache.Has(id));
  std::vector<uint8_t> bytes = {1, 2, 3, 4};
  CHECK(cache.Store(id, bytes));
  CHECK(cache.Has(id));
  cache.Remove(id);
  CHECK(!cache.Has(id));
}

} // namespace

int main() {
  TestAssetId();
  TestCrudAndRoundTrip();
  TestFolderRename();
  TestFavoritesPersistIndependently();
  TestSearch();
  TestMissingState();
  TestChangeListener();
  TestThumbnailCache();

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures;
}
