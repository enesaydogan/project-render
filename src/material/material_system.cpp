#include "material_system.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MaterialSystem {

namespace {

bool IsSupportedPackedTextureFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
         format == DXGI_FORMAT_R32G32B32A32_FLOAT;
}

uint8_t FloatToByte(float value) {
  const float clamped = (std::clamp)(value, 0.0f, 1.0f);
  return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

uint8_t SampleTextureMono8(const Asset::Texture &texture, uint32_t dstX,
                          uint32_t dstY, uint32_t dstWidth,
                          uint32_t dstHeight) {
  if (texture.width == 0 || texture.height == 0 || texture.cpuData.empty() ||
      !IsSupportedPackedTextureFormat(texture.format)) {
    return 255;
  }

  const uint32_t srcX = dstWidth > 0
                            ? (std::min)(texture.width - 1,
                                         (dstX * texture.width) / dstWidth)
                            : 0;
  const uint32_t srcY = dstHeight > 0
                            ? (std::min)(texture.height - 1,
                                         (dstY * texture.height) / dstHeight)
                            : 0;
  const size_t pixelIndex = static_cast<size_t>(srcY) * texture.width + srcX;

  if (texture.format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
    const size_t base = pixelIndex * 4;
    const size_t floatCount = texture.cpuData.size() / sizeof(float);
    if (base + 2 >= floatCount) {
      return 255;
    }
    const float *pixels =
        reinterpret_cast<const float *>(texture.cpuData.data());
    const float mono = ((std::clamp)(pixels[base + 0], 0.0f, 1.0f) +
                        (std::clamp)(pixels[base + 1], 0.0f, 1.0f) +
                        (std::clamp)(pixels[base + 2], 0.0f, 1.0f)) /
                       3.0f;
    return FloatToByte(mono);
  }

  const size_t base = pixelIndex * 4;
  if (base + 2 >= texture.cpuData.size()) {
    return 255;
  }
  const uint32_t mono = static_cast<uint32_t>(texture.cpuData[base + 0]) +
                        static_cast<uint32_t>(texture.cpuData[base + 1]) +
                        static_cast<uint32_t>(texture.cpuData[base + 2]);
  return static_cast<uint8_t>(mono / 3u);
}

void InitializeRasterConstants(RuntimeRasterMaterialConstants *outConstants) {
  if (!outConstants) {
    return;
  }
  std::memset(outConstants, 0, sizeof(RuntimeRasterMaterialConstants));
  for (int &textureIndex : outConstants->textureIndices) {
    textureIndex = -1;
  }
  outConstants->emissiveAndPad[0] = -1;
  outConstants->emissiveAndPad[1] = -1;
  outConstants->emissiveAndPad[2] = -1;
  outConstants->emissiveAndPad[3] = 0;
  outConstants->mappingVariationParams[0] =
      static_cast<float>(Asset::Material::kTriPlanarVariationOff);
  outConstants->mappingVariationParams[1] = 0.0f;
  outConstants->mappingVariationParams[2] = 0.0f;
  outConstants->mappingVariationParams[3] = 0.0f;
  for (float &textureWeight : outConstants->textureWeight0) {
    textureWeight = 1.0f;
  }
  for (float &textureWeight : outConstants->textureWeight1) {
    textureWeight = 1.0f;
  }
}

} // namespace

bool UsesReflectionGlossiness(const Asset::Material &material) {
  return material.workflow == Asset::Material::kWorkflowReflectionGlossiness;
}

const char *GetMicrosurfaceLabel(const Asset::Material &material) {
  return UsesReflectionGlossiness(material) ? "Glossiness" : "Roughness";
}

const char *GetSecondarySurfaceLabel(const Asset::Material &material) {
  return UsesReflectionGlossiness(material) ? "Reflection Weight"
                                            : "Metalness";
}

const char *GetMicrosurfaceTextureLabel(const Asset::Material &material) {
  return UsesReflectionGlossiness(material) ? "Glossiness" : "Roughness";
}

