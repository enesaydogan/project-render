#pragma once

#include "../assets/asset_loader.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace MaterialIO {

std::vector<int> BuildTextureSaveRemap(
    const std::vector<Asset::Texture> &textures,
    const std::vector<Asset::Material> &materials);

int MapSavedTextureIndex(const std::vector<int> &remap, int textureIndex);

nlohmann::json BuildMaterialsMetadata(
    const std::vector<Asset::Material> &materials,
    const std::vector<int> &textureSaveRemap);

void RestoreMaterialsFromMetadata(const nlohmann::json &materialsJson,
                                  std::vector<Asset::Material> *materials,
                                  const std::vector<Asset::Texture> &textures,
                                  std::vector<int> *remap);

} // namespace MaterialIO
