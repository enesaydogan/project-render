// Standalone, headless unit tests for the asset_library core. Links only the
// Qt-free / renderer-free registry sources, so it runs without a GPU and serves
// as the primary correctness gate for Phase 1.
//
// Built as the `asset-registry-tests` CMake target. Run the produced exe; it
// prints a summary and returns the number of failed checks (0 == success).

#include "../asset_library/asset_paths.h"
#include "../asset_library/asset_registry.h"
#include "../asset_library/cooked_payload.h"
#include "../asset_library/prpak_reader.h"
#include "../asset_library/prpak_writer.h"
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

void TestCookedModelRoundTrip() {
  std::printf("TestCookedModelRoundTrip\n");
  CookedModel model;
  CookedMesh a;
  a.materialIndex = 2;
  a.materialSlot = 1;
  a.vertexCount = 3;
  a.indexCount = 3;
  a.minBound[0] = -1.0f; a.maxBound[2] = 5.5f;
  a.vertexBytes = std::vector<uint8_t>(3 * 48, 0xAB); // 48 = sizeof Asset::Vertex
  a.indexBytes = {0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
  model.meshes.push_back(a);
  CookedMesh b;
  b.vertexCount = 0; // empty mesh edge case
  model.meshes.push_back(b);

  std::vector<uint8_t> blob;
  CHECK(SerializeCookedModel(model, blob));
  CHECK(blob.size() > 16); // has header

  CookedModel back;
  CHECK(DeserializeCookedModel(blob.data(), blob.size(), back));
  CHECK(back.meshes.size() == 2);
  if (back.meshes.size() == 2) {
    CHECK(back.meshes[0].materialIndex == 2);
    CHECK(back.meshes[0].materialSlot == 1);
    CHECK(back.meshes[0].vertexCount == 3);
    CHECK(back.meshes[0].minBound[0] == -1.0f);
    CHECK(back.meshes[0].maxBound[2] == 5.5f);
    CHECK(back.meshes[0].vertexBytes.size() == 3 * 48);
    CHECK(back.meshes[0].vertexBytes == a.vertexBytes);
    CHECK(back.meshes[0].indexBytes == a.indexBytes);
    CHECK(back.meshes[1].vertexCount == 0);
  }

  // Corruption / truncation must be rejected, not crash.
  CookedModel bad;
  CHECK(!DeserializeCookedModel(blob.data(), 8, bad));
  std::vector<uint8_t> garbage = {1, 2, 3, 4, 5, 6, 7, 8,
                                  9, 10, 11, 12, 13, 14, 15, 16};
  CHECK(!DeserializeCookedModel(garbage.data(), garbage.size(), bad));
}

void TestCookedTextureRoundTrip() {
  std::printf("TestCookedTextureRoundTrip\n");
  CookedTexture tex;
  tex.width = 256;
  tex.height = 128;
  tex.cpuFormat = 87; // some DXGI_FORMAT value
  tex.cpuMipLevels = 9;
  tex.usageSemantic = 3;
  tex.data = std::vector<uint8_t>(4096, 0x7E);

  std::vector<uint8_t> blob;
  CHECK(SerializeCookedTexture(tex, blob));
  CookedTexture back;
  CHECK(DeserializeCookedTexture(blob.data(), blob.size(), back));
  CHECK(back.width == 256);
  CHECK(back.height == 128);
  CHECK(back.cpuFormat == 87);
  CHECK(back.cpuMipLevels == 9);
  CHECK(back.usageSemantic == 3);
  CHECK(back.data == tex.data);
}

void TestCookedFileIoAndHash() {
  std::printf("TestCookedFileIoAndHash\n");
  TempDir tmp;
  AssetPaths paths(tmp.path);
  paths.EnsureLayout();
  AssetId id = AssetId::Generate();

  CookedModel model;
  CookedMesh m;
  m.vertexCount = 1;
  m.vertexBytes = std::vector<uint8_t>(48, 0x11);
  model.meshes.push_back(m);
  std::vector<uint8_t> blob;
  CHECK(SerializeCookedModel(model, blob));

  std::filesystem::path file = paths.cookedMeshPath(id);
  CHECK(WriteCookedFile(file, blob));
  CHECK(std::filesystem::exists(file));

  std::vector<uint8_t> readBack;
  CHECK(ReadCookedFile(file, readBack));
  CHECK(readBack == blob);

  CookedModel decoded;
  CHECK(DeserializeCookedModel(readBack.data(), readBack.size(), decoded));
  CHECK(decoded.meshes.size() == 1);

  // Hashing: deterministic, content-sensitive.
  uint64_t h1 = HashBytes(blob.data(), blob.size());
  uint64_t h2 = HashBytes(blob.data(), blob.size());
  CHECK(h1 == h2);
  CHECK(h1 != 0);
  std::vector<uint8_t> mutated = blob;
  mutated.back() ^= 0xFF;
  CHECK(HashBytes(mutated.data(), mutated.size()) != h1);
  CHECK(HashFile(file) != 0);
}

void TestCookedVolumeRoundTrip() {
  std::printf("TestCookedVolumeRoundTrip\n");
  CookedVolume vol;
  vol.dim[0] = 64;
  vol.dim[1] = 32;
  vol.dim[2] = 48;
  vol.brickSize = 8;
  vol.boundsMin[0] = -1.0f;
  vol.boundsMax[1] = 2.5f;
  vol.activeVoxels = 12345;
  CookedVolumeBrick b0;
  b0.bx = 1; b0.by = 2; b0.bz = 3;
  b0.minVal = 0.1f; b0.maxVal = 0.9f;
  b0.data = std::vector<uint8_t>(8 * 8 * 8, 0x42);
  vol.bricks.push_back(b0);
  CookedVolumeBrick b1;
  b1.bx = 4; b1.by = 0; b1.bz = 1;
  b1.data = std::vector<uint8_t>(8 * 8 * 8, 0xC0);
  vol.bricks.push_back(b1);
  vol.temperatureMin = 0.25f;
  vol.temperatureMax = 3.5f;
  CookedVolumeBrick temperature = b0;
  temperature.minVal = 0.25f;
  temperature.maxVal = 3.5f;
  temperature.data.assign(8 * 8 * 8, 0xA0);
  vol.temperatureBricks.push_back(temperature);

  std::vector<uint8_t> blob;
  CHECK(SerializeCookedVolume(vol, blob));
  CookedVolume back;
  CHECK(DeserializeCookedVolume(blob.data(), blob.size(), back));
  CHECK(back.dim[0] == 64 && back.dim[1] == 32 && back.dim[2] == 48);
  CHECK(back.brickSize == 8);
  CHECK(back.boundsMin[0] == -1.0f);
  CHECK(back.boundsMax[1] == 2.5f);
  CHECK(back.activeVoxels == 12345);
  CHECK(back.bricks.size() == 2);
  CHECK(back.temperatureMin == 0.25f);
  CHECK(back.temperatureMax == 3.5f);
  CHECK(back.temperatureBricks.size() == 1);
  if (back.temperatureBricks.size() == 1) {
    CHECK(back.temperatureBricks[0].data == temperature.data);
  }
  if (back.bricks.size() == 2) {
    CHECK(back.bricks[0].bx == 1 && back.bricks[0].bz == 3);
    CHECK(back.bricks[0].maxVal == 0.9f);
    CHECK(back.bricks[0].data == b0.data);
    CHECK(back.bricks[1].data.size() == 512);
  }
  CookedVolume bad;
  CHECK(!DeserializeCookedVolume(blob.data(), 8, bad));
}

void TestPrPakRoundTrip() {
  std::printf("TestPrPakRoundTrip\n");
  TempDir tmp;
  const std::filesystem::path packPath = tmp.path / "test.prpak";

  // Two assets; the second shares an identical mesh payload with the first to
  // exercise content-hash dedup.
  std::vector<uint8_t> meshBytes(2000, 0x5A);
  std::vector<uint8_t> texBytes(1500, 0x33);

  PackAssetInput a;
  a.meta.id = AssetId::Generate();
  a.meta.type = AssetType::Model;
  a.meta.displayName = "Oak";
  a.meta.tags = {"foliage"};
  a.meta.attribution = "Acme";
  a.meshPayload = meshBytes;

  PackAssetInput b;
  b.meta.id = AssetId::Generate();
  b.meta.type = AssetType::Texture;
  b.meta.displayName = "Bark";
  b.texturePayload = texBytes;
  b.meshPayload = meshBytes; // identical to a's mesh -> dedup

  PackMeta pm;
  pm.name = "TestPack";
  pm.attribution = "Acme";

  std::string err;
  CHECK(WritePack(packPath, pm, {a, b}, &err));
  CHECK(std::filesystem::exists(packPath));

  PrPakReader reader;
  CHECK(reader.Open(packPath, &err));
  CHECK(reader.meta().name == "TestPack");
  CHECK(reader.assets().size() == 2);
  CHECK(reader.has(a.meta.id));
  CHECK(reader.hasPayload(a.meta.id, PayloadKind::Mesh));
  CHECK(!reader.hasPayload(a.meta.id, PayloadKind::Texture));
  CHECK(reader.hasPayload(b.meta.id, PayloadKind::Texture));

  // Random-access payload read returns the exact bytes.
  std::vector<uint8_t> got;
  CHECK(reader.ReadPayload(a.meta.id, PayloadKind::Mesh, got));
  CHECK(got == meshBytes);
  CHECK(reader.ReadPayload(b.meta.id, PayloadKind::Texture, got));
  CHECK(got == texBytes);

  // Metadata round-tripped.
  const PackedAsset *pa = nullptr;
  for (const auto &x : reader.assets())
    if (x.meta.id == a.meta.id)
      pa = &x;
  CHECK(pa != nullptr);
  if (pa) {
    CHECK(pa->meta.displayName == "Oak");
    CHECK(pa->meta.tags.size() == 1);
    CHECK(pa->meta.attribution == "Acme");
  }

  std::string report;
  CHECK(reader.Validate(report));
}

void TestPrPakDedup() {
  std::printf("TestPrPakDedup\n");
  TempDir tmp;
  const std::filesystem::path packPath = tmp.path / "dedup.prpak";
  std::vector<uint8_t> shared(10000, 0x77);
  PackAssetInput a, b;
  a.meta.id = AssetId::Generate();
  a.meta.type = AssetType::Model;
  a.meshPayload = shared;
  b.meta.id = AssetId::Generate();
  b.meta.type = AssetType::Model;
  b.meshPayload = shared; // identical
  CHECK(WritePack(packPath, PackMeta{}, {a, b}, nullptr));
  // Deduped: one 10KB chunk stored, not two. File well under 2x payload.
  const auto sz = std::filesystem::file_size(packPath);
  CHECK(sz < 15000);
}

void TestPrPakCorruption() {
  std::printf("TestPrPakCorruption\n");
  TempDir tmp;
  const std::filesystem::path packPath = tmp.path / "corrupt.prpak";
  PackAssetInput a;
  a.meta.id = AssetId::Generate();
  a.meta.type = AssetType::Model;
  a.meshPayload = std::vector<uint8_t>(500, 0x10);
  CHECK(WritePack(packPath, PackMeta{}, {a}, nullptr));

  // Flip a byte inside the payload region (after the 8-byte magic).
  {
    std::fstream f(packPath, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(20);
    char c = 0x00;
    f.read(&c, 1);
    f.seekp(20);
    c ^= 0xFF;
    f.write(&c, 1);
  }
  PrPakReader reader;
  // Header/TOC still parse (corruption is in a payload chunk), but reading the
  // payload must fail its checksum, and Validate must report invalid.
  if (reader.Open(packPath, nullptr)) {
    std::vector<uint8_t> got;
    CHECK(!reader.ReadPayload(a.meta.id, PayloadKind::Mesh, got));
    std::string report;
    CHECK(!reader.Validate(report));
  }

  // A garbage file must not open.
  const std::filesystem::path junk = tmp.path / "junk.prpak";
  { std::ofstream(junk, std::ios::binary) << "not a pack at all............."; }
  PrPakReader r2;
  CHECK(!r2.Open(junk, nullptr));
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
  TestCookedModelRoundTrip();
  TestCookedTextureRoundTrip();
  TestCookedFileIoAndHash();
  TestCookedVolumeRoundTrip();
  TestPrPakRoundTrip();
  TestPrPakDedup();
  TestPrPakCorruption();

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures;
}
