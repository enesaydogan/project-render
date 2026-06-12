#include "import_hook.h"

#include "asset_cooker.h"
#include "asset_paths.h"
#include "asset_registry.h"
#include "cook_jobs.h"
#include "cooked_payload.h"
#include "global_registry.h"
#include "vdb_import.h"

#include "../material/material_io.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

namespace assetlib {
namespace {

// Find an existing Model asset for this source whose cooked payload is current
// and whose source has not changed (same timestamp). Returns invalid if none.
AssetId FindUpToDateModel(const AssetRegistry &registry, const AssetPaths &paths,
                          const std::string &sourcePath) {
  if (sourcePath.empty())
    return {};
  std::error_code ec;
  auto ts = std::filesystem::last_write_time(NativeSourcePath(sourcePath), ec);
  int64_t timestamp = ec ? 0 : static_cast<int64_t>(ts.time_since_epoch().count());
  for (const AssetId &id : registry.AllAssets()) {
    const AssetMetadata *m = registry.Get(id);
    if (!m || m->type != AssetType::Model || m->sourcePath != sourcePath)
      continue;
    if (timestamp != 0 && m->sourceTimestamp == timestamp &&
        HasCurrentCookedModel(registry, paths, id))
      return id;
  }
  return {};
}

} // namespace

AssetId RegisterImportedModel(const std::string &displayName,
                              const std::string &sourcePath,
                              const std::vector<Asset::GpuMesh> &meshes,
                              const std::vector<Asset::Material> &materials,
                              const std::vector<Asset::Texture> &textures) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry || meshes.empty())
    return {};
  const AssetPaths &paths = registry->paths();

  AssetId existing = FindUpToDateModel(*registry, paths, sourcePath);
  if (existing.valid())
    return existing; // already cooked & unchanged — skip recook

  ImportedAssetSet set = RegisterAndCookImport(
      *registry, paths, displayName, sourcePath, meshes, materials, textures);
  registry->TouchRecent(set.modelId);
  registry->Save(); // persist catalog now; cookState flips to Current via Pump
  return set.modelId;
}

