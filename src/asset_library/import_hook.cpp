#include "import_hook.h"

#include "asset_cooker.h"
#include "asset_paths.h"
#include "asset_registry.h"
#include "cook_jobs.h"
#include "cooked_payload.h"
#include "global_registry.h"
#include "vdb_import.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <nlohmann/json.hpp>
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
  auto ts = std::filesystem::last_write_time(
      std::filesystem::path(sourcePath), ec);
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
  return std::filesystem::path(sequence.directory) /
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
        {cookedPath, [sourcePath, options]() {
           CookedVolume cooked;
           std::string error;
           if (!VdbImport::ImportVdbToVolume(sourcePath.string(), options,
                                             cooked, &error)) {
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