void ApplyPreset(Asset::Material &m, int presetIndex) {
  auto SetRoughness = [&](float roughness) {
    m.roughness = (std::clamp)(roughness, 0.0f, 1.0f);
  };

  m.workflow = Asset::Material::kWorkflowMetalRoughness;
  m.coatWeight = 0.0f;
  m.coatRoughness = 0.1f;
  m.thinWalled = 0.0f;
  m.translucency = 0.0f;
  m.uvScale[0] = 1.0f;
  m.uvScale[1] = 1.0f;
  m.uvOffset[0] = 0.0f;
  m.uvOffset[1] = 0.0f;
  m.triPlanarEnabled = 0.0f;
  m.triPlanarScale = 1.0f;
  m.triPlanarSharpness = 4.0f;
  m.triPlanarNormalStrength = 1.0f;
  m.triPlanarRotationDegrees = 0.0f;
  m.triPlanarVariationMode = Asset::Material::kTriPlanarVariationOff;
  m.triPlanarVariationOffset = 0.0f;
  m.transmissionColor[0] = 1.0f;
  m.transmissionColor[1] = 1.0f;
  m.transmissionColor[2] = 1.0f;
  m.transmissionWeight = 0.0f;

  switch (presetIndex) {
  default:
  case 0:
    m.metalness = 0.0f;
    m.ior = 1.5f;
    SetRoughness(0.5f);
    break;
  case 1:
    m.metalness = 0.0f;
    m.ior = 1.45f;
    SetRoughness(0.75f);
    break;
  case 2:
    m.metalness = 0.0f;
    m.ior = 1.5f;
    SetRoughness(0.85f);
    break;
  case 3:
    m.metalness = 0.0f;
    m.ior = 1.5f;
    SetRoughness(0.6f);
    break;
  case 4:
    m.metalness = 0.0f;
    m.ior = 1.5f;
    SetRoughness(0.35f);
    m.coatWeight = 0.8f;
    m.coatRoughness = 0.08f;
    break;
  case 5:
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.25f);
    m.coatWeight = 0.6f;
    m.coatRoughness = 0.12f;
    break;
  case 6:
    m.metalness = 1.0f;
    m.ior = 1.0f;
    SetRoughness(0.35f);
    break;
  case 7:
    m.metalness = 1.0f;
    m.ior = 1.0f;
    SetRoughness(0.08f);
    break;
  case 8:
    m.metalness = 0.0f;
    m.ior = 1.45f;
    SetRoughness(0.35f);
    m.coatWeight = 0.25f;
    m.coatRoughness = 0.15f;
    break;
  case 9:
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.02f);
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    break;
  case 10:
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.35f);
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    break;
  case 11:
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.05f);
    m.transmissionColor[0] = 0.85f;
    m.transmissionColor[1] = 0.95f;
    m.transmissionColor[2] = 1.0f;
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    break;
  case 12:
    m.metalness = 0.0f;
    m.ior = 1.4f;
    SetRoughness(0.8f);
    m.translucency = 0.15f;
    break;
  case 13:
    m.metalness = 0.0f;
    m.ior = 1.4f;
    SetRoughness(0.65f);
    m.translucency = 0.6f;
    m.thinWalled = 1.0f;
    break;
  }
}

bool MaterialAffectsRtStructure(const Asset::Material &material) {
  return material.alphaMode != "OPAQUE" ||
         material.diffuseColor[3] < 0.999f ||
         material.transmissionWeight > 0.01f ||
         material.thinWalled > 0.5f;
}

int GetTextureIndex(const Asset::Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::BaseColor:
    return material.diffuseTexture;
  case TextureSlot::PackedSurface:
    return material.metalRoughTexture;
  case TextureSlot::Metalness:
    return material.metalnessTexture;
  case TextureSlot::RoughnessOrGlossiness:
    return material.roughnessGlossTexture;
  case TextureSlot::Normal:
    return material.normalTexture;
  case TextureSlot::Occlusion:
    return material.occlusionTexture;
  case TextureSlot::Emissive:
    return material.emissiveTexture;
  }
  return -1;
}

