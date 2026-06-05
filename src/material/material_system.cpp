#include "material_system.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MaterialSystem {

namespace {

constexpr float kMaterialFlagEpsilon = 1.0e-5f;

float ClampDielectricIor(float ior) {
  return (std::clamp)(ior, kMinMaterialIor, kMaxMaterialIor);
}

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
  outConstants->triPlanarRotationParams[0] = 0.0f;
  outConstants->triPlanarRotationParams[1] = 0.0f;
  outConstants->triPlanarRotationParams[2] = 0.0f;
  outConstants->triPlanarRotationParams[3] = 0.0f;
  for (float &textureWeight : outConstants->textureWeight0) {
    textureWeight = 1.0f;
  }
  for (float &textureWeight : outConstants->textureWeight1) {
    textureWeight = 1.0f;
  }
  for (int &textureIndex : outConstants->textureIndices2) {
    textureIndex = -1;
  }
  for (float &textureWeight : outConstants->textureWeight2) {
    textureWeight = 1.0f;
  }
  outConstants->specularColor[0] = 1.0f;
  outConstants->specularColor[1] = 1.0f;
  outConstants->specularColor[2] = 1.0f;
  outConstants->specularColor[3] = 1.0f;
  outConstants->sheenColor[0] = 1.0f;
  outConstants->sheenColor[1] = 1.0f;
  outConstants->sheenColor[2] = 1.0f;
  outConstants->sheenColor[3] = 1.0f;
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

uint32_t ClampMaterialClass(uint32_t materialClass) {
  return (std::min)(materialClass, Asset::Material::kMaterialClassEmissive);
}

const char *MaterialClassName(uint32_t materialClass) {
  switch (ClampMaterialClass(materialClass)) {
  case Asset::Material::kMaterialClassMetal:
    return "Metal";
  case Asset::Material::kMaterialClassGlass:
    return "Glass";
  case Asset::Material::kMaterialClassFabric:
    return "Fabric";
  case Asset::Material::kMaterialClassLeaf:
    return "Leaf";
  case Asset::Material::kMaterialClassEmissive:
    return "Emissive";
  case Asset::Material::kMaterialClassGeneric:
  default:
    return "Generic";
  }
}

void ApplyMaterialClassAuthoringDefaults(Asset::Material &m,
                                         uint32_t materialClass) {
  m.schemaVersion = Asset::Material::kSchemaVersionCoronaArchviz;
  m.materialClass = ClampMaterialClass(materialClass);
  switch (m.materialClass) {
  case Asset::Material::kMaterialClassMetal:
    m.workflow = Asset::Material::kWorkflowMetalRoughness;
    m.metalness = (std::max)(m.metalness, 1.0f);
    m.transmissionWeight = 0.0f;
    m.thinWalled = 0.0f;
    break;
  case Asset::Material::kMaterialClassGlass:
    m.workflow = Asset::Material::kWorkflowMetalRoughness;
    m.metalness = 0.0f;
    m.transmissionWeight = (std::max)(m.transmissionWeight, 1.0f);
    m.ior = (std::clamp)(m.ior, 1.3f, 1.8f);
    if (m.diffuseColor[3] >= 0.999f) {
      m.diffuseColor[3] = 0.35f;
    }
    m.alphaMode = "BLEND";
    break;
  case Asset::Material::kMaterialClassFabric:
    m.metalness = 0.0f;
    m.transmissionWeight = 0.0f;
    m.sheenWeight = (std::max)(m.sheenWeight, 0.35f);
    m.roughness = (std::max)(m.roughness, 0.55f);
    break;
  case Asset::Material::kMaterialClassLeaf:
    m.metalness = 0.0f;
    m.transmissionWeight = 0.0f;
    m.translucency = (std::max)(m.translucency, 0.45f);
    m.thinWalled = 1.0f;
    break;
  case Asset::Material::kMaterialClassEmissive:
    if (m.emissiveColor[0] <= 1.0e-4f && m.emissiveColor[1] <= 1.0e-4f &&
        m.emissiveColor[2] <= 1.0e-4f) {
      m.emissiveColor[0] = 1.0f;
      m.emissiveColor[1] = 1.0f;
      m.emissiveColor[2] = 1.0f;
    }
    m.emissiveIntensity = (std::max)(m.emissiveIntensity, 1.0f);
    break;
  case Asset::Material::kMaterialClassGeneric:
  default:
    break;
  }
}

void ApplyPreset(Asset::Material &m, int presetIndex) {
  auto SetRoughness = [&](float roughness) {
    m.roughness = (std::clamp)(roughness, 0.0f, 1.0f);
  };

  m.workflow = Asset::Material::kWorkflowMetalRoughness;
  m.schemaVersion = Asset::Material::kSchemaVersionCoronaArchviz;
  m.materialClass = Asset::Material::kMaterialClassGeneric;
  m.coatWeight = 0.0f;
  m.coatRoughness = 0.1f;
  m.coatIor = 1.5f;
  m.thinWalled = 0.0f;
  m.translucency = 0.0f;
  m.thickness = 0.0f;
  m.attenuationDistance = 0.0f;
  m.alphaCutoff = 0.35f;
  m.anisotropy = 0.0f;
  m.anisotropyRotation = 0.0f;
  m.sheenWeight = 0.0f;
  m.specularColor[0] = 1.0f;
  m.specularColor[1] = 1.0f;
  m.specularColor[2] = 1.0f;
  m.sheenColor[0] = 1.0f;
  m.sheenColor[1] = 1.0f;
  m.sheenColor[2] = 1.0f;
  m.uvScale[0] = 1.0f;
  m.uvScale[1] = 1.0f;
  m.uvOffset[0] = 0.0f;
  m.uvOffset[1] = 0.0f;
  m.uvRotationDegrees = 0.0f;
  m.triPlanarEnabled = 0.0f;
  m.triPlanarScale = 1.0f;
  m.triPlanarSharpness = 4.0f;
  m.triPlanarNormalStrength = 1.0f;
  m.triPlanarRotationDegrees[0] = 0.0f;
  m.triPlanarRotationDegrees[1] = 0.0f;
  m.triPlanarRotationDegrees[2] = 0.0f;
  m.triPlanarVariationMode = Asset::Material::kTriPlanarVariationOff;
  m.triPlanarVariationOffset = 0.0f;
  m.stochasticTilingRotationDegrees = 0.0f;
  m.stochasticTilingColorVariation = 0.0f;
  m.stochasticTilingMirror = false;
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
    m.coatIor = 1.52f;
    break;
  case 5:
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.25f);
    m.coatWeight = 0.6f;
    m.coatRoughness = 0.12f;
    break;
  case 6:
    m.materialClass = Asset::Material::kMaterialClassMetal;
    m.metalness = 1.0f;
    m.ior = 1.0f;
    SetRoughness(0.35f);
    break;
  case 7:
    m.materialClass = Asset::Material::kMaterialClassMetal;
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
    m.materialClass = Asset::Material::kMaterialClassGlass;
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.0f);
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    m.alphaMode = "BLEND";
    break;
  case 10:
    m.materialClass = Asset::Material::kMaterialClassGlass;
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.35f);
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    m.thickness = 0.01f;
    m.attenuationDistance = 0.25f;
    m.alphaMode = "BLEND";
    break;
  case 11:
    m.materialClass = Asset::Material::kMaterialClassGlass;
    m.metalness = 0.0f;
    m.ior = 1.52f;
    SetRoughness(0.05f);
    m.transmissionColor[0] = 0.85f;
    m.transmissionColor[1] = 0.95f;
    m.transmissionColor[2] = 1.0f;
    m.transmissionWeight = 1.0f;
    m.thinWalled = 1.0f;
    m.thickness = 0.012f;
    m.attenuationDistance = 0.15f;
    m.alphaMode = "BLEND";
    break;
  case 12:
    m.materialClass = Asset::Material::kMaterialClassFabric;
    m.metalness = 0.0f;
    m.ior = 1.4f;
    SetRoughness(0.8f);
    m.translucency = 0.15f;
    break;
  case 13:
    m.materialClass = Asset::Material::kMaterialClassLeaf;
    m.metalness = 0.0f;
    m.ior = 1.4f;
    SetRoughness(0.65f);
    m.translucency = 0.6f;
    m.thinWalled = 1.0f;
    break;
  }
}

