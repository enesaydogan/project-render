#pragma once

#include "../assets/asset_loader.h"
#include "../livelink/livelink_types.h"

#include <functional>
#include <string>

namespace MaterialLiveLink {

using ResolveTextureIndexFn =
    std::function<int(const std::string &textureBlobHash,
                      const std::string &textureUri)>;

void ApplyPayloadToMaterial(const LiveLink::MaterialChangedPayload &payload,
                            const ResolveTextureIndexFn &resolveTextureIndex,
                            Asset::Material *material);

} // namespace MaterialLiveLink