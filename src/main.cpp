#include "ImGuizmo.h"
#include "assets/asset_loader.h"
#include "clouds.h" // Add clouds
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxc_wrapper.h"
#include "dxr_helpers.h"
#include "dxr_renderer.h"
#include "editor_ui.h"
#include "file_import.h"
#include "grass_manager.h"
#include "ibl_manager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_theme.h"
#include "input_handler.h"
#include "light.h"
#include "livelink/livelink_mock_provider.h"
#include "livelink/livelink_pipe_provider.h"
#include "livelink/livelink_runtime.h"
#include "material_editor.h"
#include "material/material_system.h"
#include "oidn_denoiser.h"
#include "raster_renderer.h"
#include "resource.h"
#include "scene.h"
#include "scene_io.h"
#include <algorithm>

#ifdef USE_QT_UI
#include <QApplication>
#include <QMessageBox>
#include "qt/DX12View.h"
#include "qt/MainWindow.h"
#include "qt/QtTheme.h"
#endif
#include <chrono>
#include <cmath>
#include <codecvt>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdint>
#include <filesystem>
#include <locale>
#include <stdio.h>
#include <string>
#include <vector>

// Forward-declare ImGui Win32 WndProc handler (imgui_impl_win32.h documents
// this should be declared by user)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace fs = std::filesystem;

using MaterialCB = MaterialSystem::RuntimeRasterMaterialConstants;
using DxrMaterialData = MaterialSystem::RuntimeDxrMaterialData;
using DxrMaterialExtraData = MaterialSystem::RuntimeDxrMaterialExtraData;

// Instanced grass patch data generated from meshes that have materials marked as
// grass.
static std::vector<FGrassPatch> g_grassPatches;
static Asset::GpuMesh g_proceduralGrassBladeMesh;
static Asset::GpuMesh g_proceduralGrassMidMesh;
static bool g_proceduralGrassBladeReady = false;
static bool g_proceduralGrassMidReady = false;
extern std::vector<Asset::Material> g_loadedMaterials;

namespace {
constexpr float kTwoPi = 6.283185307179586f;

static uint32_t HashU32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static float Hash01(uint32_t x) {
  return (float)(HashU32(x) & 0x00FFFFFFU) / 16777215.0f;
}

static float SmoothStep01(float t) {
  t = (std::clamp)(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static float ValueNoise2D(float x, float y, uint32_t seed) {
  const int ix = (int)std::floor(x);
  const int iy = (int)std::floor(y);
  const float fx = x - (float)ix;
  const float fy = y - (float)iy;

  const auto corner = [seed](int cx, int cy) {
    const uint32_t packed = (uint32_t)cx * 0x1f123bb5U ^
                            (uint32_t)cy * 0x9e3779b9U ^ seed;
    return Hash01(packed);
  };

  const float v00 = corner(ix, iy);
  const float v10 = corner(ix + 1, iy);
  const float v01 = corner(ix, iy + 1);
  const float v11 = corner(ix + 1, iy + 1);
  const float sx = SmoothStep01(fx);
  const float sy = SmoothStep01(fy);
  const float vx0 = v00 + (v10 - v00) * sx;
  const float vx1 = v01 + (v11 - v01) * sx;
  return vx0 + (vx1 - vx0) * sy;
}

static float FractalNoise2D(float x, float y, uint32_t seed) {
  float value = 0.0f;
  float amplitude = 0.55f;
  float frequency = 1.0f;
  float normalization = 0.0f;
  for (int octave = 0; octave < 3; ++octave) {
    value += ValueNoise2D(x * frequency, y * frequency,
                          seed + 0x9e3779b9U * (uint32_t)(octave + 1)) *
             amplitude;
    normalization += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.17f;
  }
  return (normalization > 1e-5f) ? (value / normalization) : 0.0f;
}

static float ComputeGrassPatchWeight(const DirectX::XMFLOAT3 &position,
                                     uint32_t sourceMeshId) {
  const float seedOffset = (float)(sourceMeshId & 1023u) * 0.173f;
  const float macro = FractalNoise2D(position.x * 0.22f + seedOffset,
                                     position.z * 0.22f - seedOffset,
                                     sourceMeshId ^ 0x4f1bbcdcU);
  const float micro = FractalNoise2D(position.x * 1.05f - seedOffset * 0.5f,
                                     position.z * 1.05f + seedOffset * 0.5f,
                                     sourceMeshId ^ 0xa54ff53aU);
  const float macroMask =
      0.18f + 0.82f * SmoothStep01((macro - 0.26f) / 0.54f);
  const float microMask = 0.82f + 0.28f * micro;
  return (std::clamp)(macroMask * microMask, 0.14f, 1.0f);
}

static uint64_t HashU64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static void HashCombineU64(uint64_t &seed, uint64_t value) {
  seed ^= HashU64(value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

static bool EnsureProceduralGrassBladeMesh() {
  if (g_proceduralGrassBladeReady && g_proceduralGrassBladeMesh.vertexBuffer &&
      g_proceduralGrassBladeMesh.indexBuffer) {
    return true;
  }

  // Build a compact grass tuft so each instance reads more like a clump than
  // a single crossed billboard.
  std::vector<Asset::Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(40);
  indices.reserve(180);

  auto addBladePlane = [&](float yawRadians, float baseOffsetX,
                           float baseOffsetZ, float widthScale,
                           float heightScale, float sideLean) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const float halfWidths[4] = {0.072f, 0.052f, 0.026f, 0.006f};
    const float heights[4] = {0.00f, 0.32f, 0.71f, 1.00f};
    const float curve[4] = {0.000f, 0.018f, 0.056f, 0.105f};
    const float uvsV[4] = {1.00f, 0.64f, 0.22f, 0.00f};

    for (int row = 0; row < 4; ++row) {
      const float width = halfWidths[row] * widthScale;
      const float height = heights[row] * heightScale;
      const float bend = curve[row] * heightScale;
      const float lateral = sideLean * heights[row];

      auto makeVertex = [&](float side, float u) {
        const float localX = baseOffsetX + side * width + lateral;
        const float localY = height;
        const float localZ = baseOffsetZ + bend;
        Asset::Vertex v = {};
        v.pos[0] = localX * c - localZ * s;
        v.pos[1] = localY;
        v.pos[2] = localX * s + localZ * c;
        v.normal[0] = c;
        v.normal[1] = 0.18f;
        v.normal[2] = s;
        v.tangent[0] = -s;
        v.tangent[1] = 0.0f;
        v.tangent[2] = c;
        v.tangent[3] = 1.0f;
        v.uv[0] = u;
        v.uv[1] = uvsV[row];
        return v;
      };

      Asset::Vertex left = {};
      Asset::Vertex right = {};
      left = makeVertex(-1.0f, 0.0f);
      right = makeVertex(1.0f, 1.0f);
      vertices.push_back(left);
      vertices.push_back(right);
    }

    for (uint32_t row = 0; row < 3; ++row) {
      const uint32_t i0 = base + row * 2;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + 2;
      const uint32_t i3 = i0 + 3;
      indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3, i2, i1, i0,
                                     i3, i1, i2});
    }
  };

  addBladePlane(0.12f, -0.030f, -0.014f, 1.00f, 1.00f, -0.012f);
  addBladePlane(1.11f, 0.024f, -0.008f, 0.88f, 0.93f, 0.015f);
  addBladePlane(2.24f, 0.008f, 0.022f, 0.76f, 0.84f, -0.008f);
  addBladePlane(3.51f, -0.020f, 0.016f, 0.70f, 0.78f, 0.006f);
  addBladePlane(4.67f, 0.018f, 0.006f, 0.64f, 0.73f, -0.004f);

  Asset::GpuMesh gm = Asset::LoadMeshFromMemory(vertices, indices);
  if (!gm.vertexBuffer || !gm.indexBuffer || gm.indexCount == 0) {
    fprintf(stderr, "Grass: failed to create procedural blade mesh\n");
    return false;
  }
  gm.materialIndex = -1;
  gm.minBound[0] = -0.12f;
  gm.minBound[1] = 0.0f;
  gm.minBound[2] = -0.11f;
  gm.maxBound[0] = 0.12f;
  gm.maxBound[1] = 1.0f;
  gm.maxBound[2] = 0.12f;

  g_proceduralGrassBladeMesh = std::move(gm);
  g_proceduralGrassBladeReady = true;
  fprintf(stderr, "Grass: procedural blade mesh ready (v=%u i=%u)\n",
          g_proceduralGrassBladeMesh.vertexCount,
          g_proceduralGrassBladeMesh.indexCount);
  return true;
}

static bool EnsureProceduralGrassMidMesh() {
  if (g_proceduralGrassMidReady && g_proceduralGrassMidMesh.vertexBuffer &&
      g_proceduralGrassMidMesh.indexBuffer) {
    return true;
  }

  std::vector<Asset::Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(8);
  indices.reserve(24);

  auto addCrossPlane = [&](float yawRadians) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const float halfWidth = 0.085f;
    const float height = 0.92f;

    auto makeVertex = [&](float side, float y, float u, float v) {
      Asset::Vertex vert = {};
      const float localX = side * halfWidth;
      const float localZ = y * 0.04f;
      vert.pos[0] = localX * c - localZ * s;
      vert.pos[1] = y * height;
      vert.pos[2] = localX * s + localZ * c;
      vert.normal[0] = c;
      vert.normal[1] = 0.12f;
      vert.normal[2] = s;
      vert.tangent[0] = -s;
      vert.tangent[1] = 0.0f;
      vert.tangent[2] = c;
      vert.tangent[3] = 1.0f;
      vert.uv[0] = u;
      vert.uv[1] = v;
      return vert;
    };

    vertices.push_back(makeVertex(-1.0f, 0.0f, 0.0f, 1.0f));
    vertices.push_back(makeVertex(1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(makeVertex(-1.0f, 1.0f, 0.0f, 0.0f));
    vertices.push_back(makeVertex(1.0f, 1.0f, 1.0f, 0.0f));

    indices.insert(indices.end(),
                   {base + 0, base + 1, base + 2, base + 2, base + 1, base + 3,
                    base + 2, base + 1, base + 0, base + 3, base + 1, base + 2});
  };

  addCrossPlane(0.25f);
  addCrossPlane(1.57f);

  Asset::GpuMesh gm = Asset::LoadMeshFromMemory(vertices, indices);
  if (!gm.vertexBuffer || !gm.indexBuffer || gm.indexCount == 0) {
    fprintf(stderr, "Grass: failed to create procedural mid mesh\n");
    return false;
  }
  gm.materialIndex = -1;
  gm.minBound[0] = -0.10f;
  gm.minBound[1] = 0.0f;
  gm.minBound[2] = -0.10f;
  gm.maxBound[0] = 0.10f;
  gm.maxBound[1] = 0.92f;
  gm.maxBound[2] = 0.10f;

  g_proceduralGrassMidMesh = std::move(gm);
  g_proceduralGrassMidReady = true;
  fprintf(stderr, "Grass: procedural mid mesh ready (v=%u i=%u)\n",
          g_proceduralGrassMidMesh.vertexCount,
          g_proceduralGrassMidMesh.indexCount);
  return true;
}

static void AppendGrassPatchesFromInstance(const Scene::Instance &inst,
                                           uint32_t sourceMeshId,
                                           const Asset::Material &grassMat,
                                           std::vector<FGrassPatch> &outPatches) {
  if (!inst.mesh)
    return;
  const Asset::GpuMesh &mesh = *inst.mesh;
  const float density = (std::clamp)(grassMat.grassBladeCount, 0.0f, 1024.0f);
  if (density <= 0.0f) {
    return;
  }
  const float baseSize = (std::clamp)(grassMat.grassBladeSize, 0.05f, 5.0f);
  const float variation =
      (std::clamp)(grassMat.grassBladeVariation, 0.0f, 1.0f);
  const float densityCompensation =
      (std::clamp)(0.6f / baseSize, 0.8f, 3.5f);
  const float patchDensityScale = 0.18f;

  struct GrassTriangle {
    DirectX::XMFLOAT3 p0;
    DirectX::XMFLOAT3 p1;
    DirectX::XMFLOAT3 p2;
    DirectX::XMFLOAT3 n0;
    DirectX::XMFLOAT3 n1;
    DirectX::XMFLOAT3 n2;
    DirectX::XMFLOAT2 uv0;
    DirectX::XMFLOAT2 uv1;
    DirectX::XMFLOAT2 uv2;
    float weight = 0.0f;
  };

  std::vector<GrassTriangle> triangles;
  triangles.reserve(mesh.cpuIndices.size() / 3);
  float weightedArea = 0.0f;

  if (!mesh.cpuVertices.empty() && mesh.cpuIndices.size() >= 3) {
    const DirectX::XMVECTOR worldUp =
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    for (size_t tri = 0; tri + 2 < mesh.cpuIndices.size(); tri += 3) {
      const uint32_t i0 = mesh.cpuIndices[tri];
      const uint32_t i1 = mesh.cpuIndices[tri + 1];
      const uint32_t i2 = mesh.cpuIndices[tri + 2];
      if (i0 >= mesh.cpuVertices.size() || i1 >= mesh.cpuVertices.size() ||
          i2 >= mesh.cpuVertices.size()) {
        continue;
      }

      const auto &v0 = mesh.cpuVertices[i0];
      const auto &v1 = mesh.cpuVertices[i1];
      const auto &v2 = mesh.cpuVertices[i2];

      const DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(
          DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v0.pos)),
          inst.transform);
      const DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(
          DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v1.pos)),
          inst.transform);
      const DirectX::XMVECTOR p2 = DirectX::XMVector3TransformCoord(
          DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v2.pos)),
          inst.transform);

      const DirectX::XMVECTOR e0 = DirectX::XMVectorSubtract(p1, p0);
      const DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p2, p0);
      const DirectX::XMVECTOR faceNormal = DirectX::XMVector3Cross(e0, e1);
      const float area =
          0.5f * DirectX::XMVectorGetX(DirectX::XMVector3Length(faceNormal));
      if (area <= 1e-6f) {
        continue;
      }

      const DirectX::XMVECTOR normalWorld = DirectX::XMVector3Normalize(faceNormal);
      const float upDot =
          DirectX::XMVectorGetX(DirectX::XMVector3Dot(normalWorld, worldUp));
      if (upDot <= 0.15f) {
        continue;
      }

      GrassTriangle gt = {};
      DirectX::XMStoreFloat3(&gt.p0, p0);
      DirectX::XMStoreFloat3(&gt.p1, p1);
      DirectX::XMStoreFloat3(&gt.p2, p2);

      const DirectX::XMVECTOR n0 = DirectX::XMVector3Normalize(
          DirectX::XMVector3TransformNormal(
              DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v0.normal)),
              inst.transform));
      const DirectX::XMVECTOR n1 = DirectX::XMVector3Normalize(
          DirectX::XMVector3TransformNormal(
              DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v1.normal)),
              inst.transform));
      const DirectX::XMVECTOR n2 = DirectX::XMVector3Normalize(
          DirectX::XMVector3TransformNormal(
              DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3 *>(v2.normal)),
              inst.transform));
      DirectX::XMStoreFloat3(&gt.n0, n0);
      DirectX::XMStoreFloat3(&gt.n1, n1);
      DirectX::XMStoreFloat3(&gt.n2, n2);
      gt.uv0 = {v0.uv[0], v0.uv[1]};
      gt.uv1 = {v1.uv[0], v1.uv[1]};
      gt.uv2 = {v2.uv[0], v2.uv[1]};

      const float slopeWeight = (upDot - 0.15f) / 0.85f;
      gt.weight = area * (std::clamp)(slopeWeight, 0.0f, 1.0f);
      if (gt.weight <= 1e-6f) {
        continue;
      }
      weightedArea += gt.weight;
      triangles.push_back(gt);
    }
  }

  if (triangles.empty()) {
    const float minX = mesh.minBound[0];
    const float minZ = mesh.minBound[2];
    const float maxX = mesh.maxBound[0];
    const float maxZ = mesh.maxBound[2];
    const float width = (std::max)(maxX - minX, 0.01f);
    const float depth = (std::max)(maxZ - minZ, 0.01f);
    weightedArea = (std::max)(width * depth, 0.01f);
  }

  const int computedCount =
      (int)std::round((std::max)(weightedArea, 0.01f) * density *
                      densityCompensation * patchDensityScale);
  const int patchCount = std::clamp((std::max)(1, computedCount), 1, 262144);
  if (patchCount <= 0) {
    return;
  }

  auto emitBladeFromSeed = [&](uint32_t baseSeed) {
    DirectX::XMFLOAT3 position = {};
    DirectX::XMFLOAT3 normal = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT2 emitterUv = {0.0f, 0.0f};
    float patchWeight = 1.0f;

    if (!triangles.empty()) {
      const float triPick = Hash01(baseSeed ^ 0x3c6ef372U) * weightedArea;
      float accum = 0.0f;
      const GrassTriangle *chosen = &triangles.back();
      for (const auto &tri : triangles) {
        accum += tri.weight;
        if (triPick <= accum) {
          chosen = &tri;
          break;
        }
      }

      const float u = Hash01(baseSeed ^ 0xa54ff53aU);
      const float v = Hash01(baseSeed ^ 0x7f4a7c15U);
      const float su = std::sqrt(u);
      const float b0 = 1.0f - su;
      const float b1 = su * (1.0f - v);
      const float b2 = su * v;

      position.x = chosen->p0.x * b0 + chosen->p1.x * b1 + chosen->p2.x * b2;
      position.y = chosen->p0.y * b0 + chosen->p1.y * b1 + chosen->p2.y * b2;
      position.z = chosen->p0.z * b0 + chosen->p1.z * b1 + chosen->p2.z * b2;
      emitterUv.x = chosen->uv0.x * b0 + chosen->uv1.x * b1 + chosen->uv2.x * b2;
      emitterUv.y = chosen->uv0.y * b0 + chosen->uv1.y * b1 + chosen->uv2.y * b2;
      normal.x = chosen->n0.x * b0 + chosen->n1.x * b1 + chosen->n2.x * b2;
      normal.y = chosen->n0.y * b0 + chosen->n1.y * b1 + chosen->n2.y * b2;
      normal.z = chosen->n0.z * b0 + chosen->n1.z * b1 + chosen->n2.z * b2;

      DirectX::XMVECTOR n = DirectX::XMVector3Normalize(
          DirectX::XMLoadFloat3(&normal));
      DirectX::XMStoreFloat3(&normal, n);
      patchWeight = ComputeGrassPatchWeight(position, sourceMeshId);
      position.x += normal.x * 0.0025f;
      position.y += normal.y * 0.0025f;
      position.z += normal.z * 0.0025f;
    } else {
      const float minX = mesh.minBound[0];
      const float minZ = mesh.minBound[2];
      const float maxX = mesh.maxBound[0];
      const float maxY = mesh.maxBound[1];
      const float maxZ = mesh.maxBound[2];
      const float width = (std::max)(maxX - minX, 0.01f);
      const float depth = (std::max)(maxZ - minZ, 0.01f);
      const float u = Hash01(baseSeed ^ 0x3c6ef372U);
      const float v = Hash01(baseSeed ^ 0xa54ff53aU);
      const float localX = minX + width * u;
      const float localZ = minZ + depth * v;
      const float localY = maxY;
      emitterUv = {u, v};

      DirectX::XMVECTOR worldPos = DirectX::XMVector3TransformCoord(
          DirectX::XMVectorSet(localX, localY, localZ, 1.0f), inst.transform);
      DirectX::XMStoreFloat3(&position, worldPos);

      DirectX::XMVECTOR upWorld = DirectX::XMVector3TransformNormal(
          DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), inst.transform);
      if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(upWorld)) < 1e-8f) {
        upWorld = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
      }
      upWorld = DirectX::XMVector3Normalize(upWorld);
      DirectX::XMStoreFloat3(&normal, upWorld);
      patchWeight = ComputeGrassPatchWeight(position, sourceMeshId);
    }

    FGrassPatch patch = {};
    patch.position = position;
    const float randScale = 0.75f + 0.5f * Hash01(baseSeed ^ 0x1f123bb5U);
    const float patchScale = 0.82f + 0.36f * patchWeight;
    patch.scale =
        baseSize * (1.0f + (randScale - 1.0f) * variation) * patchScale;
    patch.normal = normal;
    const float randYaw = Hash01(baseSeed ^ 0x0f1bbcdcU) * kTwoPi;
    patch.yawRadians = randYaw;
    patch.emitterUv = emitterUv;
    patch.colorVariation = HashU32(baseSeed ^ 0xdeadbeefU);
    outPatches.push_back(patch);
  };

  int emitted = 0;
  const int maxAttempts = (std::max)(patchCount * 7, 96);
  for (int attempt = 0; attempt < maxAttempts && emitted < patchCount;
       ++attempt) {
    const uint32_t baseSeed =
        sourceMeshId * 0x9e3779b9U + (uint32_t)attempt * 0x85ebca6bU;
    DirectX::XMFLOAT3 candidatePosition = {};
    if (!triangles.empty()) {
      const float triPick = Hash01(baseSeed ^ 0x3c6ef372U) * weightedArea;
      float accum = 0.0f;
      const GrassTriangle *chosen = &triangles.back();
      for (const auto &tri : triangles) {
        accum += tri.weight;
        if (triPick <= accum) {
          chosen = &tri;
          break;
        }
      }
      const float u = Hash01(baseSeed ^ 0xa54ff53aU);
      const float v = Hash01(baseSeed ^ 0x7f4a7c15U);
      const float su = std::sqrt(u);
      const float b0 = 1.0f - su;
      const float b1 = su * (1.0f - v);
      const float b2 = su * v;
      candidatePosition.x =
          chosen->p0.x * b0 + chosen->p1.x * b1 + chosen->p2.x * b2;
      candidatePosition.y =
          chosen->p0.y * b0 + chosen->p1.y * b1 + chosen->p2.y * b2;
      candidatePosition.z =
          chosen->p0.z * b0 + chosen->p1.z * b1 + chosen->p2.z * b2;
    } else {
      const float minX = mesh.minBound[0];
      const float minZ = mesh.minBound[2];
      const float maxX = mesh.maxBound[0];
      const float maxY = mesh.maxBound[1];
      const float maxZ = mesh.maxBound[2];
      const float width = (std::max)(maxX - minX, 0.01f);
      const float depth = (std::max)(maxZ - minZ, 0.01f);
      const float u = Hash01(baseSeed ^ 0x3c6ef372U);
      const float v = Hash01(baseSeed ^ 0xa54ff53aU);
      const float localX = minX + width * u;
      const float localZ = minZ + depth * v;
      const float localY = maxY;
      DirectX::XMVECTOR worldPos = DirectX::XMVector3TransformCoord(
          DirectX::XMVectorSet(localX, localY, localZ, 1.0f), inst.transform);
      DirectX::XMStoreFloat3(&candidatePosition, worldPos);
    }

    const float patchWeight =
        ComputeGrassPatchWeight(candidatePosition, sourceMeshId);
    const float acceptProbability =
        (std::clamp)(0.22f + patchWeight * 0.92f, 0.0f, 1.0f);
    if (Hash01(baseSeed ^ 0xc2b2ae35U) > acceptProbability &&
        (patchCount - emitted) < (maxAttempts - attempt)) {
      continue;
    }
    emitBladeFromSeed(baseSeed);
    ++emitted;
  }

  for (int filler = emitted; filler < patchCount; ++filler) {
    const uint32_t baseSeed = sourceMeshId * 0x9e3779b9U +
                              (uint32_t)(maxAttempts + filler) * 0x27d4eb2dU;
    emitBladeFromSeed(baseSeed);
  }
}

