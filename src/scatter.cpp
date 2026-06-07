// scatter.cpp — implementation of the scatter system declared in scatter.h.
// Extracted from scene.cpp on 2026-06-07. Owns scatter-specific state and
// only reaches into scene.cpp via the small surface in scene_internal.h.

#include "scatter.h"

#include "assets/asset_loader.h"
#include "camera.h"
#include "scene.h"
#include "scene_internal.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Mesh library lives in main.cpp / scene.cpp; we read it directly the same
// way scene.cpp does.
extern std::vector<Asset::GpuMesh> g_loadedMeshes;

namespace Scene {

namespace {

// ---- Module state --------------------------------------------------------
std::vector<ScatterModel> s_scatterModels;
std::vector<Instance> s_scatterInstanceCache;
uint64_t s_scatterRuntimeRevision = 1;
uint64_t s_scatterInstanceCacheRevision = 0;
ScatterRuntimeStats s_scatterRuntimeStats;
size_t s_scatterPickTargetIndex = static_cast<size_t>(-1);
// Cache invalidation for camera-distance culling (A1). When any object uses
// min/max distance, moving the camera must invalidate the cache. Without
// this the cache key was just s_scatterRuntimeRevision, so distance-faded
// scatter froze on cache build and snapped on the next authoring edit.
float s_scatterCacheCameraPos[3] = {0.0f, 0.0f, 0.0f};
bool s_scatterCacheUsesCameraDistance = false;

// A7: targets whose mesh has no CPU vertex copy produce zero triangles
// silently. Log once per (model, target) pair so the user sees the cause
// without spamming the console every frame.
std::unordered_set<uint64_t> s_warnedMissingCpuMesh;
uint64_t MakeMissingCpuMeshKey(size_t modelIndex, size_t targetIndex) {
  return (static_cast<uint64_t>(modelIndex) << 32) |
         static_cast<uint32_t>(targetIndex);
}

void MarkScatterRuntimeChanged(
    RendererInvalidationPlan plan =
        RendererInvalidationPlan::FullAccelerationStructureRebuild) {
  ++s_scatterRuntimeRevision;
  s_scatterInstanceCacheRevision = 0;
  ApplyRendererInvalidation(plan);
  NotifySceneChanged();
}

void PopulateScatterTargetMetadata(ScatterTarget &target) {
  const auto &nodes = GetNodes();
  if (target.nodeIndex >= nodes.size()) {
    return;
  }
  const Node &node = nodes[target.nodeIndex];
  target.nodeName = node.name;
  target.sourcePath = node.sourcePath;
  target.importGroupKey = node.importGroupKey;
  if (target.meshIndex < g_loadedMeshes.size()) {
    const Asset::GpuMesh &mesh = g_loadedMeshes[target.meshIndex];
    target.materialIndex = mesh.materialIndex;
    target.materialSlot = mesh.materialSlot;
  }
}

// ---- Hash / noise helpers (unchanged from scene.cpp original) ------------

uint32_t ScatterHashU32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float ScatterHash01(uint32_t x) {
  return static_cast<float>(ScatterHashU32(x) & 0x00ffffffu) / 16777215.0f;
}

float ScatterSmoothStep(float x) {
  x = (std::clamp)(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

float ScatterValueNoise2D(float x, float y, uint32_t seed) {
  const int ix = static_cast<int>(std::floor(x));
  const int iy = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(ix);
  const float fy = y - static_cast<float>(iy);
  const float sx = ScatterSmoothStep(fx);
  const float sy = ScatterSmoothStep(fy);
  auto cell = [seed](int cx, int cy) {
    const uint32_t hx = static_cast<uint32_t>(cx) * 0x8da6b343u;
    const uint32_t hy = static_cast<uint32_t>(cy) * 0xd8163841u;
    return ScatterHash01(seed ^ hx ^ hy);
  };
  const float v00 = cell(ix, iy);
  const float v10 = cell(ix + 1, iy);
  const float v01 = cell(ix, iy + 1);
  const float v11 = cell(ix + 1, iy + 1);
  const float vx0 = v00 + (v10 - v00) * sx;
  const float vx1 = v01 + (v11 - v01) * sx;
  return vx0 + (vx1 - vx0) * sy;
}

float ScatterFractalNoise2D(float x, float y, uint32_t seed) {
  float sum = 0.0f;
  float amp = 0.5f;
  float scale = 1.0f;
  float norm = 0.0f;
  for (int octave = 0; octave < 3; ++octave) {
    sum += ScatterValueNoise2D(x * scale, y * scale,
                               seed + static_cast<uint32_t>(octave) *
                                          0x9e3779b9u) *
           amp;
    norm += amp;
    scale *= 2.03f;
    amp *= 0.5f;
  }
  return norm > 0.0f ? sum / norm : 0.0f;
}

// ---- Collision avoidance spatial hash ------------------------------------

struct ScatterCollisionCell {
  int x = 0;
  int y = 0;
  int z = 0;
  bool operator==(const ScatterCollisionCell &rhs) const {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }
};
struct ScatterCollisionCellHash {
  size_t operator()(const ScatterCollisionCell &cell) const {
    uint32_t h = ScatterHashU32(static_cast<uint32_t>(cell.x));
    h ^= ScatterHashU32(static_cast<uint32_t>(cell.y) + 0x9e3779b9u);
    h ^= ScatterHashU32(static_cast<uint32_t>(cell.z) + 0x85ebca6bu);
    return static_cast<size_t>(h);
  }
};
ScatterCollisionCell ScatterCollisionCellFor(const DirectX::XMFLOAT3 &position,
                                             float cellSize) {
  return {static_cast<int>(std::floor(position.x / cellSize)),
          static_cast<int>(std::floor(position.y / cellSize)),
          static_cast<int>(std::floor(position.z / cellSize))};
}

DirectX::XMVECTOR ScatterSafeNormalize(DirectX::XMVECTOR v,
                                       DirectX::XMVECTOR fallback) {
  if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v)) < 1e-10f) {
    return fallback;
  }
  return DirectX::XMVector3Normalize(v);
}

DirectX::XMMATRIX MakeScatterTransform(const DirectX::XMFLOAT3 &position,
                                       const DirectX::XMFLOAT3 &normal,
                                       const ScatterObject &object,
                                       float yawRadians, float pitchRadians,
                                       float rollRadians, float uniformScale) {
  const DirectX::XMVECTOR worldUp =
      DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  DirectX::XMVECTOR surfaceUp =
      ScatterSafeNormalize(DirectX::XMLoadFloat3(&normal), worldUp);
  DirectX::XMVECTOR up = DirectX::XMVector3Normalize(DirectX::XMVectorLerp(
      worldUp, surfaceUp, (std::clamp)(object.normalAlign, 0.0f, 1.0f)));

  DirectX::XMVECTOR helper =
      (std::fabs(DirectX::XMVectorGetY(up)) > 0.92f)
          ? DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
          : worldUp;
  DirectX::XMVECTOR right = ScatterSafeNormalize(
      DirectX::XMVector3Cross(helper, up),
      DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
  DirectX::XMVECTOR forward = ScatterSafeNormalize(
      DirectX::XMVector3Cross(up, right),
      DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

  DirectX::XMMATRIX basis(right, up, forward,
                          DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
  DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationRollPitchYaw(
      pitchRadians, yawRadians, rollRadians);
  DirectX::XMMATRIX local = DirectX::XMMatrixMultiply(rotation, basis);

  DirectX::XMFLOAT4X4 m = {};
  DirectX::XMStoreFloat4x4(&m, local);
  m._11 *= uniformScale; m._12 *= uniformScale; m._13 *= uniformScale;
  m._21 *= uniformScale; m._22 *= uniformScale; m._23 *= uniformScale;
  m._31 *= uniformScale; m._32 *= uniformScale; m._33 *= uniformScale;
  m._41 = position.x; m._42 = position.y; m._43 = position.z; m._44 = 1.0f;
  return DirectX::XMLoadFloat4x4(&m);
}

struct ScatterTriangle {
  DirectX::XMFLOAT3 p0;
  DirectX::XMFLOAT3 p1;
  DirectX::XMFLOAT3 p2;
  DirectX::XMFLOAT3 n0;
  DirectX::XMFLOAT3 n1;
  DirectX::XMFLOAT3 n2;
  float weight = 0.0f;
};

void GatherScatterTriangles(const ScatterTarget &target,
                            size_t modelIndex, size_t targetIndex,
                            const float *nodeWorld,
                            std::vector<ScatterTriangle> &triangles,
                            float &weightedArea) {
  const auto &nodes = GetNodes();
  if (!target.enabled || target.nodeIndex >= nodes.size() ||
      target.meshIndex >= g_loadedMeshes.size()) {
    return;
  }
  if (!g_loadedMeshes[target.meshIndex].cpuVertices.size() ||
      g_loadedMeshes[target.meshIndex].cpuIndices.size() < 3) {
    // A7: this is the silent-failure case the user kept hitting after the
    // texture-compression CPU-evict pass started dropping cpuVertices for
    // already-uploaded meshes. Warn once per (model, target) pair.
    const uint64_t key = MakeMissingCpuMeshKey(modelIndex, targetIndex);
    if (s_warnedMissingCpuMesh.insert(key).second) {
      fprintf(stderr,
              "Scatter: target %zu/%zu on node '%s' references mesh %zu "
              "which has no CPU vertex copy; scatter will see zero area for "
              "this target. (Skip the CPU-evict pass for scatter source "
              "meshes, or re-import the asset.)\n",
              modelIndex, targetIndex,
              target.nodeName.empty() ? "?" : target.nodeName.c_str(),
              target.meshIndex);
    }
    return;
  }

  const Asset::GpuMesh &mesh = g_loadedMeshes[target.meshIndex];
  DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(
      reinterpret_cast<const DirectX::XMFLOAT4X4 *>(nodeWorld));
  const DirectX::XMVECTOR worldUp =
      DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

  for (size_t tri = 0; tri + 2 < mesh.cpuIndices.size(); tri += 3) {
    const uint32_t i0 = mesh.cpuIndices[tri + 0];
    const uint32_t i1 = mesh.cpuIndices[tri + 1];
    const uint32_t i2 = mesh.cpuIndices[tri + 2];
    if (i0 >= mesh.cpuVertices.size() || i1 >= mesh.cpuVertices.size() ||
        i2 >= mesh.cpuVertices.size()) {
      continue;
    }

    const Asset::Vertex &v0 = mesh.cpuVertices[i0];
    const Asset::Vertex &v1 = mesh.cpuVertices[i1];
    const Asset::Vertex &v2 = mesh.cpuVertices[i2];
    const DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat3(
            reinterpret_cast<const DirectX::XMFLOAT3 *>(v0.pos)),
        world);
    const DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat3(
            reinterpret_cast<const DirectX::XMFLOAT3 *>(v1.pos)),
        world);
    const DirectX::XMVECTOR p2 = DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat3(
            reinterpret_cast<const DirectX::XMFLOAT3 *>(v2.pos)),
        world);
    const DirectX::XMVECTOR e0 = DirectX::XMVectorSubtract(p1, p0);
    const DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p2, p0);
    const DirectX::XMVECTOR faceNormal = DirectX::XMVector3Cross(e0, e1);
    const float area =
        0.5f * DirectX::XMVectorGetX(DirectX::XMVector3Length(faceNormal));
    if (area <= 1e-6f) {
      continue;
    }
    const DirectX::XMVECTOR normal =
        DirectX::XMVector3Normalize(faceNormal);
    const float upDot = (std::max)(
        -1.0f, (std::min)(1.0f, DirectX::XMVectorGetX(
                                    DirectX::XMVector3Dot(normal, worldUp))));
    const float slopeDegrees = std::acos(upDot) * 57.2957795f;

    ScatterTriangle out = {};
    DirectX::XMStoreFloat3(&out.p0, p0);
    DirectX::XMStoreFloat3(&out.p1, p1);
    DirectX::XMStoreFloat3(&out.p2, p2);
    DirectX::XMStoreFloat3(&out.n0, normal);
    DirectX::XMStoreFloat3(&out.n1, normal);
    DirectX::XMStoreFloat3(&out.n2, normal);
    out.weight = area * (std::max)(target.weight, 0.0f);
    if (out.weight <= 1e-6f || slopeDegrees > 89.5f) {
      continue;
    }
    weightedArea += out.weight;
    triangles.push_back(out);
  }
}

