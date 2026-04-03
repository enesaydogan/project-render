#include "material_livelink.h"

#include <algorithm>
#include <cmath>

namespace MaterialLiveLink {

void ApplyPayloadToMaterial(const LiveLink::MaterialChangedPayload &payload,
                            const ResolveTextureIndexFn &resolveTextureIndex,
                            Asset::Material *material) {
  if (!material) {
    return;
  }

  for (size_t channel = 0; channel < payload.baseColor.size(); ++channel) {
    material->diffuseColor[channel] = payload.baseColor[channel];
    material->emissiveColor[channel] = payload.emissiveColor[channel];
  }

  if (resolveTextureIndex) {
    material->diffuseTexture = resolveTextureIndex(
        payload.baseColorTextureBlobHash, payload.baseColorTextureUri);
    material->normalTexture = resolveTextureIndex(
        payload.normalTextureBlobHash, payload.normalTextureUri);
    material->emissiveTexture = resolveTextureIndex(
        payload.emissiveTextureBlobHash, payload.emissiveTextureUri);
    material->occlusionTexture = resolveTextureIndex(
        payload.occlusionTextureBlobHash, payload.occlusionTextureUri);
    material->metalRoughTexture = resolveTextureIndex(
        payload.metalRoughTextureBlobHash, payload.metalRoughTextureUri);
    material->metalnessTexture = resolveTextureIndex(
        payload.metalnessTextureBlobHash, payload.metalnessTextureUri);
    material->roughnessGlossTexture = resolveTextureIndex(
        payload.roughnessGlossTextureBlobHash,
        payload.roughnessGlossTextureUri);
  }

  material->emissiveIntensity = payload.emissiveIntensity;
  material->roughness = payload.roughness;
  material->metalness = payload.metalness;
  material->specularWeight = payload.specularWeight;
  material->ior = payload.ior;
  material->transmissionWeight = payload.transmissionWeight;
  for (size_t channel = 0; channel < payload.transmissionColor.size();
       ++channel) {
    material->transmissionColor[channel] = payload.transmissionColor[channel];
  }
  material->coatWeight = payload.coatWeight;
  material->coatRoughness = payload.coatRoughness;
  material->thinWalled = payload.thinWalled;
  material->translucency = payload.translucency;
  material->uvScale[0] =
      std::fabs(payload.uvScale[0]) > 1.0e-6f ? payload.uvScale[0] : 1.0f;
  material->uvScale[1] =
      std::fabs(payload.uvScale[1]) > 1.0e-6f ? payload.uvScale[1] : 1.0f;
  material->uvOffset[0] = payload.uvOffset[0];
  material->uvOffset[1] = payload.uvOffset[1];
  material->triPlanarEnabled = payload.triPlanarEnabled > 0.5f ? 1.0f : 0.0f;
  material->triPlanarScale =
      std::fabs(payload.triPlanarScale) > 1.0e-6f
          ? std::fabs(payload.triPlanarScale)
          : 1.0f;
  material->triPlanarSharpness =
      std::clamp(payload.triPlanarSharpness, 0.25f, 16.0f);
  material->triPlanarNormalStrength =
      (std::max)(0.0f, payload.triPlanarNormalStrength);
  material->doubleSided = payload.doubleSided;
  material->alphaMode = payload.alphaMode.empty() ? "OPAQUE" : payload.alphaMode;
  material->invertRoughnessTexture = payload.invertRoughnessTexture;
  material->workflow = payload.workflow;
  material->runtimeMetalRoughTexture = -1;

  if (!payload.materialModel.empty()) {
    material->schemaVersion = Asset::Material::kSchemaVersionOpenPbrSubset;
  }
}

} // namespace MaterialLiveLink