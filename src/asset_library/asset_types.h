#pragma once
#include <cstdint>
#include <string>

// Asset type taxonomy for the registry. The initial set mirrors
// notes/asset-menagement.md "Asset Types"; later candidates (IES, decal, etc.)
// can be appended without renumbering existing values.
namespace assetlib {

enum class AssetType : uint32_t {
  Unknown = 0,
  Model = 1,
  Material = 2,
  Texture = 3,
  ScatterObject = 4,
  ScatterPreset = 5,
  CloudVolume = 6,
  CloudPreset = 7,
  Hdri = 8,
  EnvironmentPreset = 9,
};

// Stable string token used in the JSON registry. Never localize or reorder;
// these strings are persisted.
inline const char *AssetTypeToString(AssetType t) {
  switch (t) {
  case AssetType::Model:
    return "model";
  case AssetType::Material:
    return "material";
  case AssetType::Texture:
    return "texture";
  case AssetType::ScatterObject:
    return "scatter_object";
  case AssetType::ScatterPreset:
    return "scatter_preset";
  case AssetType::CloudVolume:
    return "cloud_volume";
  case AssetType::CloudPreset:
    return "cloud_preset";
  case AssetType::Hdri:
    return "hdri";
  case AssetType::EnvironmentPreset:
    return "environment_preset";
  case AssetType::Unknown:
  default:
    return "unknown";
  }
}

inline AssetType AssetTypeFromString(const std::string &s) {
  if (s == "model")
    return AssetType::Model;
  if (s == "material")
    return AssetType::Material;
  if (s == "texture")
    return AssetType::Texture;
  if (s == "scatter_object")
    return AssetType::ScatterObject;
  if (s == "scatter_preset")
    return AssetType::ScatterPreset;
  if (s == "cloud_volume")
    return AssetType::CloudVolume;
  if (s == "cloud_preset")
    return AssetType::CloudPreset;
  if (s == "hdri")
    return AssetType::Hdri;
  if (s == "environment_preset")
    return AssetType::EnvironmentPreset;
  return AssetType::Unknown;
}

// Human-facing label for UI (safe to change / localize).
inline const char *AssetTypeDisplayName(AssetType t) {
  switch (t) {
  case AssetType::Model:
    return "Model";
  case AssetType::Material:
    return "Material";
  case AssetType::Texture:
    return "Texture";
  case AssetType::ScatterObject:
    return "Scatter Object";
  case AssetType::ScatterPreset:
    return "Scatter Preset";
  case AssetType::CloudVolume:
    return "Cloud Volume";
  case AssetType::CloudPreset:
    return "Cloud Preset";
  case AssetType::Hdri:
    return "HDRI";
  case AssetType::EnvironmentPreset:
    return "Environment Preset";
  case AssetType::Unknown:
  default:
    return "Unknown";
  }
}

} // namespace assetlib