void AppendScatterInstancesForObject(
    const ScatterModel &model, size_t modelIndex, const ScatterObject &object,
    size_t objectIndex, const std::vector<ScatterTriangle> &triangles,
    const std::vector<float> &triangleWeightCdf, float weightedArea,
    uint32_t &remainingModelBudget,
    std::vector<Instance> &outInstances) {
  if (!model.enabled || !object.enabled || object.meshIndices.empty() ||
      triangles.empty() || weightedArea <= 1e-6f ||
      object.densityPerSquareMeter <= 0.0f || object.weight <= 0.0f) {
    return;
  }
  if (remainingModelBudget == 0) {
    return;
  }

  // A3: clamp before float->uint32 to avoid wrap on huge terrains
  // (100 km² * 10/m² overflows UINT32_MAX). Pre-cast clamp protects against
  // both signed values from upstream multiplication errors and the silent
  // wrap that produced "scatter went away when I cranked density."
  const float rawDesired =
      weightedArea * object.densityPerSquareMeter * object.weight *
      (std::clamp)(model.previewDensityScale, 0.0f, 1.0f);
  const float desired =
      (std::clamp)(std::round(rawDesired), 0.0f,
                   static_cast<float>((std::numeric_limits<uint32_t>::max)()));
  // A2: track per-cap overflow so the panel can distinguish "raise model
  // budget" from "raise per-object cap."
  uint32_t requested = static_cast<uint32_t>(desired);
  uint32_t instanceCount = requested;
  const uint32_t objectHardCap = (object.previewMaxInstances > 0)
                                     ? (std::min)(object.maxInstances,
                                                  object.previewMaxInstances)
                                     : object.maxInstances;
  uint32_t skippedByObjectCap = 0;
  if (instanceCount > objectHardCap) {
    skippedByObjectCap = instanceCount - objectHardCap;
    instanceCount = objectHardCap;
  }
  s_scatterRuntimeStats.skippedByObjectCap += skippedByObjectCap;
  uint32_t skippedByModelBudget = 0;
  if (instanceCount > remainingModelBudget) {
    skippedByModelBudget = instanceCount - remainingModelBudget;
    s_scatterRuntimeStats.skippedByBudget += skippedByModelBudget;
    instanceCount = remainingModelBudget;
  }
  if (instanceCount == 0) {
    return;
  }
  ++s_scatterRuntimeStats.activeObjects;
  // Per-object stats slot. instancesGenerated is filled in at the end of
  // this function from the actual emitted count (which can differ when
  // collision/edge/slope rejects drop instances).
  ScatterObjectStats perObjectStats = {};
  perObjectStats.modelIndex = modelIndex;
  perObjectStats.objectIndex = objectIndex;
  perObjectStats.modelName = model.name;
  perObjectStats.objectName = object.name;
  perObjectStats.skippedByObjectCap = skippedByObjectCap;
  const uint32_t generatedBeforeObject =
      s_scatterRuntimeStats.generatedInstances;

  const float twoPi = 6.283185307179586f;
  const float yawRange =
      (std::max)(0.0f, object.randomYawDegrees) * (twoPi / 360.0f);
  const float pitchRange =
      (std::max)(0.0f, object.randomPitchDegrees) * (twoPi / 360.0f);
  const float rollRange =
      (std::max)(0.0f, object.randomRollDegrees) * (twoPi / 360.0f);
  const float minScale =
      (std::max)(0.001f, (std::min)(object.minScale, object.maxScale));
  const float maxScale =
      (std::max)(minScale, (std::max)(object.minScale, object.maxScale));
  const float minDistance2 =
      (std::max)(0.0f, object.minDistance) *
      (std::max)(0.0f, object.minDistance);
  const float maxDistance2 =
      (std::max)(0.0f, object.maxDistance) *
      (std::max)(0.0f, object.maxDistance);
  const float clumpScale = (std::max)(0.0f, object.clumpScale);
  const float clumpStrength =
      (std::clamp)(object.clumpStrength, 0.0f, 1.0f);
  const float edgeAvoidance =
      (std::clamp)(object.edgeAvoidance, 0.0f, 0.33f);
  const float collisionRadius =
      (std::max)(0.0f, object.collisionAvoidanceRadius);
  const float collisionRadius2 = collisionRadius * collisionRadius;
  const float cameraX = g_cameraData.pos[0];
  const float cameraY = g_cameraData.pos[1];
  const float cameraZ = g_cameraData.pos[2];
  std::unordered_map<ScatterCollisionCell, std::vector<DirectX::XMFLOAT3>,
                     ScatterCollisionCellHash>
      acceptedCollisionPoints;
  auto collidesWithAcceptedPoint = [&](const DirectX::XMFLOAT3 &position) {
    if (collisionRadius <= 1e-4f) {
      return false;
    }
    const ScatterCollisionCell cell =
        ScatterCollisionCellFor(position, collisionRadius);
    for (int dz = -1; dz <= 1; ++dz) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const ScatterCollisionCell neighbor{cell.x + dx, cell.y + dy,
                                              cell.z + dz};
          const auto it = acceptedCollisionPoints.find(neighbor);
          if (it == acceptedCollisionPoints.end()) {
            continue;
          }
          for (const DirectX::XMFLOAT3 &accepted : it->second) {
            const float diffX = position.x - accepted.x;
            const float diffY = position.y - accepted.y;
            const float diffZ = position.z - accepted.z;
            const float dist2 =
                diffX * diffX + diffY * diffY + diffZ * diffZ;
            if (dist2 < collisionRadius2) {
              return true;
            }
          }
        }
      }
    }
    return false;
  };
  auto addAcceptedCollisionPoint = [&](const DirectX::XMFLOAT3 &position) {
    if (collisionRadius <= 1e-4f) {
      return;
    }
    acceptedCollisionPoints[ScatterCollisionCellFor(position, collisionRadius)]
        .push_back(position);
  };

  for (uint32_t instanceIndex = 0;
       instanceIndex < instanceCount && remainingModelBudget > 0;
       ++instanceIndex) {
    const uint32_t baseSeed =
        model.seed * 0x9e3779b9u ^
        static_cast<uint32_t>(modelIndex + 1) * 0x85ebca6bu ^
        static_cast<uint32_t>(objectIndex + 1) * 0xc2b2ae35u ^
        instanceIndex * 0x27d4eb2du;

    // A4: binary-search the prefix-sum CDF instead of linear walking the
    // triangle list per instance. Was O(N) per instance, ~5 B ops at 100k
    // triangles * 50k instances; now O(log N). CDF is built once per model
    // at the AppendScatterInstances level and passed down.
    const float triPick = ScatterHash01(baseSeed ^ 0x165667b1u) * weightedArea;
    auto cdfIt = std::upper_bound(triangleWeightCdf.begin(),
                                  triangleWeightCdf.end(), triPick);
    const size_t triIdx =
        (cdfIt == triangleWeightCdf.end())
            ? triangles.size() - 1
            : static_cast<size_t>(cdfIt - triangleWeightCdf.begin());
    const ScatterTriangle *chosen = &triangles[triIdx];

    const float u = ScatterHash01(baseSeed ^ 0x9f123bb5u);
    const float v = ScatterHash01(baseSeed ^ 0x4f1bbcdcu);
    const float su = std::sqrt(u);
    const float b0 = 1.0f - su;
    const float b1 = su * (1.0f - v);
    const float b2 = su * v;
    if (edgeAvoidance > 0.0f &&
        (std::min)(b0, (std::min)(b1, b2)) < edgeAvoidance) {
      continue;
    }
    DirectX::XMFLOAT3 position = {
        chosen->p0.x * b0 + chosen->p1.x * b1 + chosen->p2.x * b2,
        chosen->p0.y * b0 + chosen->p1.y * b1 + chosen->p2.y * b2,
        chosen->p0.z * b0 + chosen->p1.z * b1 + chosen->p2.z * b2};
    DirectX::XMFLOAT3 normal = {
        chosen->n0.x * b0 + chosen->n1.x * b1 + chosen->n2.x * b2,
        chosen->n0.y * b0 + chosen->n1.y * b1 + chosen->n2.y * b2,
        chosen->n0.z * b0 + chosen->n1.z * b1 + chosen->n2.z * b2};

    if (position.y < object.heightMin || position.y > object.heightMax) {
      continue;
    }
    const float cameraDx = position.x - cameraX;
    const float cameraDy = position.y - cameraY;
    const float cameraDz = position.z - cameraZ;
    const float cameraDist2 =
        cameraDx * cameraDx + cameraDy * cameraDy + cameraDz * cameraDz;
    if (minDistance2 > 0.0f && cameraDist2 < minDistance2) {
      continue;
    }
    if (maxDistance2 > 0.0f && cameraDist2 > maxDistance2) {
      continue;
    }
    if (clumpScale > 1e-4f && clumpStrength > 0.0f) {
      const float noise = ScatterFractalNoise2D(
          position.x / clumpScale, position.z / clumpScale,
          model.seed ^ static_cast<uint32_t>(objectIndex + 17) * 0x45d9f3bu);
      const float clustered = ScatterSmoothStep((noise - 0.35f) / 0.45f);
      const float acceptProbability =
          1.0f + (clustered - 1.0f) * clumpStrength;
      if (ScatterHash01(baseSeed ^ 0x6d2b79f5u) > acceptProbability) {
        continue;
      }
    }
    if (collidesWithAcceptedPoint(position)) {
      continue;
    }
    DirectX::XMVECTOR n = ScatterSafeNormalize(
        DirectX::XMLoadFloat3(&normal),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    DirectX::XMStoreFloat3(&normal, n);
    const float upDot = (std::clamp)(normal.y, -1.0f, 1.0f);
    const float slopeDegrees = std::acos(upDot) * 57.2957795f;
    if (slopeDegrees < object.slopeMinDegrees ||
        slopeDegrees > object.slopeMaxDegrees) {
      continue;
    }

    if (object.jitterMeters > 0.0f) {
      position.x +=
          (ScatterHash01(baseSeed ^ 0xdd4f5a11u) * 2.0f - 1.0f) *
          object.jitterMeters;
      position.z +=
          (ScatterHash01(baseSeed ^ 0xabc98388u) * 2.0f - 1.0f) *
          object.jitterMeters;
    }

    const float scale =
        minScale + (maxScale - minScale) * ScatterHash01(baseSeed ^ 0x632be59bu);
    const float yaw =
        (ScatterHash01(baseSeed ^ 0x85157af5u) - 0.5f) * yawRange;
    const float pitch =
        (ScatterHash01(baseSeed ^ 0x94d049bbu) - 0.5f) * pitchRange;
    const float roll =
        (ScatterHash01(baseSeed ^ 0xbf58476du) - 0.5f) * rollRange;
    const DirectX::XMMATRIX transform =
        MakeScatterTransform(position, normal, object, yaw, pitch, roll, scale);

    bool emittedPlacement = false;
    for (size_t objectMeshOffset = 0;
         objectMeshOffset < object.meshIndices.size(); ++objectMeshOffset) {
      if (remainingModelBudget == 0) {
        break;
      }
      const size_t meshIndex = object.meshIndices[objectMeshOffset];
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
      if (!mesh.vertexBuffer || !mesh.indexBuffer || mesh.vertexCount == 0 ||
          mesh.indexCount == 0) {
        continue;
      }
      Instance inst;
      inst.name = model.name + " / " + object.name;
      if (objectMeshOffset < object.meshLocalTransforms.size()) {
        DirectX::XMMATRIX local = DirectX::XMLoadFloat4x4(
            reinterpret_cast<const DirectX::XMFLOAT4X4 *>(
                object.meshLocalTransforms[objectMeshOffset].data()));
        inst.transform = DirectX::XMMatrixMultiply(local, transform);
      } else {
        inst.transform = transform;
      }
      inst.id = -100000 - static_cast<int>(modelIndex);
      inst.mesh = &mesh;
      outInstances.push_back(std::move(inst));
      ++s_scatterRuntimeStats.generatedInstances;
      --remainingModelBudget;
      emittedPlacement = true;
    }
    if (emittedPlacement) {
      addAcceptedCollisionPoint(position);
    }
  }

  perObjectStats.instancesGenerated =
      s_scatterRuntimeStats.generatedInstances - generatedBeforeObject;
  if (perObjectStats.instancesGenerated > 0 ||
      perObjectStats.skippedByObjectCap > 0) {
    s_scatterRuntimeStats.perObject.push_back(std::move(perObjectStats));
  }
}

} // namespace