bool MaterialAffectsRtStructure(const Asset::Material &material) {
  const float clampedMetalness = (std::clamp)(material.metalness, 0.0f, 1.0f);
  const float effectiveTransmission =
      (std::clamp)(material.transmissionWeight, 0.0f, 1.0f) *
      (1.0f - clampedMetalness);
  return material.alphaMode != "OPAQUE" ||
         material.diffuseColor[3] < 0.999f ||
         material.opacityTexture >= 0 ||
         effectiveTransmission > kMaterialFlagEpsilon ||
         material.thinWalled > 0.5f;
}

int GetTextureIndex(const Asset::Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::BaseColor:
    return material.diffuseTexture;
  case TextureSlot::Opacity:
    return material.opacityTexture;
  case TextureSlot::PackedSurface:
    return material.metalRoughTexture;
  case TextureSlot::Metalness:
    return material.metalnessTexture;
  case TextureSlot::RoughnessOrGlossiness:
    return material.roughnessGlossTexture;
  case TextureSlot::Normal:
    return material.normalTexture;
  case TextureSlot::CoatNormal:
    return material.coatNormalTexture;
  case TextureSlot::Occlusion:
    return material.occlusionTexture;
  case TextureSlot::Emissive:
    return material.emissiveTexture;
  case TextureSlot::SpecularColor:
    return material.specularColorTexture;
  case TextureSlot::Thickness:
    return material.thicknessTexture;
  case TextureSlot::Parallax:
    return material.parallaxTexture;
  }
  return -1;
}

