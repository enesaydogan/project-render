#pragma once

#include "../assets/asset_loader.h"

#include <cstdint>

namespace MaterialSystem {

enum class TextureSlot {
  BaseColor = 0,
  PackedSurface = 1,
  Metalness = 2,
  RoughnessOrGlossiness = 3,
  Normal = 4,
  Occlusion = 5,
  Emissive = 6,
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
  float triPlanarParams[4];
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
  float triPlanarParams[4];
  float shadingParams[4];
  float transmissionColor[4];
};

bool UsesReflectionGlossiness(const Asset::Material &material);
const char *GetMicrosurfaceLabel(const Asset::Material &material);
const char *GetSecondarySurfaceLabel(const Asset::Material &material);
const char *GetMicrosurfaceTextureLabel(const Asset::Material &material);

void ApplyPreset(Asset::Material &material, int presetIndex);
bool MaterialAffectsRtStructure(const Asset::Material &material);

int GetTextureIndex(const Asset::Material &material, TextureSlot slot);
void SetTextureIndex(Asset::Material &material, TextureSlot slot, int textureIndex);

bool NeedsDerivedPackedSurfaceTexture(const Asset::Material &material);
bool BuildDerivedPackedSurfaceTexture(const Asset::Material &material,
                                      const Asset::Texture *metalnessTexture,
                                      const Asset::Texture *roughnessOrGlossinessTexture,
                                      Asset::Texture *outTexture);

uint32_t BuildRuntimeMaterialFlags(const Asset::Material &material);
int GetEffectivePackedSurfaceTextureIndex(const Asset::Material &material);
uint32_t PackTexturePair(int lowTextureIndex, int highTextureIndex);

void BuildRuntimeDxrMaterialData(const Asset::Material &material,
                                 RuntimeDxrMaterialData *outCore,
                                 RuntimeDxrMaterialExtraData *outExtra);
void BuildRuntimeRasterMaterialConstants(
    const Asset::Material &material,
    RuntimeRasterMaterialConstants *outConstants);

} // namespace MaterialSystem