// ==========================================================================
// Public API
// ==========================================================================

const std::vector<ScatterModel> &GetScatterModels() { return s_scatterModels; }

void SetScatterModels(std::vector<ScatterModel> models) {
  s_scatterModels = std::move(models);
  MarkScatterRuntimeChanged();
}

size_t AddScatterModel(const std::string &name) {
  ScatterModel model;
  model.name = name;
  model.seed = static_cast<uint32_t>(s_scatterModels.size() + 1);
  s_scatterModels.push_back(std::move(model));
  MarkScatterRuntimeChanged();
  return s_scatterModels.size() - 1;
}

bool RemoveScatterModel(size_t index) {
  if (index >= s_scatterModels.size()) {
    return false;
  }
  s_scatterModels.erase(s_scatterModels.begin() + index);
  MarkScatterRuntimeChanged();
  return true;
}

bool UpdateScatterModel(size_t index, const ScatterModel &model) {
  if (index >= s_scatterModels.size()) {
    return false;
  }
  s_scatterModels[index] = model;
  MarkScatterRuntimeChanged();
  return true;
}

bool UpdateScatterModelHeader(size_t index, const ScatterModel &header) {
  if (index >= s_scatterModels.size()) {
    return false;
  }
  ScatterModel &model = s_scatterModels[index];
  model.name = header.name;
  model.seed = header.seed;
  model.enabled = header.enabled;
  model.previewDensityScale = header.previewDensityScale;
  model.previewInstanceBudget = header.previewInstanceBudget;
  // Header doesn't touch mesh set membership; TlasRefresh is enough.
  MarkScatterRuntimeChanged(RendererInvalidationPlan::TlasRefresh);
  return true;
}

