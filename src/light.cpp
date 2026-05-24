#include "light.h"
#include "assets/asset_loader.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace DX12Context;

// Externals from main.cpp
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;

void FlattenLights(const std::vector<LightPrototype> &prototypes,
                   const std::vector<LightInstance> &instances,
                   std::vector<Light> &outLights,
                   std::vector<std::pair<size_t, size_t>> &outMapping) {
  outLights.clear();
  outMapping.clear();

  for (size_t instIdx = 0; instIdx < instances.size(); ++instIdx) {
    const auto &inst = instances[instIdx];
    if (!inst.enabled)
      continue;
    if (inst.prototypeIndex >= prototypes.size())
      continue;

    const auto &proto = prototypes[inst.prototypeIndex];
    if (!proto.enabled)
      continue;

    Light l = {};
    l.type = proto.type;
    l.position[0] = inst.position[0];
    l.position[1] = inst.position[1];
    l.position[2] = inst.position[2];
    l.emission[0] = proto.color[0] * proto.intensity;
    l.emission[1] = proto.color[1] * proto.intensity;
    l.emission[2] = proto.color[2] * proto.intensity;
    l.direction[0] = inst.direction[0];
    l.direction[1] = inst.direction[1];
    l.direction[2] = inst.direction[2];
    l.radius = proto.radius;
    l.innerConeAngle = proto.innerConeAngle;
    l.outerConeAngle = proto.outerConeAngle;
    l.areaExtents[0] = proto.areaExtents[0];
    l.areaExtents[1] = proto.areaExtents[1];
    l.iesAtlasIndex = proto.iesAtlasIndex;

    outMapping.push_back({inst.prototypeIndex, instIdx});
    outLights.push_back(l);
  }
}
