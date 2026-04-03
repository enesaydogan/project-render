#include "material_io.h"

#include <cstring>

namespace MaterialIO {

std::vector<int> BuildTextureSaveRemap(
    const std::vector<Asset::Texture> &textures) {
  std::vector<int> remap(textures.size(), -1);
  int nextIndex = 0;
  for (size_t textureIndex = 0; textureIndex < textures.size();
       ++textureIndex) {
    if (textures[textureIndex].hiddenInEditor) {
      continue;
    }
    remap[textureIndex] = nextIndex++;
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
        {"sv", mat.schemaVersion},
        {"bc",
         {mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2],
          mat.diffuseColor[3]}},
        {"mt", mat.metalness},
        {"rg", mat.roughness},
        {"sw", mat.specularWeight},
        {"tc",
         {mat.transmissionColor[0], mat.transmissionColor[1],
          mat.transmissionColor[2]}},
        {"tr", mat.transmissionWeight},
        {"io", mat.ior},
        {"ec",
         {mat.emissiveColor[0], mat.emissiveColor[1], mat.emissiveColor[2],
          mat.emissiveColor[3]}},
        {"ei", mat.emissiveIntensity},
        {"cw", mat.coatWeight},
        {"cr", mat.coatRoughness},
        {"th", mat.thinWalled},
        {"tl", mat.translucency},
        {"us", {mat.uvScale[0], mat.uvScale[1]}},
        {"uo", {mat.uvOffset[0], mat.uvOffset[1]}},
        {"te", mat.triPlanarEnabled},
        {"ts", mat.triPlanarScale},
        {"ths", mat.triPlanarSharpness},
        {"tns", mat.triPlanarNormalStrength},
        {"wf", mat.workflow},
        {"txd", MapSavedTextureIndex(textureSaveRemap, mat.diffuseTexture)},
        {"txn", MapSavedTextureIndex(textureSaveRemap, mat.normalTexture)},
        {"txe", MapSavedTextureIndex(textureSaveRemap, mat.emissiveTexture)},
        {"txo", MapSavedTextureIndex(textureSaveRemap, mat.occlusionTexture)},
        {"txm",
         MapSavedTextureIndex(textureSaveRemap, mat.metalRoughTexture)},
        {"txmt",
         MapSavedTextureIndex(textureSaveRemap, mat.metalnessTexture)},
        {"txrg",
         MapSavedTextureIndex(textureSaveRemap, mat.roughnessGlossTexture)},
        {"ds", mat.doubleSided},
        {"am", mat.alphaMode},
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

    int materialIndex = -1;
    for (size_t existingIndex = 0; existingIndex < materials->size();
         ++existingIndex) {
      if (std::string((*materials)[existingIndex].name) == name) {
        materialIndex = static_cast<int>(existingIndex);
        break;
      }
    }

    if (materialIndex == -1) {
      Asset::Material material;
      strncpy_s(material.name, name.c_str(), sizeof(material.name) - 1);
      materials->push_back(material);
      materialIndex = static_cast<int>(materials->size()) - 1;
    }

    (*remap)[jsonIndex] = materialIndex;
    auto &material = (*materials)[static_cast<size_t>(materialIndex)];

    material.schemaVersion = savedMaterial.value(
        "sv", Asset::Material::kSchemaVersionOpenPbrSubset);
    if (savedMaterial.contains("bc")) {
      for (int channel = 0; channel < 4; ++channel) {
        material.diffuseColor[channel] = savedMaterial["bc"][channel];
      }
    }
    material.metalness = savedMaterial.value("mt", material.metalness);
    material.roughness = savedMaterial.value("rg", material.roughness);
    material.specularWeight =
        savedMaterial.value("sw", material.specularWeight);
    if (savedMaterial.contains("tc")) {
      for (int channel = 0; channel < 3; ++channel) {
        material.transmissionColor[channel] = savedMaterial["tc"][channel];
      }
    }
    material.transmissionWeight =
        savedMaterial.value("tr", material.transmissionWeight);
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
    material.triPlanarEnabled =
        savedMaterial.value("te", material.triPlanarEnabled);
    material.triPlanarScale =
        savedMaterial.value("ts", material.triPlanarScale);
    material.triPlanarSharpness =
        savedMaterial.value("ths", material.triPlanarSharpness);
    material.triPlanarNormalStrength =
        savedMaterial.value("tns", material.triPlanarNormalStrength);
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
      *field = (textureIndex >= 0 &&
                textureIndex < static_cast<int>(textures.size()))
                   ? textureIndex
                   : -1;
    };

    restoreTextureIndex("txd", &material.diffuseTexture);
    restoreTextureIndex("txn", &material.normalTexture);
    restoreTextureIndex("txe", &material.emissiveTexture);
    restoreTextureIndex("txo", &material.occlusionTexture);
    restoreTextureIndex("txm", &material.metalRoughTexture);
    restoreTextureIndex("txmt", &material.metalnessTexture);
    restoreTextureIndex("txrg", &material.roughnessGlossTexture);

    material.runtimeMetalRoughTexture = -1;
    material.doubleSided = savedMaterial.value("ds", material.doubleSided);
    material.alphaMode = savedMaterial.value("am", material.alphaMode);
    material.schemaVersion = Asset::Material::kSchemaVersionOpenPbrSubset;
  }
}

} // namespace MaterialIO