bool UpdateScatterTarget(size_t modelIndex, size_t targetIndex,
                         const ScatterTarget &target) {
  if (modelIndex >= s_scatterModels.size() ||
      targetIndex >= s_scatterModels[modelIndex].targets.size()) {
    return false;
  }
  ScatterTarget &existing =
      s_scatterModels[modelIndex].targets[targetIndex];
  const bool meshBindingChanged = existing.nodeIndex != target.nodeIndex ||
                                  existing.meshIndex != target.meshIndex;
  existing = target;
  MarkScatterRuntimeChanged(
      meshBindingChanged ? RendererInvalidationPlan::FullAccelerationStructureRebuild
                         : RendererInvalidationPlan::TlasRefresh);
  return true;
}

bool UpdateScatterObject(size_t modelIndex, size_t objectIndex,
                         const ScatterObject &object) {
  if (modelIndex >= s_scatterModels.size() ||
      objectIndex >= s_scatterModels[modelIndex].objects.size()) {
    return false;
  }
  ScatterObject &existing = s_scatterModels[modelIndex].objects[objectIndex];
  const bool meshSetChanged = existing.meshIndices != object.meshIndices;
  existing = object;
  MarkScatterRuntimeChanged(
      meshSetChanged ? RendererInvalidationPlan::FullAccelerationStructureRebuild
                     : RendererInvalidationPlan::TlasRefresh);
  return true;
}