float GetTextureAmount(const Asset::Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::BaseColor:
    return material.diffuseTextureAmount;
  case TextureSlot::Opacity:
    return material.opacityTextureAmount;
  case TextureSlot::PackedSurface:
    return material.metalRoughTextureAmount;
  case TextureSlot::Metalness:
    return material.metalnessTextureAmount;
  case TextureSlot::RoughnessOrGlossiness:
    return material.roughnessGlossTextureAmount;
  case TextureSlot::Normal:
    return material.normalTextureAmount;
  case TextureSlot::CoatNormal:
    return material.coatNormalTextureAmount;
  case TextureSlot::Occlusion:
    return material.occlusionTextureAmount;
  case TextureSlot::Emissive:
    return material.emissiveTextureAmount;
  case TextureSlot::SpecularColor:
    return material.specularColorTextureAmount;
  case TextureSlot::Thickness:
    return material.thicknessTextureAmount;
  case TextureSlot::Parallax:
    return material.parallaxDepthScale;
  }
  return 1.0f;
}

void SetTextureIndex(Asset::Material &material, TextureSlot slot,
                     int textureIndex) {
  switch (slot) {
  case TextureSlot::BaseColor:
    material.diffuseTexture = textureIndex;
    break;
  case TextureSlot::Opacity:
    material.opacityTexture = textureIndex;
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
  case TextureSlot::CoatNormal:
    material.coatNormalTexture = textureIndex;
    break;
  case TextureSlot::Occlusion:
    material.occlusionTexture = textureIndex;
    break;
  case TextureSlot::Emissive:
    material.emissiveTexture = textureIndex;
    break;
  case TextureSlot::SpecularColor:
    material.specularColorTexture = textureIndex;
    break;
  case TextureSlot::Thickness:
    material.thicknessTexture = textureIndex;
    break;
  case TextureSlot::Parallax:
    material.parallaxTexture = textureIndex;
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
  case TextureSlot::Opacity:
    material.opacityTextureAmount = clampedAmount;
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
  case TextureSlot::CoatNormal:
    material.coatNormalTextureAmount = clampedAmount;
    break;
  case TextureSlot::Occlusion:
    material.occlusionTextureAmount = clampedAmount;
    break;
  case TextureSlot::Emissive:
    material.emissiveTextureAmount = clampedAmount;
    break;
  case TextureSlot::SpecularColor:
    material.specularColorTextureAmount = clampedAmount;
    break;
  case TextureSlot::Thickness:
    material.thicknessTextureAmount = clampedAmount;
    break;
  case TextureSlot::Parallax:
    material.parallaxDepthScale = (std::clamp)(textureAmount, 0.0f, 0.25f);
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
  const bool hasSpecularColor =
      std::fabs(material.specularColor[0] - 1.0f) > 1.0e-5f ||
      std::fabs(material.specularColor[1] - 1.0f) > 1.0e-5f ||
      std::fabs(material.specularColor[2] - 1.0f) > 1.0e-5f ||
      material.specularColorTexture >= 0;
  const bool hasVolume =
      material.thickness > 1.0e-5f || material.thicknessTexture >= 0 ||
      material.attenuationDistance > 1.0e-5f;

  uint32_t flags = 0;
  if (material.alphaMode != "OPAQUE" || material.diffuseColor[3] < 0.999f ||
      material.opacityTexture >= 0) {
    flags |= kRuntimeMaterialFlagAlphaTested;
  }
  if (material.thinWalled > 0.5f) {
    flags |= kRuntimeMaterialFlagThinWalled;
  }
  if (material.translucency > kMaterialFlagEpsilon) {
    flags |= kRuntimeMaterialFlagTranslucent;
  }
  if (material.triPlanarEnabled > 0.5f) {
    flags |= kRuntimeMaterialFlagTriPlanar;
  }
  if (std::fabs(material.uvScale[0] - 1.0f) > 1e-5f ||
      std::fabs(material.uvScale[1] - 1.0f) > 1e-5f ||
      std::fabs(material.uvOffset[0]) > 1e-5f ||
      std::fabs(material.uvOffset[1]) > 1e-5f ||
      std::fabs(material.uvRotationDegrees) > 1e-5f) {
    flags |= kRuntimeMaterialFlagUvTransform;
  }
  if (transmission > kMaterialFlagEpsilon || material.thinWalled > 0.5f) {
    flags |= kRuntimeMaterialFlagGlass;
  }
  if (material.doubleSided) {
    flags |= kRuntimeMaterialFlagDoubleSided;
  }
  if (material.invertRoughnessTexture) {
    flags |= kRuntimeMaterialFlagInvertRoughness;
  }
  if (material.opacityTexture >= 0) {
    flags |= kRuntimeMaterialFlagHasOpacityTexture;
  }
  if (hasSpecularColor) {
    flags |= kRuntimeMaterialFlagHasSpecularColor;
  }
  if (hasVolume) {
    flags |= kRuntimeMaterialFlagHasVolume;
  }
  if (material.coatNormalTexture >= 0 &&
      material.coatNormalTextureAmount > 1.0e-5f &&
      material.coatWeight > 1.0e-5f) {
    flags |= kRuntimeMaterialFlagHasCoatNormal;
  }
  const uint32_t parallaxMode =
      (std::clamp)(material.parallaxMode, Asset::Material::kParallaxModeOff,
                   Asset::Material::kParallaxModeWindowBox);
  if (material.parallaxTexture >= 0 &&
      (parallaxMode != Asset::Material::kParallaxModeOff ||
       material.parallaxDepthScale > 1.0e-5f)) {
    flags |= kRuntimeMaterialFlagParallaxMapped;
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
  outCore->emissiveIor[3] = ClampDielectricIor(material.ior);

  const float roughness = (std::clamp)(material.roughness, 0.0f, 1.0f);
  const float metalness = (std::clamp)(material.metalness, 0.0f, 1.0f);
  const float transmission =
      (std::clamp)(material.transmissionWeight, 0.0f, 1.0f);
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
  outCore->packedTextures[2] =
      PackTexturePair(material.emissiveTexture, material.opacityTexture);
  outCore->packedTextures[3] =
      PackTexturePair(material.specularColorTexture, material.thicknessTexture);

  outExtra->coatLayerParams[0] = (std::clamp)(material.coatWeight, 0.0f, 1.0f);
  outExtra->coatLayerParams[1] =
      (std::clamp)(material.coatRoughness, 0.0f, 1.0f);
  outExtra->coatLayerParams[2] = material.thinWalled;
  outExtra->coatLayerParams[3] = material.translucency;

  outExtra->uvTransform[0] = material.uvScale[0];
  outExtra->uvTransform[1] = material.uvScale[1];
  outExtra->uvTransform[2] = material.uvOffset[0];
  outExtra->uvTransform[3] = material.uvOffset[1];
  outExtra->uvRotationParams[0] = material.uvRotationDegrees;
  outExtra->uvRotationParams[1] = material.normalMapFlipY ? 1.0f : 0.0f;
  outExtra->uvRotationParams[2] = 0.0f;
  outExtra->uvRotationParams[3] = 0.0f;

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
  outExtra->mappingVariationParams[2] =
      (std::clamp)(material.stochasticTilingRotationDegrees, 0.0f, 360.0f);
  outExtra->mappingVariationParams[3] =
      (std::clamp)(material.stochasticTilingColorVariation, 0.0f, 1.0f);
  outExtra->triPlanarRotationParams[0] = material.triPlanarRotationDegrees[0];
  outExtra->triPlanarRotationParams[1] = material.triPlanarRotationDegrees[1];
  outExtra->triPlanarRotationParams[2] = material.triPlanarRotationDegrees[2];
  outExtra->triPlanarRotationParams[3] =
      material.stochasticTilingMirror ? 1.0f : 0.0f;

  outExtra->shadingParams[0] = (std::max)(0.0f, material.emissiveIntensity);
  outExtra->shadingParams[1] =
      (std::clamp)(material.specularWeight, 0.0f, 1.0f);
  outExtra->shadingParams[2] =
      (material.alphaMode == "MASK")
          ? (std::clamp)(material.alphaCutoff, 0.0f, 1.0f)
          : -1.0f;
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
  outExtra->textureWeight1[3] =
      (std::clamp)(material.opacityTextureAmount, 0.0f, 1.0f);
  outExtra->extraPackedTextures[0] =
      PackTexturePair(material.coatNormalTexture, material.parallaxTexture);
  outExtra->extraPackedTextures[1] = PackTexturePair(-1, -1);
  outExtra->extraPackedTextures[2] = PackTexturePair(-1, -1);
  outExtra->extraPackedTextures[3] = PackTexturePair(-1, -1);
  outExtra->volumeParams[0] = (std::max)(0.0f, material.thickness);
  outExtra->volumeParams[1] = (std::max)(0.0f, material.attenuationDistance);
  outExtra->volumeParams[2] = (std::clamp)(material.thicknessTextureAmount,
                                           0.0f, 1.0f);
  outExtra->volumeParams[3] = ClampDielectricIor(material.coatIor);
  outExtra->specularColor[0] =
      (std::clamp)(material.specularColor[0], 0.0f, 1.0f);
  outExtra->specularColor[1] =
      (std::clamp)(material.specularColor[1], 0.0f, 1.0f);
  outExtra->specularColor[2] =
      (std::clamp)(material.specularColor[2], 0.0f, 1.0f);
  outExtra->specularColor[3] =
      (std::clamp)(material.specularColorTextureAmount, 0.0f, 1.0f);
  outExtra->sheenColor[0] =
      (std::clamp)(material.sheenColor[0], 0.0f, 1.0f);
  outExtra->sheenColor[1] =
      (std::clamp)(material.sheenColor[1], 0.0f, 1.0f);
  outExtra->sheenColor[2] =
      (std::clamp)(material.sheenColor[2], 0.0f, 1.0f);
  outExtra->sheenColor[3] = 1.0f;
  outExtra->lobeParams[0] = (std::clamp)(material.coatNormalTextureAmount,
                                         0.0f, 1.0f);
  outExtra->lobeParams[1] = (std::clamp)(material.anisotropy, -1.0f, 1.0f);
  outExtra->lobeParams[2] = material.anisotropyRotation;
  outExtra->lobeParams[3] = (std::clamp)(material.sheenWeight, 0.0f, 1.0f);
  uint32_t parallaxMode =
      (std::clamp)(material.parallaxMode, Asset::Material::kParallaxModeOff,
                   Asset::Material::kParallaxModeWindowBox);
  if (parallaxMode == Asset::Material::kParallaxModeOff &&
      material.parallaxTexture >= 0 && material.parallaxDepthScale > 1.0e-5f) {
    parallaxMode = Asset::Material::kParallaxModeHeightMap;
  }
  outExtra->parallaxParams[0] =
      (std::clamp)(material.parallaxDepthScale, 0.0f, 0.25f);
  outExtra->parallaxParams[1] = static_cast<float>(parallaxMode);
  outExtra->parallaxParams[2] =
      (std::clamp)(material.parallaxRoomDepth, 0.1f, 100.0f);
  outExtra->parallaxParams[3] =
      (std::clamp)(material.parallaxWindowAspect, 0.05f, 20.0f);
  outExtra->parallaxTransform[0] =
      (std::clamp)(material.parallaxUvScale[0], 0.01f, 100.0f);
  outExtra->parallaxTransform[1] =
      (std::clamp)(material.parallaxUvScale[1], 0.01f, 100.0f);
  outExtra->parallaxTransform[2] =
      (std::clamp)(material.parallaxUvOffset[0], -100.0f, 100.0f);
  outExtra->parallaxTransform[3] =
      (std::clamp)(material.parallaxUvOffset[1], -100.0f, 100.0f);
  outExtra->parallaxOptions[0] = material.parallaxBackFace ? 1.0f : 0.0f;
  outExtra->parallaxOptions[1] = 0.0f;
  outExtra->parallaxOptions[2] = 0.0f;
  outExtra->parallaxOptions[3] = 0.0f;
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
  outConstants->emissiveColor[3] = ClampDielectricIor(material.ior);

  outConstants->textureIndices[0] = material.diffuseTexture;
  outConstants->textureIndices[1] = material.opacityTexture;
  outConstants->textureIndices[2] = material.normalTexture;
  outConstants->textureIndices[3] = material.specularColorTexture;

  outConstants->emissiveAndPad[0] = material.emissiveTexture;
  outConstants->emissiveAndPad[1] = material.occlusionTexture;
  outConstants->emissiveAndPad[2] = GetEffectivePackedSurfaceTextureIndex(material);
  outConstants->emissiveAndPad[3] = material.invertRoughnessTexture ? 1 : 0;

  outConstants->extraParams[0] = material.emissiveIntensity;
  outConstants->extraParams[1] =
      (material.alphaMode == "MASK")
          ? (std::clamp)(material.alphaCutoff, 0.0f, 1.0f)
          : -1.0f;
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
  outConstants->uvRotationParams[0] = material.uvRotationDegrees;
  outConstants->uvRotationParams[1] =
      material.normalMapFlipY ? 1.0f : 0.0f;
  outConstants->uvRotationParams[2] = 0.0f;
  outConstants->uvRotationParams[3] = 0.0f;

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
  outConstants->mappingVariationParams[2] =
      (std::clamp)(material.stochasticTilingRotationDegrees, 0.0f, 360.0f);
  outConstants->mappingVariationParams[3] =
      (std::clamp)(material.stochasticTilingColorVariation, 0.0f, 1.0f);
  outConstants->triPlanarRotationParams[0] = material.triPlanarRotationDegrees[0];
  outConstants->triPlanarRotationParams[1] = material.triPlanarRotationDegrees[1];
  outConstants->triPlanarRotationParams[2] = material.triPlanarRotationDegrees[2];
  outConstants->triPlanarRotationParams[3] =
      material.stochasticTilingMirror ? 1.0f : 0.0f;

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
  outConstants->textureWeight1[3] =
      (std::clamp)(material.opacityTextureAmount, 0.0f, 1.0f);
  outConstants->textureIndices2[0] = material.coatNormalTexture;
  outConstants->textureIndices2[1] = material.thicknessTexture;
  outConstants->textureIndices2[2] = material.parallaxTexture;
  outConstants->textureIndices2[3] = -1;
  outConstants->textureWeight2[0] =
      (std::clamp)(material.coatNormalTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight2[1] =
      (std::clamp)(material.thicknessTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight2[2] =
      (std::clamp)(material.specularColorTextureAmount, 0.0f, 1.0f);
  outConstants->textureWeight2[3] =
      (std::clamp)(material.parallaxDepthScale, 0.0f, 0.25f);
  outConstants->volumeParams[0] = (std::max)(0.0f, material.thickness);
  outConstants->volumeParams[1] = (std::max)(0.0f, material.attenuationDistance);
  outConstants->volumeParams[2] = (std::clamp)(material.alphaCutoff, 0.0f, 1.0f);
  outConstants->volumeParams[3] = ClampDielectricIor(material.coatIor);
  outConstants->specularColor[0] =
      (std::clamp)(material.specularColor[0], 0.0f, 1.0f);
  outConstants->specularColor[1] =
      (std::clamp)(material.specularColor[1], 0.0f, 1.0f);
  outConstants->specularColor[2] =
      (std::clamp)(material.specularColor[2], 0.0f, 1.0f);
  outConstants->specularColor[3] =
      (std::clamp)(material.specularColorTextureAmount, 0.0f, 1.0f);
  outConstants->sheenColor[0] =
      (std::clamp)(material.sheenColor[0], 0.0f, 1.0f);
  outConstants->sheenColor[1] =
      (std::clamp)(material.sheenColor[1], 0.0f, 1.0f);
  outConstants->sheenColor[2] =
      (std::clamp)(material.sheenColor[2], 0.0f, 1.0f);
  outConstants->sheenColor[3] = 1.0f;
  outConstants->lobeParams[0] = (std::clamp)(material.anisotropy, -1.0f, 1.0f);
  outConstants->lobeParams[1] = material.anisotropyRotation;
  outConstants->lobeParams[2] = (std::clamp)(material.sheenWeight, 0.0f, 1.0f);
  outConstants->lobeParams[3] = (std::clamp)(material.coatNormalTextureAmount,
                                             0.0f, 1.0f);
  uint32_t parallaxMode =
      (std::clamp)(material.parallaxMode, Asset::Material::kParallaxModeOff,
                   Asset::Material::kParallaxModeWindowBox);
  if (parallaxMode == Asset::Material::kParallaxModeOff &&
      material.parallaxTexture >= 0 && material.parallaxDepthScale > 1.0e-5f) {
    parallaxMode = Asset::Material::kParallaxModeHeightMap;
  }
  outConstants->parallaxParams[0] =
      (std::clamp)(material.parallaxDepthScale, 0.0f, 0.25f);
  outConstants->parallaxParams[1] = static_cast<float>(parallaxMode);
  outConstants->parallaxParams[2] =
      (std::clamp)(material.parallaxRoomDepth, 0.1f, 100.0f);
  outConstants->parallaxParams[3] =
      (std::clamp)(material.parallaxWindowAspect, 0.05f, 20.0f);
  outConstants->parallaxTransform[0] =
      (std::clamp)(material.parallaxUvScale[0], 0.01f, 100.0f);
  outConstants->parallaxTransform[1] =
      (std::clamp)(material.parallaxUvScale[1], 0.01f, 100.0f);
  outConstants->parallaxTransform[2] =
      (std::clamp)(material.parallaxUvOffset[0], -100.0f, 100.0f);
  outConstants->parallaxTransform[3] =
      (std::clamp)(material.parallaxUvOffset[1], -100.0f, 100.0f);
  outConstants->parallaxOptions[0] = material.parallaxBackFace ? 1.0f : 0.0f;
  outConstants->parallaxOptions[1] = 0.0f;
  outConstants->parallaxOptions[2] = 0.0f;
  outConstants->parallaxOptions[3] = 0.0f;
}

} // namespace MaterialSystem
