#include "material_io.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace MaterialIO {
namespace {

uint32_t InferMaterialClass(const Asset::Material &material) {
  if (material.isGrass || material.translucency > 0.05f) {
    return Asset::Material::kMaterialClassLeaf;
  }
  if (material.transmissionWeight > 1.0e-5f ||
      material.thinWalled > 0.5f) {
    return Asset::Material::kMaterialClassGlass;
  }
  if (material.metalness > 0.5f) {
    return Asset::Material::kMaterialClassMetal;
  }
  const float emissiveMax = (std::max)(
      material.emissiveColor[0],
      (std::max)(material.emissiveColor[1], material.emissiveColor[2]));
  if (emissiveMax * (std::max)(material.emissiveIntensity, 0.0f) >
      1.0e-4f) {
    return Asset::Material::kMaterialClassEmissive;
  }
  return Asset::Material::kMaterialClassGeneric;
}

uint32_t ClampMaterialClass(uint32_t materialClass) {
  return (std::min)(materialClass, Asset::Material::kMaterialClassEmissive);
}

nlohmann::json BuildMappingMetadata(const Asset::Material &material) {
  return {
      {"us", {material.uvScale[0], material.uvScale[1]}},
      {"uo", {material.uvOffset[0], material.uvOffset[1]}},
      {"ur", material.uvRotationDegrees},
      {"te", material.triPlanarEnabled},
      {"ts", material.triPlanarScale},
      {"ths", material.triPlanarSharpness},
      {"tns", material.triPlanarNormalStrength},
      {"trr",
       {material.triPlanarRotationDegrees[0],
        material.triPlanarRotationDegrees[1],
        material.triPlanarRotationDegrees[2]}},
      {"tvm", material.triPlanarVariationMode},
      {"tvo", material.triPlanarVariationOffset},
      {"tvr", material.stochasticTilingRotationDegrees},
      {"tvc", material.stochasticTilingColorVariation},
      {"tvmr", material.stochasticTilingMirror},
      {"pm", material.parallaxMode},
      {"tap", material.parallaxDepthScale},
      {"prd", material.parallaxRoomDepth},
      {"pwa", material.parallaxWindowAspect},
      {"pvs", {material.parallaxUvScale[0], material.parallaxUvScale[1]}},
      {"pvo", {material.parallaxUvOffset[0], material.parallaxUvOffset[1]}},
      {"pbf", material.parallaxBackFace},
  };
}

void RestoreMappingMetadata(const nlohmann::json &mapping,
                            Asset::Material &material) {
  if (!mapping.is_object()) {
    return;
  }

  if (mapping.contains("us") && mapping["us"].is_array() &&
      mapping["us"].size() >= 2) {
    material.uvScale[0] = mapping["us"][0].get<float>();
    material.uvScale[1] = mapping["us"][1].get<float>();
  }
  if (mapping.contains("uo") && mapping["uo"].is_array() &&
      mapping["uo"].size() >= 2) {
    material.uvOffset[0] = mapping["uo"][0].get<float>();
    material.uvOffset[1] = mapping["uo"][1].get<float>();
  }
  material.triPlanarEnabled =
      mapping.value("te", material.triPlanarEnabled);
  material.uvRotationDegrees =
      mapping.value("ur", material.uvRotationDegrees);
  material.triPlanarScale =
      mapping.value("ts", material.triPlanarScale);
  material.triPlanarSharpness =
      mapping.value("ths", material.triPlanarSharpness);
  material.triPlanarNormalStrength =
      mapping.value("tns", material.triPlanarNormalStrength);
  if (mapping.contains("trr") && mapping["trr"].is_array() &&
      mapping["trr"].size() >= 3) {
    for (int axis = 0; axis < 3; ++axis) {
      material.triPlanarRotationDegrees[axis] =
          mapping["trr"][axis].get<float>();
    }
  }
  material.triPlanarVariationMode =
      mapping.value("tvm", material.triPlanarVariationMode);
  material.triPlanarVariationOffset =
      mapping.value("tvo", material.triPlanarVariationOffset);
  material.stochasticTilingRotationDegrees =
      mapping.value("tvr", material.stochasticTilingRotationDegrees);
  material.stochasticTilingColorVariation =
      mapping.value("tvc", material.stochasticTilingColorVariation);
  material.stochasticTilingMirror =
      mapping.value("tvmr", material.stochasticTilingMirror);
  material.parallaxMode =
      mapping.value("pm", material.parallaxMode);
  material.parallaxDepthScale =
      mapping.value("tap", material.parallaxDepthScale);
  material.parallaxRoomDepth =
      mapping.value("prd", material.parallaxRoomDepth);
  material.parallaxWindowAspect =
      mapping.value("pwa", material.parallaxWindowAspect);
  if (mapping.contains("pvs") && mapping["pvs"].is_array() &&
      mapping["pvs"].size() >= 2) {
    material.parallaxUvScale[0] = mapping["pvs"][0].get<float>();
    material.parallaxUvScale[1] = mapping["pvs"][1].get<float>();
  }
  if (mapping.contains("pvo") && mapping["pvo"].is_array() &&
      mapping["pvo"].size() >= 2) {
    material.parallaxUvOffset[0] = mapping["pvo"][0].get<float>();
    material.parallaxUvOffset[1] = mapping["pvo"][1].get<float>();
  }
  material.parallaxBackFace =
      mapping.value("pbf", material.parallaxBackFace);
}

} // namespace