bool AddSelectedNodesAsScatterTargets(size_t scatterIndex) {
  if (scatterIndex >= s_scatterModels.size()) {
    return false;
  }
  ScatterModel &model = s_scatterModels[scatterIndex];
  const auto &nodes = GetNodes();
  bool added = false;
  for (size_t nodeIndex : GetSelectedNodeIndices()) {
    if (nodeIndex >= nodes.size()) {
      continue;
    }
    std::vector<size_t> candidateNodes;
    candidateNodes.push_back(nodeIndex);
    for (size_t childIndex = 0; childIndex < nodes.size(); ++childIndex) {
      if (childIndex != nodeIndex &&
          IsNodeDescendantOf(childIndex, nodeIndex)) {
        candidateNodes.push_back(childIndex);
      }
    }
    for (size_t targetNodeIndex : candidateNodes) {
      if (targetNodeIndex >= nodes.size()) {
        continue;
      }
      for (size_t meshIndex : nodes[targetNodeIndex].meshIndices) {
        if (meshIndex >= g_loadedMeshes.size()) {
          continue;
        }
        auto exists = std::find_if(
            model.targets.begin(), model.targets.end(),
            [targetNodeIndex, meshIndex](const ScatterTarget &target) {
              return target.nodeIndex == targetNodeIndex &&
                     target.meshIndex == meshIndex;
            });
        if (exists != model.targets.end()) {
          continue;
        }
        ScatterTarget target;
        target.nodeIndex = targetNodeIndex;
        target.meshIndex = meshIndex;
        PopulateScatterTargetMetadata(target);
        model.targets.push_back(target);
        added = true;
      }
    }
  }
  if (added) {
    MarkScatterRuntimeChanged();
  }
  return added;
}

