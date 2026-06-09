#include "cloud_assets.h"

#include "asset_library/asset_metadata.h"
#include "asset_library/asset_registry.h"
#include "asset_library/global_registry.h"
#include "clouds.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CloudAssets {
namespace {

// Serialize the art-relevant CloudParams (animation time and padding excluded).
json ParamsToJson(const CloudParams &p) {
  json j;
  j["density"] = p.density;
  j["absorption"] = p.absorption;
  j["coverage"] = p.coverage;
  j["scattering"] = p.scattering;
  j["steps"] = p.steps;
  j["sunIntensity"] = p.sunIntensity;
  j["cloudTop"] = p.cloudTop;
  j["cloudBottom"] = p.cloudBottom;
  j["windSpeed"] = p.windSpeed;
  j["baseScale"] = p.baseScale;
  j["detailScale"] = p.detailScale;
  j["coverageScale"] = p.coverageScale;
  j["coverageVariation"] = p.coverageVariation;
  j["erosion"] = p.erosion;
  j["warpStrength"] = p.warpStrength;
  j["shapePower"] = p.shapePower;
  j["powderStrength"] = p.powderStrength;
  j["cirrusAmount"] = p.cirrusAmount;
  j["cloudShadowStrength"] = p.cloudShadowStrength;
  j["shadowSteps"] = p.shadowSteps;
  j["shadowStepSize"] = p.shadowStepSize;
  j["shadowLod"] = p.shadowLod;
  j["maxSteps"] = p.maxSteps;
  j["verticalStepMeters"] = p.verticalStepMeters;
  j["shadowEvery"] = p.shadowEvery;
  j["shadowDensityThreshold"] = p.shadowDensityThreshold;
  j["previewBakeSamples"] = p.previewBakeSamples;
  j["finalBakeSamples"] = p.finalBakeSamples;
  j["bakeJitterStrength"] = p.bakeJitterStrength;
  j["multiScatterBoost"] = p.multiScatterBoost;
  j["silverLiningStrength"] = p.silverLiningStrength;
  j["cloudType"] = p.cloudType;
  j["groundBounceStrength"] = p.groundBounceStrength;
  j["shadowSoftness"] = p.shadowSoftness;
  return j;
}

void ParamsFromJson(const json &j, CloudParams &p) {
  p.density = j.value("density", p.density);
  p.absorption = j.value("absorption", p.absorption);
  p.coverage = j.value("coverage", p.coverage);
  p.scattering = j.value("scattering", p.scattering);
  p.steps = j.value("steps", p.steps);
  p.sunIntensity = j.value("sunIntensity", p.sunIntensity);
  p.cloudTop = j.value("cloudTop", p.cloudTop);
  p.cloudBottom = j.value("cloudBottom", p.cloudBottom);
  p.windSpeed = j.value("windSpeed", p.windSpeed);
  p.baseScale = j.value("baseScale", p.baseScale);
  p.detailScale = j.value("detailScale", p.detailScale);
  p.coverageScale = j.value("coverageScale", p.coverageScale);
  p.coverageVariation = j.value("coverageVariation", p.coverageVariation);
  p.erosion = j.value("erosion", p.erosion);
  p.warpStrength = j.value("warpStrength", p.warpStrength);
  p.shapePower = j.value("shapePower", p.shapePower);
  p.powderStrength = j.value("powderStrength", p.powderStrength);
  p.cirrusAmount = j.value("cirrusAmount", p.cirrusAmount);
  p.cloudShadowStrength = j.value("cloudShadowStrength", p.cloudShadowStrength);
  p.shadowSteps = j.value("shadowSteps", p.shadowSteps);
  p.shadowStepSize = j.value("shadowStepSize", p.shadowStepSize);
  p.shadowLod = j.value("shadowLod", p.shadowLod);
  p.maxSteps = j.value("maxSteps", p.maxSteps);
  p.verticalStepMeters = j.value("verticalStepMeters", p.verticalStepMeters);
  p.shadowEvery = j.value("shadowEvery", p.shadowEvery);
  p.shadowDensityThreshold =
      j.value("shadowDensityThreshold", p.shadowDensityThreshold);
  p.previewBakeSamples = j.value("previewBakeSamples", p.previewBakeSamples);
  p.finalBakeSamples = j.value("finalBakeSamples", p.finalBakeSamples);
  p.bakeJitterStrength = j.value("bakeJitterStrength", p.bakeJitterStrength);
  p.multiScatterBoost = j.value("multiScatterBoost", p.multiScatterBoost);
  p.silverLiningStrength =
      j.value("silverLiningStrength", p.silverLiningStrength);
  p.cloudType = j.value("cloudType", p.cloudType);
  p.groundBounceStrength =
      j.value("groundBounceStrength", p.groundBounceStrength);
  p.shadowSoftness = j.value("shadowSoftness", p.shadowSoftness);
}

} // namespace

assetlib::AssetId SaveCurrentAsPreset(const std::string &name) {
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg)
    return {};
  assetlib::AssetMetadata m;
  m.type = assetlib::AssetType::CloudPreset;
  m.displayName = name.empty() ? "Cloud Preset" : name;
  m.virtualPath = "Clouds/Presets";
  m.cookState = assetlib::CookState::Current; // params are the payload
  json doc;
  doc["params"] = ParamsToJson(g_cloudManager.GetParams());
  m.importSettingsJson = doc.dump();
  assetlib::AssetId id = reg->Add(std::move(m));
  reg->TouchRecent(id);
  reg->Save();
  return id;
}

bool ApplyPreset(const assetlib::AssetId &id) {
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg)
    return false;
  const assetlib::AssetMetadata *meta = reg->Get(id);
  if (!meta || meta->type != assetlib::AssetType::CloudPreset)
    return false;
  json doc;
  try {
    doc = json::parse(meta->importSettingsJson);
  } catch (const json::exception &) {
    return false;
  }
  if (!doc.contains("params"))
    return false;
  ParamsFromJson(doc["params"], g_cloudManager.GetParams());
  g_cloudManager.RequestBake();
  return true;
}

} // namespace CloudAssets