static uint64_t ComputeGrassSceneHash(const std::vector<Scene::Instance> &instances) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const auto &inst : instances) {
    if (!inst.mesh) {
      continue;
    }
    const int matIdx = inst.mesh->materialIndex;
    if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size()) {
      continue;
    }
    const auto &mat = g_loadedMaterials[matIdx];
    if (!mat.isGrass) {
      continue;
    }

    HashCombineU64(hash, (uint64_t)(uintptr_t)inst.mesh);
    HashCombineU64(hash, (uint64_t)matIdx);

    const float *m = reinterpret_cast<const float *>(&inst.transform);
    for (int i = 0; i < 16; ++i) {
      uint32_t bits = 0;
      memcpy(&bits, &m[i], sizeof(bits));
      HashCombineU64(hash, bits);
    }

    const float grassFloats[] = {
        mat.grassBladeCount, mat.grassBladeSize, mat.grassBladeVariation,
        mat.grassColor[0],   mat.grassColor[1],  mat.grassColor[2]};
    for (float v : grassFloats) {
      uint32_t bits = 0;
      memcpy(&bits, &v, sizeof(bits));
      HashCombineU64(hash, bits);
    }
  }
  return hash;
}
} // namespace

// Top-level exception handler for debug builds.
#ifdef _DEBUG
static LONG WINAPI TopLevelExceptionHandler(EXCEPTION_POINTERS *ep) {
  if (ep && ep->ExceptionRecord) {
    fprintf(stderr, "TopLevelExceptionHandler: code=0x%08x at IP=0x%p\n",
            (unsigned)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
  } else {
    fprintf(stderr, "TopLevelExceptionHandler: called with null record\n");
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Hint to NVIDIA/AMD drivers to prefer the high-performance GPU on Optimus
// systems
extern "C" {
__declspec(dllexport) unsigned long long NvOptimusEnablement = 0x00000001ULL;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// RenderMode is now defined in scene.h

RenderMode g_currentRenderMode = RenderMode::Raster;
// Panel visibility flags moved to editor_ui.cpp
// Debug toggles for DXR
bool g_dxrDebugUV = false;
bool g_dxrDumpPixels = false;
bool g_dxrHitDebug = false; // encode primitive ID in hit shader for debugging
bool g_dxrDumpD3D12Messages = false; // dump D3D12 InfoQueue messages to stderr
bool g_rasterDebugUV = false; // show raster UVs in mesh pixel shader (debug)
bool g_verboseRenderLogs =
    false; // when true, prints render-loop diagnostics (disabled by default)
bool g_rasterWireframe =
    false; // show meshes in wireframe / disable culling (debug)
bool g_rasterDebugDepth =
    false; // compile shader to output depth as color for debugging

// Global runtime flags (set by command-line)
bool g_debugLog = false; // enable verbose debug logging (use --debug-log)
bool g_fastImport =
    false; // enable Assimp optimization flags to speed imports (--fast-import)

CloudManager g_cloudManager; // Global Global Manager
bool g_cloudRenderingEnabled = true;

ComPtr<ID3D12Resource> g_exportRenderTarget;
ComPtr<ID3D12DescriptorHeap> g_exportRtvHeap;
UINT g_exportRenderTargetWidth = 0;
UINT g_exportRenderTargetHeight = 0;
D3D12_RESOURCE_STATES g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
D3D12_CPU_DESCRIPTOR_HANDLE g_exportPreviewSrvCpu = {0};
D3D12_GPU_DESCRIPTOR_HANDLE g_exportPreviewSrvGpu = {0};
bool g_exportPreviewSrvAllocated = false;

static ComPtr<ID3D12DescriptorHeap> g_imguiHeap;
DescriptorHeapAllocator g_cbvSrvAllocator;
HWND g_hwnd = nullptr;
static constexpr wchar_t kMainWindowTitle[] = L"Project-Render";

// Window dimensions
bool g_appClosing = false;

// Loaded meshes from Asset loader
std::vector<Asset::GpuMesh> g_loadedMeshes;
std::vector<Asset::Material> g_loadedMaterials;
std::vector<Asset::Texture> g_loadedTextures;
D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart = {0};
D3D12_CPU_DESCRIPTOR_HANDLE g_texturesCpuStart = {0};
UINT g_textureDescriptorCapacity = 0;
static constexpr UINT kSceneTextureDescriptorCapacity = 16384;
D3D12_GPU_DESCRIPTOR_HANDLE g_envMapGpuHandle = {0};
static ComPtr<ID3D12Resource>
    g_materialBuffer; // Persistent material constant buffer
UINT g_textureDescriptorCount = 0;
static ComPtr<ID3D12Resource> g_materialConstantBuffer;
static void *g_materialCbMappedData = nullptr;
static ComPtr<ID3D12Resource>
    g_materialStructuredBuffer; // Tightly packed for DXR
static ComPtr<ID3D12Resource>
    g_materialExtraStructuredBuffer; // Secondary material data for DXR
static ComPtr<ID3D12Resource>
    g_meshStructuredBuffer; // Mesh mapping info for DXR

static std::string g_lastAssetStatus; // Human-readable status for the Assets UI
static std::string
    g_selectedAssetPath; // Path chosen by Open dialog (not yet imported)

// Simple pipeline objects
ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3D12PipelineState> g_pipelineState;
static ComPtr<ID3D12Resource> g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
ComPtr<ID3D12Resource> g_constantBuffer;
void *g_constantCbMappedData = nullptr;
static float g_offsetX = 0.2f;

// Grid rendering resources
static ComPtr<ID3D12Resource> g_gridVertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_gridVBView = {};
static UINT g_gridVertexCount = 0;
static ComPtr<ID3D12PipelineState> g_gridPipelineState;
static ComPtr<ID3D12PipelineState> g_meshSimplePipelineState;
// Grid line thickness in world units (used to expand lines into thin quads)
static float g_gridThickness = 0.02f; // increase to make lines thicker

bool g_drawGrid = false; // toggle grid rendering (default OFF)
// Sky model UI state (serialized by SceneIO).
float g_timeOfDay = 10.0f;
float g_northOffset = 0.0f;
float g_latitudeDeg = 50.08f; // Prague default latitude
float g_dayOfYear = 172.0f;   // June solstice-ish

// Small camera module is defined in src/camera.h/.cpp
#include "camera.h"

// Simple Vec3 helper for CPU-side math
struct Vec3 {
  float x, y, z;
};

// --- DXR Globals ---
// DXR implementation moved to DxrRenderer module
#include "dxr_renderer.h"

// NVIDIA Streamline (DLSS-SR + DLSS-RR)
#include "streamline_manager.h"
// Now defined in DX12Context::g_streamline

// Raw Helper to Add Subobject
struct SubobjectWrapper {
  D3D12_STATE_SUBOBJECT subobject;
  // definition storage
  D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
  D3D12_EXPORT_DESC rayGenExport;
  D3D12_EXPORT_DESC missExport;
  D3D12_EXPORT_DESC hitExport;
  D3D12_HIT_GROUP_DESC hitGroupDesc;
  D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
  D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
  D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig;
};

// Ray tracing pipeline and AS builder moved into DxrRenderer module

/* Ray Tracing state object creation moved into
 * DxrRenderer::CreateRayTracingPipeline() */

/* Ray tracing pipeline creation moved to DxrRenderer */

/* Shader table creation moved into DxrRenderer::CreateRayTracingPipeline() */

/* Output UAV creation moved into DxrRenderer::CreateRayTracingPipeline() */

// DXR acceleration structure build moved to
// DxrRenderer::BuildAccelerationStructures (see src/dxr_renderer.cpp for
// implementation)

inline void TransitionResource(ID3D12GraphicsCommandList *cmdList,
                               ID3D12Resource *resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
  if (before == after)
    return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
}

// Fallback helper to avoid name-resolution problems after refactor
static inline void TR(ID3D12GraphicsCommandList *cmdList,
                      ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                      D3D12_RESOURCE_STATES after) {
  TransitionResource(cmdList, resource, before, after);
}

// Helper to find shader file with fallback paths
static std::wstring FindShaderFile(const wchar_t *relativePath) {
  // Try multiple possible locations
  std::vector<std::wstring> searchPaths;
  searchPaths.push_back(relativePath); // Current directory
  searchPaths.push_back(std::wstring(L"..\\..\\") +
                        relativePath); // From build/Release/
  searchPaths.push_back(std::wstring(L"..\\") +
                        relativePath); // From build directory

  for (size_t i = 0; i < searchPaths.size(); ++i) {
    if (fs::exists(searchPaths[i])) {
      return searchPaths[i];
    }
  }

  // Return original path if not found (will fail later with clear error)
  return relativePath;
}

// Enable D3D12 debug layer when available (debug builds)
static void EnableD3D12DebugLayer() {
#ifdef _DEBUG
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
  }
#endif
}

static void EnforceReleaseDebugFlags() {
#ifndef _DEBUG
  g_dxrDebugUV = false;
  g_dxrDumpPixels = false;
  g_dxrHitDebug = false;
  g_dxrDumpD3D12Messages = false;
  g_rasterDebugUV = false;
  g_rasterWireframe = false;
  g_rasterDebugDepth = false;
  g_debugLog = false;
#endif
}

static void EnsureMainWindowTitle(HWND hwnd) {
  if (!hwnd)
    return;
  wchar_t currentTitle[128] = {};
  GetWindowTextW(hwnd, currentTitle, (int)_countof(currentTitle));
  if (wcscmp(currentTitle, kMainWindowTitle) != 0) {
    SetWindowTextW(hwnd, kMainWindowTitle);
    static wchar_t s_lastSeenBadTitle[128] = {};
    if (wcscmp(currentTitle, s_lastSeenBadTitle) != 0) {
      wcsncpy_s(s_lastSeenBadTitle, currentTitle, _TRUNCATE);
      fprintf(stderr,
              "EnsureMainWindowTitle: repaired window title ('%ls' -> '%ls')\n",
              currentTitle, kMainWindowTitle);
    }
  }
}

// Select the first suitable hardware adapter (non-software) that supports D3D12
static void GetHardwareAdapter(IDXGIFactory4 *pFactory,
                               IDXGIAdapter1 **ppAdapter) {
  *ppAdapter = nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  SIZE_T maxDedicatedMem = 0;
  ComPtr<IDXGIAdapter1> bestAdapter;

  for (UINT adapterIndex = 0;; ++adapterIndex) {
    if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &adapter))
      break;

    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue; // skip software adapters

    // Check D3D12 support
    ComPtr<ID3D12Device> testDevice;
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&testDevice)))) {
      // Prefer the adapter with the most dedicated video memory (likely the
      // dGPU)
      if (desc.DedicatedVideoMemory > maxDedicatedMem) {
        maxDedicatedMem = desc.DedicatedVideoMemory;
        bestAdapter = adapter;
      }
    }
  }

  if (bestAdapter) {
    bestAdapter.CopyTo(ppAdapter);
  }
}