bool AddSelectedNodesAsScatterObjects(size_t scatterIndex) {
  if (scatterIndex >= s_scatterModels.size()) {
    return false;
  }
  ScatterModel &model = s_scatterModels[scatterIndex];
  const auto &nodes = GetNodes();
  bool added = false;
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();
  for (size_t nodeIndex : GetSelectedNodeIndices()) {
    if (nodeIndex >= nodes.size()) {
      continue;
    }
    float invRoot[16];
    if (nodeIndex >= worldTransforms.size() ||
        !Inverse4x4(worldTransforms[nodeIndex].data(), invRoot)) {
      continue;
    }

    std::vector<std::pair<size_t, std::array<float, 16>>> meshEntries;
    auto appendNodeMeshes = [&](size_t sourceNodeIndex) {
      if (sourceNodeIndex >= nodes.size() ||
          sourceNodeIndex >= worldTransforms.size()) {
        return;
      }
      std::array<float, 16> localTransform = {};
      MulColumnMajor4x4(invRoot, worldTransforms[sourceNodeIndex].data(),
                        localTransform.data());
      for (size_t meshIndex : nodes[sourceNodeIndex].meshIndices) {
        if (meshIndex < g_loadedMeshes.size()) {
          meshEntries.push_back({meshIndex, localTransform});
        }
      }
    };

    appendNodeMeshes(nodeIndex);
    for (size_t childIndex = 0; childIndex < nodes.size(); ++childIndex) {
      if (childIndex == nodeIndex ||
          !IsNodeDescendantOf(childIndex, nodeIndex)) {
        continue;
      }
      appendNodeMeshes(childIndex);
    }
    if (meshEntries.empty()) {
      continue;
    }
    ScatterObject object;
    object.name = nodes[nodeIndex].name.empty() ? "Scatter Object"
                                                : nodes[nodeIndex].name;
    object.sourceNodeName = nodes[nodeIndex].name;
    object.sourcePath = nodes[nodeIndex].sourcePath;
    object.meshIndices.reserve(meshEntries.size());
    object.meshLocalTransforms.reserve(meshEntries.size());
    for (const auto &entry : meshEntries) {
      object.meshIndices.push_back(entry.first);
      object.meshLocalTransforms.push_back(entry.second);
    }
    object.densityPerSquareMeter = 8.0f;
    object.maxInstances = 12000;
    model.objects.push_back(std::move(object));
    added = true;
  }
  if (added) {
    MarkScatterRuntimeChanged();
  }
  return added;
}