float GetTextureAmount(const Asset::Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::BaseColor:
    return material.diffuseTextureAmount;
  case TextureSlot::PackedSurface:
    return material.metalRoughTextureAmount;
  case TextureSlot::Metalness:
    return material.metalnessTextureAmount;
  case TextureSlot::RoughnessOrGlossiness:
    return material.roughnessGlossTextureAmount;
  case TextureSlot::Normal:
    return material.normalTextureAmount;
  case TextureSlot::Occlusion:
    return material.occlusionTextureAmount;
  case TextureSlot::Emissive:
    return material.emissiveTextureAmount;
  }
  return 1.0f;
}

void SetTextureIndex(Asset::Material &material, TextureSlot slot,
                     int textureIndex) {
  switch (slot) {
  case TextureSlot::BaseColor:
    material.diffuseTexture = textureIndex;
    break;
  case TextureSlot::PackedSurface:
    material.metalRoughTexture = textureIndex;
    break;
  case TextureSlot::Metalness:
    material.metalnessTexture = textureIndex;
    break;
  case TextureSlot::RoughnessOrGlossiness:
    material.roughnessGlossTexture = textureIndex;
    break;
  case TextureSlot::Normal:
    material.normalTexture = textureIndex;
    break;
  case TextureSlot::Occlusion:
    material.occlusionTexture = textureIndex;
    break;
  case TextureSlot::Emissive:
    material.emissiveTexture = textureIndex;
    break;
  }
}

void SetTextureAmount(Asset::Material &material, TextureSlot slot,
                      float textureAmount) {
  const float clampedAmount = (std::clamp)(textureAmount, 0.0f, 1.0f);
  switch (slot) {
  case TextureSlot::BaseColor:
    material.diffuseTextureAmount = clampedAmount;
    break;
  case TextureSlot::PackedSurface:
    material.metalRoughTextureAmount = clampedAmount;
    break;
  case TextureSlot::Metalness:
    material.metalnessTextureAmount = clampedAmount;
    break;
  case TextureSlot::RoughnessOrGlossiness:
    material.roughnessGlossTextureAmount = clampedAmount;
    break;
  case TextureSlot::Normal:
    material.normalTextureAmount = clampedAmount;
    break;
  case TextureSlot::Occlusion:
    material.occlusionTextureAmount = clampedAmount;
    break;
  case TextureSlot::Emissive:
    material.emissiveTextureAmount = clampedAmount;
    break;
  }
}

bool NeedsDerivedPackedSurfaceTexture(const Asset::Material &material) {
  return material.metalnessTexture >= 0 || material.roughnessGlossTexture >= 0;
}

bool BuildDerivedPackedSurfaceTexture(
    const Asset::Material &material, const Asset::Texture *metalnessTexture,
    const Asset::Texture *roughnessOrGlossinessTexture,
    Asset::Texture *outTexture) {
  if (!outTexture || !NeedsDerivedPackedSurfaceTexture(material)) {
    return false;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  const auto considerTexture = [&](const Asset::Texture *texture) {
    if (!texture || texture->cpuData.empty() ||
        !IsSupportedPackedTextureFormat(texture->format)) {
      return;
    }
    width = (std::max)(width, texture->width);
    height = (std::max)(height, texture->height);
  };

  considerTexture(metalnessTexture);
  considerTexture(roughnessOrGlossinessTexture);
  if (width == 0 || height == 0) {
    return false;
  }

  std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4, 255);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t base = (static_cast<size_t>(y) * width + x) * 4;

      uint8_t metalness = 255;
      if (!UsesReflectionGlossiness(material) && metalnessTexture) {
        const float sampledMetalness =
            static_cast<float>(SampleTextureMono8(*metalnessTexture, x, y,
                                                  width, height)) /
            255.0f;
        metalness = FloatToByte((std::lerp)(1.0f, sampledMetalness,
                                            (std::clamp)(
                                                material.metalnessTextureAmount,
                                                0.0f, 1.0f)));
      }

      uint8_t roughness = 255;
      if (roughnessOrGlossinessTexture) {
        float sampledMicrosurface =
            static_cast<float>(SampleTextureMono8(*roughnessOrGlossinessTexture,
                                                  x, y, width, height)) /
            255.0f;
        if (UsesReflectionGlossiness(material)) {
          sampledMicrosurface = 1.0f - sampledMicrosurface;
        }
        roughness = FloatToByte((std::lerp)(1.0f, sampledMicrosurface,
                                            (std::clamp)(
                                                material
                                                    .roughnessGlossTextureAmount,
                                                0.0f, 1.0f)));
      }

      packed[base + 0] = 255;
      packed[base + 1] = roughness;
      packed[base + 2] = metalness;
      packed[base + 3] = 255;
    }
  }

  Asset::Texture derived = Asset::LoadTextureFromMemory(
      packed.data(), static_cast<int>(width), static_cast<int>(height),
      DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!derived.resource) {
    return false;
  }
  derived.hiddenInEditor = true;
  *outTexture = std::move(derived);
  return true;
}