namespace {
using json = nlohmann::json;

std::string LowerExt(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  if (!ext.empty() && ext[0] == '.')
    ext.erase(0, 1);
  for (char &c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

int64_t SourceTimestamp(const std::filesystem::path &path) {
  std::error_code ec;
  const auto timestamp = std::filesystem::last_write_time(path, ec);
  return ec ? 0
            : static_cast<int64_t>(timestamp.time_since_epoch().count());
}

struct VdbSequence {
  std::string directory;
  std::string prefix;
  std::string suffix;
  uint32_t firstFrame = 0;
  uint32_t frameCount = 0;
  uint32_t padding = 0;
};

std::filesystem::path SequenceSourcePath(const VdbSequence &sequence,
                                         uint32_t frameIndex) {
  std::ostringstream number;
  number << std::setw(static_cast<int>(sequence.padding))
         << std::setfill('0') << (sequence.firstFrame + frameIndex);
  // directory comes from UTF-8 registry JSON; convert before path use.
  return NativeSourcePath(sequence.directory) /
         (sequence.prefix + number.str() + sequence.suffix);
}

bool DetectVdbSequence(const std::string &path, VdbSequence &out) {
  const std::filesystem::path source(path);
  const std::string stem = source.stem().string();
  std::smatch match;
  if (!std::regex_match(stem, match, std::regex(R"(^(.*?)(\d+)$)")))
    return false;

  const std::string prefix = match[1].str();
  const std::string digits = match[2].str();
  const std::string suffix = source.extension().string();
  const std::string escapedPrefix =
      std::regex_replace(prefix, std::regex(R"([.^$|()\\[\]{}*+?])"),
                         R"(\$&)");
  const std::string escapedSuffix =
      std::regex_replace(suffix, std::regex(R"([.^$|()\\[\]{}*+?])"),
                         R"(\$&)");
  const std::regex framePattern(
      "^" + escapedPrefix + "(\\d{" + std::to_string(digits.size()) + "})" +
          escapedSuffix + "$",
      std::regex::icase);
  std::vector<uint32_t> frames;
  std::error_code ec;
  for (const auto &entry :
       std::filesystem::directory_iterator(source.parent_path(), ec)) {
    if (ec || !entry.is_regular_file())
      continue;
    const std::string candidate = entry.path().filename().string();
    std::smatch candidateMatch;
    if (!std::regex_match(candidate, candidateMatch, framePattern))
      continue;
    frames.push_back(
        static_cast<uint32_t>(std::stoul(candidateMatch[1].str())));
  }
  if (frames.size() < 2)
    return false;
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
  for (size_t i = 1; i < frames.size(); ++i)
    if (frames[i] != frames.front() + static_cast<uint32_t>(i))
      return false;

  out.directory = source.parent_path().string();
  out.prefix = prefix;
  out.suffix = suffix;
  out.firstFrame = frames.front();
  out.frameCount = static_cast<uint32_t>(frames.size());
  out.padding = static_cast<uint32_t>(digits.size());
  return true;
}

std::string NormalizedSequenceDirectory(const std::string &path) {
  std::error_code ec;
  std::filesystem::path normalized =
      std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
  if (ec) {
    ec.clear();
    normalized = std::filesystem::absolute(std::filesystem::path(path), ec);
  }
  std::string value =
      (ec ? std::filesystem::path(path) : normalized).generic_string();
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

AssetId FindImportedSequence(const AssetRegistry &registry,
                             const VdbSequence &sequence) {
  const std::string directory =
      NormalizedSequenceDirectory(sequence.directory);
  for (const AssetId &id : registry.AllAssets()) {
    const AssetMetadata *metadata = registry.Get(id);
    if (!metadata || metadata->type != AssetType::Volume)
      continue;
    try {
      const json settings = json::parse(metadata->importSettingsJson);
      if (!settings.contains("sequence") ||
          !settings["sequence"].is_object()) {
        continue;
      }
      const json &candidate = settings["sequence"];
      if (NormalizedSequenceDirectory(
              candidate.value("directory", std::string())) == directory &&
          candidate.value("prefix", std::string()) == sequence.prefix &&
          candidate.value("suffix", std::string()) == sequence.suffix) {
        return id;
      }
    } catch (...) {
    }
  }
  return {};
}

bool ParseVdbSequenceSettings(const AssetMetadata &metadata,
                              VdbSequence &sequence,
                              VdbImport::ImportOptions &options) {
  try {
    const json settings = json::parse(metadata.importSettingsJson);
    if (!settings.contains("sequence") ||
        !settings["sequence"].is_object()) {
      return false;
    }
    const json &stored = settings["sequence"];
    sequence.directory = stored.value("directory", std::string());
    sequence.prefix = stored.value("prefix", std::string());
    sequence.suffix = stored.value("suffix", std::string(".vdb"));
    sequence.firstFrame = stored.value("firstFrame", 0u);
    sequence.frameCount = stored.value("frameCount", 0u);
    sequence.padding = stored.value("padding", 0u);
    options.densityGrid = settings.value("densityGrid", std::string());
    options.temperatureGrid =
        settings.value("temperatureGrid", std::string());
    return !sequence.directory.empty() && sequence.frameCount > 1;
  } catch (...) {
    return false;
  }
}

std::filesystem::path SequenceCookedPath(const AssetPaths &paths,
                                         const AssetId &id,
                                         uint32_t frameIndex) {
  return frameIndex == 0 ? paths.cookedVolumePath(id)
                         : paths.cookedVolumeFramePath(id, frameIndex);
}

bool SequenceCookComplete(const AssetPaths &paths, const AssetId &id,
                          const VdbSequence &sequence) {
  for (uint32_t frame = 0; frame < sequence.frameCount; ++frame) {
    std::error_code ec;
    if (!std::filesystem::exists(
            SequenceCookedPath(paths, id, frame), ec) ||
        ec) {
      return false;
    }
  }
  return true;
}

bool QueueVdbSequenceCook(AssetRegistry &registry, const AssetId &id,
                          const VdbSequence &sequence,
                          const VdbImport::ImportOptions &options,
                          bool recookAll) {
  CookService &cook = CookService::Get();
  if (cook.IsPending(id))
    return false;

  // Pre-pass: union active-voxel bboxes across the full sequence so every
  // cooked frame samples the same world-space region. Without this the
  // per-frame bbox shifts each frame and the volume "jumps" at playback.
  VdbImport::ImportOptions sequenceOptions = options;
  {
    int32_t unionMin[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t unionMax[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    bool anyValid = false;
    for (uint32_t frame = 0; frame < sequence.frameCount; ++frame) {
      const std::filesystem::path sourcePath =
          SequenceSourcePath(sequence, frame);
      int32_t mn[3], mx[3];
      std::string error;
      if (!VdbImport::QueryActiveBounds(sourcePath.string(), options, mn, mx,
                                        &error)) {
        // Skip empty / unreadable frames; they will fail at cook time too.
        continue;
      }
      anyValid = true;
      for (int i = 0; i < 3; ++i) {
        if (mn[i] < unionMin[i]) unionMin[i] = mn[i];
        if (mx[i] > unionMax[i]) unionMax[i] = mx[i];
      }
    }
    if (anyValid) {
      sequenceOptions.overrideBoundsValid = true;
      for (int i = 0; i < 3; ++i) {
        sequenceOptions.overrideBoundsMin[i] = unionMin[i];
        sequenceOptions.overrideBoundsMax[i] = unionMax[i];
      }
    }
  }

  std::vector<CookService::Output> outputs;
  outputs.reserve(sequence.frameCount);
  for (uint32_t frame = 0; frame < sequence.frameCount; ++frame) {
    const std::filesystem::path cookedPath =
        SequenceCookedPath(registry.paths(), id, frame);
    std::error_code ec;
    if (!recookAll && std::filesystem::exists(cookedPath, ec) && !ec)
      continue;
    const std::filesystem::path sourcePath =
        SequenceSourcePath(sequence, frame);
    outputs.push_back(
        {cookedPath, [sourcePath, sequenceOptions]() {
           CookedVolume cooked;
           std::string error;
           if (!VdbImport::ImportVdbToVolume(sourcePath.string(),
                                             sequenceOptions, cooked,
                                             &error)) {
             fprintf(stderr, "VDB sequence cook failed for '%s': %s\n",
                     sourcePath.string().c_str(), error.c_str());
             return std::vector<uint8_t>{};
           }
           std::vector<uint8_t> blob;
           if (!SerializeCookedVolume(cooked, blob))
             blob.clear();
           return blob;
         }});
  }

  const AssetMetadata *metadata = registry.Get(id);
  if (!metadata)
    return false;
  AssetMetadata updated = *metadata;
  if (outputs.empty()) {
    updated.cookState = CookState::Current;
    registry.Update(updated);
    registry.Save();
    return true;
  }
  updated.cookState = CookState::Stale;
  registry.Update(updated);
  registry.Save();
  cook.EnqueueBatch(id, std::move(outputs));
  return false;
}
} // namespace

AssetId ImportFileToLibrary(const std::string &path) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry)
    return {};
  const std::string ext = LowerExt(path);
  const std::string name = std::filesystem::path(path).stem().string();

  const bool isModel = ext == "gltf" || ext == "glb" || ext == "obj" ||
                       ext == "stl" || ext == "fbx";
  if (isModel) {
    // Decode without GPU upload; cooking reads the CPU geometry directly.
    const bool prevDefer = Asset::GetDeferGpuUpload();
    Asset::SetDeferGpuUpload(true);
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;
    const bool ok = Asset::LoadModel(path, meshes, &materials, &textures);
    Asset::SetDeferGpuUpload(prevDefer);
    if (!ok || meshes.empty())
      return {};
    return RegisterImportedModel(name, path, meshes, materials, textures);
  }

  if (ext == "vdb") {
    return ImportVdbFileToLibrary(path, {});
  }

  const bool isImage = ext == "png" || ext == "jpg" || ext == "jpeg" ||
                       ext == "tga" || ext == "dds" || ext == "exr" ||
                       ext == "hdr" || ext == "bmp";
  if (isImage) {
    const bool hdr = ext == "exr" || ext == "hdr";
    Asset::Texture tex = Asset::LoadTextureFromFile(path, hdr);
    if (tex.cpuData.empty())
      return {};
    AssetId id =
        RegisterAndCookTexture(*registry, registry->paths(), name, path, tex);
    registry->TouchRecent(id);
    registry->Save();
    return id;
  }

  return {}; // unsupported type for cooking
}

AssetId ImportVdbFileToLibrary(const std::string &path,
                               const VdbImport::ImportOptions &options) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry)
    return {};
  VdbSequence sequence;
  const bool isSequence = DetectVdbSequence(path, sequence);
  if (isSequence) {
    const std::string name =
        sequence.prefix.empty() ? std::filesystem::path(path).stem().string()
                                : sequence.prefix;
    const std::filesystem::path firstSource =
        SequenceSourcePath(sequence, 0);
    const AssetId existing = FindImportedSequence(*registry, sequence);
    if (existing.valid()) {
      if (const AssetMetadata *metadata = registry->Get(existing)) {
        AssetMetadata updated = *metadata;
        json settings = json::object();
        try {
          settings = json::parse(updated.importSettingsJson);
        } catch (...) {
        }
        const bool channelChanged =
            settings.value("densityGrid", std::string()) !=
                options.densityGrid ||
            settings.value("temperatureGrid", std::string()) !=
                options.temperatureGrid;
        settings["densityGrid"] = options.densityGrid;
        settings["temperatureGrid"] = options.temperatureGrid;
        if (settings.contains("sequence") &&
            settings["sequence"].is_object()) {
          json &storedSequence = settings["sequence"];
          if (!storedSequence.contains("fps") ||
              storedSequence.value("fps", 30.0f) == 30.0f) {
            storedSequence["fps"] = 24.0f;
          }
        }
        updated.displayName = name;
        updated.sourcePath = firstSource.string();
        updated.sourceContentHash = HashFile(firstSource);
        updated.sourceTimestamp = SourceTimestamp(firstSource);
        updated.cookerVersion = kCookerVersionVolume;
        updated.importSettingsJson = settings.dump();
        registry->Update(updated);
        QueueVdbSequenceCook(*registry, existing, sequence, options,
                             channelChanged);
      }
      registry->TouchRecent(existing);
      registry->Save();
      return existing;
    }

    AssetMetadata metadata;
    metadata.type = AssetType::Volume;
    metadata.displayName = name;
    metadata.virtualPath = "Imported/Volumes";
    metadata.sourcePath = firstSource.string();
    metadata.sourceContentHash = HashFile(firstSource);
    metadata.sourceTimestamp = SourceTimestamp(firstSource);
    metadata.cookerVersion = kCookerVersionVolume;
    metadata.cookState = CookState::Stale;
    json settings;
    settings["densityGrid"] = options.densityGrid;
    settings["temperatureGrid"] = options.temperatureGrid;
    settings["sequence"] = {
        {"directory", sequence.directory},
        {"prefix", sequence.prefix},
        {"suffix", sequence.suffix},
        {"firstFrame", sequence.firstFrame},
        {"frameCount", sequence.frameCount},
        {"padding", sequence.padding},
        {"fps", 24.0},
    };
    metadata.importSettingsJson = settings.dump();
    const AssetId id = registry->Add(std::move(metadata));
    QueueVdbSequenceCook(*registry, id, sequence, options, true);
    registry->TouchRecent(id);
    registry->Save();
    return id;
  }
  std::string importPath = path;
  CookedVolume vol;
  std::string error;
  if (!VdbImport::ImportVdbToVolume(importPath, options, vol, &error)) {
    if (!error.empty())
      fprintf(stderr, "VDB import failed: %s\n", error.c_str());
    return {};
  }
  const std::string name = std::filesystem::path(path).stem().string();
  AssetId id = RegisterAndCookVolume(*registry, registry->paths(), name,
                                     importPath, vol);
  registry->TouchRecent(id);
  registry->Save();
  return id;
}

namespace {

std::vector<AssetId>
ModelBundleTextureIds(const AssetRegistry &registry,
                      const AssetMetadata &modelMetadata) {
  std::vector<AssetId> ids;
  try {
    const json settings = json::parse(modelMetadata.importSettingsJson);
    if (settings.contains("cookBundle") &&
        settings["cookBundle"].is_object()) {
      const json &bundle = settings["cookBundle"];
      if (bundle.contains("textureIds") &&
          bundle["textureIds"].is_array()) {
        for (const json &value : bundle["textureIds"]) {
          AssetId id;
          if (value.is_string() &&
              AssetId::FromString(value.get<std::string>(), id) &&
              registry.Get(id)) {
            ids.push_back(id);
          }
        }
      }
    }
  } catch (...) {
  }
  if (!ids.empty())
    return ids;

  // Registries created before cook-bundle metadata used deterministic names.
  // Recover that index mapping once, then persist it below for future starts.
  const std::string prefix = modelMetadata.displayName + " Tex ";
  std::map<size_t, AssetId> indexed;
  for (const AssetId &id : registry.AllAssets()) {
    const AssetMetadata *metadata = registry.Get(id);
    if (!metadata || metadata->type != AssetType::Texture ||
        metadata->displayName.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::string suffix = metadata->displayName.substr(prefix.size());
    if (suffix.empty() ||
        !std::all_of(suffix.begin(), suffix.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
      continue;
    }
    try {
      indexed.emplace(static_cast<size_t>(std::stoull(suffix)), id);
    } catch (...) {
    }
  }
  if (indexed.empty())
    return ids;
  ids.resize(indexed.rbegin()->first + 1);
  for (const auto &[index, id] : indexed)
    ids[index] = id;
  return ids;
}

bool ModelBundleNeedsRepair(const AssetRegistry &registry,
                            const AssetPaths &paths,
                            const AssetMetadata &modelMetadata) {
  for (const AssetId &materialId : modelMetadata.dependencies) {
    const AssetMetadata *material = registry.Get(materialId);
    std::error_code ec;
    if (!material || material->cookState != CookState::Current ||
        !std::filesystem::exists(paths.cookedMaterialPath(materialId), ec) ||
        ec) {
      return true;
    }
    for (const AssetId &textureId : material->dependencies) {
      const AssetMetadata *texture = registry.Get(textureId);
      if (!texture || texture->cookState != CookState::Current ||
          !ValidateCookedFileHeader(paths.cookedTexturePath(textureId),
                                    CookedPayloadKind::Texture)) {
        return true;
      }
    }
  }
  return false;
}

bool EnqueueModelBundleRecook(AssetRegistry &registry, const AssetPaths &paths,
                              const AssetId &modelId,
                              const AssetMetadata &modelMetadata) {
  struct DecodeState {
    std::once_flag once;
    std::string sourcePath;
    bool loaded = false;
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;

    void Decode() {
      std::call_once(once, [this]() {
        const bool previousDefer = Asset::GetDeferGpuUpload();
        Asset::SetDeferGpuUpload(true);
        loaded = Asset::LoadModel(sourcePath, meshes, &materials, &textures);
        Asset::SetDeferGpuUpload(previousDefer);
      });
    }
  };

  const std::vector<AssetId> textureIds =
      ModelBundleTextureIds(registry, modelMetadata);
  const std::vector<AssetId> materialIds = modelMetadata.dependencies;
  auto decoded = std::make_shared<DecodeState>();
  decoded->sourcePath = modelMetadata.sourcePath;

  std::vector<CookService::Output> outputs;
  outputs.reserve(materialIds.size() + textureIds.size() + 1);

  // Small material outputs go first so progress advances immediately after
  // the one source decode instead of appearing stuck during model compression.
  for (size_t index = 0; index < materialIds.size(); ++index) {
    const AssetId materialId = materialIds[index];
    const AssetMetadata *materialMetadata = registry.Get(materialId);
    if (!materialMetadata)
      continue;
    const std::vector<AssetId> dependencies = materialMetadata->dependencies;
    outputs.push_back(
        {paths.cookedMaterialPath(materialId),
         [decoded, index, dependencies]() {
           decoded->Decode();
           if (!decoded->loaded || index >= decoded->materials.size())
             return std::vector<uint8_t>();
           const Asset::Material &material = decoded->materials[index];
           const std::vector<Asset::Material> one = {material};
           const std::vector<int> remap =
               MaterialIO::BuildTextureSaveRemap(decoded->textures, one);
           json doc;
           doc["material"] = MaterialIO::BuildMaterialsMetadata(one, remap);
           doc["textures"] = json::array();
           for (const AssetId &textureId : dependencies)
             doc["textures"].push_back(textureId.ToString());
           const std::string text = doc.dump();
           return std::vector<uint8_t>(text.begin(), text.end());
         },
         materialId});
  }

  for (size_t index = 0; index < textureIds.size(); ++index) {
    const AssetId textureId = textureIds[index];
    if (!textureId.valid() || !registry.Get(textureId))
      continue;
    outputs.push_back(
        {paths.cookedTexturePath(textureId),
         [decoded, index]() {
           decoded->Decode();
           std::vector<uint8_t> blob;
           if (decoded->loaded && index < decoded->textures.size())
             SerializeCookedTexture(ToCookedTexture(decoded->textures[index]),
                                    blob);
           return blob;
         },
         textureId});
  }

  outputs.push_back(
      {paths.cookedMeshPath(modelId),
       [decoded]() {
         decoded->Decode();
         std::vector<uint8_t> blob;
         if (decoded->loaded && !decoded->meshes.empty())
           SerializeCookedModel(ToCookedModel(decoded->meshes), blob);
         return blob;
       },
       modelId});

  if (outputs.empty())
    return false;

  for (const AssetId &id : materialIds) {
    if (const AssetMetadata *metadata = registry.Get(id)) {
      AssetMetadata updated = *metadata;
      updated.cookState = CookState::Stale;
      registry.Update(updated);
    }
  }
  for (const AssetId &id : textureIds) {
    if (const AssetMetadata *metadata = registry.Get(id)) {
      AssetMetadata updated = *metadata;
      updated.cookState = CookState::Stale;
      registry.Update(updated);
    }
  }

  AssetMetadata updatedModel = modelMetadata;
  updatedModel.cookState = CookState::Stale;
  json settings = json::object();
  try {
    settings = json::parse(updatedModel.importSettingsJson);
    if (!settings.is_object())
      settings = json::object();
  } catch (...) {
  }
  settings["cookBundle"]["textureIds"] = json::array();
  for (const AssetId &id : textureIds)
    settings["cookBundle"]["textureIds"].push_back(id.ToString());
  updatedModel.importSettingsJson = settings.dump();
  registry.Update(updatedModel);

  CookService::Get().EnqueueBatch(modelId, std::move(outputs));
  return true;
}

// Enqueue a background recook of one asset from its source file. The produce
// lambda runs on the cook worker and must not touch the registry; it captures
// everything it needs by value (same contract as the import-time cooks).
void EnqueueRecookFromSource(const AssetPaths &paths, const AssetId &id,
                             AssetType type, const std::string &sourcePath,
                             const std::string &importSettingsJson) {
  CookService &cook = CookService::Get();
  switch (type) {
  case AssetType::Model:
    cook.Enqueue(id, paths.cookedMeshPath(id), [sourcePath]() {
      const bool prevDefer = Asset::GetDeferGpuUpload();
      Asset::SetDeferGpuUpload(true);
      std::vector<Asset::GpuMesh> meshes;
      const bool loaded = Asset::LoadModel(sourcePath, meshes);
      Asset::SetDeferGpuUpload(prevDefer);
      std::vector<uint8_t> blob;
      if (loaded && !meshes.empty())
        SerializeCookedModel(ToCookedModel(meshes), blob);
      return blob;
    });
    break;
  case AssetType::Texture: {
    const std::string ext = LowerExt(sourcePath);
    const bool isHdr = (ext == "hdr" || ext == "exr");
    cook.Enqueue(id, paths.cookedTexturePath(id), [sourcePath, isHdr]() {
      Asset::Texture tex = Asset::LoadTextureFromFile(sourcePath, isHdr);
      std::vector<uint8_t> blob;
      if (tex.width > 0 && !tex.cpuData.empty())
        SerializeCookedTexture(ToCookedTexture(tex), blob);
      return blob;
    });
    break;
  }
  case AssetType::Volume: {
    VdbImport::ImportOptions options;
    try {
      const json settings = json::parse(importSettingsJson);
      options.densityGrid = settings.value("densityGrid", std::string());
      options.temperatureGrid =
          settings.value("temperatureGrid", std::string());
    } catch (...) {
    }
    cook.Enqueue(id, paths.cookedVolumePath(id), [sourcePath, options]() {
      CookedVolume cooked;
      std::string error;
      std::vector<uint8_t> blob;
      if (VdbImport::ImportVdbToVolume(sourcePath, options, cooked, &error)) {
        SerializeCookedVolume(cooked, blob);
      } else if (!error.empty()) {
        fprintf(stderr, "Volume recook failed for '%s': %s\n",
                sourcePath.c_str(), error.c_str());
      }
      return blob;
    });
    break;
  }
  default:
    break;
  }
}

} // namespace

ResumeCookStats ResumePendingCooks() {
  ResumeCookStats stats;
  AssetRegistry *registry = GlobalRegistry();
  if (!registry)
    return stats;
  CookService &cook = CookService::Get();
  const AssetPaths &paths = registry->paths();
  registry->RefreshSourceStates();
  bool changed = false;

  std::vector<AssetId> assetIds = registry->AllAssets();
  std::stable_sort(assetIds.begin(), assetIds.end(),
                   [registry](const AssetId &a, const AssetId &b) {
                     const AssetMetadata *ma = registry->Get(a);
                     const AssetMetadata *mb = registry->Get(b);
                     const bool aModel = ma && ma->type == AssetType::Model;
                     const bool bModel = mb && mb->type == AssetType::Model;
                     return aModel && !bModel;
                   });

  // Durable checkpoints are authoritative: they contain the exact imported
  // CPU payload captured before scene state could be cleared. Promote these
  // first so derived materials/textures never get misclassified as sourceless.
  for (const AssetId &id : assetIds) {
    const AssetMetadata *metadata = registry->Get(id);
    if (!metadata || metadata->fromPack)
      continue;
    const std::filesystem::path stagedPath = paths.pendingCookPath(id);
    std::error_code ec;
    if (!std::filesystem::exists(stagedPath, ec) || ec)
      continue;

    std::filesystem::path finalPath;
    std::optional<CookedPayloadKind> payloadKind;
    switch (metadata->type) {
    case AssetType::Model:
      finalPath = paths.cookedMeshPath(id);
      payloadKind = CookedPayloadKind::Model;
      break;
    case AssetType::Texture:
      finalPath = paths.cookedTexturePath(id);
      payloadKind = CookedPayloadKind::Texture;
      break;
    case AssetType::Material:
      finalPath = paths.cookedMaterialPath(id);
      break;
    case AssetType::Volume:
      finalPath = paths.cookedVolumePath(id);
      payloadKind = CookedPayloadKind::Volume;
      break;
    default:
      continue;
    }

    AssetMetadata updated = *metadata;
    updated.cookState = CookState::Stale;
    registry->Update(updated);
    cook.EnqueueBatch(
        id, {{finalPath,
              [stagedPath, payloadKind]() {
                std::vector<uint8_t> staged;
                ReadCookedFile(stagedPath, staged);
                if (payloadKind) {
                  std::vector<uint8_t> compressed;
                  if (RecompressCookedPayload(staged, *payloadKind,
                                              compressed)) {
                    return compressed;
                  }
                }
                return staged;
              },
              id, stagedPath}});
    stats.requeued++;
    changed = true;
  }

  for (const AssetId &id : assetIds) {
    const AssetMetadata *meta = registry->Get(id);
    if (!meta || meta->fromPack)
      continue;
    if (cook.IsPending(id))
      continue;

    // Per-type cooked payload path + cheap header validation. Material
    // payloads are plain JSON (no header), so existence is the check.
    std::filesystem::path cookedPath;
    bool fileValid = false;
    uint32_t expectedVersion = 0;
    std::error_code ec;
    switch (meta->type) {
    case AssetType::Model:
      cookedPath = paths.cookedMeshPath(id);
      expectedVersion = kCookerVersionMesh;
      fileValid =
          ValidateCookedFileHeader(cookedPath, CookedPayloadKind::Model);
      break;
    case AssetType::Texture:
      cookedPath = paths.cookedTexturePath(id);
      expectedVersion = kCookerVersionTexture;
      fileValid =
          ValidateCookedFileHeader(cookedPath, CookedPayloadKind::Texture);
      break;
    case AssetType::Material:
      cookedPath = paths.cookedMaterialPath(id);
      expectedVersion = kCookerVersionTexture; // matches CookAndRegisterMaterial
      fileValid = std::filesystem::exists(cookedPath, ec) && !ec;
      break;
    case AssetType::Volume:
      cookedPath = paths.cookedVolumePath(id);
      expectedVersion = kCookerVersionVolume;
      fileValid =
          ValidateCookedFileHeader(cookedPath, CookedPayloadKind::Volume);
      break;
    default:
      continue; // registry-only entry types have no cooked payload
    }

    const bool sourceAvailable =
        !meta->sourcePath.empty() &&
        std::filesystem::exists(NativeSourcePath(meta->sourcePath), ec) && !ec;

    if (meta->type == AssetType::Model && sourceAvailable &&
        ModelBundleNeedsRepair(*registry, paths, *meta)) {
      if (EnqueueModelBundleRecook(*registry, paths, id, *meta)) {
        stats.requeued++;
        changed = true;
        continue;
      }
    }

    if (meta->cookState == CookState::Current && fileValid)
      continue; // healthy

    // Volume sequences manage one cooked file per frame; reuse the
    // sequence-aware path (adopts complete sequences, cooks only the
    // missing frames).
    if (meta->type == AssetType::Volume) {
      VdbSequence sequence;
      VdbImport::ImportOptions seqOptions;
      if (ParseVdbSequenceSettings(*meta, sequence, seqOptions)) {
        if (SequenceCookComplete(paths, id, sequence)) {
          AssetMetadata updated = *meta;
          updated.cookState = CookState::Current;
          registry->Update(updated);
          stats.adopted++;
          changed = true;
        } else {
          std::error_code seqEc;
          const bool sourceOk =
              std::filesystem::exists(SequenceSourcePath(sequence, 0),
                                      seqEc) &&
              !seqEc;
          if (sourceOk) {
            QueueVdbSequenceCook(*registry, id, sequence, seqOptions, false);
            stats.requeued++;
          } else if (meta->cookState != CookState::Failed) {
            AssetMetadata updated = *meta;
            updated.cookState = CookState::Failed;
            registry->Update(updated);
            stats.missing++;
            changed = true;
            fprintf(stderr,
                    "AssetLibrary: '%s' sequence frames and source are both "
                    "missing - flagged\n",
                    meta->displayName.c_str());
          }
        }
        continue;
      }
    }

    // 1) Adopt: the cook finished writing before exit but the registry never
    //    flipped to Current (the flip happens on the main thread via Pump).
    //    WriteCookedFile is atomic, so a header-valid file is complete.
    if (fileValid && (meta->cookState == CookState::Stale ||
                      meta->cookState == CookState::NotCooked)) {
      AssetMetadata updated = *meta;
      updated.cookState = CookState::Current;
      updated.cookerVersion = expectedVersion;
      registry->Update(updated);
      stats.adopted++;
      changed = true;
      continue;
    }

    // 2) Recook from the source file when it is reachable. Materials have no
    //    standalone source (their payload derives from the model import), so
    //    they are excluded here.
    if (sourceAvailable && meta->type != AssetType::Material) {
      AssetMetadata updated = *meta;
      updated.cookState = CookState::Stale;
      registry->Update(updated);
      changed = true;
      if (meta->type == AssetType::Model) {
        EnqueueModelBundleRecook(*registry, paths, id, *meta);
      } else {
        EnqueueRecookFromSource(paths, id, meta->type, meta->sourcePath,
                                meta->importSettingsJson);
      }
      stats.requeued++;
      continue;
    }

    // 3) No source, but a version-valid payload exists (e.g. Failed left over
    //    from an old recook attempt, or a material whose JSON survived). The
    //    payload is the only copy of the data — adopt it.
    if (fileValid) {
      AssetMetadata updated = *meta;
      updated.cookState = CookState::Current;
      updated.cookerVersion = expectedVersion;
      registry->Update(updated);
      stats.adopted++;
      changed = true;
      continue;
    }

    // 4) No payload and no reachable source: flag as missing. sourceState was
    //    refreshed above (Missing when a path is recorded but absent).
    if (meta->cookState != CookState::Failed) {
      AssetMetadata updated = *meta;
      updated.cookState = CookState::Failed;
      registry->Update(updated);
      changed = true;
    }
    stats.missing++;
    fprintf(stderr,
            "AssetLibrary: '%s' has no cooked payload and its source %s - "
            "flagged as missing\n",
            meta->displayName.c_str(),
            meta->sourcePath.empty() ? "was never recorded"
                                     : "file is missing");
  }

  if (changed)
    registry->Save();
  return stats;
}

bool EnsureVdbSequenceCooked(const AssetId &id) {
  AssetRegistry *registry = GlobalRegistry();
  if (!registry)
    return false;
  const AssetMetadata *metadata = registry->Get(id);
  if (!metadata || metadata->type != AssetType::Volume)
    return true;
  AssetMetadata effectiveMetadata = *metadata;
  try {
    json settings = json::parse(effectiveMetadata.importSettingsJson);
    if (settings.contains("sequence") &&
        settings["sequence"].is_object()) {
      json &sequenceSettings = settings["sequence"];
      if (!sequenceSettings.contains("fps") ||
          sequenceSettings.value("fps", 30.0f) == 30.0f) {
        sequenceSettings["fps"] = 24.0f;
        effectiveMetadata.importSettingsJson = settings.dump();
        registry->Update(effectiveMetadata);
        registry->Save();
      }
    }
  } catch (...) {
  }
  VdbSequence sequence;
  VdbImport::ImportOptions options;
  if (!ParseVdbSequenceSettings(effectiveMetadata, sequence, options))
    return true;
  if (CookService::Get().IsPending(id))
    return false;
  if (SequenceCookComplete(registry->paths(), id, sequence)) {
    if (effectiveMetadata.cookState != CookState::Current) {
      AssetMetadata updated = effectiveMetadata;
      updated.cookState = CookState::Current;
      registry->Update(updated);
      registry->Save();
    }
    return true;
  }
  if (metadata->cookState == CookState::Failed)
    return false;
  QueueVdbSequenceCook(*registry, id, sequence, options, false);
  return false;
}

} // namespace assetlib