uint64_t GetScatterRuntimeRevision() { return s_scatterRuntimeRevision; }

ScatterRuntimeStats GetScatterRuntimeStats() { return s_scatterRuntimeStats; }

void SetScatterPickTarget(size_t scatterIndex) {
  s_scatterPickTargetIndex = scatterIndex < s_scatterModels.size()
                                 ? scatterIndex
                                 : static_cast<size_t>(-1);
}

bool IsScatterPickingTarget() {
  return s_scatterPickTargetIndex < s_scatterModels.size();
}

void CancelScatterPick() {
  s_scatterPickTargetIndex = static_cast<size_t>(-1);
}

bool SetScatterObjectSourcesHidden(size_t scatterIndex, size_t objectIndex,
                                   bool hidden) {
  if (scatterIndex >= s_scatterModels.size() ||
      objectIndex >= s_scatterModels[scatterIndex].objects.size()) {
    return false;
  }
  ScatterObject &object = s_scatterModels[scatterIndex].objects[objectIndex];
  object.librarySourceHidden = hidden;
  bool changed = false;
  std::vector<Node> &nodes = GetMutableNodes();
  for (Node &node : nodes) {
    bool ownsMesh = false;
    for (size_t nodeMeshIndex : node.meshIndices) {
      if (std::find(object.meshIndices.begin(), object.meshIndices.end(),
                    nodeMeshIndex) != object.meshIndices.end()) {
        ownsMesh = true;
        break;
      }
    }
    if (!ownsMesh || node.visible == !hidden) {
      continue;
    }
    node.visible = !hidden;
    changed = true;
  }
  if (changed) {
    MarkScatterRuntimeChanged(RendererInvalidationPlan::TlasRefresh);
  } else {
    MarkScatterRuntimeChanged(RendererInvalidationPlan::AccumulationOnly);
  }
  return true;
}

bool RemoveUnusedScatterObjects(size_t scatterIndex) {
  if (scatterIndex >= s_scatterModels.size()) {
    return false;
  }
  ScatterModel &model = s_scatterModels[scatterIndex];
  const size_t objectsBefore = model.objects.size();
  const size_t targetsBefore = model.targets.size();
  model.objects.erase(
      std::remove_if(model.objects.begin(), model.objects.end(),
                     [](const ScatterObject &object) {
                       return object.meshIndices.empty() ||
                              std::none_of(
                                  object.meshIndices.begin(),
                                  object.meshIndices.end(),
                                  [](size_t meshIndex) {
                                    return meshIndex < g_loadedMeshes.size();
                                  });
                     }),
      model.objects.end());
  // A8: also prune targets that lost their node (nodeIndex set to -1 by
  // ReindexScatterNodeReferencesAfterRemoval). Without this they sit as
  // "<missing node>" rows in the panel forever.
  const auto &nodes = GetNodes();
  model.targets.erase(
      std::remove_if(model.targets.begin(), model.targets.end(),
                     [&nodes](const ScatterTarget &target) {
                       return target.nodeIndex == static_cast<size_t>(-1) ||
                              target.nodeIndex >= nodes.size() ||
                              target.meshIndex >= g_loadedMeshes.size();
                     }),
      model.targets.end());
  const bool changed = model.objects.size() != objectsBefore ||
                       model.targets.size() != targetsBefore;
  if (changed) {
    MarkScatterRuntimeChanged();
  }
  return changed;
}

bool AddScatterTargetFromPick(size_t scatterIndex, float screenX, float screenY,
                              float screenWidth, float screenHeight) {
  if (scatterIndex >= s_scatterModels.size()) {
    return false;
  }
  SceneMeshPickHit hit;
  if (!PickSceneMeshAt(screenX, screenY, screenWidth, screenHeight, hit)) {
    return false;
  }

  ScatterModel &model = s_scatterModels[scatterIndex];
  auto existing =
      std::find_if(model.targets.begin(), model.targets.end(),
                   [&hit](const ScatterTarget &target) {
                     return target.nodeIndex == hit.nodeIndex &&
                            target.meshIndex == hit.meshIndex;
                   });
  if (existing != model.targets.end()) {
    existing->enabled = true;
    PopulateScatterTargetMetadata(*existing);
    MarkScatterRuntimeChanged();
    return true;
  }

  ScatterTarget target;
  target.nodeIndex = hit.nodeIndex;
  target.meshIndex = hit.meshIndex;
  PopulateScatterTargetMetadata(target);
  model.targets.push_back(std::move(target));
  MarkScatterRuntimeChanged();
  return true;
}

bool HandleScatterPick(float screenX, float screenY, float screenWidth,
                       float screenHeight) {
  if (!IsScatterPickingTarget()) {
    return false;
  }
  const size_t targetScatter = s_scatterPickTargetIndex;
  const bool added = AddScatterTargetFromPick(targetScatter, screenX, screenY,
                                              screenWidth, screenHeight);
  if (added) {
    CancelScatterPick();
  }
  return added;
}

// ==========================================================================
// Hooks called from scene.cpp
// ==========================================================================