std::vector<int> BuildTextureSaveRemap(
    const std::vector<Asset::Texture> &textures,
    const std::vector<Asset::Material> &materials) {
  std::vector<int> remap(textures.size(), -1);
  std::vector<uint8_t> referenced(textures.size(), 0);
  const auto markTexture = [&](int textureIndex) {
    if (textureIndex < 0 ||
        textureIndex >= static_cast<int>(textures.size())) {
      return;
    }
    referenced[static_cast<size_t>(textureIndex)] = 1;
  };

  for (const Asset::Material &material : materials) {
    markTexture(material.diffuseTexture);
    markTexture(material.opacityTexture);
    markTexture(material.normalTexture);
    markTexture(material.coatNormalTexture);
    markTexture(material.emissiveTexture);
    markTexture(material.occlusionTexture);
    markTexture(material.metalRoughTexture);
    markTexture(material.metalnessTexture);
    markTexture(material.roughnessGlossTexture);
    markTexture(material.specularColorTexture);
    markTexture(material.thicknessTexture);
    markTexture(material.parallaxTexture);
  }

  // Content-deduplicate referenced textures so identical images (e.g. the same
  // texture pulled in by several instantiations of one library asset) are
  // embedded once. Identical-content textures share a saved slot; slots are
  // numbered 0..N-1 in first-occurrence order so the writer can emit each slot
  // exactly once and the loader reconstructs them by slot index.
  const auto sameContent = [](const Asset::Texture &a,
                              const Asset::Texture &b) {
    return a.width == b.width && a.height == b.height &&
           a.cpuFormat == b.cpuFormat && a.cpuMipLevels == b.cpuMipLevels &&
           a.cpuData.size() == b.cpuData.size() &&
           (a.cpuData.empty() ||
            std::memcmp(a.cpuData.data(), b.cpuData.data(),
                        a.cpuData.size()) == 0);
  };
  const auto hashContent = [](const Asset::Texture &t) -> uint64_t {
    uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
    const auto mix = [&](const void *p, size_t n) {
      const uint8_t *b = static_cast<const uint8_t *>(p);
      for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
      }
    };
    mix(&t.width, sizeof(t.width));
    mix(&t.height, sizeof(t.height));
    uint32_t fmt = static_cast<uint32_t>(t.cpuFormat);
    mix(&fmt, sizeof(fmt));
    mix(&t.cpuMipLevels, sizeof(t.cpuMipLevels));
    if (!t.cpuData.empty())
      mix(t.cpuData.data(), t.cpuData.size());
    return h;
  };

  // hash -> representative source texture index per distinct slot under it.
  std::unordered_map<uint64_t, std::vector<size_t>> byHash;
  int nextIndex = 0;
  for (size_t textureIndex = 0; textureIndex < textures.size();
       ++textureIndex) {
    if (!referenced[textureIndex] || textures[textureIndex].hiddenInEditor) {
      continue;
    }
    const uint64_t h = hashContent(textures[textureIndex]);
    int slot = -1;
    auto it = byHash.find(h);
    if (it != byHash.end()) {
      for (size_t rep : it->second) {
        if (sameContent(textures[rep], textures[textureIndex])) {
          slot = remap[rep];
          break;
        }
      }
    }
    if (slot < 0) {
      slot = nextIndex++;
      byHash[h].push_back(textureIndex);
    }
    remap[textureIndex] = slot;
  }
  return remap;
}

