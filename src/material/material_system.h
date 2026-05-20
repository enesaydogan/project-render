#pragma once

#include "../assets/asset_loader.h"

#include <cstdint>

namespace MaterialSystem {

constexpr float kMinMaterialIor = 1.0f;
constexpr float kMaxMaterialIor = 10.0f;

enum class TextureSlot {
  BaseColor = 0,
  Opacity = 1,
  PackedSurface = 2,
  Metalness = 3,
  RoughnessOrGlossiness = 4,
  Normal = 5,
  CoatNormal = 6,
  Occlusion = 7,
  Emissive = 8,
  SpecularColor = 9,
  Thickness = 10,
  Parallax = 11,
};

enum RuntimeMaterialFlags : uint32_t {
  kRuntimeMaterialFlagAlphaTested = 1u << 0,
  kRuntimeMaterialFlagThinWalled = 1u << 1,
  kRuntimeMaterialFlagTranslucent = 1u << 2,
  kRuntimeMaterialFlagTriPlanar = 1u << 3,
  kRuntimeMaterialFlagUvTransform = 1u << 4,
  kRuntimeMaterialFlagGlass = 1u << 5,
  kRuntimeMaterialFlagDoubleSided = 1u << 6,
  kRuntimeMaterialFlagInvertRoughness = 1u << 7,
  kRuntimeMaterialFlagHasOpacityTexture = 1u << 8,
  kRuntimeMaterialFlagHasSpecularColor = 1u << 9,
  kRuntimeMaterialFlagHasVolume = 1u << 10,
  kRuntimeMaterialFlagHasCoatNormal = 1u << 11,
  kRuntimeMaterialFlagParallaxMapped = 1u << 12,
};

struct RuntimeRasterMaterialConstants {
  float diffuseColor[4];
  float surfaceParams[4];
  float transmissionParams[4];
  float emissiveColor[4];
  int textureIndices[4];
  int emissiveAndPad[4];
  float extraParams[4];
  float coatLayerParams[4];
  float uvTransform[4];
  float uvRotationParams[4];
  float triPlanarParams[4];
  float mappingVariationParams[4];
  float triPlanarRotationParams[4];
  float textureWeight0[4];
  float textureWeight1[4];
  int textureIndices2[4];
  float textureWeight2[4]; // x=coatNormal, y=thickness, z=specularColor, w=parallaxDepth
  float volumeParams[4];
  float specularColor[4];
  float sheenColor[4];
  float lobeParams[4];
  float parallaxParams[4];
  float parallaxTransform[4];
  float parallaxOptions[4];
};

struct RuntimeDxrMaterialData {
  float baseColorOpacity[4];
  float emissiveIor[4];
  float pbrParamsFlags[4];
  uint32_t packedTextures[4];
};

struct RuntimeDxrMaterialExtraData {
  float coatLayerParams[4];
  float uvTransform[4];
  float uvRotationParams[4];
  float triPlanarParams[4];
  float mappingVariationParams[4];
  float triPlanarRotationParams[4];
  float shadingParams[4];
  float transmissionColor[4];
  float textureWeight0[4];
  float textureWeight1[4];
  uint32_t extraPackedTextures[4]; // x packs coat normal (low) + parallax depth (high)
  float volumeParams[4];
  float specularColor[4];
  float sheenColor[4];
  float lobeParams[4];
  float parallaxParams[4];
  float parallaxTransform[4];
  float parallaxOptions[4];
};

bool UsesReflectionGlossiness(const Asset::Material &material);
const char *GetMicrosurfaceLabel(const Asset::Material &material);
const char *GetSecondarySurfaceLabel(const Asset::Material &material);
const char *GetMicrosurfaceTextureLabel(const Asset::Material &material);

void ApplyPreset(Asset::Material &material, int presetIndex);
const char *MaterialClassName(uint32_t materialClass);
uint32_t ClampMaterialClass(uint32_t materialClass);
void ApplyMaterialClassAuthoringDefaults(Asset::Material &material,
                                         uint32_t materialClass);
bool MaterialAffectsRtStructure(const Asset::Material &material);

int GetTextureIndex(const Asset::Material &material, TextureSlot slot);
void SetTextureIndex(Asset::Material &material, TextureSlot slot, int textureIndex);
float GetTextureAmount(const Asset::Material &material, TextureSlot slot);
void SetTextureAmount(Asset::Material &material, TextureSlot slot,
                      float textureAmount);

bool NeedsDerivedPackedSurfaceTexture(const Asset::Material &material);
bool BuildDerivedPackedSurfaceTexture(const Asset::Material &material,
                                      const Asset::Texture *metalnessTexture,
                                      const Asset::Texture *roughnessOrGlossinessTexture,
                                      Asset::Texture *outTexture);

uint32_t BuildRuntimeMaterialFlags(const Asset::Material &material);
int GetEffectivePackedSurfaceTextureIndex(const Asset::Material &material);
float GetEffectivePackedSurfaceTextureAmount(const Asset::Material &material);
uint32_t PackTexturePair(int lowTextureIndex, int highTextureIndex);

void BuildRuntimeDxrMaterialData(const Asset::Material &material,
                                 RuntimeDxrMaterialData *outCore,
                                 RuntimeDxrMaterialExtraData *outExtra);
void BuildRuntimeRasterMaterialConstants(
    const Asset::Material &material,
    RuntimeRasterMaterialConstants *outConstants);

} // namespace MaterialSystem