uint32_t BuildRuntimeMaterialFlags(const Asset::Material &material) {
  const float clampedMetalness = (std::clamp)(material.metalness, 0.0f, 1.0f);
  float transmission = (std::clamp)(material.transmissionWeight, 0.0f, 1.0f);
  transmission *= (1.0f - clampedMetalness);

  uint32_t flags = 0;
  if (material.alphaMode != "OPAQUE" || material.diffuseColor[3] < 0.999f) {
    flags |= kRuntimeMaterialFlagAlphaTested;
  }
  if (material.thinWalled > 0.5f) {
    flags |= kRuntimeMaterialFlagThinWalled;
  }
  if (material.translucency > 0.01f) {
    flags |= kRuntimeMaterialFlagTranslucent;
  }
  if (material.triPlanarEnabled > 0.5f) {
    flags |= kRuntimeMaterialFlagTriPlanar;
  }
  if (std::fabs(material.uvScale[0] - 1.0f) > 1e-5f ||
      std::fabs(material.uvScale[1] - 1.0f) > 1e-5f ||
      std::fabs(material.uvOffset[0]) > 1e-5f ||
      std::fabs(material.uvOffset[1]) > 1e-5f) {
    flags |= kRuntimeMaterialFlagUvTransform;
  }
  if (transmission > 0.01f || material.thinWalled > 0.5f) {
    flags |= kRuntimeMaterialFlagGlass;
  }
  if (material.doubleSided) {
    flags |= kRuntimeMaterialFlagDoubleSided;
  }
  if (material.invertRoughnessTexture) {
    flags |= kRuntimeMaterialFlagInvertRoughness;
  }
  return flags;
}

int GetEffectivePackedSurfaceTextureIndex(const Asset::Material &material) {
  return material.runtimeMetalRoughTexture >= 0
             ? material.runtimeMetalRoughTexture
             : material.metalRoughTexture;
}

float GetEffectivePackedSurfaceTextureAmount(const Asset::Material &material) {
  return material.runtimeMetalRoughTexture >= 0
             ? 1.0f
             : (std::clamp)(material.metalRoughTextureAmount, 0.0f, 1.0f);
}

uint32_t PackTexturePair(int lowTextureIndex, int highTextureIndex) {
  const uint32_t low =
      (lowTextureIndex >= 0) ? (static_cast<uint32_t>(lowTextureIndex) & 0xFFFFu)
                             : 0xFFFFu;
  const uint32_t high =
      (highTextureIndex >= 0) ? (static_cast<uint32_t>(highTextureIndex) & 0xFFFFu)
                              : 0xFFFFu;
  return low | (high << 16);
}

