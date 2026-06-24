#include "material_livelink.h"

#include <algorithm>
#include <cmath>

namespace MaterialLiveLink {
namespace {

uint32_t InferLiveLinkMaterialClass(
    const LiveLink::MaterialChangedPayload &payload) {
  if (payload.materialClass <= Asset::Material::kMaterialClassEmissive) {
    return payload.materialClass;
  }
  const float emissiveMax =
      (std::max)(payload.emissiveColor[0],
                 (std::max)(payload.emissiveColor[1],
                            payload.emissiveColor[2])) *
      (std::max)(payload.emissiveIntensity, 0.0f);
  if (emissiveMax > 1.0e-4f) {
    return Asset::Material::kMaterialClassEmissive;
  }
  if (payload.transmissionWeight > 1.0e-4f || payload.thinWalled > 0.5f) {
    return Asset::Material::kMaterialClassGlass;
  }
  if (payload.translucency > 1.0e-4f) {
    return Asset::Material::kMaterialClassLeaf;
  }
  if (payload.metalness > 0.5f) {
    return Asset::Material::kMaterialClassMetal;
  }
  if (payload.sheenWeight > 1.0e-4f) {
    return Asset::Material::kMaterialClassFabric;
  }
  return Asset::Material::kMaterialClassGeneric;
}

} // namespace

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
    material->opacityTexture = resolveTextureIndex(
        payload.opacityTextureBlobHash, payload.opacityTextureUri);
    material->normalTexture = resolveTextureIndex(
        payload.normalTextureBlobHash, payload.normalTextureUri);
    material->coatNormalTexture = resolveTextureIndex(
        payload.coatNormalTextureBlobHash, payload.coatNormalTextureUri);
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
    material->specularColorTexture = resolveTextureIndex(
        payload.specularColorTextureBlobHash, payload.specularColorTextureUri);
    material->thicknessTexture = resolveTextureIndex(
        payload.thicknessTextureBlobHash, payload.thicknessTextureUri);
  }

  material->diffuseTextureAmount =
      std::clamp(payload.baseColorTextureAmount, 0.0f, 1.0f);
  material->opacityTextureAmount =
      std::clamp(payload.opacityTextureAmount, 0.0f, 1.0f);
  material->metalRoughTextureAmount =
      std::clamp(payload.packedSurfaceTextureAmount, 0.0f, 1.0f);
  material->metalnessTextureAmount =
      std::clamp(payload.metalnessTextureAmount, 0.0f, 1.0f);
  material->roughnessGlossTextureAmount =
      std::clamp(payload.roughnessGlossTextureAmount, 0.0f, 1.0f);
  material->normalTextureAmount =
      std::clamp(payload.normalTextureAmount, 0.0f, 1.0f);
  material->normalMapFlipY = payload.normalMapFlipY;
  material->useBumpMap = payload.useBumpMap;
  material->coatNormalTextureAmount =
      std::clamp(payload.coatNormalTextureAmount, 0.0f, 1.0f);
  material->occlusionTextureAmount =
      std::clamp(payload.occlusionTextureAmount, 0.0f, 1.0f);
  material->emissiveTextureAmount =
      std::clamp(payload.emissiveTextureAmount, 0.0f, 1.0f);
  material->specularColorTextureAmount =
      std::clamp(payload.specularColorTextureAmount, 0.0f, 1.0f);
  material->thicknessTextureAmount =
      std::clamp(payload.thicknessTextureAmount, 0.0f, 1.0f);

  material->emissiveIntensity = payload.emissiveIntensity;
  material->roughness = payload.roughness;
  material->metalness = payload.metalness;
  material->specularWeight = payload.specularWeight;
  for (size_t channel = 0; channel < payload.specularColor.size();
       ++channel) {
    material->specularColor[channel] = payload.specularColor[channel];
  }
  material->ior = payload.ior;
  material->reflectionIor = payload.reflectionIor;
  material->transmissionWeight = payload.transmissionWeight;
  for (size_t channel = 0; channel < payload.transmissionColor.size();
       ++channel) {
    material->transmissionColor[channel] = payload.transmissionColor[channel];
  }
  material->thickness = payload.thickness;
  material->attenuationDistance = payload.attenuationDistance;
  material->coatWeight = payload.coatWeight;
  material->coatRoughness = payload.coatRoughness;
  material->coatIor = payload.coatIor;
  material->anisotropy = payload.anisotropy;
  material->anisotropyRotation = payload.anisotropyRotation;
  material->sheenWeight = payload.sheenWeight;
  for (size_t channel = 0; channel < payload.sheenColor.size(); ++channel) {
    material->sheenColor[channel] = payload.sheenColor[channel];
  }
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
  material->alphaCutoff = std::clamp(payload.alphaCutoff, 0.0f, 1.0f);
  material->invertRoughnessTexture = payload.invertRoughnessTexture;
  material->workflow = payload.workflow;
  material->runtimeMetalRoughTexture = -1;

  material->schemaVersion = Asset::Material::kSchemaVersionCoronaArchviz;
  material->materialClass = InferLiveLinkMaterialClass(payload);
}

} // namespace MaterialLiveLink