int MapSavedTextureIndex(const std::vector<int> &remap, int textureIndex) {
  if (textureIndex < 0 || textureIndex >= static_cast<int>(remap.size())) {
    return -1;
  }
  return remap[static_cast<size_t>(textureIndex)];
}

nlohmann::json BuildMaterialsMetadata(
    const std::vector<Asset::Material> &materials,
    const std::vector<int> &textureSaveRemap) {
  nlohmann::json materialArray = nlohmann::json::array();
  for (const auto &mat : materials) {
    materialArray.push_back({
        {"n", std::string(mat.name)},
        {"sv", Asset::Material::kSchemaVersionCoronaArchviz},
        {"materialClass", ClampMaterialClass(mat.materialClass)},
        {"mc", ClampMaterialClass(mat.materialClass)},
        {"bc",
         {mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2],
          mat.diffuseColor[3]}},
        {"mt", mat.metalness},
        {"rg", mat.roughness},
        {"sw", mat.specularWeight},
        {"sc",
         {mat.specularColor[0], mat.specularColor[1], mat.specularColor[2]}},
        {"tc",
         {mat.transmissionColor[0], mat.transmissionColor[1],
          mat.transmissionColor[2]}},
        {"tr", mat.transmissionWeight},
        {"tk", mat.thickness},
        {"ad", mat.attenuationDistance},
        {"io", mat.ior},
        {"ec",
         {mat.emissiveColor[0], mat.emissiveColor[1], mat.emissiveColor[2],
          mat.emissiveColor[3]}},
        {"ei", mat.emissiveIntensity},
        {"cw", mat.coatWeight},
        {"cr", mat.coatRoughness},
        {"ci", mat.coatIor},
        {"an", mat.anisotropy},
        {"ar", mat.anisotropyRotation},
        {"shw", mat.sheenWeight},
        {"shc", {mat.sheenColor[0], mat.sheenColor[1], mat.sheenColor[2]}},
        {"th", mat.thinWalled},
        {"tl", mat.translucency},
        {"mp", BuildMappingMetadata(mat)},
        {"us", {mat.uvScale[0], mat.uvScale[1]}},
        {"uo", {mat.uvOffset[0], mat.uvOffset[1]}},
        {"ur", mat.uvRotationDegrees},
        {"te", mat.triPlanarEnabled},
        {"ts", mat.triPlanarScale},
        {"ths", mat.triPlanarSharpness},
        {"tns", mat.triPlanarNormalStrength},
        {"trr",
         {mat.triPlanarRotationDegrees[0], mat.triPlanarRotationDegrees[1],
          mat.triPlanarRotationDegrees[2]}},
        {"tvm", mat.triPlanarVariationMode},
        {"tvo", mat.triPlanarVariationOffset},
        {"tvr", mat.stochasticTilingRotationDegrees},
        {"tvc", mat.stochasticTilingColorVariation},
        {"tvmr", mat.stochasticTilingMirror},
        {"wf", mat.workflow},
        {"txd", MapSavedTextureIndex(textureSaveRemap, mat.diffuseTexture)},
        {"txa", MapSavedTextureIndex(textureSaveRemap, mat.opacityTexture)},
        {"txn", MapSavedTextureIndex(textureSaveRemap, mat.normalTexture)},
        {"txcn", MapSavedTextureIndex(textureSaveRemap, mat.coatNormalTexture)},
        {"txe", MapSavedTextureIndex(textureSaveRemap, mat.emissiveTexture)},
        {"txo", MapSavedTextureIndex(textureSaveRemap, mat.occlusionTexture)},
        {"txm",
         MapSavedTextureIndex(textureSaveRemap, mat.metalRoughTexture)},
        {"txmt",
         MapSavedTextureIndex(textureSaveRemap, mat.metalnessTexture)},
        {"txrg",
         MapSavedTextureIndex(textureSaveRemap, mat.roughnessGlossTexture)},
        {"txsc",
         MapSavedTextureIndex(textureSaveRemap, mat.specularColorTexture)},
        {"txtk", MapSavedTextureIndex(textureSaveRemap, mat.thicknessTexture)},
        {"txp", MapSavedTextureIndex(textureSaveRemap, mat.parallaxTexture)},
        {"tad", mat.diffuseTextureAmount},
        {"taa", mat.opacityTextureAmount},
        {"tamr", mat.metalRoughTextureAmount},
        {"tamt", mat.metalnessTextureAmount},
        {"targ", mat.roughnessGlossTextureAmount},
        {"tan", mat.normalTextureAmount},
        {"nmfy", mat.normalMapFlipY},
        {"bump", mat.useBumpMap},
        {"tacn", mat.coatNormalTextureAmount},
        {"tao", mat.occlusionTextureAmount},
        {"tae", mat.emissiveTextureAmount},
        {"tasc", mat.specularColorTextureAmount},
        {"tatk", mat.thicknessTextureAmount},
        {"pm", mat.parallaxMode},
        {"tap", mat.parallaxDepthScale},
        {"prd", mat.parallaxRoomDepth},
        {"pwa", mat.parallaxWindowAspect},
        {"pvs", {mat.parallaxUvScale[0], mat.parallaxUvScale[1]}},
        {"pvo", {mat.parallaxUvOffset[0], mat.parallaxUvOffset[1]}},
        {"pbf", mat.parallaxBackFace},
        {"ds", mat.doubleSided},
        {"am", mat.alphaMode},
        {"ac", mat.alphaCutoff},
        {"gr", mat.isGrass},
        {"gc", {mat.grassColor[0], mat.grassColor[1], mat.grassColor[2]}},
        {"gs", mat.grassBladeSize},
        {"gn", mat.grassBladeCount},
        {"gv", mat.grassBladeVariation},
    });
  }
  return materialArray;
}