static void ExecuteCommandListAndWait(ID3D12GraphicsCommandList *cmdList) {
  ThrowIfFailed(cmdList->Close());
  ID3D12CommandList *lists[] = {cmdList};
  DX12Context::g_commandQueue->ExecuteCommandLists(1, lists);

  // Wait for completion
  ComPtr<ID3D12Fence> fence;
  ThrowIfFailed(DX12Context::g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&fence)));
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  ThrowIfFailed(DX12Context::g_commandQueue->Signal(fence.Get(), 1));
  if (fence->GetCompletedValue() < 1) {
    ThrowIfFailed(fence->SetEventOnCompletion(1, event));
    WaitForSingleObject(event, INFINITE);
  }
  CloseHandle(event);
}

void CreateTestTexture() {
  // Create a simple 2x2 checkerboard texture for testing
  const UINT width = 2;
  const UINT height = 2;
  const UINT pixelSize = 4; // RGBA
  BYTE textureData[width * height * pixelSize] = {
      255, 0,   0,   255, // Red
      0,   255, 0,   255, // Green
      0,   0,   255, 255, // Blue
      255, 255, 0,   255  // Yellow
  };

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC uploadDesc = {};
  uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  uploadDesc.Width = width * height * pixelSize;
  uploadDesc.Height = 1;
  uploadDesc.DepthOrArraySize = 1;
  uploadDesc.MipLevels = 1;
  uploadDesc.SampleDesc.Count = 1;
  uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> uploadBuffer;
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  // Copy texture data to upload buffer
  void *mappedData = nullptr;
  uploadBuffer->Map(0, nullptr, &mappedData);
  memcpy(mappedData, textureData, sizeof(textureData));
  uploadBuffer->Unmap(0, nullptr);

  // Create the texture resource
  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Width = width;
  textureDesc.Height = height;
  textureDesc.DepthOrArraySize = 1;
  textureDesc.MipLevels = 1;
  textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ComPtr<ID3D12Resource> texture;
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

  if (g_texturesCpuStart.ptr == 0 || g_textureDescriptorCapacity == 0) {
    fprintf(stderr,
            "CreateTestTexture: texture descriptor table unavailable\n");
    return;
  }
  const UINT newIndex = (UINT)g_loadedTextures.size();
  if (newIndex >= g_textureDescriptorCapacity) {
    fprintf(stderr,
            "CreateTestTexture: texture descriptor capacity exceeded (%u)\n",
            g_textureDescriptorCapacity);
    return;
  }
  const UINT descInc = DX12Context::g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE textureCpu = g_texturesCpuStart;
  textureCpu.ptr += (SIZE_T)newIndex * descInc;

  // Create SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  DX12Context::g_device->CreateShaderResourceView(texture.Get(), &srvDesc,
                                                  textureCpu);

  // Copy from upload buffer to texture
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(DX12Context::g_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)));
  ThrowIfFailed(DX12Context::g_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr,
      IID_PPV_ARGS(&cmdList)));

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = uploadBuffer.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Offset = 0;
  src.PlacedFootprint.Footprint.Width = width;
  src.PlacedFootprint.Footprint.Height = height;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = width * pixelSize;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  // Transition to shader resource
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  cmdList->ResourceBarrier(1, &barrier);

  ExecuteCommandListAndWait(cmdList.Get());

  // Store the texture
  Asset::Texture testTex;
  testTex.resource = texture;
  testTex.width = width;
  testTex.height = height;
  testTex.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  testTex.mipLevels = 1;
  g_loadedTextures.push_back(testTex);
  g_textureDescriptorCount = (UINT)g_loadedTextures.size();

  // Log to stderr only
  fprintf(stderr, "CreateTestTexture: Created 2x2 checkerboard texture #%u\n",
          newIndex);
}