void ReindexScatterNodeReferencesAfterRemoval(size_t removedNodeIndex) {
  for (ScatterModel &model : s_scatterModels) {
    for (ScatterTarget &target : model.targets) {
      if (target.nodeIndex == removedNodeIndex) {
        target.nodeIndex = static_cast<size_t>(-1);
        target.enabled = false;
      } else if (target.nodeIndex != static_cast<size_t>(-1) &&
                 target.nodeIndex > removedNodeIndex) {
        --target.nodeIndex;
      }
    }
  }
  ++s_scatterRuntimeRevision;
  s_scatterInstanceCacheRevision = 0;
}

size_t RemapScatterTargetMaterialIndices(const std::vector<int> &remap) {
  size_t rewrites = 0;
  for (ScatterModel &model : s_scatterModels) {
    for (ScatterTarget &target : model.targets) {
      if (target.materialIndex >= 0 &&
          target.materialIndex < static_cast<int>(remap.size())) {
        const int newIndex = remap[target.materialIndex];
        if (newIndex != target.materialIndex) {
          target.materialIndex = newIndex;
          ++rewrites;
        }
      }
    }
  }
  return rewrites;
}

void OnSceneStateChanged() {
  ++s_scatterRuntimeRevision;
  s_scatterInstanceCacheRevision = 0;
}

// A1 helper: does any object in any enabled model use min/max distance
// culling? If so, the cache must invalidate when the camera moves.
bool AnyModelUsesCameraDistance() {
  for (const ScatterModel &model : s_scatterModels) {
    if (!model.enabled) {
      continue;
    }
    for (const ScatterObject &object : model.objects) {
      if (!object.enabled) {
        continue;
      }
      if (object.minDistance > 0.0f || object.maxDistance > 0.0f) {
        return true;
      }
    }
  }
  return false;
}

void AppendScatterInstances(std::vector<Instance> &outInstances) {
  if (s_scatterModels.empty()) {
    s_scatterInstanceCache.clear();
    s_scatterInstanceCacheRevision = s_scatterRuntimeRevision;
    s_scatterRuntimeStats = {};
    s_scatterRuntimeStats.revision = s_scatterRuntimeRevision;
    return;
  }

  // A1: camera-relative distance culling means the cache is stale when the
  // camera moves, even though the authoring revision hasn't bumped.
  const bool cameraMoved =
      s_scatterCacheUsesCameraDistance &&
      (g_cameraData.pos[0] != s_scatterCacheCameraPos[0] ||
       g_cameraData.pos[1] != s_scatterCacheCameraPos[1] ||
       g_cameraData.pos[2] != s_scatterCacheCameraPos[2]);

  if (s_scatterInstanceCacheRevision == s_scatterRuntimeRevision &&
      !cameraMoved) {
    outInstances.insert(outInstances.end(), s_scatterInstanceCache.begin(),
                        s_scatterInstanceCache.end());
    return;
  }

  s_scatterInstanceCache.clear();
  s_scatterRuntimeStats = {};
  s_scatterRuntimeStats.revision = s_scatterRuntimeRevision;
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();
  const auto &nodes = GetNodes();
  for (size_t modelIndex = 0; modelIndex < s_scatterModels.size();
       ++modelIndex) {
    const ScatterModel &model = s_scatterModels[modelIndex];
    if (!model.enabled || model.targets.empty() || model.objects.empty()) {
      continue;
    }

    std::vector<ScatterTriangle> triangles;
    triangles.reserve(4096);
    float weightedArea = 0.0f;
    for (size_t targetIndex = 0; targetIndex < model.targets.size();
         ++targetIndex) {
      const ScatterTarget &target = model.targets[targetIndex];
      if (!target.enabled || target.nodeIndex >= nodes.size() ||
          !nodes[target.nodeIndex].visible ||
          target.nodeIndex >= worldTransforms.size()) {
        continue;
      }
      GatherScatterTriangles(target, modelIndex, targetIndex,
                             worldTransforms[target.nodeIndex].data(),
                             triangles, weightedArea);
      ++s_scatterRuntimeStats.activeTargets;
    }

    if (triangles.empty() || weightedArea <= 1e-6f) {
      continue;
    }

    // A4: build the weight CDF once per model. All objects in the model
    // sample from the same triangle pool, so the CDF is shared.
    std::vector<float> triangleWeightCdf(triangles.size());
    float runningWeight = 0.0f;
    for (size_t i = 0; i < triangles.size(); ++i) {
      runningWeight += triangles[i].weight;
      triangleWeightCdf[i] = runningWeight;
    }

    uint32_t remainingModelBudget = model.previewInstanceBudget;
    for (size_t objectIndex = 0; objectIndex < model.objects.size();
         ++objectIndex) {
      AppendScatterInstancesForObject(model, modelIndex,
                                      model.objects[objectIndex], objectIndex,
                                      triangles, triangleWeightCdf,
                                      weightedArea, remainingModelBudget,
                                      s_scatterInstanceCache);
      if (remainingModelBudget == 0) {
        break;
      }
    }
  }
  s_scatterInstanceCacheRevision = s_scatterRuntimeRevision;
  s_scatterCacheUsesCameraDistance = AnyModelUsesCameraDistance();
  s_scatterCacheCameraPos[0] = g_cameraData.pos[0];
  s_scatterCacheCameraPos[1] = g_cameraData.pos[1];
  s_scatterCacheCameraPos[2] = g_cameraData.pos[2];
  outInstances.insert(outInstances.end(), s_scatterInstanceCache.begin(),
                      s_scatterInstanceCache.end());
}

} // namespace Scene