void BuildRuntimeDxrMaterialData(const Asset::Material &material,
                                 RuntimeDxrMaterialData *outCore,
                                 RuntimeDxrMaterialExtraData *outExtra) {
  if (!outCore || !outExtra) {
    return;
  }

  std::memset(outCore, 0, sizeof(RuntimeDxrMaterialData));
  std::memset(outExtra, 0, sizeof(RuntimeDxrMaterialExtraData));

  std::memcpy(outCore->baseColorOpacity, material.diffuseColor,
              sizeof(float) * 4);
  std::memcpy(outCore->emissiveIor, material.emissiveColor,
              sizeof(float) * 3);
  outCore->emissiveIor[3] = material.ior;

  const float roughness = (std::clamp)(material.roughness, 0.0f, 1.0f);
  const float metalness = (std::clamp)(material.metalness, 0.0f, 1.0f);
  float transmission = (std::clamp)(material.transmissionWeight, 0.0f, 1.0f);
  transmission *= (1.0f - metalness);
  const uint32_t flags = BuildRuntimeMaterialFlags(material);
  float flagsAsFloat = 0.0f;
  std::memcpy(&flagsAsFloat, &flags, sizeof(flags));

  outCore->pbrParamsFlags[0] = metalness;
  outCore->pbrParamsFlags[1] = roughness;
  outCore->pbrParamsFlags[2] = transmission;
  outCore->pbrParamsFlags[3] = flagsAsFloat;

  outCore->packedTextures[0] =
      PackTexturePair(material.diffuseTexture, material.normalTexture);
  outCore->packedTextures[1] =
      PackTexturePair(GetEffectivePackedSurfaceTextureIndex(material),
                      material.occlusionTexture);
  outCore->packedTextures[2] = PackTexturePair(material.emissiveTexture, -1);
  outCore->packedTextures[3] = PackTexturePair(-1, -1);

  outExtra->coatLayerParams[0] = (std::clamp)(material.coatWeight, 0.0f, 1.0f);
  outExtra->coatLayerParams[1] =
      (std::clamp)(material.coatRoughness, 0.0f, 1.0f);
  outExtra->coatLayerParams[2] = material.thinWalled;
  outExtra->coatLayerParams[3] = material.translucency;

  outExtra->uvTransform[0] = material.uvScale[0];
  outExtra->uvTransform[1] = material.uvScale[1];
  outExtra->uvTransform[2] = material.uvOffset[0];
  outExtra->uvTransform[3] = material.uvOffset[1];

  outExtra->triPlanarParams[0] = material.triPlanarEnabled;
  outExtra->triPlanarParams[1] = material.triPlanarScale;
  outExtra->triPlanarParams[2] = material.triPlanarSharpness;
  outExtra->triPlanarParams[3] = material.triPlanarNormalStrength;
  outExtra->mappingVariationParams[0] = static_cast<float>(
      (std::clamp)(material.triPlanarVariationMode,
                   Asset::Material::kTriPlanarVariationOff,
                   Asset::Material::kTriPlanarVariationPerSurface));
  outExtra->mappingVariationParams[1] =
      (std::clamp)(material.triPlanarVariationOffset, 0.0f, 1.0f);
  outExtra->mappingVariationParams[2] = material.triPlanarRotationDegrees;
  outExtra->mappingVariationParams[3] = 0.0f;

  outExtra->shadingParams[0] = (std::max)(0.0f, material.emissiveIntensity);
  outExtra->shadingParams[1] =
      (std::clamp)(material.specularWeight, 0.0f, 1.0f);
  outExtra->shadingParams[2] = (material.alphaMode == "MASK") ? 0.35f : -1.0f;
  outExtra->shadingParams[3] = material.isGrass ? 1.0f : 0.0f;

  outExtra->transmissionColor[0] =
      (std::clamp)(material.transmissionColor[0], 0.0f, 1.0f);
  outExtra->transmissionColor[1] =
      (std::clamp)(material.transmissionColor[1], 0.0f, 1.0f);
  outExtra->transmissionColor[2] =
      (std::clamp)(material.transmissionColor[2], 0.0f, 1.0f);
  outExtra->transmissionColor[3] = 1.0f;

    outExtra->textureWeight0[0] =
      (std::clamp)(material.diffuseTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight0[1] = GetEffectivePackedSurfaceTextureAmount(material);
    outExtra->textureWeight0[2] =
      (std::clamp)(material.metalnessTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight0[3] =
      (std::clamp)(material.roughnessGlossTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight1[0] =
      (std::clamp)(material.normalTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight1[1] =
      (std::clamp)(material.occlusionTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight1[2] =
      (std::clamp)(material.emissiveTextureAmount, 0.0f, 1.0f);
    outExtra->textureWeight1[3] = 0.0f;
}

void BuildRuntimeRasterMaterialConstants(
    const Asset::Material &material,
    RuntimeRasterMaterialConstants *outConstants) {
  if (!outConstants) {
    return;
  }

  InitializeRasterConstants(outConstants);
  std::memcpy(outConstants->diffuseColor, material.diffuseColor,
              sizeof(float) * 4);
  outConstants->surfaceParams[0] =
      (std::clamp)(material.roughness, 0.0f, 1.0f);
  outConstants->surfaceParams[1] = material.metalness;
  outConstants->surfaceParams[2] =
      (std::clamp)(material.specularWeight, 0.0f, 1.0f);
  outConstants->surfaceParams[3] = 0.0f;

  outConstants->transmissionParams[0] = material.transmissionColor[0];
  outConstants->transmissionParams[1] = material.transmissionColor[1];
  outConstants->transmissionParams[2] = material.transmissionColor[2];
  outConstants->transmissionParams[3] =
      (std::clamp)(material.transmissionWeight, 0.0f, 1.0f);

  std::memcpy(outConstants->emissiveColor, material.emissiveColor,
              sizeof(float) * 3);
  outConstants->emissiveColor[3] = material.ior;

  outConstants->textureIndices[0] = material.diffuseTexture;
  outConstants->textureIndices[1] = -1;
  outConstants->textureIndices[2] = material.normalTexture;
  outConstants->textureIndices[3] = -1;

  outConstants->emissiveAndPad[0] = material.emissiveTexture;
  outConstants->emissiveAndPad[1] = material.occlusionTexture;
  outConstants->emissiveAndPad[2] = GetEffectivePackedSurfaceTextureIndex(material);
  outConstants->emissiveAndPad[3] = material.invertRoughnessTexture ? 1 : 0;

  outConstants->extraParams[0] = material.emissiveIntensity;
  outConstants->extraParams[1] = (material.alphaMode == "MASK") ? 0.35f : -1.0f;
  outConstants->extraParams[2] = (material.alphaMode == "MASK") ? 1.0f : 0.0f;
  outConstants->extraParams[3] = material.isGrass ? 1.0f : 0.0f;

  outConstants->coatLayerParams[0] =
      (std::clamp)(material.coatWeight, 0.0f, 1.0f);
  outConstants->coatLayerParams[1] =
      (std::clamp)(material.coatRoughness, 0.0f, 1.0f);
  outConstants->coatLayerParams[2] = material.thinWalled;
  outConstants->coatLayerParams[3] = material.translucency;

  outConstants->uvTransform[0] = material.uvScale[0];
  outConstants->uvTransform[1] = material.uvScale[1];
  outConstants->uvTransform[2] = material.uvOffset[0];
  outConstants->uvTransform[3] = material.uvOffset[1];

  outConstants->triPlanarParams[0] = material.triPlanarEnabled;
  outConstants->triPlanarParams[1] = material.triPlanarScale;
  outConstants->triPlanarParams[2] = material.triPlanarSharpness;
  outConstants->triPlanarParams[3] = material.triPlanarNormalStrength;
  outConstants->mappingVariationParams[0] = static_cast<float>(
      (std::clamp)(material.triPlanarVariationMode,
                   Asset::Material::kTriPlanarVariationOff,
                   Asset::Material::kTriPlanarVariationPerSurface));
  outConstants->mappingVariationParams[1] =
      (std::clamp)(material.triPlanarVariationOffset, 0.0f, 1.0f);
  outConstants->mappingVariationParams[2] = material.triPlanarRotationDegrees;
  outConstants->mappingVariationParams[3] = 0.0f;

  outConstants->textureWeight0[0] =
      (std::clamp)(material.diffuseTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight0[1] =
      GetEffectivePackedSurfaceTextureAmount(material);
  outConstants->textureWeight0[2] =
      (std::clamp)(material.metalnessTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight0[3] =
      (std::clamp)(material.roughnessGlossTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight1[0] =
      (std::clamp)(material.normalTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight1[1] =
      (std::clamp)(material.occlusionTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight1[2] =
      (std::clamp)(material.emissiveTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight1[3] = 0.0f;
}

} // namespace MaterialSystem