bool InitApplication(HWND hwnd) {

  g_hwnd = hwnd;

  if (!DX12Context::InitD3D12(hwnd)) {
    return false;
  }

  // Provide Streamline manager to DXR module (optional feature).
  DxrRenderer::SetStreamlineManager(&DX12Context::g_streamline);

  // Probe DXR support on the current device.
  DxrRenderer::Initialize(DX12Context::g_device.Get());
  // grass manager uses the same device for its compute buffers/pipelines
  GrassManager::Initialize(DX12Context::g_device.Get());

  if (g_rayTracingSupported) {
    fprintf(stderr, "DXR Ray Tracing Supported (probe)\n");
    DxrRenderer::CreateRayTracingPipeline(DX12Context::g_windowWidth,
                                          DX12Context::g_windowHeight);
    fprintf(stderr, "InitApplication: CreateRayTracingPipeline finished\n");
  } else {
    fprintf(stderr, "DXR Ray Tracing NOT supported on this device\n");
  }

  // Initialize descriptor allocator for CBV/SRV/UAV
  g_cbvSrvAllocator.Init(DX12Context::g_device.Get(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536,
                         FrameCount);
  RasterRenderer::CreateShadowResources(DX12Context::g_device.Get());

  // Reserve a dedicated contiguous descriptor range for scene textures.
  // Texture indices in materials/shaders directly index into this table.
  {
    DescriptorAllocation textureTableAlloc =
        g_cbvSrvAllocator.AllocatePersistent(kSceneTextureDescriptorCapacity);
    g_texturesCpuStart = textureTableAlloc.cpu;
    g_texturesGpuStart = textureTableAlloc.gpu;
    g_textureDescriptorCapacity = kSceneTextureDescriptorCapacity;
    g_textureDescriptorCount = 0;
  }

  // Now that fence and event are valid, attach command queue & fence to DXR
  // renderer
  DxrRenderer::SetCommandQueue(
      DX12Context::g_commandQueue.Get(), DX12Context::g_fence.Get(),
      DX12Context::g_fenceValues, &DX12Context::g_frameIndex,
      DX12Context::g_fenceEvent);

  // --- Create a root signature with CBV b0 (vertex), descriptor table t0
  // (SRV), and CBV b1 (pixel material) ---
  D3D12_ROOT_PARAMETER rootParameters[8] = {};
  // b0 - transform CBV for vertex shader AND pixel shader (needed for view
  // direction)
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[0].Descriptor.ShaderRegister = 0;
  rootParameters[0].Descriptor.RegisterSpace = 0;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // t0-t1023 - descriptor table (SRV) for pixel shader (large buffer for
  // bindless-style indexing)
  static D3D12_DESCRIPTOR_RANGE descRange = {};
  descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descRange.NumDescriptors = 2048; // Support many textures concurrently
  descRange.BaseShaderRegister = 0;
  descRange.RegisterSpace = 0;
  descRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
  rootParameters[1].DescriptorTable.pDescriptorRanges = &descRange;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // b1 - material CBV for pixel shader
  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[2].Descriptor.ShaderRegister = 1;
  rootParameters[2].Descriptor.RegisterSpace = 0;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // b2 - world matrix as root constants for vertex and pixel shaders
  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameters[3].Constants.ShaderRegister = 2;
  rootParameters[3].Constants.RegisterSpace = 0;
  rootParameters[3].Constants.Num32BitValues = 16;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // t0, space1 - Environment Map Descriptor Table (Texture2D)
  static D3D12_DESCRIPTOR_RANGE envMapRange = {};
  envMapRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  envMapRange.NumDescriptors = 2; // Env Map (t0) + Shadow Map (t1)
  envMapRange.BaseShaderRegister = 0;
  envMapRange.RegisterSpace = 1;
  envMapRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
  rootParameters[4].DescriptorTable.pDescriptorRanges = &envMapRange;
  rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // Cloud resources (space2): CBV b10 + SRV t10,t11
  static D3D12_DESCRIPTOR_RANGE cloudRanges[2] = {};
  cloudRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  cloudRanges[0].NumDescriptors = 1;
  cloudRanges[0].BaseShaderRegister = 10; // b10
  cloudRanges[0].RegisterSpace = 2;
  cloudRanges[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  cloudRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  cloudRanges[1].NumDescriptors = 3;      // Base + Detail + BakedSky
  cloudRanges[1].BaseShaderRegister = 10; // t10, t11, t12
  cloudRanges[1].RegisterSpace = 2;
  cloudRanges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[5].DescriptorTable.NumDescriptorRanges = 2;
  rootParameters[5].DescriptorTable.pDescriptorRanges = cloudRanges;
  rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // t0, space3 - grass instance buffer as root SRV for the grass vertex path.
  rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  rootParameters[6].Descriptor.ShaderRegister = 0;
  rootParameters[6].Descriptor.RegisterSpace = 3;
  rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  // t1, space3 - visible grass index list as root SRV for the grass vertex
  // path.
  rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  rootParameters[7].Descriptor.ShaderRegister = 1;
  rootParameters[7].Descriptor.RegisterSpace = 3;
  rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  // static sampler for textures
  D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
  // Default sampler (space 0, register 0)
  samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
  samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].MipLODBias = 0;
  samplers[0].MaxAnisotropy = 16;
  samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplers[0].ShaderRegister = 0;
  samplers[0].RegisterSpace = 0;
  samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Cloud sampler (space 2, register 0)
  samplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
  samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].MipLODBias = 0;
  samplers[1].MaxAnisotropy = 16;
  samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplers[1].ShaderRegister = 0;
  samplers[1].RegisterSpace = 2;
  samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Shadow sampler (space 1, register 1)
  samplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  samplers[2].ShaderRegister = 1;
  samplers[2].RegisterSpace = 1;
  samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  rootSignatureDesc.NumParameters = _countof(rootParameters);
  rootSignatureDesc.pParameters = rootParameters;
  rootSignatureDesc.NumStaticSamplers = 3;
  rootSignatureDesc.pStaticSamplers = samplers;
  rootSignatureDesc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3D12SerializeRootSignature(
      &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  ThrowIfFailed(DX12Context::g_device->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&g_rootSignature)));

  // --- Compile simple shaders for demo triangle (DXC / SM6) ---
  ComPtr<IDxcBlob> vsBlob;
  ComPtr<IDxcBlob> psBlob;
  std::wstring simpleShaderPath = FindShaderFile(L"shaders\\simple.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading simple shader from: %ls\n",
              simpleShaderPath.c_str());
    fprintf(stderr, "%s", debugMsg);
  }
  // Use local DXC helper instance here (module-level ones exist in raster/dxr
  // modules)
  DxcHelper localDxc;
  try {
    vsBlob = localDxc.Compile(simpleShaderPath, L"VSMain", L"vs_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: VS compile failed (DXC) for %ls: %s\n",
            simpleShaderPath.c_str(), e.what());
    return false;
  }
  try {
    psBlob = localDxc.Compile(simpleShaderPath, L"PSMain", L"ps_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: PS compile failed (DXC) for %ls: %s\n",
            simpleShaderPath.c_str(), e.what());
    return false;
  }

  // --- Compile mesh PBR shaders (full vertex layout + PBR material pixel
  // shader) using DXC (SM6) ---
  ComPtr<IDxcBlob> vsMeshBlob;
  ComPtr<IDxcBlob> psMeshBlob;
  std::wstring pbrShaderPath = FindShaderFile(L"shaders\\pbr_mesh.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading PBR shader from: %ls\n",
              pbrShaderPath.c_str());
    fprintf(stderr, "%s", debugMsg);
  }
  try {
    vsMeshBlob = localDxc.Compile(pbrShaderPath, L"VSMainMesh", L"vs_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: VS compile failed (DXC) for %ls: %s\n",
            pbrShaderPath.c_str(), e.what());
    return false;
  }
  try {
    psMeshBlob = localDxc.Compile(pbrShaderPath, L"PSMainMesh", L"ps_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: PS compile failed (DXC) for %ls: %s\n",
            pbrShaderPath.c_str(), e.what());
    return false;
  }

  // --- Create PSO ---
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
  psoDesc.pRootSignature = g_rootSignature.Get();
  psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

  D3D12_RASTERIZER_DESC rasterDesc = {};
  rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
  rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
  rasterDesc.FrontCounterClockwise = FALSE;
  rasterDesc.DepthBias = 0;
  rasterDesc.DepthBiasClamp = 0.0f;
  rasterDesc.SlopeScaledDepthBias = 0.0f;
  rasterDesc.DepthClipEnable = TRUE;
  rasterDesc.MultisampleEnable = FALSE;
  rasterDesc.AntialiasedLineEnable = FALSE;
  rasterDesc.ForcedSampleCount = 0;
  rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  D3D12_BLEND_DESC blendDesc = {};
  blendDesc.AlphaToCoverageEnable = FALSE;
  blendDesc.IndependentBlendEnable = FALSE;
  for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
    blendDesc.RenderTarget[i].BlendEnable = FALSE;
    blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[i].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
  }

  D3D12_DEPTH_STENCIL_DESC depthDesc = {};
  depthDesc.DepthEnable = FALSE;
  depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  depthDesc.StencilEnable = FALSE;

  psoDesc.RasterizerState = rasterDesc;
  psoDesc.BlendState = blendDesc;
  psoDesc.DepthStencilState = depthDesc;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
  psoDesc.SampleDesc.Count = 1;

  ThrowIfFailed(DX12Context::g_device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_pipelineState)));

  // Create grid resources using raster module
  RasterRenderer::CreateGridResources(DX12Context::g_device.Get(),
                                      g_gridThickness);

  // --- Create a mesh PSO (position-only vertex layout, simple pixel shader)
  // ---
  D3D12_INPUT_ELEMENT_DESC meshInputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC meshPsoDesc =
      psoDesc; // copy base description
  meshPsoDesc.InputLayout = {meshInputLayout, _countof(meshInputLayout)};
  meshPsoDesc.VS = {vsMeshBlob->GetBufferPointer(),
                    vsMeshBlob->GetBufferSize()};
  meshPsoDesc.PS = {psMeshBlob->GetBufferPointer(),
                    psMeshBlob->GetBufferSize()};
  meshPsoDesc.NumRenderTargets = 2; // Color + Normal
  meshPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
  meshPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

  // Enable depth testing for mesh rendering
  meshPsoDesc.DepthStencilState.DepthEnable = TRUE;
  meshPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  meshPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  meshPsoDesc.DepthStencilState.StencilEnable = FALSE;

  {
    HRESULT hrMesh = DX12Context::g_device->CreateGraphicsPipelineState(
        &meshPsoDesc, IID_PPV_ARGS(&RasterRenderer::g_meshPipelineState));
    if (FAILED(hrMesh)) {
      fprintf(stderr,
              "InitApplication: CreateGraphicsPipelineState (mesh) failed: "
              "0x%08x\n",
              (unsigned)hrMesh);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(DX12Context::g_device->QueryInterface(
              IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg =
              reinterpret_cast<D3D12_MESSAGE *>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(
              stderr,
              "D3D12 INFO (PSO create): Category=%d Severity=%d ID=%d: %s\n",
              (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
              pMsg->pDescription);
        }
      }
#endif
    }
    ThrowIfFailed(hrMesh);
  }

  // Recreate the mesh PSO via RasterRenderer to pick up debug defines (e.g.
  // RASTER_DEBUG_DEPTH)
  RasterRenderer::RecreateMeshPipeline(DX12Context::g_device.Get(),
                                       g_rootSignature.Get());

  // Additionally create a simple mesh PSO that reads only POSITION and draws a
  // constant color
  {
    std::wstring simplePath = FindShaderFile(L"shaders\\simple.hlsl");
    ComPtr<IDxcBlob> vsMeshSimpleBlob;
    ComPtr<IDxcBlob> psMeshSimpleBlob;
    try {
      vsMeshSimpleBlob =
          localDxc.Compile(simplePath, L"VSMainMeshSimple", L"vs_6_0");
      psMeshSimpleBlob =
          localDxc.Compile(simplePath, L"PSMainMeshSimple", L"ps_6_0");
    } catch (const std::exception &e) {
      fprintf(stderr, "InitD3D12: simple mesh shader compile failed: %s\n",
              e.what());
      vsMeshSimpleBlob = nullptr;
      psMeshSimpleBlob = nullptr;
    }

    if (vsMeshSimpleBlob && psMeshSimpleBlob) {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC simplePso = meshPsoDesc;
      D3D12_INPUT_ELEMENT_DESC posOnlyLayout[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
      simplePso.InputLayout = {posOnlyLayout, _countof(posOnlyLayout)};
      simplePso.VS = {vsMeshSimpleBlob->GetBufferPointer(),
                      vsMeshSimpleBlob->GetBufferSize()};
      simplePso.PS = {psMeshSimpleBlob->GetBufferPointer(),
                      psMeshSimpleBlob->GetBufferSize()};
      if (g_rootSignature)
        simplePso.pRootSignature = g_rootSignature.Get();
      ThrowIfFailed(DX12Context::g_device->CreateGraphicsPipelineState(
          &simplePso, IID_PPV_ARGS(&g_meshSimplePipelineState)));
    }
  }

  // --- Initialize ImGui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ApplyModernImGuiTheme();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  // Legacy ImGui DX12 init path does not advertise RendererHasTextures.
  // Build the atlas up-front so first NewFrame doesn't hit font-atlas assert.
  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ImGui_ImplWin32_Init(hwnd);
  // Initialize DX12 backend with the main CBV/SRV/UAV heap so we can show
  // thumbnails using existing engine texture SRVs.
  DescriptorAllocation imguiFontAlloc = g_cbvSrvAllocator.AllocatePersistent(1);
  ImGui_ImplDX12_Init(DX12Context::g_device.Get(), FrameCount,
                      DXGI_FORMAT_R10G10B10A2_UNORM, g_cbvSrvAllocator.Heap(),
                      imguiFontAlloc.cpu, imguiFontAlloc.gpu);

  ImGui_ImplDX12_CreateDeviceObjects();
  // When viewports are enabled we want windows created by ImGui to look
  // consistent across platform-native child windows.
  ImGuiStyle &style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
  }

  // Initialize asset loader with device & command queue so it can perform
  // uploads
  Asset::Initialize(DX12Context::g_device.Get(),
                    DX12Context::g_commandQueue.Get());
  if (EnsureProceduralGrassBladeMesh()) {
    GrassManager::SetPatchMesh(&g_proceduralGrassBladeMesh);
  }
  if (EnsureProceduralGrassMidMesh()) {
    GrassManager::SetMidPatchMesh(&g_proceduralGrassMidMesh);
  }

  // Initialize IBL Manager and load default environment map
  IBLManager::Get().Initialize(DX12Context::g_device.Get(),
                               DX12Context::g_commandQueue.Get());
  /*
  if (!IBLManager::Get().LoadEnvironmentMap("assets/env.exr")) {
    fprintf(
        stderr,
        "Main: Failed to load assets/env.exr, checking for assets/env.hdr\n");
    IBLManager::Get().LoadEnvironmentMap("assets/env.hdr");
  }
  */

  // Initialize Prague Sky Model
  IBLManager::Get().InitializeSkyModel("assets/PragueSkyModelDataset.dat");
  IBLManager::Get().SetIBLSource(IBLManager::IBLSource::PragueSkyModel);

  // Always allocate a descriptor for the environment map, so it can be updated
  // later even if no file is currently loaded.
  {
    DescriptorAllocation alloc = g_cbvSrvAllocator.AllocatePersistent(1);
    IBLManager::Get().SetGPUHandle(alloc.gpu);
    IBLManager::Get().SetCPUHandle(alloc.cpu);
    g_envMapGpuHandle = alloc.gpu;

    // If loaded, create the view immediately.
    // If not loaded, we ideally need a valid descriptor (null SRV or dummy
    // texture) to prevent crashes if the shader accesses it.
    if (IBLManager::Get().IsLoaded()) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Format = IBLManager::Get().GetEnvMap().format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = (UINT)-1;
      DX12Context::g_device->CreateShaderResourceView(
          IBLManager::Get().GetEnvMap().resource.Get(), &srvDesc, alloc.cpu);
    } else {
      // Create a default scalar (null) SRV or similar?
      // For Texture2D, a null SRV describes a "null resource" but with valid
      // format info. Or we can rely on IBLManager creating a dummy texture.
      // Let's create a NULL SRV so it's a valid descriptor (returns 0 on
      // sample).
      D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
      nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      nullSrvDesc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      nullSrvDesc.Format =
          DXGI_FORMAT_R32G32B32A32_FLOAT; // Standard env map format
      nullSrvDesc.Texture2D.MipLevels = 1;
      DX12Context::g_device->CreateShaderResourceView(nullptr, &nullSrvDesc,
                                                      alloc.cpu);
    }
  }

  // Log: reached post-Create pipeline initialization (stderr only)
  fprintf(stderr, "InitD3D12: Post-pipeline initialization reached\n");

  // Demo triangle removed. Scene will rely on loaded assets for visible
  // geometry. If you need a synthetic fallback, the code below will create a
  // cube when auto-load fails.

  // --- Create constant buffer (upload heap) ---
  struct AlignConstants {
    float offset[4];
  };
  const UINT64 cbSize = (sizeof(AlignConstants) + 255) & ~255;

  D3D12_RESOURCE_DESC cbDesc = {};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = cbSize;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_constantBuffer)));
  ThrowIfFailed(g_constantBuffer->Map(
      0, nullptr, reinterpret_cast<void **>(&g_constantCbMappedData)));

  // Initialize constant buffer data (small offset)
  AlignConstants constants = {{0.2f, 0.0f, 0.0f, 0.0f}};
  memcpy(g_constantCbMappedData, &constants, sizeof(constants));

  // --- Create persistent material constant buffer ---
  // Large enough to hold many unique material instances per frame
  using MaterialCB = MaterialSystem::RuntimeRasterMaterialConstants;
  using DxrMaterialData = MaterialSystem::RuntimeDxrMaterialData;
  using DxrMaterialExtraData = MaterialSystem::RuntimeDxrMaterialExtraData;
  const UINT64 matCbSizeSingle = (sizeof(MaterialCB) + 255) & ~255;
  const UINT64 matCbSize = matCbSizeSingle * 16384; // Support up to 16384 calls
  D3D12_RESOURCE_DESC matCbDesc = {};
  matCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  matCbDesc.Width = matCbSize;
  matCbDesc.Height = 1;
  matCbDesc.DepthOrArraySize = 1;
  matCbDesc.MipLevels = 1;
  matCbDesc.SampleDesc.Count = 1;
  matCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matCbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_materialConstantBuffer)));
  ThrowIfFailed(g_materialConstantBuffer->Map(
      0, nullptr, reinterpret_cast<void **>(&g_materialCbMappedData)));

  // Material Structured Buffer for DXR (tightly packed, no 256B alignment)
  {
    const UINT64 matSbSize = sizeof(DxrMaterialData) * 16384;
    D3D12_RESOURCE_DESC matSbDesc = matCbDesc;
    matSbDesc.Width = matSbSize;
    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matSbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_materialStructuredBuffer)));
  }

  // Material Extra Structured Buffer for DXR (secondary/conditional data)
  {
    const UINT64 matExtraSbSize = sizeof(DxrMaterialExtraData) * 16384;
    D3D12_RESOURCE_DESC matExtraSbDesc = matCbDesc;
    matExtraSbDesc.Width = matExtraSbSize;
    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matExtraSbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_materialExtraStructuredBuffer)));
  }

  // Mesh Structured Buffer for DXR
  {
    struct MeshData {
      int materialIndex;
      int vbIndex;
      int ibIndex;
      int pad;
    };
    const UINT64 meshSbSize = sizeof(MeshData) * 16384;
    D3D12_RESOURCE_DESC meshSbDesc = matCbDesc;
    meshSbDesc.Width = meshSbSize;
    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &meshSbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_meshStructuredBuffer)));
  }

  // Allocate camera constant buffer (upload heap)
  {
    const UINT64 camCbSize = (sizeof(CameraCB) + 255) & ~255;
    D3D12_RESOURCE_DESC camCbDesc = {};
    camCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    camCbDesc.Width = camCbSize;
    camCbDesc.Height = 1;
    camCbDesc.DepthOrArraySize = 1;
    camCbDesc.MipLevels = 1;
    camCbDesc.SampleDesc.Count = 1;
    camCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &camCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_cameraConstantBuffer)));

    // Initialize with default camera
    UINT8 *pCamData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(g_cameraConstantBuffer->Map(
        0, &readRange, reinterpret_cast<void **>(&pCamData)));
    memcpy(pCamData, &g_cameraData, sizeof(g_cameraData));
    g_cameraConstantBuffer->Unmap(0, nullptr);
  }

  // Initialize Cloud Manager (Generate noise texture, upload params)
  {
    fprintf(stderr, "Initializing Cloud Manager...\n");

    // Use a temporary command list to ensure clean state
    ComPtr<ID3D12CommandAllocator> tempAlloc;
    ThrowIfFailed(DX12Context::g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAlloc)));
    ComPtr<ID3D12GraphicsCommandList> tempList;
    ThrowIfFailed(DX12Context::g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAlloc.Get(), nullptr,
        IID_PPV_ARGS(&tempList)));

    g_cloudManager.Initialize(DX12Context::g_device.Get(), tempList.Get());

    // Execute immediately using the existing helper which closes the list and
    // waits
    ExecuteCommandListAndWait(tempList.Get());
  }

  // NOTE: g_commandList was closed early in InitD3D12 (after creation).
  // It will be Reset() in the first frame's PopulateCommandList.

  // Log successful InitD3D12 completion (stderr only)
  fprintf(stderr, "InitD3D12: Completed OK (returning true)\n");

  return true;
}