void RestoreMaterialsFromMetadata(const nlohmann::json &materialsJson,
                                  std::vector<Asset::Material> *materials,
                                  const std::vector<Asset::Texture> &textures,
                                  std::vector<int> *remap) {
  if (!materials || !remap || !materialsJson.is_array()) {
    return;
  }

  remap->assign(materialsJson.size(), -1);
  for (size_t jsonIndex = 0; jsonIndex < materialsJson.size(); ++jsonIndex) {
    const auto &savedMaterial = materialsJson[jsonIndex];
    const std::string name = savedMaterial.value("n", "Material");

    // Scene files store material slots by index. Display names are not unique
    // enough to use as restore identity; collapsing same-named materials
    // changes mesh material indices and the wavefront material-bin path.
    Asset::Material restoredMaterial;
    strncpy_s(restoredMaterial.name, name.c_str(),
              sizeof(restoredMaterial.name) - 1);
    materials->push_back(restoredMaterial);
    const int materialIndex = static_cast<int>(materials->size()) - 1;

    (*remap)[jsonIndex] = materialIndex;
    auto &material = (*materials)[static_cast<size_t>(materialIndex)];

    const uint32_t savedSchema = savedMaterial.value(
        "sv", Asset::Material::kSchemaVersionOpenPbrSubset);
    material.schemaVersion = Asset::Material::kSchemaVersionCoronaArchviz;
    const uint32_t savedMaterialClass = savedMaterial.value(
        "materialClass",
        savedMaterial.value(
            "mc",
            static_cast<uint32_t>(Asset::Material::kMaterialClassGeneric)));
    material.materialClass = ClampMaterialClass(savedMaterialClass);
    if (savedMaterial.contains("bc")) {
      for (int channel = 0; channel < 4; ++channel) {
        material.diffuseColor[channel] = savedMaterial["bc"][channel];
      }
    }
    material.metalness = savedMaterial.value("mt", material.metalness);
    material.roughness = savedMaterial.value("rg", material.roughness);
    material.specularWeight =
        savedMaterial.value("sw", material.specularWeight);
    if (savedMaterial.contains("sc")) {
      for (int channel = 0; channel < 3; ++channel) {
        material.specularColor[channel] = savedMaterial["sc"][channel];
      }
    }
    if (savedMaterial.contains("tc")) {
      for (int channel = 0; channel < 3; ++channel) {
        material.transmissionColor[channel] = savedMaterial["tc"][channel];
      }
    }
    material.transmissionWeight =
        savedMaterial.value("tr", material.transmissionWeight);
    material.thickness = savedMaterial.value("tk", material.thickness);
    material.attenuationDistance =
        savedMaterial.value("ad", material.attenuationDistance);
    material.ior = savedMaterial.value("io", material.ior);
    if (savedMaterial.contains("ec")) {
      for (int channel = 0; channel < 4; ++channel) {
        material.emissiveColor[channel] = savedMaterial["ec"][channel];
      }
    }
    material.emissiveIntensity =
        savedMaterial.value("ei", material.emissiveIntensity);
    material.coatWeight = savedMaterial.value("cw", material.coatWeight);
    material.coatRoughness =
        savedMaterial.value("cr", material.coatRoughness);
    material.coatIor = savedMaterial.value("ci", material.coatIor);
    material.anisotropy = savedMaterial.value("an", material.anisotropy);
    material.anisotropyRotation =
        savedMaterial.value("ar", material.anisotropyRotation);
    material.sheenWeight = savedMaterial.value("shw", material.sheenWeight);
    if (savedMaterial.contains("shc")) {
      for (int channel = 0; channel < 3; ++channel) {
        material.sheenColor[channel] = savedMaterial["shc"][channel];
      }
    }
    material.thinWalled = savedMaterial.value("th", material.thinWalled);
    material.translucency =
        savedMaterial.value("tl", material.translucency);
    material.workflow = savedMaterial.value("wf", material.workflow);
    if (savedMaterial.contains("us")) {
      for (int channel = 0; channel < 2; ++channel) {
        material.uvScale[channel] = savedMaterial["us"][channel];
      }
    }
    if (savedMaterial.contains("uo")) {
      for (int channel = 0; channel < 2; ++channel) {
        material.uvOffset[channel] = savedMaterial["uo"][channel];
      }
    }
    material.uvRotationDegrees =
        savedMaterial.value("ur", material.uvRotationDegrees);
    material.triPlanarEnabled =
        savedMaterial.value("te", material.triPlanarEnabled);
    material.triPlanarScale =
        savedMaterial.value("ts", material.triPlanarScale);
    material.triPlanarSharpness =
        savedMaterial.value("ths", material.triPlanarSharpness);
    material.triPlanarNormalStrength =
        savedMaterial.value("tns", material.triPlanarNormalStrength);
    if (savedMaterial.contains("trr")) {
      for (int axis = 0; axis < 3; ++axis) {
        material.triPlanarRotationDegrees[axis] = savedMaterial["trr"][axis];
      }
    } else {
      material.triPlanarRotationDegrees[0] = 0.0f;
      material.triPlanarRotationDegrees[1] =
          savedMaterial.value("trd", material.triPlanarRotationDegrees[1]);
      material.triPlanarRotationDegrees[2] = 0.0f;
    }
    material.triPlanarVariationMode = savedMaterial.value(
      "tvm", material.triPlanarVariationMode);
    material.triPlanarVariationOffset =
      savedMaterial.value("tvo", material.triPlanarVariationOffset);
    material.stochasticTilingRotationDegrees =
      savedMaterial.value("tvr", material.stochasticTilingRotationDegrees);
    material.stochasticTilingColorVariation =
      savedMaterial.value("tvc", material.stochasticTilingColorVariation);
    material.stochasticTilingMirror =
      savedMaterial.value("tvmr", material.stochasticTilingMirror);
    material.isGrass = savedMaterial.value("gr", material.isGrass);
    if (savedMaterial.contains("gc")) {
      for (int channel = 0; channel < 3; ++channel) {
        material.grassColor[channel] = savedMaterial["gc"][channel];
      }
    } else if (material.isGrass) {
      material.grassColor[0] = material.diffuseColor[0];
      material.grassColor[1] = material.diffuseColor[1];
      material.grassColor[2] = material.diffuseColor[2];
    }
    material.grassBladeSize =
        savedMaterial.value("gs", material.grassBladeSize);
    material.grassBladeCount =
        savedMaterial.value("gn", material.grassBladeCount);
    material.grassBladeVariation =
        savedMaterial.value("gv", material.grassBladeVariation);

    const auto restoreTextureIndex = [&](const char *key, int *field) {
      if (!field || !savedMaterial.contains(key)) {
        return;
      }
      const int textureIndex = savedMaterial[key];
      if (textureIndex < 0 ||
          textureIndex >= static_cast<int>(textures.size())) {
        *field = -1;
        return;
      }
      const Asset::Texture &texture =
          textures[static_cast<size_t>(textureIndex)];
      *field = (texture.resource && texture.width > 0 && texture.height > 0)
                   ? textureIndex
                   : -1;
    };

    restoreTextureIndex("txd", &material.diffuseTexture);
    restoreTextureIndex("txa", &material.opacityTexture);
    restoreTextureIndex("txn", &material.normalTexture);
    restoreTextureIndex("txcn", &material.coatNormalTexture);
    restoreTextureIndex("txe", &material.emissiveTexture);
    restoreTextureIndex("txo", &material.occlusionTexture);
    restoreTextureIndex("txm", &material.metalRoughTexture);
    restoreTextureIndex("txmt", &material.metalnessTexture);
    restoreTextureIndex("txrg", &material.roughnessGlossTexture);
    restoreTextureIndex("txsc", &material.specularColorTexture);
    restoreTextureIndex("txtk", &material.thicknessTexture);
    restoreTextureIndex("txp", &material.parallaxTexture);
    material.diffuseTextureAmount =
      savedMaterial.value("tad", material.diffuseTextureAmount);
    material.opacityTextureAmount =
      savedMaterial.value("taa", material.opacityTextureAmount);
    material.metalRoughTextureAmount =
      savedMaterial.value("tamr", material.metalRoughTextureAmount);
    material.metalnessTextureAmount =
      savedMaterial.value("tamt", material.metalnessTextureAmount);
    material.roughnessGlossTextureAmount =
      savedMaterial.value("targ", material.roughnessGlossTextureAmount);
    material.normalTextureAmount =
      savedMaterial.value("tan", material.normalTextureAmount);
    material.normalMapFlipY = savedMaterial.value(
        "nmfy", savedMaterial.value("nmgl", material.normalMapFlipY));
    material.useBumpMap = savedMaterial.value("bump", material.useBumpMap);
    material.coatNormalTextureAmount =
      savedMaterial.value("tacn", material.coatNormalTextureAmount);
    material.occlusionTextureAmount =
      savedMaterial.value("tao", material.occlusionTextureAmount);
    material.emissiveTextureAmount =
      savedMaterial.value("tae", material.emissiveTextureAmount);
    material.specularColorTextureAmount =
      savedMaterial.value("tasc", material.specularColorTextureAmount);
    material.thicknessTextureAmount =
      savedMaterial.value("tatk", material.thicknessTextureAmount);
    material.parallaxMode =
      savedMaterial.value("pm", material.parallaxMode);
    material.parallaxDepthScale =
      savedMaterial.value("tap", material.parallaxDepthScale);
    material.parallaxRoomDepth =
      savedMaterial.value("prd", material.parallaxRoomDepth);
    material.parallaxWindowAspect =
      savedMaterial.value("pwa", material.parallaxWindowAspect);
    if (savedMaterial.contains("pvs") && savedMaterial["pvs"].is_array()) {
      const auto &arr = savedMaterial["pvs"];
      if (arr.size() >= 2) {
        material.parallaxUvScale[0] =
          arr[0].get<float>();
        material.parallaxUvScale[1] =
          arr[1].get<float>();
      }
    }
    if (savedMaterial.contains("pvo") && savedMaterial["pvo"].is_array()) {
      const auto &arr = savedMaterial["pvo"];
      if (arr.size() >= 2) {
        material.parallaxUvOffset[0] =
          arr[0].get<float>();
        material.parallaxUvOffset[1] =
          arr[1].get<float>();
      }
    }

    material.parallaxBackFace =
      savedMaterial.value("pbf", material.parallaxBackFace);
    if (savedMaterial.contains("mp")) {
      RestoreMappingMetadata(savedMaterial["mp"], material);
    }
    material.runtimeMetalRoughTexture = -1;
    material.doubleSided = savedMaterial.value("ds", material.doubleSided);
    material.alphaMode = savedMaterial.value("am", material.alphaMode);
    material.alphaCutoff = savedMaterial.value("ac", material.alphaCutoff);
    if (savedSchema < Asset::Material::kSchemaVersionCoronaArchviz ||
        !savedMaterial.contains("mc")) {
      material.materialClass = InferMaterialClass(material);
    }
    material.schemaVersion = Asset::Material::kSchemaVersionCoronaArchviz;
  }
}

} // namespace MaterialIO