// Mesh PSO recreation moved to RasterRenderer::RecreateMeshPipeline
// (src/raster_renderer.cpp)

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  if (Input::g_imguiEnabled &&
      ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    return true;

  switch (message) {
  case WM_SETTEXT: {
    // Let Windows process the text first, then enforce our title policy.
    LRESULT result = DefWindowProcW(hWnd, message, wParam, lParam);
    EnsureMainWindowTitle(hWnd);
    return result;
  }
  case WM_CLOSE:
    g_appClosing = true;
    PostQuitMessage(0);
    return 0;
  case WM_SIZE:
    if (!g_appClosing && DX12Context::g_swapChain && wParam != SIZE_MINIMIZED) {
      DX12Context::QueueResize(LOWORD(lParam), HIWORD(lParam));
    }
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine,
                   int nCmdShow) {
#ifdef _DEBUG
  SetUnhandledExceptionFilter(TopLevelExceptionHandler);
#endif

  // Do not create or show a console window here.
  // We assume the caller runs the executable from a terminal (or redirects
  // stdout/stderr). Logging still uses stderr but we won't forcibly allocate a
  // console window.

  // Log to stderr only
  fprintf(stderr, "Application starting...\n");

  // Parse command line for flags and optional custom glTF file
  std::string customGltfPath;
  std::string sceneToLoad;
  bool enableMockLiveLink = false;
  bool autoConnectMaxLiveLinkPipe = false;
  bool autoConnectArchicadLiveLinkPipe = false;
  std::string maxLiveLinkPipeName = "project-render-max-livelink";
  std::string archicadLiveLinkPipeName = "project-render-archicad-livelink";

  // window handle (Qt path will obtain from widget, Win32 path from CreateWindow)
  HWND hwnd = nullptr;
  if (lpCmdLine && *lpCmdLine) {
    std::string cmd = lpCmdLine;
    std::istringstream iss(cmd);
    std::string token;
    while (iss >> token) {
      if (token == "--debug-log") {
#ifdef _DEBUG
        g_debugLog = true;
#else
        fprintf(stderr, "--debug-log ignored in non-debug builds\n");
#endif
      } else if (token == "--fast-import" || token == "--optimize-import") {
        g_fastImport = true;
      } else if (token == "--load") {
        if (iss >> token) {
          // remove quotes if present
          if (!token.empty() && token.front() == '"')
            token = token.substr(1);
          if (!token.empty() && token.back() == '"')
            token = token.substr(0, token.size() - 1);
          sceneToLoad = token;
        }
      } else if (token == "--mock-livelink") {
        enableMockLiveLink = true;
      } else if (token == "--max-livelink-pipe") {
        autoConnectMaxLiveLinkPipe = true;
        if (iss.good()) {
          std::streampos rewindPos = iss.tellg();
          std::string nextToken;
          if (iss >> nextToken) {
            if (!nextToken.empty() && nextToken[0] != '-') {
              maxLiveLinkPipeName = nextToken;
            } else if (rewindPos != std::streampos(-1)) {
              iss.clear();
              iss.seekg(rewindPos);
            }
          } else {
            iss.clear();
          }
        }
      } else if (token == "--archicad-livelink-pipe") {
        autoConnectArchicadLiveLinkPipe = true;
        if (iss.good()) {
          std::streampos rewindPos = iss.tellg();
          std::string nextToken;
          if (iss >> nextToken) {
            if (!nextToken.empty() && nextToken[0] != '-') {
              archicadLiveLinkPipeName = nextToken;
            } else if (rewindPos != std::streampos(-1)) {
              iss.clear();
              iss.seekg(rewindPos);
            }
          } else {
            iss.clear();
          }
        }
      } else {
        // first non-flag token is interpreted as a path
        if (customGltfPath.empty()) {
          // remove quotes if present
          if (!token.empty() && token.front() == '"')
            token = token.substr(1);
          if (!token.empty() && token.back() == '"')
            token = token.substr(0, token.size() - 1);
          customGltfPath = token;
        }
      }
    }
  }

#ifdef USE_QT_UI
  int qtArgc = 0;
  char *qtArgv[] = { nullptr };
  QApplication app(qtArgc, qtArgv);
  ApplyQtTheme(app);

  EnforceReleaseDebugFlags();

  MainWindow w;
  w.show();

  // obtain native handle from DX12View inside MainWindow
  hwnd = reinterpret_cast<HWND>(w.view()->winId());

  if (!InitApplication(hwnd)) {
    QMessageBox::critical(nullptr, "Error", "Failed to initialize application");
    return -1;
  }
#else
  const wchar_t CLASS_NAME[] = L"ProjectRenderWndClass";

  // use the extended version so we can set a small icon directly
  WNDCLASSEXW wcx = {};
  wcx.cbSize = sizeof(wcx);
  wcx.lpfnWndProc = WndProc;
  wcx.hInstance = hInstance;
  wcx.lpszClassName = CLASS_NAME;

  // load icon defined in resources/app.rc (large + small)
  HICON hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
  if (!hIcon) {
    fprintf(stderr, "Main: failed to load icon resource (0x%08x)\n",
            GetLastError());
  }
  wcx.hIcon = hIcon;   // big icon for Alt-Tab/taskbar
  wcx.hIconSm = hIcon; // small icon for title bar

  RegisterClassExW(&wcx);

  hwnd = CreateWindowExW(0, CLASS_NAME, kMainWindowTitle,
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1920, 1080, nullptr, nullptr, hInstance, nullptr);

  if (!hwnd) {
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);

  EnforceReleaseDebugFlags();

  // Log that we showed the window (stderr only)
  fprintf(stderr, "ShowWindow called\n");

  if (!InitApplication(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize application", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  // Defensive: restore the intended Unicode caption in case any startup path
  // accidentally set it through an ANSI codepath.
  SetWindowTextW(hwnd, kMainWindowTitle);

  fprintf(stderr, "InitApplication returned OK\n");
#endif

  // Scene Setup
  if (!sceneToLoad.empty()) {
    if (fs::exists(sceneToLoad)) {
      if (SceneIO::LoadScene(sceneToLoad)) {
        SetCurrentScenePath(sceneToLoad);
        fprintf(stderr, "Startup: loaded scene %s\n", sceneToLoad.c_str());
      } else {
        fprintf(stderr, "Startup: failed to load scene %s\n",
                sceneToLoad.c_str());
      }
    } else {
      fprintf(stderr, "Startup: --load path not found: %s\n",
              sceneToLoad.c_str());
    }
  } else if (!customGltfPath.empty()) {
    if (fs::exists(customGltfPath)) {
      float rootPos[3] = {0, 0, 0};
      if (Scene::ImportModel(customGltfPath, rootPos)) {
        ResetCamera();
      }
    } else {
      fprintf(stderr, "Startup: custom mesh path not found: %s\n",
              customGltfPath.c_str());
    }
  } else {
    // Default: Just ground plane, no auto-loaded GLBs anymore
    Scene::AddDefaultPlane(0.0f);
  }

  try {
    auto provider = std::make_unique<LiveLink::MockLiveLinkProvider>();
    const auto providerId =
        LiveLink::GetCoordinator().RegisterProvider(std::move(provider));
    if (enableMockLiveLink) {
      if (LiveLink::GetCoordinator().ConnectProvider(providerId)) {
        fprintf(stderr,
                "Startup: mock live-link provider enabled (--mock-livelink)\n");
      } else {
        fprintf(stderr,
                "Startup: failed to connect mock live-link provider\n");
      }
    }
  } catch (const std::exception &e) {
    fprintf(stderr,
            "Startup: failed to initialize mock live-link provider: %s\n",
            e.what());
  }

  try {
    auto provider = std::make_unique<LiveLink::NamedPipeLiveLinkProvider>(
        maxLiveLinkPipeName, "3dsMax2025Pipe");
    const auto providerId =
        LiveLink::GetCoordinator().RegisterProvider(std::move(provider));
    if (autoConnectMaxLiveLinkPipe) {
      if (LiveLink::GetCoordinator().ConnectProvider(providerId)) {
        fprintf(stderr,
                "Startup: max live-link pipe enabled (--max-livelink-pipe %s)\n",
                maxLiveLinkPipeName.c_str());
      } else {
        fprintf(stderr,
                "Startup: failed to connect max live-link pipe provider\n");
      }
    }
  } catch (const std::exception &e) {
    fprintf(stderr,
            "Startup: failed to initialize max live-link pipe provider: %s\n",
            e.what());
  }

  try {
    auto provider = std::make_unique<LiveLink::NamedPipeLiveLinkProvider>(
        archicadLiveLinkPipeName, "Archicad28Pipe");
    const auto providerId =
        LiveLink::GetCoordinator().RegisterProvider(std::move(provider));
    if (autoConnectArchicadLiveLinkPipe) {
      if (LiveLink::GetCoordinator().ConnectProvider(providerId)) {
        fprintf(stderr,
                "Startup: archicad live-link pipe enabled (--archicad-livelink-pipe %s)\n",
                archicadLiveLinkPipeName.c_str());
      } else {
        fprintf(stderr,
                "Startup: failed to connect archicad live-link pipe provider\n");
      }
    }
  } catch (const std::exception &e) {
    fprintf(stderr,
            "Startup: failed to initialize archicad live-link pipe provider: %s\n",
            e.what());
  }

  // ReSTIR DI: Initialize test lights for Phase 2
  {
    // User requested to remove all lights except the sun
    std::vector<Light> testLights;
    DxrRenderer::UpdateLights(testLights);
  }

  // Basic message loop + simple render
  MSG msg = {};
  // DenoiserModeFromIndex, DenoiserIndexFromMode, EnsureExportRenderTarget,
  // RestoreRenderExportState, StartRenderExportJob moved to editor_ui.cpp

  auto PopulateCommandList = [&]() {
    if (g_renderExportJob.active) {
      g_currentRenderMode = RenderMode::DXR;
    }

    // Update Sky Parameters (Run every frame to ensure consistency)
    {
      const float PI = 3.14159265f;
      const float DEG2RAD = PI / 180.0f;

      // Physically-based solar position model (local solar time).
      // Latitude: [-66.5, 66.5], Day: [1, 365], Hour angle: 15 deg/hour.
      const float latitudeRad = g_latitudeDeg * DEG2RAD;
      const float day = (std::clamp)(g_dayOfYear, 1.0f, 365.0f);
      const float declDeg =
          23.44f * std::sin(2.0f * PI * (284.0f + day) / 365.0f);
      const float declRad = declDeg * DEG2RAD;
      const float hourAngleRad = (g_timeOfDay - 12.0f) * 15.0f * DEG2RAD;

      float sinEl =
          std::sin(latitudeRad) * std::sin(declRad) +
          std::cos(latitudeRad) * std::cos(declRad) * std::cos(hourAngleRad);
      sinEl = (std::clamp)(sinEl, -1.0f, 1.0f);
      float elRad = std::asin(sinEl);

      // Azimuth from North, clockwise: 90=east, 180=south, 270=west.
      float azNorthRad =
          std::atan2(std::sin(hourAngleRad),
                     std::cos(hourAngleRad) * std::sin(latitudeRad) -
                         std::tan(declRad) * std::cos(latitudeRad)) +
          PI;
      azNorthRad += g_northOffset * DEG2RAD;

      // Prague model convention: azimuth is in XY plane from +X toward +Y.
      // Our world convention in shaders is +Z = north, +X = east, +Y = up.
      // Mapping Prague(X,Y,Z) -> World(X,Z,Y) means:
      //   azPrague = 0 at world +X (east), +PI/2 at world +Z (north).
      float azPragueRad = (PI * 0.5f) - azNorthRad;
      while (azPragueRad < -PI)
        azPragueRad += 2.0f * PI;
      while (azPragueRad > PI)
        azPragueRad -= 2.0f * PI;

      if (elRad < -0.15f) {
        // Keep a bit below horizon for twilight behavior from Prague model,
        // but avoid excessively negative values.
        elRad = -0.15f;
      }

      bool usingFileIBL =
          (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
      if (!usingFileIBL) {
        // Get current parameters to preserve other sliders
        float sunSize = IBLManager::Get().GetSunSize();
        float sunInt = IBLManager::Get().GetSunIntensity();

        // Apply to Sky Model
        IBLManager::Get().SetSolarAltitude(elRad);
        IBLManager::Get().SetSolarAzimuth(azPragueRad);
        IBLManager::Get().UpdateSkyModel();

        // Sync Directional Light (from sky model)
        float sunX = std::sin(azNorthRad) * std::cos(elRad);
        float sunZ = std::cos(azNorthRad) * std::cos(elRad);
        float sunY = std::sin(elRad);

        g_cameraData.lightDir[0] = sunX;
        g_cameraData.lightDir[1] = sunY;
        g_cameraData.lightDir[2] = sunZ;
        // Pass angular radius in radians to w component
        g_cameraData.lightDir[3] = sunSize * DEG2RAD * 0.5f;

        // Sync Sun Color from Sky Model
        auto sunRGB = IBLManager::Get().GetSunColor();
        g_cameraData.lightColor[0] = sunRGB.x;
        g_cameraData.lightColor[1] = sunRGB.y;
        g_cameraData.lightColor[2] = sunRGB.z;

        // Sync Sun Intensity
        g_cameraData.lightColor[3] = sunInt;
      } else {
        // File IBL: use extracted sun if available, otherwise turn off
        if (IBLManager::Get().HasFileSun()) {
          auto worldDir = IBLManager::Get().GetFileSunWorldDir();
          g_cameraData.lightDir[0] = worldDir.x;
          g_cameraData.lightDir[1] = worldDir.y;
          g_cameraData.lightDir[2] = worldDir.z;
          g_cameraData.lightDir[3] =
              IBLManager::Get().GetFileSunRadiusDeg() * DEG2RAD * 0.5f;

          auto rad = IBLManager::Get().GetFileSunRadiance();
          g_cameraData.lightColor[0] = rad.x;
          g_cameraData.lightColor[1] = rad.y;
          g_cameraData.lightColor[2] = rad.z;
          g_cameraData.lightColor[3] = IBLManager::Get().GetFileSunIntensity();
        } else {
          g_cameraData.lightDir[3] = 0.0f;
          g_cameraData.lightColor[3] = 0.0f;
        }
      }

      // IBL environment map rotation (degrees), consumed by shaders.
      g_cameraData.iblRotationDegrees =
          IBLManager::Get().GetIblRotationDegrees();

      // Calculate Shadow Matrix
      {
        using namespace DirectX;
        XMVECTOR lightDir =
            XMVector3Normalize(XMVectorSet(g_cameraData.lightDir[0],
                                           g_cameraData.lightDir[1],
                                           g_cameraData.lightDir[2], 0.0f));
        XMVECTOR camPos = XMLoadFloat3((XMFLOAT3 *)g_cameraData.pos);
        XMVECTOR camFwd = XMLoadFloat3((XMFLOAT3 *)g_cameraData.forward);
        XMVECTOR target = camPos + camFwd * 18.0f;
        XMVECTOR lightForward = XMVectorNegate(lightDir);
        XMVECTOR lightPos = target - lightForward * 70.0f;
        XMVECTOR lightUp = XMVectorSet(0, 1, 0, 0);
        if (fabsf(XMVectorGetX(XMVector3Dot(lightForward, lightUp))) > 0.98f) {
          lightUp = XMVectorSet(0, 0, 1, 0);
        }

        XMMATRIX view = XMMatrixLookToLH(lightPos, lightForward, lightUp);
        XMMATRIX proj = XMMatrixOrthographicLH(56.0f, 56.0f, 1.0f, 140.0f);
        XMMATRIX shadowMat = view * proj;
        XMStoreFloat4x4((XMFLOAT4X4 *)g_cameraData.shadowMatrix,
                        XMMatrixTranspose(shadowMat));

        // ViewProj and InvViewProj for SSR/SSAO.
        // Raster mesh VS projects with positive forward Z, so these matrices
        // must use the same left-handed convention or screen-space effects
        // reconstruct from a different camera basis.
        XMVECTOR camUp = XMLoadFloat3((XMFLOAT3 *)g_cameraData.up);
        XMMATRIX camView = XMMatrixLookToLH(camPos, camFwd, camUp);
        XMMATRIX camProj = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(g_cameraData.fov), g_cameraData.aspect,
            g_cameraData.nearZ, g_cameraData.farZ);
        XMMATRIX vp = camView * camProj;
        XMVECTOR det;
        XMMATRIX invVp = XMMatrixInverse(&det, vp);
        XMStoreFloat4x4((XMFLOAT4X4 *)g_cameraData.viewProj,
                        XMMatrixTranspose(vp));
        XMStoreFloat4x4((XMFLOAT4X4 *)g_cameraData.invViewProj,
                        XMMatrixTranspose(invVp));
      }

      UpdateCameraCB();
    }

    // Log to stderr only (controlled by verbose flag)
    if (g_verboseRenderLogs)
      fprintf(stderr, "PopulateCommandList start\n");

    // Reset per-frame command allocator and command list
    ThrowIfFailed(DX12Context::g_frameResources[DX12Context::g_frameIndex]
                      .commandAllocator->Reset());
    ThrowIfFailed(DX12Context::g_commandList->Reset(
        DX12Context::g_frameResources[DX12Context::g_frameIndex]
            .commandAllocator.Get(),
        nullptr));

    DxrRenderer::BeginFrameProfiling(DX12Context::g_commandList.Get());

    // Reset per-frame transient descriptor allocator
    g_cbvSrvAllocator.ResetFrame(DX12Context::g_frameIndex);

    // Get RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        DX12Context::g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr = rtvHandle.ptr + (SIZE_T)(DX12Context::g_frameIndex *
                                             DX12Context::g_rtvDescriptorSize);

    D3D12_RECT presentationRect = {0, 0, (LONG)DX12Context::g_windowWidth,
                     (LONG)DX12Context::g_windowHeight};
    bool safeFramePreviewActive = false;
    UINT previewWidth = 0;
    UINT previewHeight = 0;
    if (g_renderExportJob.active && g_renderExportJob.targetWidth > 0 &&
        g_renderExportJob.targetHeight > 0) {
      presentationRect.left = 0;
      presentationRect.top = 0;
      presentationRect.right = (LONG)g_renderExportJob.targetWidth;
      presentationRect.bottom = (LONG)g_renderExportJob.targetHeight;
      previewWidth = g_renderExportJob.targetWidth;
      previewHeight = g_renderExportJob.targetHeight;
    } else {
      safeFramePreviewActive = GetSafeFramePreviewRect(
          DX12Context::g_windowWidth, DX12Context::g_windowHeight,
          presentationRect);
      previewWidth =
          (std::max)(1u, static_cast<UINT>(presentationRect.right -
                                           presentationRect.left));
      previewHeight =
          (std::max)(1u, static_cast<UINT>(presentationRect.bottom -
                                           presentationRect.top));
    }
    if (!g_renderExportJob.active && g_rayTracingSupported) {
      static UINT s_lastPreviewWidth = 0;
      static UINT s_lastPreviewHeight = 0;
      if (previewWidth != s_lastPreviewWidth ||
          previewHeight != s_lastPreviewHeight) {
        DxrRenderer::CreateRayTracingPipeline(previewWidth, previewHeight);
        s_lastPreviewWidth = previewWidth;
        s_lastPreviewHeight = previewHeight;
      }
    }

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (float)DX12Context::g_windowWidth;
    viewport.Height = (float)DX12Context::g_windowHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {0, 0, (LONG)DX12Context::g_windowWidth,
                              (LONG)DX12Context::g_windowHeight};

    DX12Context::g_commandList->RSSetViewports(1, &viewport);
    DX12Context::g_commandList->RSSetScissorRects(1, &scissorRect);

    if (IsSceneLoadInProgress()) {
      TR(DX12Context::g_commandList.Get(),
         DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
         D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      FLOAT clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
      DX12Context::g_commandList->ClearRenderTargetView(rtvHandle, clearColor,
                                                        0, nullptr);
    } else {
      // --- Rebuild grass patch list every frame (shared by DXR & Raster)
      // ---
      int activeGrassMaterialIndex = -1;
      {
        static UINT s_prevGrassPatchCount = (UINT)-1;
        static int s_prevGrassMaterial = -2;
        static uint64_t s_prevGrassSceneHash = 0;
        auto sceneInstances_grass = Scene::GetInstances();
        const uint64_t grassSceneHash = ComputeGrassSceneHash(sceneInstances_grass);
        const bool grassSceneChanged = (grassSceneHash != s_prevGrassSceneHash);
        if (grassSceneChanged) {
          g_grassPatches.clear();
          uint32_t grassSourceId = 0;
          for (const auto &inst : sceneInstances_grass) {
            if (!inst.mesh)
              continue;
            int matIdx = inst.mesh->materialIndex;
            if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
              continue;
            const auto &mat = g_loadedMaterials[matIdx];
            if (!mat.isGrass)
              continue;
            if (activeGrassMaterialIndex < 0)
              activeGrassMaterialIndex = matIdx;
            AppendGrassPatchesFromInstance(inst, grassSourceId++, mat,
                                           g_grassPatches);
          }
          GrassManager::SetPatches(g_grassPatches);
          s_prevGrassSceneHash = grassSceneHash;
        } else {
          for (const auto &inst : sceneInstances_grass) {
            if (!inst.mesh)
              continue;
            int matIdx = inst.mesh->materialIndex;
            if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
              continue;
            if (g_loadedMaterials[matIdx].isGrass) {
              activeGrassMaterialIndex = matIdx;
              break;
            }
          }
        }
        GrassManager::PrepareGpuBuffers(DX12Context::g_commandList.Get());
        // Grass always instances dedicated procedural meshes for near and mid.
        if (EnsureProceduralGrassBladeMesh()) {
          if (activeGrassMaterialIndex >= 0) {
            g_proceduralGrassBladeMesh.materialIndex = activeGrassMaterialIndex;
          }
          GrassManager::SetPatchMesh(&g_proceduralGrassBladeMesh);
        }
        if (EnsureProceduralGrassMidMesh()) {
          if (activeGrassMaterialIndex >= 0) {
            g_proceduralGrassMidMesh.materialIndex = activeGrassMaterialIndex;
          }
          GrassManager::SetMidPatchMesh(&g_proceduralGrassMidMesh);
        }
        const UINT currentPatchCount = (UINT)g_grassPatches.size();
        const bool grassTopologyChanged =
            (currentPatchCount != s_prevGrassPatchCount) ||
            (activeGrassMaterialIndex != s_prevGrassMaterial);
        if (grassSceneChanged || grassTopologyChanged) {
          if (grassTopologyChanged) {
            DxrRenderer::RequestAccelerationStructureRebuild();
          } else {
            DxrRenderer::RequestAccelerationStructureUpdate();
          }
          DxrRenderer::ResetAccumulation();
          s_prevGrassPatchCount = currentPatchCount;
          s_prevGrassMaterial = activeGrassMaterialIndex;
        }
      }

      // Render based on current mode
      switch (g_currentRenderMode) {
      case RenderMode::DXR: {
        if (!DxrRenderer::IsReady()) {
          try {
            DX12Context::WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(previewWidth,
                                                  previewHeight);
          } catch (const std::exception &e) {
            fprintf(stderr,
                    "DXR lazy pipeline create failed during mode switch: %s\n",
                    e.what());
          } catch (...) {
            fprintf(
                stderr,
                "DXR lazy pipeline create failed during mode switch: unknown "
                "exception\n");
          }
        }

        // Use DXR module to perform ray dispatch and copy to backbuffer
        if (DxrRenderer::IsReady()) {
          // Update Structured Material Buffers for DXR.
          // Core material data stays at 64 bytes; heavy/conditional values live
          // in a secondary buffer.
          if (g_materialStructuredBuffer && g_materialExtraStructuredBuffer &&
              !g_loadedMaterials.empty()) {
            UINT8 *pCore = nullptr;
            UINT8 *pExtra = nullptr;
            D3D12_RANGE readRange = {0, 0};
            const bool coreMapped = SUCCEEDED(g_materialStructuredBuffer->Map(
                0, &readRange, reinterpret_cast<void **>(&pCore)));
            const bool extraMapped =
                SUCCEEDED(g_materialExtraStructuredBuffer->Map(
                    0, &readRange, reinterpret_cast<void **>(&pExtra)));
            if (coreMapped && extraMapped) {
              for (size_t i = 0; i < g_loadedMaterials.size() && i < 16384;
                   ++i) {
                const auto &srcMat = g_loadedMaterials[i];

                DxrMaterialData mat = {};
                DxrMaterialExtraData extra = {};
                MaterialSystem::BuildRuntimeDxrMaterialData(srcMat, &mat,
                                                           &extra);

                memcpy(pCore + i * sizeof(DxrMaterialData), &mat,
                       sizeof(DxrMaterialData));
                memcpy(pExtra + i * sizeof(DxrMaterialExtraData), &extra,
                       sizeof(DxrMaterialExtraData));
              }
            }
            if (coreMapped) {
              g_materialStructuredBuffer->Unmap(0, nullptr);
            }
            if (extraMapped) {
              g_materialExtraStructuredBuffer->Unmap(0, nullptr);
            }
          }

          // Update Mesh Structured Buffer for DXR
          auto activeMeshes = Scene::GetActiveMeshes();
          auto sceneInstances = Scene::GetInstances();
          const Asset::GpuMesh *patchMesh = GrassManager::GetPatchMesh();
          if (patchMesh && patchMesh->vertexBuffer && patchMesh->indexBuffer) {
            const bool alreadyPresent =
                std::any_of(activeMeshes.begin(), activeMeshes.end(),
                            [patchMesh](const Asset::GpuMesh *m) {
                              return m && m->vertexBuffer.Get() ==
                                              patchMesh->vertexBuffer.Get();
                            });
            if (!alreadyPresent) {
              activeMeshes.push_back(patchMesh);
            }
          }
          if (g_meshStructuredBuffer && !activeMeshes.empty()) {
            struct MeshData {
              int materialIndex;
              int vbIndex;
              int ibIndex;
              int pad;
            };
            UINT8 *pData = nullptr;
            D3D12_RANGE readRange = {0, 0};
            if (SUCCEEDED(g_meshStructuredBuffer->Map(
                    0, &readRange, reinterpret_cast<void **>(&pData)))) {
              // We use global indices for vertices/indices in DXR
              for (size_t i = 0; i < activeMeshes.size() && i < 16384; ++i) {
                MeshData m = {};
                m.materialIndex = activeMeshes[i]->materialIndex;
                m.vbIndex = (int)i;
                m.ibIndex = (int)i;
                memcpy(pData + i * sizeof(MeshData), &m, sizeof(MeshData));
              }
              g_meshStructuredBuffer->Unmap(0, nullptr);
            }
          }

          ID3D12Resource *dxrTarget =
              DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get();
          D3D12_CPU_DESCRIPTOR_HANDLE dxrRtv = rtvHandle;
          if (g_renderExportJob.active && g_exportRenderTarget &&
              g_exportRtvHeap) {
            dxrTarget = g_exportRenderTarget.Get();
            dxrRtv = g_exportRtvHeap->GetCPUDescriptorHandleForHeapStart();
          }

          bool dxrOk = DxrRenderer::RenderFrame(
              DX12Context::g_commandList.Get(),
              DX12Context::g_frameResources[DX12Context::g_frameIndex]
                  .commandAllocator.Get(),
              DX12Context::g_frameIndex, dxrTarget, dxrRtv,
              g_cameraConstantBuffer.Get(), g_materialStructuredBuffer.Get(),
              g_texturesGpuStart, g_textureDescriptorCount, activeMeshes,
              g_meshStructuredBuffer.Get(),
              g_materialExtraStructuredBuffer.Get(),
              static_cast<UINT>(presentationRect.left),
              static_cast<UINT>(presentationRect.top), previewWidth,
              previewHeight);
          if (dxrOk) {
            if (g_renderExportJob.active &&
                dxrTarget == g_exportRenderTarget.Get()) {
              g_exportRenderTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            // Success DXR render - Draw Grid with depth checks
            if (!g_renderExportJob.active && g_drawGrid) {
              D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
                  DX12Context::g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
              if (safeFramePreviewActive) {
                D3D12_VIEWPORT safeViewport = {
                    static_cast<float>(presentationRect.left),
                    static_cast<float>(presentationRect.top),
                    static_cast<float>(previewWidth),
                    static_cast<float>(previewHeight), 0.0f, 1.0f};
                DX12Context::g_commandList->RSSetViewports(1, &safeViewport);
                DX12Context::g_commandList->RSSetScissorRects(1,
                                                              &presentationRect);
              }
              DX12Context::g_commandList->ClearDepthStencilView(
                  dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

              // 1. Scene depth pre-pass for grid occlusion.
              DX12Context::g_commandList->OMSetRenderTargets(0, nullptr, FALSE,
                                                             &dsvHandle);
              DX12Context::g_commandList->SetGraphicsRootSignature(
                  g_rootSignature.Get());
              RasterRenderer::DrawSceneDepthOnly(
                  DX12Context::g_commandList.Get(),
                  g_cameraConstantBuffer.Get(), sceneInstances);

              // 2. Draw the grid against the populated depth buffer.
              DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle,
                                                             FALSE, &dsvHandle);
              RasterRenderer::DrawGrid(DX12Context::g_commandList.Get(),
                                       g_cameraConstantBuffer.Get());
              if (safeFramePreviewActive) {
                DX12Context::g_commandList->RSSetViewports(1, &viewport);
                DX12Context::g_commandList->RSSetScissorRects(1, &scissorRect);
              }
            }
            if (g_renderExportJob.active) {
              TR(DX12Context::g_commandList.Get(),
                 DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
                 D3D12_RESOURCE_STATE_PRESENT,
                 D3D12_RESOURCE_STATE_RENDER_TARGET);
              FLOAT clearColor[] = {0.08f, 0.08f, 0.09f, 1.0f};
              DX12Context::g_commandList->ClearRenderTargetView(
                  rtvHandle, clearColor, 0, nullptr);
              DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle,
                                                             FALSE, nullptr);
            }
          } else {
            // If RenderFrame failed, fall back to red clear.
            TR(DX12Context::g_commandList.Get(),
               DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
               D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

            FLOAT clearColor[] = {0.8f, 0.2f, 0.2f, 1.0f};
            DX12Context::g_commandList->ClearRenderTargetView(
                rtvHandle, clearColor, 0, nullptr);
            DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle,
                                                           FALSE, nullptr);
          }
        } else {
          // DXR mode was selected but the renderer isn't ready yet. Avoid
          // leaving the previous raster frame onscreen.
          if (g_verboseRenderLogs)
            fprintf(stderr, "Main: DXR path selected but IsReady()==false\n");
          TR(DX12Context::g_commandList.Get(),
             DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
          FLOAT clearColor[] = {0.8f, 0.2f, 0.2f, 1.0f};
          DX12Context::g_commandList->ClearRenderTargetView(
              rtvHandle, clearColor, 0, nullptr);
          DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                         nullptr);
        }
        break;
      }

      case RenderMode::Raster: {
        // Fast Rasterization Path
        // Log to stderr only (controlled by verbose flag)
        if (g_verboseRenderLogs)
          fprintf(stderr, "Entering Raster Path\n");

        TR(DX12Context::g_commandList.Get(),
           DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
           D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
            DX12Context::g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE rasterRtv = rtvHandle;
        bool rasterHdrReady = RasterRenderer::PrepareHdrRenderTarget(
            DX12Context::g_device.Get(), DX12Context::g_commandList.Get(),
          previewWidth, previewHeight, dsvHandle);

        if (!rasterHdrReady) {
          // Fallback: bind backbuffer directly
          DX12Context::g_commandList->OMSetRenderTargets(1, &rasterRtv, FALSE,
                                                         &dsvHandle);
          FLOAT clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
          DX12Context::g_commandList->ClearRenderTargetView(
              rasterRtv, clearColor, 0, nullptr);
          DX12Context::g_commandList->ClearDepthStencilView(
              dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }

        DX12Context::g_commandList->SetGraphicsRootSignature(
            g_rootSignature.Get());
        // Use camera constant buffer for proper camera movement
        if (g_cameraConstantBuffer) {
          DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
              0, g_cameraConstantBuffer->GetGPUVirtualAddress());
        }

        // HDR render targets already bound by PrepareHdrRenderTarget above

        // Bind global descriptor heap once for all raster calls
        ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
        DX12Context::g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

        // Draw Skybox (Always passes depth, but doesn't write depth)
        if (g_cloudManager.NeedsBake()) {
          fprintf(stderr,
                  "Main: calling g_cloudManager.BakeSky() before DrawSkybox\n");
          g_cloudManager.BakeSky(DX12Context::g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());
          fprintf(stderr, "Main: returned from g_cloudManager.BakeSky()\n");
        }
        RasterRenderer::DrawSkybox(DX12Context::g_commandList.Get(),
                                   g_cameraConstantBuffer.Get());

        // Draw ground grid (optional) via raster module
        if (g_drawGrid) {
          RasterRenderer::DrawGrid(DX12Context::g_commandList.Get(),
                                   g_cameraConstantBuffer.Get());
        }

        // Draw loaded meshes
        auto sceneInstances = Scene::GetInstances();
        if (!sceneInstances.empty() && RasterRenderer::g_meshPipelineState) {
          // Log to stderr only (controlled by verbose flag)
          if (g_verboseRenderLogs)
            fprintf(stderr, "Drawing %zu instances\n", sceneInstances.size());

          // Shadow Pass
          RasterRenderer::DrawShadowMap(DX12Context::g_commandList.Get(),
                                        g_cameraConstantBuffer.Get(),
                                        sceneInstances);
        
          // Re-bind HDR render targets for main pass
          RasterRenderer::BindHdrRenderTarget(DX12Context::g_device.Get(), DX12Context::g_commandList.Get(), dsvHandle);

          // Use the RasterRenderer mesh PSO (may output debug depth/uv
          // depending on compile defines)
          DX12Context::g_commandList->SetPipelineState(
              RasterRenderer::g_meshPipelineState.Get());
          DX12Context::g_commandList->IASetPrimitiveTopology(
              D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          // HDR render targets already bound by PrepareHdrRenderTarget
          // Use camera constant buffer for mesh rendering
          if (g_cameraConstantBuffer) {
            DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                0, g_cameraConstantBuffer->GetGPUVirtualAddress());
          }

          // Bind common textures and IBL once for all instances
          if (g_textureDescriptorCount > 0) {
            DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
                1, g_texturesGpuStart);
          }

            // Bind Env Map + Shadow Map at root index 4 (space 1).
            // When no IBL is loaded, bind a null env SRV so raster shading stays
            // defined instead of sampling an unbound descriptor.
            {
            auto alloc =
              g_cbvSrvAllocator.Allocate(DX12Context::g_frameIndex % 2, 2);
            auto device = DX12Context::g_device.Get();

            if (IBLManager::Get().IsLoaded()) {
              device->CopyDescriptorsSimple(
                1, alloc.cpu, IBLManager::Get().GetCPUHandle(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            } else {
              D3D12_SHADER_RESOURCE_VIEW_DESC nullEnvSrv = {};
              nullEnvSrv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
              nullEnvSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
              nullEnvSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
              nullEnvSrv.Texture2D.MipLevels = 1;
              device->CreateShaderResourceView(nullptr, &nullEnvSrv,
                               alloc.cpu);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu = alloc.cpu;
            shadowCpu.ptr += device->GetDescriptorHandleIncrementSize(
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CopyDescriptorsSimple(
              1, shadowCpu, RasterRenderer::GetShadowMapSrvCpu(),
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
              4, alloc.gpu);
            }

          // Draw all instances
          int lastMaterialIndex = -2;
          ID3D12Resource *lastVB = nullptr;
          ID3D12Resource *lastIB = nullptr;

          for (size_t i = 0; i < sceneInstances.size(); ++i) {
            const auto &inst = sceneInstances[i];
            if (!inst.mesh)
              continue;
            const auto &gm = *inst.mesh;
            // Skip meshes that have been deleted or not properly initialized
            if (!gm.vertexBuffer || !gm.indexBuffer ||
                gm.ibView.SizeInBytes == 0)
              continue;

            // Set instance transform
            DX12Context::g_commandList->SetGraphicsRoot32BitConstants(
                3, 16, &inst.transform, 0);

            if (gm.materialIndex >= 0 &&
                gm.materialIndex < (int)g_loadedMaterials.size()) {
              if (gm.materialIndex != lastMaterialIndex) {
              MaterialCB matCB = {};

                const auto &srcMat = g_loadedMaterials[gm.materialIndex];
              MaterialSystem::BuildRuntimeRasterMaterialConstants(srcMat,
                                        &matCB);

                if (g_materialCbMappedData) {
                  const UINT64 matSlotSize = (sizeof(MaterialCB) + 255) & ~255;
                  // Use material index based slotting to capitalize on shared
                  // materials
                  UINT64 offset = (gm.materialIndex % 16384) * matSlotSize;
                  memcpy((uint8_t *)g_materialCbMappedData + offset, &matCB,
                         sizeof(matCB));
                  DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                      2, g_materialConstantBuffer->GetGPUVirtualAddress() +
                             offset);
                }
                lastMaterialIndex = gm.materialIndex;
              }
            }

            if (gm.vertexBuffer.Get() != lastVB) {
              DX12Context::g_commandList->IASetVertexBuffers(0, 1, &gm.vbView);
              lastVB = gm.vertexBuffer.Get();
            }
            if (gm.indexBuffer.Get() != lastIB) {
              DX12Context::g_commandList->IASetIndexBuffer(&gm.ibView);
              lastIB = gm.indexBuffer.Get();
            }

            if (gm.indexCount > 0) {
              DX12Context::g_commandList->DrawIndexedInstanced(
                  gm.indexCount, 1, 0, 0, 0);
            }
          }
        }

        if (activeGrassMaterialIndex >= 0 &&
            activeGrassMaterialIndex < (int)g_loadedMaterials.size() &&
            RasterRenderer::g_grassPipelineState &&
            GrassManager::GetPatchCount() > 0 &&
            g_proceduralGrassBladeMesh.vertexBuffer &&
            g_proceduralGrassBladeMesh.indexBuffer) {
          GrassManager::CullingAndPrepareIndirect(DX12Context::g_commandList.Get(),
                                                  g_cameraConstantBuffer.Get());

          DX12Context::g_commandList->SetPipelineState(
              RasterRenderer::g_grassPipelineState.Get());
          DX12Context::g_commandList->IASetPrimitiveTopology(
              D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          if (g_cameraConstantBuffer) {
            DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                0, g_cameraConstantBuffer->GetGPUVirtualAddress());
          }
          if (g_textureDescriptorCount > 0) {
            DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
                1, g_texturesGpuStart);
          }
            {
            auto alloc =
              g_cbvSrvAllocator.Allocate(DX12Context::g_frameIndex % 2, 2);
            auto device = DX12Context::g_device.Get();
            if (IBLManager::Get().IsLoaded()) {
              device->CopyDescriptorsSimple(
                1, alloc.cpu, IBLManager::Get().GetCPUHandle(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            } else {
              D3D12_SHADER_RESOURCE_VIEW_DESC nullEnvSrv = {};
              nullEnvSrv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
              nullEnvSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
              nullEnvSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
              nullEnvSrv.Texture2D.MipLevels = 1;
              device->CreateShaderResourceView(nullptr, &nullEnvSrv,
                               alloc.cpu);
            }
            D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu = alloc.cpu;
            shadowCpu.ptr += device->GetDescriptorHandleIncrementSize(
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CopyDescriptorsSimple(
              1, shadowCpu, RasterRenderer::GetShadowMapSrvCpu(),
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
              4, alloc.gpu);
            }

            MaterialCB grassMatCB = {};

          const auto &srcMat = g_loadedMaterials[activeGrassMaterialIndex];
            MaterialSystem::BuildRuntimeRasterMaterialConstants(srcMat,
                                      &grassMatCB);

          if (g_materialCbMappedData) {
            const UINT64 matSlotSize = (sizeof(MaterialCB) + 255) & ~255;
            UINT64 offset = (activeGrassMaterialIndex % 16384) * matSlotSize;
            memcpy((uint8_t *)g_materialCbMappedData + offset, &grassMatCB,
                   sizeof(grassMatCB));
            DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                2, g_materialConstantBuffer->GetGPUVirtualAddress() + offset);
          }

          DX12Context::g_commandList->SetGraphicsRootShaderResourceView(
              6, GrassManager::GetInstanceBufferGpuAddress());
          DX12Context::g_commandList->SetGraphicsRootShaderResourceView(
              7, GrassManager::GetVisibleBufferGpuAddress(
                     GrassManager::LodBand::Near));
          GrassManager::DrawVisible(DX12Context::g_commandList.Get(),
                                    GrassManager::LodBand::Near);
          if (g_proceduralGrassMidMesh.vertexBuffer &&
              g_proceduralGrassMidMesh.indexBuffer) {
            DX12Context::g_commandList->SetGraphicsRootShaderResourceView(
                7, GrassManager::GetVisibleBufferGpuAddress(
                       GrassManager::LodBand::Mid));
            GrassManager::DrawVisible(DX12Context::g_commandList.Get(),
                                      GrassManager::LodBand::Mid);
          }
        }

        if (rasterHdrReady) {
          RasterRenderer::TonemapHdrToBackbuffer(
              DX12Context::g_device.Get(), DX12Context::g_commandList.Get(),
              DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
              previewWidth, previewHeight, g_cameraConstantBuffer.Get(),
              DX12Context::g_depthBuffer.Get(),
              static_cast<UINT>(presentationRect.left),
              static_cast<UINT>(presentationRect.top));
        }

        DxrRenderer::EndFrameProfiling(DX12Context::g_commandList.Get());
        break;
      }
      }
    } // End else !IsSceneLoadInProgress()

    // Render ImGui (Overlay on top of whatever was drawn)
    if (g_renderExportJob.active && g_exportRenderTarget &&
        g_exportPreviewSrvGpu.ptr != 0 &&
        g_exportRenderTargetState !=
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      TR(DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
         g_exportRenderTargetState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      g_exportRenderTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Ensure UI always targets the swapchain backbuffer RTV.
    DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                   nullptr);

    ID3D12DescriptorHeap *ppHeaps[] = {g_cbvSrvAllocator.Heap()};
    DX12Context::g_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),
                                  DX12Context::g_commandList.Get());

    // Handle multi-viewport windows (platform windows) when enabled so
    // ImGui viewports receive input and are properly rendered.
    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }

    if (g_renderExportJob.active && g_exportRenderTarget &&
        g_exportRenderTargetState != D3D12_RESOURCE_STATE_PRESENT) {
      TR(DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
         g_exportRenderTargetState, D3D12_RESOURCE_STATE_PRESENT);
      g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
    }

    // Transition back to present
    TR(DX12Context::g_commandList.Get(),
       DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
       D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(DX12Context::g_commandList->Close());
  };

  auto RecreateDevice = [&]() {
    // Invalidate ImGui device objects before device reset
    ImGui_ImplDX12_InvalidateDeviceObjects();

    // Release GPU resources
    g_pipelineState.Reset();
    g_rootSignature.Reset();
    g_vertexBuffer.Reset();
    g_constantBuffer.Reset();
    DX12Context::g_commandList.Reset();

    for (UINT i = 0; i < FrameCount; ++i) {
      DX12Context::g_frameResources[i].commandAllocator.Reset();
    }

    // Attempt reinitialization
    if (!InitApplication(g_hwnd)) {
      MessageBoxA(nullptr, "Failed to recreate D3D12 device.",
                  "Device Recovery", MB_OK | MB_ICONERROR);
      ExitProcess(static_cast<UINT>(-1));
    }
  };

  auto CheckDeviceRemoved = [&]() {
    HRESULT reason = DX12Context::g_device->GetDeviceRemovedReason();
    if (FAILED(reason)) {
      // Attempt to recreate the device
      RecreateDevice();
      return true;
    }
    return false;
  };

  // Log entering main loop (stderr only)
  fprintf(stderr, "Entering main loop\n");
  fflush(stderr);

  // Setup timing for camera movement
  static auto prevTime = std::chrono::high_resolution_clock::now();

  // Enter main loop (simple, no extra SEH wrappers)
  while (msg.message != WM_QUIT) {
    // fprintf(stderr, "MainLoop: start iteration\n");
    fflush(stderr);
 #ifdef USE_QT_UI
    app.processEvents();
    if (!w.isVisible() || g_appClosing)
      break;
 #else
    bool handledWindowMessage = false;
    EnsureMainWindowTitle(hwnd);
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      handledWindowMessage = true;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT || g_appClosing)
        break;
    }
    if (msg.message == WM_QUIT || g_appClosing)
      break;
 #endif
    if (g_appClosing)
      break;

    if (DX12Context::g_swapChain) {
      UINT resizeW = 0;
      UINT resizeH = 0;
      if (DX12Context::ConsumePendingResize(resizeW, resizeH)) {
        DX12Context::ResizeSwapChain(resizeW, resizeH);
      }
    }

    // Compute delta time
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> dtDur = now - prevTime;
    float dt = dtDur.count();
    prevTime = now;

    // FPS accumulation (smoothed, updated every 0.25s)
    static float g_fps = 0.0f;
    static float g_fpsTimer = 0.0f;
    static int g_fpsFrames = 0;
    ++g_fpsFrames;
    g_fpsTimer += dt;
    if (g_fpsTimer >= 0.25f) {
      g_fps = static_cast<float>(g_fpsFrames) / g_fpsTimer;
      g_fpsFrames = 0;
      g_fpsTimer = 0.0f;
    }

    Input::Update(dt);

    LiveLink::TickRuntime();

    D3D12_RECT previewRect = {0, 0, (LONG)DX12Context::g_windowWidth,
                  (LONG)DX12Context::g_windowHeight};
    GetSafeFramePreviewRect(DX12Context::g_windowWidth,
                DX12Context::g_windowHeight, previewRect);
    const UINT previewWidth =
      (std::max)(1u, static_cast<UINT>(previewRect.right - previewRect.left));
    const UINT previewHeight =
      (std::max)(1u, static_cast<UINT>(previewRect.bottom - previewRect.top));

    if (g_renderExportJob.active) {
      g_currentRenderMode = RenderMode::DXR;

      const UINT currentSpp = DxrRenderer::GetDisplayedSampleCount();
      const float currentNoise = DxrRenderer::GetCurrentNoiseLevel();
      const bool hasNoiseEstimate = DxrRenderer::HasNoiseEstimate();
      const bool sppDone = currentSpp >= (UINT)g_renderExportJob.targetMaxSpp;
      const bool noiseDone =
          (currentSpp >= g_renderExportJob.minSppBeforeNoiseStop) &&
          hasNoiseEstimate &&
          (currentNoise <= g_renderExportJob.targetNoiseThreshold * 0.90f);

        const bool reachedEnd =
          sppDone || (g_renderExportJob.allowNoiseThresholdStop && noiseDone);

      if (reachedEnd && !g_renderExportJob.completionArmed) {
        g_renderExportJob.completionArmed = true;
        g_renderExportJob.completionFrames = 0;
        g_renderExportJob.settleFramesRemaining =
            (g_renderExportSettings.denoiserIndex == 0) ? 1 : 3;
      }

      if (g_renderExportJob.completionArmed) {
        ++g_renderExportJob.completionFrames;
        if (g_renderExportJob.settleFramesRemaining > 0) {
          --g_renderExportJob.settleFramesRemaining;
        } else {
          const bool denoiserEnabled =
              (g_renderExportSettings.denoiserIndex != 0);
          const bool denoisedReady = DxrRenderer::HasDenoisedOutput();
          // Wait for the one-shot denoiser to produce output. Keep a timeout so
          // export cannot hang forever on denoiser failures.
          if (denoiserEnabled && !denoisedReady &&
              g_renderExportJob.completionFrames < 240) {
            // keep waiting
          } else {
            const bool exported = DxrRenderer::ExportTonemappedFrameToPng(
                g_renderExportJob.outputPath);
            const std::string outPathUtf8 =
                WStringToUtf8(g_renderExportJob.outputPath);
            if (exported) {
              g_renderExportStatus = "Saved: " + outPathUtf8;
              fprintf(stderr, "Render export finished: %s\n",
                      outPathUtf8.c_str());
            } else {
              g_renderExportStatus = "Export failed: " + outPathUtf8;
              fprintf(stderr, "Render export failed: %s\n",
                      outPathUtf8.c_str());
            }
            RestoreRenderExportState();
            AdvanceBatchRenderExport(exported);
            AdvanceAnimationRenderExport(exported);
          }
        }
      }
    }

    // Update camera forward from yaw/pitch
    g_cameraData.forward[0] = (cosf(g_camPitch) * sinf(g_camYaw));
    g_cameraData.forward[1] = sinf(g_camPitch);
    g_cameraData.forward[2] = (cosf(g_camPitch) * -cosf(g_camYaw));

    // Match the projection aspect to the actual render target. Export jobs use
    // an offscreen target that can differ from the viewport size.
    const bool usingExportAspect =
        g_renderExportJob.active && g_renderExportJob.targetWidth > 0 &&
        g_renderExportJob.targetHeight > 0;
    const float targetWidth = usingExportAspect
                                  ? (float)g_renderExportJob.targetWidth
                    : (float)previewWidth;
    const float targetHeight = usingExportAspect
                                   ? (float)g_renderExportJob.targetHeight
                     : (float)previewHeight;
    if (targetHeight > 0.0f) {
      g_cameraData.aspect = targetWidth / targetHeight;
    }
#ifdef _DEBUG
    g_cameraData.debugMode = (float)g_debugMode;
#else
    g_debugMode = 0;
    g_cameraData.debugMode = 0.0f;
    g_cameraData.debugVisualizationMode = 0.0f;
#endif
    g_cameraData.lightCount = (float)DxrRenderer::GetLightCount();
    g_cameraData.frameCount = (float)DxrRenderer::GetDisplayedSampleCount();
    const bool fileIblActive =
        (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
    const bool proceduralSkyActive =
      (IBLManager::Get().GetIBLSource() ==
       IBLManager::IBLSource::PragueSkyModel);
    const bool effectiveCloudRendering =
        g_cloudRenderingEnabled && !fileIblActive;
    g_cameraData.cloudRenderingEnabled = effectiveCloudRendering ? 1.0f : 0.0f;
    g_cameraData.dxrProceduralSkyBoost = proceduralSkyActive ? 2.7f : 1.0f;
    UpdateCameraCB();

    // Update Cloud Manager (uploads changed params to GPU)
    g_cloudManager.Update(dt, DX12Context::g_frameIndex);

    // Editor UI (moved to editor_ui.cpp)
    DrawEditorUI(g_fps, g_timeOfDay, g_northOffset, g_latitudeDeg, g_dayOfYear);

  #ifdef USE_QT_UI
    const bool canIdleDxr =
      !g_renderExportJob.active && g_currentRenderMode == RenderMode::DXR &&
      DxrRenderer::IsReady() && DxrRenderer::CanIdleWithoutRendering();
  #else
    const bool canIdleDxr =
      !handledWindowMessage && !g_renderExportJob.active &&
      g_currentRenderMode == RenderMode::DXR && DxrRenderer::IsReady() &&
      DxrRenderer::CanIdleWithoutRendering();
  #endif
    if (canIdleDxr) {
      prevTime = std::chrono::high_resolution_clock::now();
      WaitMessage();
      prevTime = std::chrono::high_resolution_clock::now();
      continue;
    }

    // fprintf(stderr, "MainLoop: PopulateCommandList start\n");
    PopulateCommandList();
    // fprintf(stderr, "MainLoop: PopulateCommandList done\n");

    ID3D12CommandList *ppCommandLists[] = {DX12Context::g_commandList.Get()};
    DX12Context::g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                                                     ppCommandLists);
    DxrRenderer::SubmitAsyncRestirWork();
    // fprintf(stderr, "MainLoop: ExecuteCommandLists done\n");

    // fprintf(stderr, "MainLoop: Present start\n");
    const HRESULT presentHr = DX12Context::g_swapChain->Present(1, 0);
    if (FAILED(presentHr)) {
      fprintf(stderr, "MainLoop: Present failed with hr=0x%08x\n",
              static_cast<unsigned>(presentHr));
      if (presentHr == DXGI_ERROR_DEVICE_REMOVED ||
          presentHr == DXGI_ERROR_DEVICE_RESET ||
          presentHr == DXGI_ERROR_DEVICE_HUNG) {
        RecreateDevice();
        prevTime = std::chrono::high_resolution_clock::now();
        continue;
      }
      if (presentHr == E_ABORT || presentHr == DXGI_ERROR_ACCESS_LOST) {
        if (CheckDeviceRemoved()) {
          prevTime = std::chrono::high_resolution_clock::now();
          continue;
        }
        Sleep(16);
        prevTime = std::chrono::high_resolution_clock::now();
        continue;
      }
      ThrowIfFailed(presentHr);
    }
    // fprintf(stderr, "MainLoop: Present done\n");
    //  Signal and increment the fence value.
    const UINT64 currentFenceValue =
        DX12Context::g_fenceValues[DX12Context::g_frameIndex];
    ThrowIfFailed(DX12Context::g_commandQueue->Signal(
        DX12Context::g_fence.Get(), currentFenceValue));
    DX12Context::g_fenceValues[DX12Context::g_frameIndex]++;

    // Wait for previous frame
    DX12Context::WaitForPreviousFrame();

    // Check if the GPU was removed (TDR) during the last frame
    if (CheckDeviceRemoved()) {
      fprintf(stderr, "MainLoop: Device was removed. Re-initializing...\n");
      continue; // Start fresh next iteration
    }

    // fprintf(stderr, "MainLoop: end iteration\n");
  }

  // Shutdown ImGui and cleanup
  // Persist panel visibility so user window open/closed state is remembered
  SavePanelVisibility();
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // Cleanup fence event
  CloseHandle(DX12Context::g_fenceEvent);

  return 0;
}
