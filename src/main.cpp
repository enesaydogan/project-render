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
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMessageBox>
#include <QPixmap>
#include "qt/DX12View.h"
#include "qt/MainWindow.h"
#include "qt/QtTheme.h"
#endif
#include <chrono>
#include <cmath>
#include <codecvt>
#include <commctrl.h>
#include <commdlg.h>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <locale>
#include <oleidl.h>
#include <shellapi.h>
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
static bool g_forceGrassRuntimeRefresh = true;
static UINT g_prevGrassPatchCount = (UINT)-1;
static uint64_t g_prevGrassMaterialHash = 0;
static uint64_t g_prevGrassSceneHash = 0;
extern std::vector<Asset::Material> g_loadedMaterials;

void RequestGrassRuntimeRefreshForSceneLoad() {
  g_forceGrassRuntimeRefresh = true;
  g_prevGrassPatchCount = (UINT)-1;
  g_prevGrassMaterialHash = 0;
  g_prevGrassSceneHash = 0;
  g_grassPatches.clear();
  GrassManager::SetPatches(g_grassPatches);
  DxrRenderer::RequestAccelerationStructureRebuild();
  DxrRenderer::RequestSceneLoadWarmup("grass scene load refresh");
  DxrRenderer::ResetAccumulation();
}

namespace {
constexpr float kTwoPi = 6.283185307179586f;
constexpr DWORD kFinalFrameIdleWaitMs = 16;
constexpr DWORD kIdleUiFrameIntervalMs = 33;

static void WaitForSoftIdleMessage(DWORD timeoutMs = kFinalFrameIdleWaitMs) {
  MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT,
                              MWMO_INPUTAVAILABLE);
}

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

  // Build a low, arched grass tuft.  The user-facing blade size is treated as
  // a lawn scale, so the mesh stays broad and soft instead of becoming tall
  // upright spikes.
  std::vector<Asset::Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(80);
  indices.reserve(360);

  auto addBladePlane = [&](float yawRadians, float baseOffsetX,
                           float baseOffsetZ, float widthScale,
                           float heightScale, float sideLean,
                           float forwardLean) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const float halfWidths[5] = {0.082f, 0.072f, 0.050f, 0.023f, 0.005f};
    const float heights[5] = {0.00f, 0.13f, 0.28f, 0.44f, 0.56f};
    const float curve[5] = {0.000f, 0.030f, 0.078f, 0.138f, 0.205f};
    const float uvsV[5] = {1.00f, 0.76f, 0.48f, 0.21f, 0.00f};

    for (int row = 0; row < 5; ++row) {
      const float width = halfWidths[row] * widthScale;
      const float height = heights[row] * heightScale;
      const float bend = curve[row] * heightScale * forwardLean;
      const float lateral = sideLean * heights[row];

      auto makeVertex = [&](float side, float u) {
        const float localX = baseOffsetX + side * width + lateral;
        const float localY = height;
        const float localZ = baseOffsetZ + bend;
        Asset::Vertex v = {};
        v.pos[0] = localX * c - localZ * s;
        v.pos[1] = localY;
        v.pos[2] = localX * s + localZ * c;
        v.normal[0] = 0.55f * c;
        v.normal[1] = 0.58f;
        v.normal[2] = 0.55f * s;
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

    for (uint32_t row = 0; row < 4; ++row) {
      const uint32_t i0 = base + row * 2;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + 2;
      const uint32_t i3 = i0 + 3;
      indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3, i2, i1, i0,
                                     i3, i1, i2});
    }
  };

  addBladePlane(0.12f, -0.040f, -0.020f, 1.10f, 1.00f, -0.026f, 1.08f);
  addBladePlane(0.86f, 0.026f, -0.030f, 0.95f, 0.90f, 0.022f, 1.20f);
  addBladePlane(1.62f, -0.004f, 0.006f, 0.88f, 0.82f, -0.014f, 0.92f);
  addBladePlane(2.39f, 0.036f, 0.020f, 0.76f, 0.70f, 0.016f, 1.32f);
  addBladePlane(3.18f, -0.030f, 0.030f, 0.82f, 0.76f, -0.018f, 1.15f);
  addBladePlane(4.05f, 0.010f, 0.010f, 0.68f, 0.62f, 0.010f, 1.38f);
  addBladePlane(5.18f, -0.010f, -0.006f, 0.72f, 0.66f, -0.008f, 0.85f);

  Asset::GpuMesh gm = Asset::LoadMeshFromMemory(vertices, indices);
  if (!gm.vertexBuffer || !gm.indexBuffer || gm.indexCount == 0) {
    fprintf(stderr, "Grass: failed to create procedural blade mesh\n");
    return false;
  }
  gm.materialIndex = -1;
  gm.minBound[0] = -0.16f;
  gm.minBound[1] = 0.0f;
  gm.minBound[2] = -0.15f;
  gm.maxBound[0] = 0.16f;
  gm.maxBound[1] = 0.58f;
  gm.maxBound[2] = 0.24f;

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
  vertices.reserve(32);
  indices.reserve(128);

  auto addMidBlade = [&](float yawRadians, float baseOffsetX,
                         float baseOffsetZ, float heightScale,
                         float forwardLean) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const float halfWidths[3] = {0.056f, 0.034f, 0.006f};
    const float heights[3] = {0.00f, 0.16f, 0.30f};
    const float curve[3] = {0.000f, 0.050f, 0.120f};
    const float uvsV[3] = {1.00f, 0.48f, 0.00f};

    for (int row = 0; row < 3; ++row) {
      const float width = halfWidths[row];
      const float height = heights[row] * heightScale;
      const float bend = curve[row] * heightScale * forwardLean;

      auto makeVertex = [&](float side, float u) {
        Asset::Vertex vert = {};
        const float localX = baseOffsetX + side * width;
        const float localZ = baseOffsetZ + bend;
        vert.pos[0] = localX * c - localZ * s;
        vert.pos[1] = height;
        vert.pos[2] = localX * s + localZ * c;
        vert.normal[0] = 0.34f * c;
        vert.normal[1] = 0.82f;
        vert.normal[2] = 0.34f * s;
        vert.tangent[0] = -s;
        vert.tangent[1] = 0.0f;
        vert.tangent[2] = c;
        vert.tangent[3] = 1.0f;
        vert.uv[0] = u;
        vert.uv[1] = uvsV[row];
        return vert;
      };

      vertices.push_back(makeVertex(-1.0f, 0.0f));
      vertices.push_back(makeVertex(1.0f, 1.0f));
    }

    for (uint32_t row = 0; row < 2; ++row) {
      const uint32_t i0 = base + row * 2;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + 2;
      const uint32_t i3 = i0 + 3;
      indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3, i2, i1, i0,
                                     i3, i1, i2});
    }
  };

  addMidBlade(0.25f, -0.030f, -0.012f, 1.00f, 1.00f);
  addMidBlade(1.42f, 0.026f, -0.018f, 0.86f, 1.18f);
  addMidBlade(2.80f, 0.004f, 0.016f, 0.78f, 0.92f);
  addMidBlade(4.40f, -0.014f, 0.010f, 0.70f, 1.28f);

  Asset::GpuMesh gm = Asset::LoadMeshFromMemory(vertices, indices);
  if (!gm.vertexBuffer || !gm.indexBuffer || gm.indexCount == 0) {
    fprintf(stderr, "Grass: failed to create procedural mid mesh\n");
    return false;
  }
  gm.materialIndex = -1;
  gm.minBound[0] = -0.11f;
  gm.minBound[1] = 0.0f;
  gm.minBound[2] = -0.10f;
  gm.maxBound[0] = 0.11f;
  gm.maxBound[1] = 0.31f;
  gm.maxBound[2] = 0.16f;

  g_proceduralGrassMidMesh = std::move(gm);
  g_proceduralGrassMidReady = true;
  fprintf(stderr, "Grass: procedural mid mesh ready (v=%u i=%u)\n",
          g_proceduralGrassMidMesh.vertexCount,
          g_proceduralGrassMidMesh.indexCount);
  return true;
}

static void AppendGrassPatchesFromInstance(const Scene::Instance &inst,
                                           uint32_t sourceMeshId,
                                           uint32_t sourceMaterialIndex,
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
  const float patchDensityScale = 0.34f;

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
    const float randScale = 0.68f + 0.56f * Hash01(baseSeed ^ 0x1f123bb5U);
    const float patchScale = 0.78f + 0.24f * patchWeight;
    patch.scale =
        baseSize * (1.0f + (randScale - 1.0f) * variation) * patchScale;
    patch.normal = normal;
    const float randYaw = Hash01(baseSeed ^ 0x0f1bbcdcU) * kTwoPi;
    patch.yawRadians = randYaw;
    patch.emitterUv = emitterUv;
    patch.colorVariation = HashU32(baseSeed ^ 0xdeadbeefU);
    patch.packedData = sourceMaterialIndex & 0xFFFFu;
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

static bool EnsureInteractiveDxrPipelineSize(UINT width, UINT height,
                                             const char *context) {
  if (!g_rayTracingSupported || width == 0 || height == 0) {
    return false;
  }

  UINT currentPresentWidth = 0;
  UINT currentPresentHeight = 0;
  DxrRenderer::GetPipelinePresentSize(currentPresentWidth,
                                      currentPresentHeight);
  if (DxrRenderer::IsReady() && width == currentPresentWidth &&
      height == currentPresentHeight) {
    return true;
  }

  try {
    DxrRenderer::WaitForAsyncRestirIdle();
    DX12Context::WaitGPUIdle();
    DxrRenderer::CreateRayTracingPipeline(width, height);
    DxrRenderer::ResetAccumulation();
    return DxrRenderer::IsReady();
  } catch (const std::exception &e) {
    fprintf(stderr, "DXR pipeline resize failed (%s, %u x %u): %s\n",
            context ? context : "unknown", width, height, e.what());
  } catch (...) {
    fprintf(stderr, "DXR pipeline resize failed (%s, %u x %u): unknown\n",
            context ? context : "unknown", width, height);
  }
  return false;
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
static ComPtr<ID3D12RootSignature> g_previewPresentRootSignature;
static ComPtr<ID3D12PipelineState> g_previewPresentPipelineState;

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
float g_iblIntensity = 1.0f;  // IBL intensity multiplier (dxrProceduralSkyBoost)
float g_iblIndirectBoost = 1.0f; // Indirect-only IBL lighting multiplier

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

static bool EnsurePreviewPresentPipeline() {
  if (g_previewPresentRootSignature && g_previewPresentPipelineState) {
    return true;
  }
  if (!DX12Context::g_device) {
    return false;
  }

  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;
  srvRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER rootParam = {};
  rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParam.DescriptorTable.NumDescriptorRanges = 1;
  rootParam.DescriptorTable.pDescriptorRanges = &srvRange;
  rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MipLODBias = 0.0f;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MinLOD = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &rootParam;
  rootDesc.NumStaticSamplers = 1;
  rootDesc.pStaticSamplers = &sampler;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  HRESULT hr = D3D12SerializeRootSignature(
      &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
  if (FAILED(hr)) {
    if (error) {
      fprintf(stderr, "Preview present root signature error: %s\n",
              (char *)error->GetBufferPointer());
    }
    return false;
  }
  hr = DX12Context::g_device->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&g_previewPresentRootSignature));
  if (FAILED(hr)) {
    fprintf(stderr, "Preview present root signature create failed: 0x%08x\n",
            (unsigned)hr);
    return false;
  }

  DxcHelper dxc;
  ComPtr<IDxcBlob> vsBlob;
  ComPtr<IDxcBlob> psBlob;
  const std::wstring shaderPath =
      FindShaderFile(L"shaders\\preview_present.hlsl");
  try {
    vsBlob = dxc.Compile(shaderPath, L"VSMain", L"vs_6_0");
    psBlob = dxc.Compile(shaderPath, L"PSMain", L"ps_6_0");
  } catch (const std::exception &e) {
    fprintf(stderr, "Preview present shader compile failed: %s\n", e.what());
    g_previewPresentRootSignature.Reset();
    return false;
  }

  D3D12_RASTERIZER_DESC rasterDesc = {};
  rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
  rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
  rasterDesc.DepthClipEnable = TRUE;

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
  depthDesc.StencilEnable = FALSE;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = g_previewPresentRootSignature.Get();
  psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  psoDesc.RasterizerState = rasterDesc;
  psoDesc.BlendState = blendDesc;
  psoDesc.DepthStencilState = depthDesc;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
  psoDesc.SampleDesc.Count = 1;

  hr = DX12Context::g_device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_previewPresentPipelineState));
  if (FAILED(hr)) {
    fprintf(stderr, "Preview present PSO create failed: 0x%08x\n",
            (unsigned)hr);
    g_previewPresentRootSignature.Reset();
    return false;
  }

  return true;
}

static bool DrawRenderPreviewToBackbuffer(
    ID3D12GraphicsCommandList *cmdList, ID3D12Resource *backbuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv, ID3D12Resource *previewTexture,
    D3D12_RESOURCE_STATES &previewState,
    D3D12_GPU_DESCRIPTOR_HANDLE previewSrv) {
  if (!cmdList || !backbuffer || !previewTexture || previewSrv.ptr == 0 ||
      !EnsurePreviewPresentPipeline()) {
    return false;
  }

  const D3D12_RESOURCE_DESC srcDesc = previewTexture->GetDesc();
  const D3D12_RESOURCE_DESC dstDesc = backbuffer->GetDesc();
  const UINT srcWidth = static_cast<UINT>(srcDesc.Width);
  const UINT srcHeight = srcDesc.Height;
  const UINT dstWidth = static_cast<UINT>(dstDesc.Width);
  const UINT dstHeight = dstDesc.Height;
  if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0) {
    return false;
  }

  TR(cmdList, previewTexture, previewState,
     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  previewState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  const FLOAT clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  cmdList->ClearRenderTargetView(backbufferRtv, clearColor, 0, nullptr);
  cmdList->OMSetRenderTargets(1, &backbufferRtv, FALSE, nullptr);

  const float srcAspect = (float)srcWidth / (float)srcHeight;
  const bool tiledExportPreview =
      g_renderExportJob.active && g_renderExportJob.tileState.enabled &&
      g_renderExportJob.tileState.fullWidth > 0 &&
      g_renderExportJob.tileState.fullHeight > 0 &&
      g_renderExportJob.tileState.tileHeight > 0;
  const float previewAspect =
      tiledExportPreview
          ? ((float)g_renderExportJob.tileState.fullWidth /
             (float)g_renderExportJob.tileState.fullHeight)
          : srcAspect;
  float drawW = (float)dstWidth;
  float drawH = drawW / previewAspect;
  if (drawH > (float)dstHeight) {
    drawH = (float)dstHeight;
    drawW = drawH * previewAspect;
  }
  float drawX = ((float)dstWidth - drawW) * 0.5f;
  float drawY = ((float)dstHeight - drawH) * 0.5f;
  float tileDrawW = drawW;
  float tileDrawH = drawH;
  if (tiledExportPreview) {
    const RenderExportTileState &tile = g_renderExportJob.tileState;
    tileDrawW = drawW * ((float)tile.tileWidth / (float)tile.fullWidth);
    tileDrawH = drawH * ((float)tile.tileHeight / (float)tile.fullHeight);
    drawX += drawW * ((float)tile.tileOffsetX / (float)tile.fullWidth);
    drawY += drawH * ((float)tile.tileOffsetY / (float)tile.fullHeight);
  }

  D3D12_VIEWPORT previewViewport = {};
  previewViewport.TopLeftX = drawX;
  previewViewport.TopLeftY = drawY;
  previewViewport.Width = tileDrawW;
  previewViewport.Height = tileDrawH;
  previewViewport.MinDepth = 0.0f;
  previewViewport.MaxDepth = 1.0f;

  D3D12_RECT previewScissor = {
      (LONG)std::floor(drawX), (LONG)std::floor(drawY),
      (LONG)std::ceil(drawX + tileDrawW),
      (LONG)std::ceil(drawY + tileDrawH)};
  previewScissor.left = (std::max<LONG>)(0, previewScissor.left);
  previewScissor.top = (std::max<LONG>)(0, previewScissor.top);
  previewScissor.right =
      (std::min<LONG>)((LONG)dstWidth, previewScissor.right);
  previewScissor.bottom =
      (std::min<LONG>)((LONG)dstHeight, previewScissor.bottom);

  ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
  cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
  cmdList->SetGraphicsRootSignature(g_previewPresentRootSignature.Get());
  cmdList->SetPipelineState(g_previewPresentPipelineState.Get());
  cmdList->RSSetViewports(1, &previewViewport);
  cmdList->RSSetScissorRects(1, &previewScissor);
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cmdList->SetGraphicsRootDescriptorTable(0, previewSrv);
  cmdList->DrawInstanced(3, 1, 0, 0);
  return true;
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
  D3D12_ROOT_PARAMETER rootParameters[9] = {};
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

  // b3 - selected grass material index for per-material grass draw passes.
  rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameters[8].Constants.ShaderRegister = 3;
  rootParameters[8].Constants.RegisterSpace = 0;
  rootParameters[8].Constants.Num32BitValues = 1;
  rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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

static bool IsSupportedDroppedModelPath(const std::wstring &path) {
  std::wstring ext = std::filesystem::path(path).extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return ext == L".skp" || ext == L".gltf" || ext == L".glb" ||
         ext == L".obj" || ext == L".stl" || ext == L".fbx" ||
         ext == L".ltm" || ext == L".lmod";
}

static bool ImportFirstDroppedModelFileHandle(HDROP drop,
                                              const POINT *dropScreenPoint =
                                                  nullptr) {
  if (!drop) {
    return false;
  }

  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    const UINT len = DragQueryFileW(drop, i, nullptr, 0);
    if (len == 0) {
      continue;
    }

    std::wstring path(len + 1, L'\0');
    DragQueryFileW(drop, i, path.data(), len + 1);
    path.resize(len);
    if (!IsSupportedDroppedModelPath(path)) {
      continue;
    }

    float placement[3] = {};
    if (dropScreenPoint &&
        Scene::ResolveViewportImportPlacement(
            static_cast<float>(dropScreenPoint->x),
            static_cast<float>(dropScreenPoint->y),
            static_cast<float>(DX12Context::g_windowWidth),
            static_cast<float>(DX12Context::g_windowHeight), placement)) {
      return Scene::ImportModelAsync(WStringToUtf8(path), placement);
    }
    return Scene::ImportModelAsync(WStringToUtf8(path));
  }

  return false;
}

static bool ImportFirstDroppedModelFile(HDROP drop) {
  if (!drop) {
    return false;
  }

  POINT dropPoint = {};
  const POINT *dropScreenPoint = nullptr;
  if (DragQueryPoint(drop, &dropPoint) && g_hwnd &&
      ClientToScreen(g_hwnd, &dropPoint)) {
    dropScreenPoint = &dropPoint;
  }

  const bool startedImport =
      ImportFirstDroppedModelFileHandle(drop, dropScreenPoint);

  DragFinish(drop);
  return startedImport;
}

class ModelFileDropTarget : public IDropTarget {
public:
  ModelFileDropTarget() : refCount_(1) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *ppvObject = static_cast<IDropTarget *>(this);
      AddRef();
      return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const LONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
      delete this;
    }
    return static_cast<ULONG>(count);
  }

  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *dataObject, DWORD,
                                      POINTL, DWORD *effect) override {
    if (!effect) {
      return E_POINTER;
    }
    *effect = HasSupportedFiles(dataObject) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD *effect) override {
    if (!effect) {
      return E_POINTER;
    }
    if (*effect != DROPEFFECT_NONE) {
      *effect = DROPEFFECT_COPY;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }

  HRESULT STDMETHODCALLTYPE Drop(IDataObject *dataObject, DWORD,
                                 POINTL dropPoint, DWORD *effect) override {
    if (!effect) {
      return E_POINTER;
    }

    *effect = DROPEFFECT_NONE;
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1,
                        TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (!dataObject || FAILED(dataObject->GetData(&format, &medium))) {
      return S_OK;
    }

    bool startedImport = false;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
      const POINT dropScreenPoint = {dropPoint.x, dropPoint.y};
      startedImport = ImportFirstDroppedModelFileHandle(
          static_cast<HDROP>(medium.hGlobal), &dropScreenPoint);
    }
    ReleaseStgMedium(&medium);
    *effect = startedImport ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }

private:
  bool HasSupportedFiles(IDataObject *dataObject) const {
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1,
                        TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (!dataObject || FAILED(dataObject->GetData(&format, &medium))) {
      return false;
    }

    bool supported = false;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
      const UINT count = DragQueryFileW(static_cast<HDROP>(medium.hGlobal),
                                        0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < count; ++i) {
        const UINT len = DragQueryFileW(static_cast<HDROP>(medium.hGlobal), i,
                                        nullptr, 0);
        if (len == 0) {
          continue;
        }

        std::wstring path(len + 1, L'\0');
        DragQueryFileW(static_cast<HDROP>(medium.hGlobal), i, path.data(),
                       len + 1);
        path.resize(len);
        if (IsSupportedDroppedModelPath(path)) {
          supported = true;
          break;
        }
      }
    }

    ReleaseStgMedium(&medium);
    return supported;
  }

  LONG refCount_;
};

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
    if (IsSceneIoJobActive()) {
      fprintf(stderr,
              "Close request ignored while scene %s is in progress.\n",
              IsSceneIoSaveJob() ? "save" : "load");
      return 0;
    }
    g_appClosing = true;
    PostQuitMessage(0);
    return 0;
  case WM_SIZE:
    if (!g_appClosing && DX12Context::g_swapChain && wParam != SIZE_MINIMIZED) {
      DxrRenderer::RequestInteractiveWake("window resize");
      DX12Context::QueueResize(LOWORD(lParam), HIWORD(lParam));
    }
    return 0;
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
    DxrRenderer::RequestInteractiveWake("window input");
    break;
  case WM_DROPFILES:
    DxrRenderer::RequestInteractiveWake("model drop");
    ImportFirstDroppedModelFile(reinterpret_cast<HDROP>(wParam));
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine,
                   int nCmdShow) {
#ifdef USE_QT_UI
  (void)nCmdShow;
#endif
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
#ifndef USE_QT_UI
  ModelFileDropTarget *dropTarget = nullptr;
  bool oleInitialized = false;
#endif
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

  QIcon qtAppIcon;
  HICON hQtIcon = static_cast<HICON>(LoadImageW(
      hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0,
      LR_DEFAULTSIZE));
  if (hQtIcon) {
    qtAppIcon.addPixmap(QPixmap::fromImage(QImage::fromHICON(hQtIcon)));
    DestroyIcon(hQtIcon);
  }
  if (qtAppIcon.isNull()) {
    QString iconPath = QCoreApplication::applicationDirPath() +
                       QStringLiteral("/resources/app.ico");
    if (!QFileInfo::exists(iconPath)) {
      iconPath = QStringLiteral("resources/app.ico");
    }
    if (QFileInfo::exists(iconPath)) {
      qtAppIcon = QIcon(iconPath);
    }
  }
  if (!qtAppIcon.isNull()) {
    app.setWindowIcon(qtAppIcon);
  }

  EnforceReleaseDebugFlags();

  MainWindow w;
  if (!qtAppIcon.isNull()) {
    w.setWindowIcon(qtAppIcon);
  }
  w.show();

  // obtain native handle from DX12View inside MainWindow
  hwnd = reinterpret_cast<HWND>(w.view()->winId());

  if (!InitApplication(hwnd)) {
    QMessageBox::critical(nullptr, "Error", "Failed to initialize application");
    return -1;
  }
#else
  const HRESULT oleHr = OleInitialize(nullptr);
  if (SUCCEEDED(oleHr)) {
    oleInitialized = true;
  } else {
    fprintf(stderr, "Main: OleInitialize failed (0x%08x)\n",
            static_cast<unsigned int>(oleHr));
  }

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

  bool useLegacyDropFiles = true;
  if (oleInitialized) {
    dropTarget = new ModelFileDropTarget();
    const HRESULT dropHr = RegisterDragDrop(hwnd, dropTarget);
    if (SUCCEEDED(dropHr)) {
      useLegacyDropFiles = false;
    } else {
      fprintf(stderr, "Main: RegisterDragDrop failed (0x%08x)\n",
              static_cast<unsigned int>(dropHr));
      dropTarget->Release();
      dropTarget = nullptr;
    }
  }
  if (useLegacyDropFiles) {
    DragAcceptFiles(hwnd, TRUE);
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
        RequestGrassRuntimeRefreshForSceneLoad();
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

      const auto &shadowNodes = Scene::GetNodes();

      // Calculate Shadow Matrix
      {
        using namespace DirectX;
        const float shadowMapSize =
            (float)(std::max)(1u, RasterRenderer::GetShadowMapSize());

        XMVECTOR lightDir =
            XMVector3Normalize(XMVectorSet(g_cameraData.lightDir[0],
                                           g_cameraData.lightDir[1],
                                           g_cameraData.lightDir[2], 0.0f));
        XMVECTOR camPos = XMLoadFloat3((XMFLOAT3 *)g_cameraData.pos);
        XMVECTOR camFwd = XMLoadFloat3((XMFLOAT3 *)g_cameraData.forward);
        XMVECTOR camUp = XMLoadFloat3((XMFLOAT3 *)g_cameraData.up);
        XMVECTOR camRight = XMVector3Normalize(XMVector3Cross(camUp, camFwd));
        XMVECTOR lightForward = XMVectorNegate(lightDir);
        XMVECTOR lightUp = XMVectorSet(0, 1, 0, 0);
        if (fabsf(XMVectorGetX(XMVector3Dot(lightForward, lightUp))) > 0.98f) {
          lightUp = XMVectorSet(0, 0, 1, 0);
        }

        const float shadowNear = (std::max)(g_cameraData.nearZ, 0.5f);
        const float shadowFar =
            (std::max)(shadowNear + 1.0f,
                       (std::min)(g_cameraData.farZ, 120.0f));
        const float tanHalfFov =
            tanf(XMConvertToRadians(g_cameraData.fov) * 0.5f);
        const float nearHalfY = shadowNear * tanHalfFov;
        const float nearHalfX = nearHalfY * g_cameraData.aspect;
        const float farHalfY = shadowFar * tanHalfFov;
        const float farHalfX = farHalfY * g_cameraData.aspect;
        const XMVECTOR nearCenter = camPos + camFwd * shadowNear;
        const XMVECTOR farCenter = camPos + camFwd * shadowFar;
        const XMVECTOR nearRight = camRight * nearHalfX;
        const XMVECTOR nearUp = camUp * nearHalfY;
        const XMVECTOR farRight = camRight * farHalfX;
        const XMVECTOR farUp = camUp * farHalfY;

        XMVECTOR frustumCorners[8] = {
            nearCenter - nearRight - nearUp, nearCenter + nearRight - nearUp,
            nearCenter - nearRight + nearUp, nearCenter + nearRight + nearUp,
            farCenter - farRight - farUp,   farCenter + farRight - farUp,
            farCenter - farRight + farUp,   farCenter + farRight + farUp,
        };

        XMVECTOR shadowTarget = XMVectorZero();
        bool haveSceneBounds = false;
        XMVECTOR sceneBoundsMin = XMVectorReplicate(FLT_MAX);
        XMVECTOR sceneBoundsMax = XMVectorReplicate(-FLT_MAX);
        std::vector<XMVECTOR> sceneWorldCorners;
        sceneWorldCorners.reserve(256);
        auto transformNodePoint = [](const float m[16], const float p[3]) {
          return XMVectorSet(
              p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
              p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
              p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14], 1.0f);
        };

        for (const auto &node : shadowNodes) {
          if (!node.visible)
            continue;

          for (size_t meshIndex : node.meshIndices) {
            if (meshIndex >= g_loadedMeshes.size())
              continue;

            const auto &mesh = g_loadedMeshes[meshIndex];
            const float boundsMinX = mesh.minBound[0];
            const float boundsMinY = mesh.minBound[1];
            const float boundsMinZ = mesh.minBound[2];
            const float boundsMaxX = mesh.maxBound[0];
            const float boundsMaxY = mesh.maxBound[1];
            const float boundsMaxZ = mesh.maxBound[2];
            const float localBounds[8][3] = {
                {boundsMinX, boundsMinY, boundsMinZ},
                {boundsMaxX, boundsMinY, boundsMinZ},
                {boundsMinX, boundsMaxY, boundsMinZ},
                {boundsMaxX, boundsMaxY, boundsMinZ},
                {boundsMinX, boundsMinY, boundsMaxZ},
                {boundsMaxX, boundsMinY, boundsMaxZ},
                {boundsMinX, boundsMaxY, boundsMaxZ},
                {boundsMaxX, boundsMaxY, boundsMaxZ},
            };

            for (const auto &localCorner : localBounds) {
              XMVECTOR worldCorner =
                  transformNodePoint(node.transform, localCorner);
              sceneBoundsMin = XMVectorMin(sceneBoundsMin, worldCorner);
              sceneBoundsMax = XMVectorMax(sceneBoundsMax, worldCorner);
              sceneWorldCorners.push_back(worldCorner);
              haveSceneBounds = true;
            }
          }
        }

        if (haveSceneBounds) {
          shadowTarget = (sceneBoundsMin + sceneBoundsMax) * 0.5f;
        } else {
          for (const XMVECTOR &corner : frustumCorners) {
            shadowTarget += corner;
          }
          shadowTarget *= (1.0f / 8.0f);
        }

        float shadowPaddingXY = 6.0f;
        float shadowPaddingZ = 80.0f;
        if (haveSceneBounds) {
          const XMVECTOR sceneExtent = sceneBoundsMax - sceneBoundsMin;
          const float extentX = fabsf(XMVectorGetX(sceneExtent));
          const float extentY = fabsf(XMVectorGetY(sceneExtent));
          const float extentZ = fabsf(XMVectorGetZ(sceneExtent));
          const float sceneRadius =
              0.5f * sqrtf(extentX * extentX + extentY * extentY +
                           extentZ * extentZ);
          shadowPaddingXY = (std::max)(6.0f, sceneRadius * 0.35f);
          shadowPaddingZ = (std::max)(80.0f, sceneRadius * 2.0f);
        }

        XMVECTOR lightPos = shadowTarget - lightForward * shadowPaddingZ;
        XMVECTOR lightRight =
            XMVector3Normalize(XMVector3Cross(lightUp, lightForward));
        XMVECTOR lightBasisUp =
            XMVector3Normalize(XMVector3Cross(lightForward, lightRight));

        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float minZ = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        float maxZ = -FLT_MAX;
        auto expandShadowBounds = [&](FXMVECTOR worldPoint) {
          const XMVECTOR rel = worldPoint - lightPos;
          XMVECTOR lightSpacePoint = XMVectorSet(
              XMVectorGetX(XMVector3Dot(rel, lightRight)),
              XMVectorGetX(XMVector3Dot(rel, lightBasisUp)),
              XMVectorGetX(XMVector3Dot(rel, lightForward)), 1.0f);
          minX = (std::min)(minX, XMVectorGetX(lightSpacePoint));
          minY = (std::min)(minY, XMVectorGetY(lightSpacePoint));
          minZ = (std::min)(minZ, XMVectorGetZ(lightSpacePoint));
          maxX = (std::max)(maxX, XMVectorGetX(lightSpacePoint));
          maxY = (std::max)(maxY, XMVectorGetY(lightSpacePoint));
          maxZ = (std::max)(maxZ, XMVectorGetZ(lightSpacePoint));
        };
        if (!haveSceneBounds) {
          for (const XMVECTOR &corner : frustumCorners) {
            expandShadowBounds(corner);
          }
        }

        for (const XMVECTOR &worldCorner : sceneWorldCorners) {
          expandShadowBounds(worldCorner);
        }

        minX -= shadowPaddingXY;
        minY -= shadowPaddingXY;
        maxX += shadowPaddingXY;
        maxY += shadowPaddingXY;

        float orthoWidth = (std::max)(maxX - minX, 1.0f);
        float orthoHeight = (std::max)(maxY - minY, 1.0f);
        float centerX = 0.5f * (minX + maxX);
        float centerY = 0.5f * (minY + maxY);
        const float texelSizeX = orthoWidth / shadowMapSize;
        const float texelSizeY = orthoHeight / shadowMapSize;
        centerX = roundf(centerX / texelSizeX) * texelSizeX;
        centerY = roundf(centerY / texelSizeY) * texelSizeY;

        minX = centerX - orthoWidth * 0.5f;
        maxX = centerX + orthoWidth * 0.5f;
        minY = centerY - orthoHeight * 0.5f;
        maxY = centerY + orthoHeight * 0.5f;

        const float nearPlane = (std::max)(0.1f, minZ - shadowPaddingZ);
        const float farPlane = (std::max)(nearPlane + 1.0f, maxZ + shadowPaddingZ);
        XMMATRIX view = XMMatrixLookToLH(lightPos, lightForward, lightBasisUp);
        XMMATRIX proj = XMMatrixOrthographicOffCenterLH(
            minX, maxX, minY, maxY, nearPlane, farPlane);
        XMMATRIX shadowMat = view * proj;
        XMStoreFloat4x4((XMFLOAT4X4 *)g_cameraData.shadowMatrix, shadowMat);
        const float invWidth =
            2.0f / (std::max)(maxX - minX, 1.0e-4f);
        const float invHeight =
            2.0f / (std::max)(maxY - minY, 1.0e-4f);
        const float invDepth =
            1.0f / (std::max)(farPlane - nearPlane, 1.0e-4f);
        const float biasX = -(maxX + minX) / (std::max)(maxX - minX, 1.0e-4f);
        const float biasY = -(maxY + minY) / (std::max)(maxY - minY, 1.0e-4f);
        const float biasZ = -nearPlane * invDepth;
        g_cameraData.shadowViewRow0[0] = XMVectorGetX(lightRight);
        g_cameraData.shadowViewRow0[1] = XMVectorGetY(lightRight);
        g_cameraData.shadowViewRow0[2] = XMVectorGetZ(lightRight);
        g_cameraData.shadowViewRow0[3] =
            -XMVectorGetX(XMVector3Dot(lightPos, lightRight));
        g_cameraData.shadowViewRow1[0] = XMVectorGetX(lightBasisUp);
        g_cameraData.shadowViewRow1[1] = XMVectorGetY(lightBasisUp);
        g_cameraData.shadowViewRow1[2] = XMVectorGetZ(lightBasisUp);
        g_cameraData.shadowViewRow1[3] =
            -XMVectorGetX(XMVector3Dot(lightPos, lightBasisUp));
        g_cameraData.shadowViewRow2[0] = XMVectorGetX(lightForward);
        g_cameraData.shadowViewRow2[1] = XMVectorGetY(lightForward);
        g_cameraData.shadowViewRow2[2] = XMVectorGetZ(lightForward);
        g_cameraData.shadowViewRow2[3] =
            -XMVectorGetX(XMVector3Dot(lightPos, lightForward));
        g_cameraData.shadowProjParams0[0] = invWidth;
        g_cameraData.shadowProjParams0[1] = invHeight;
        g_cameraData.shadowProjParams0[2] = invDepth;
        g_cameraData.shadowProjParams0[3] = biasX;
        g_cameraData.shadowProjParams1[0] = biasY;
        g_cameraData.shadowProjParams1[1] = biasZ;
        g_cameraData.shadowProjParams1[2] = 0.0f;
        g_cameraData.shadowProjParams1[3] = 0.0f;

        // ViewProj and InvViewProj for SSR/SSAO.
        // Raster mesh VS projects with positive forward Z, so these matrices
        // must use the same left-handed convention or screen-space effects
        // reconstruct from a different camera basis.
        XMVECTOR projectionFwd = camFwd;
        XMVECTOR projectionUp = camUp;
        float verticalCenterShift = 0.0f;
        if (g_cameraData.verticalTiltCorrection > 0.5f) {
          const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
          XMVECTOR levelForward =
              XMVectorSubtract(camFwd,
                               XMVectorScale(worldUp,
                                             XMVectorGetX(
                                                 XMVector3Dot(camFwd, worldUp))));
          const float levelLengthSq =
              XMVectorGetX(XMVector3LengthSq(levelForward));
          if (levelLengthSq > 1.0e-6f) {
            projectionFwd = XMVector3Normalize(levelForward);
            const XMVECTOR projectionRight =
                XMVector3Normalize(XMVector3Cross(projectionFwd, worldUp));
            projectionUp =
                XMVector3Normalize(XMVector3Cross(projectionRight,
                                                  projectionFwd));
            const float levelDot =
                std::max(0.025f,
                         XMVectorGetX(XMVector3Dot(camFwd, projectionFwd)));
            verticalCenterShift = std::clamp(
                XMVectorGetX(XMVector3Dot(camFwd, projectionUp)) / levelDot,
                -40.0f, 40.0f);
          }
        }
        XMMATRIX camView = XMMatrixLookToLH(camPos, projectionFwd, projectionUp);
        XMMATRIX camProj;
        if (g_cameraData.verticalTiltCorrection > 0.5f &&
            std::abs(verticalCenterShift) > 1.0e-6f) {
          const float nearZ = g_cameraData.nearZ;
          const float fInv =
              tanf(XMConvertToRadians(g_cameraData.fov) * 0.5f);
          const float halfWidth = nearZ * fInv * g_cameraData.aspect;
          const float bottom = nearZ * (verticalCenterShift - fInv);
          const float top = nearZ * (verticalCenterShift + fInv);
          camProj = XMMatrixPerspectiveOffCenterLH(
              -halfWidth, halfWidth, bottom, top, nearZ,
              g_cameraData.farZ);
        } else {
          camProj = XMMatrixPerspectiveFovLH(
              XMConvertToRadians(g_cameraData.fov), g_cameraData.aspect,
              g_cameraData.nearZ, g_cameraData.farZ);
        }
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

    if (IsSceneStateLockedByIo()) {
      TR(DX12Context::g_commandList.Get(),
         DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
         D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      FLOAT clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
      DX12Context::g_commandList->ClearRenderTargetView(rtvHandle, clearColor,
                                                        0, nullptr);
    } else {
      // --- Rebuild grass patch list every frame (shared by DXR & Raster)
      // ---
      std::vector<int> activeGrassMaterialIndices;
      {
        const bool forceGrassRefresh = g_forceGrassRuntimeRefresh;
        auto sceneInstances_grass = Scene::GetInstances();
        const uint64_t grassSceneHash = ComputeGrassSceneHash(sceneInstances_grass);
        const bool grassSceneChanged =
            forceGrassRefresh || (grassSceneHash != g_prevGrassSceneHash);
        auto addGrassMaterialIndex = [&activeGrassMaterialIndices](int matIdx) {
          if (std::find(activeGrassMaterialIndices.begin(),
                        activeGrassMaterialIndices.end(),
                        matIdx) == activeGrassMaterialIndices.end()) {
            activeGrassMaterialIndices.push_back(matIdx);
          }
        };
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
            addGrassMaterialIndex(matIdx);
            AppendGrassPatchesFromInstance(inst, grassSourceId++,
                                           static_cast<uint32_t>(matIdx), mat,
                                           g_grassPatches);
          }
          GrassManager::SetPatches(g_grassPatches);
          g_prevGrassSceneHash = grassSceneHash;
        } else {
          for (const auto &inst : sceneInstances_grass) {
            if (!inst.mesh)
              continue;
            int matIdx = inst.mesh->materialIndex;
            if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
              continue;
            if (g_loadedMaterials[matIdx].isGrass) {
              addGrassMaterialIndex(matIdx);
            }
          }
        }
        GrassManager::PrepareGpuBuffers(DX12Context::g_commandList.Get());
        // Grass always instances dedicated procedural meshes for near and mid.
        if (EnsureProceduralGrassBladeMesh()) {
          if (!activeGrassMaterialIndices.empty()) {
            g_proceduralGrassBladeMesh.materialIndex =
                activeGrassMaterialIndices.front();
          }
          GrassManager::SetPatchMesh(&g_proceduralGrassBladeMesh);
        }
        if (EnsureProceduralGrassMidMesh()) {
          if (!activeGrassMaterialIndices.empty()) {
            g_proceduralGrassMidMesh.materialIndex =
                activeGrassMaterialIndices.front();
          }
          GrassManager::SetMidPatchMesh(&g_proceduralGrassMidMesh);
        }
        const UINT currentPatchCount = (UINT)g_grassPatches.size();
        uint64_t grassMaterialHash = 0xcbf29ce484222325ULL;
        for (int matIdx : activeGrassMaterialIndices) {
          HashCombineU64(grassMaterialHash, static_cast<uint64_t>(matIdx + 1));
        }
        const bool grassTopologyChanged =
            (currentPatchCount != g_prevGrassPatchCount) ||
            (grassMaterialHash != g_prevGrassMaterialHash);
        if (grassSceneChanged || grassTopologyChanged) {
          if (forceGrassRefresh || grassTopologyChanged) {
            DxrRenderer::RequestAccelerationStructureRebuild();
          } else {
            DxrRenderer::RequestAccelerationStructureUpdate();
          }
          DxrRenderer::ResetAccumulation();
          g_prevGrassPatchCount = currentPatchCount;
          g_prevGrassMaterialHash = grassMaterialHash;
          g_forceGrassRuntimeRefresh = false;
        }
      }

      // Render based on current mode
      switch (g_currentRenderMode) {
      case RenderMode::DXR: {
        std::string pipelineRecreateContext;
        if (DxrRenderer::ConsumePipelineRecreateRequest(
                &pipelineRecreateContext)) {
          try {
            DxrRenderer::WaitForAsyncRestirIdle();
            DX12Context::WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(previewWidth,
                                                  previewHeight);
            DxrRenderer::ResetAccumulation();
          } catch (const std::exception &e) {
            fprintf(stderr,
                    "DXR queued pipeline recreate failed (%s): %s\n",
                    pipelineRecreateContext.c_str(), e.what());
          } catch (...) {
            fprintf(stderr,
                    "DXR queued pipeline recreate failed (%s): unknown "
                    "exception\n",
                    pipelineRecreateContext.c_str());
          }
        }

        if (!DxrRenderer::IsReady()) {
          try {
            DxrRenderer::WaitForAsyncRestirIdle();
            DX12Context::WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(previewWidth,
                                                  previewHeight);
            DxrRenderer::ResetAccumulation();
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
          Scene::EnsureIESAtlasReady();

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
            const bool renderingToOffscreenTarget =
              g_renderExportJob.active && g_exportRenderTarget && g_exportRtvHeap;
          if (g_renderExportJob.active && g_exportRenderTarget &&
              g_exportRtvHeap) {
            if (g_exportRenderTargetState != D3D12_RESOURCE_STATE_PRESENT) {
              TR(DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
                 g_exportRenderTargetState, D3D12_RESOURCE_STATE_PRESENT);
              g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
            }
            dxrTarget = g_exportRenderTarget.Get();
            dxrRtv = g_exportRtvHeap->GetCPUDescriptorHandleForHeapStart();
          }
            const UINT dxrPresentationX = renderingToOffscreenTarget
                            ? 0u
                            : static_cast<UINT>(presentationRect.left);
            const UINT dxrPresentationY = renderingToOffscreenTarget
                            ? 0u
                            : static_cast<UINT>(presentationRect.top);
            const UINT dxrPresentationWidth = renderingToOffscreenTarget
                              ? g_renderExportJob.targetWidth
                              : previewWidth;
            const UINT dxrPresentationHeight = renderingToOffscreenTarget
                               ? g_renderExportJob.targetHeight
                               : previewHeight;

          bool dxrOk = DxrRenderer::RenderFrame(
              DX12Context::g_commandList.Get(),
              DX12Context::g_frameResources[DX12Context::g_frameIndex]
                  .commandAllocator.Get(),
              DX12Context::g_frameIndex, dxrTarget, dxrRtv,
              g_cameraConstantBuffer.Get(), g_materialStructuredBuffer.Get(),
              g_texturesGpuStart, g_textureDescriptorCount, activeMeshes,
              g_meshStructuredBuffer.Get(),
              g_materialExtraStructuredBuffer.Get(), dxrPresentationX,
              dxrPresentationY, dxrPresentationWidth,
              dxrPresentationHeight);
          if (dxrOk) {
            if (g_renderExportJob.active &&
                dxrTarget == g_exportRenderTarget.Get()) {
              g_exportRenderTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
              DxrRenderer::CopyTonemappedFrameToResource(
                  DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
                  &g_exportRenderTargetState);
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
              DrawRenderPreviewToBackbuffer(
                  DX12Context::g_commandList.Get(),
                  DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
                  rtvHandle, g_exportRenderTarget.Get(),
                  g_exportRenderTargetState, g_exportPreviewSrvGpu);
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
          g_cloudManager.BakeSky(DX12Context::g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());
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
        const bool rasterGrassReady =
            !activeGrassMaterialIndices.empty() &&
            RasterRenderer::g_grassPipelineState &&
            GrassManager::GetPatchCount() > 0 &&
            GrassManager::GetPatchMesh() &&
            GrassManager::GetPatchMesh()->vertexBuffer &&
            GrassManager::GetPatchMesh()->indexBuffer;
        if (rasterGrassReady) {
          GrassManager::CullingAndPrepareIndirect(
              DX12Context::g_commandList.Get(), g_cameraConstantBuffer.Get());
        }

        if (((!sceneInstances.empty() && RasterRenderer::g_meshPipelineState) ||
             rasterGrassReady)) {
          // Log to stderr only (controlled by verbose flag)
          if (g_verboseRenderLogs && !sceneInstances.empty())
            fprintf(stderr, "Drawing %zu instances\n", sceneInstances.size());

          // Shadow Pass
          RasterRenderer::DrawShadowMap(DX12Context::g_commandList.Get(),
                                        g_cameraConstantBuffer.Get(),
                                        sceneInstances,
                                        rasterGrassReady);
        
          // Re-bind HDR render targets for main pass
          RasterRenderer::BindHdrRenderTarget(DX12Context::g_device.Get(), DX12Context::g_commandList.Get(), dsvHandle);

        }

        if (!sceneInstances.empty() && RasterRenderer::g_meshPipelineState) {
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

        if (rasterGrassReady) {
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

          DX12Context::g_commandList->SetGraphicsRootShaderResourceView(
              6, GrassManager::GetInstanceBufferGpuAddress());
          for (int grassMaterialIndex : activeGrassMaterialIndices) {
            if (grassMaterialIndex < 0 ||
                grassMaterialIndex >= (int)g_loadedMaterials.size()) {
              continue;
            }

            MaterialCB grassMatCB = {};
            const auto &srcMat = g_loadedMaterials[grassMaterialIndex];
            MaterialSystem::BuildRuntimeRasterMaterialConstants(srcMat,
                                                                &grassMatCB);

            if (g_materialCbMappedData) {
              const UINT64 matSlotSize = (sizeof(MaterialCB) + 255) & ~255;
              UINT64 offset = (grassMaterialIndex % 16384) * matSlotSize;
              memcpy((uint8_t *)g_materialCbMappedData + offset, &grassMatCB,
                     sizeof(grassMatCB));
              DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                  2, g_materialConstantBuffer->GetGPUVirtualAddress() + offset);
            }

            const uint32_t selectedGrassMaterial =
                static_cast<uint32_t>(grassMaterialIndex);
            DX12Context::g_commandList->SetGraphicsRoot32BitConstants(
                8, 1, &selectedGrassMaterial, 0);
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
        }

        if (rasterHdrReady) {
          // Composite any active volumetric volume into the HDR target before
          // tonemapping (reads scene depth, marches the density field).
          RasterRenderer::RunVolumetric(
              DX12Context::g_device.Get(), DX12Context::g_commandList.Get(),
              g_cameraConstantBuffer.Get(), DX12Context::g_depthBuffer.Get());
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
    } // End else !IsSceneIoJobActive()

    if (!g_renderExportJob.active && HasPreviewRenderImage()) {
      DrawRenderPreviewToBackbuffer(
          DX12Context::g_commandList.Get(),
          DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
          rtvHandle, g_exportRenderTarget.Get(), g_exportRenderTargetState,
          g_exportPreviewSrvGpu);
    }

    // Render ImGui (Overlay on top of whatever was drawn)
    if ((g_renderExportJob.active || HasPreviewRenderImage()) && g_exportRenderTarget &&
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
    g_previewPresentPipelineState.Reset();
    g_previewPresentRootSignature.Reset();
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
      fprintf(stderr,
              "Device removed before recovery attempt "
              "(GetDeviceRemovedReason=0x%08x, wavefrontStage=%s)\n",
              (unsigned)reason, DxrRenderer::GetWavefrontStageName());
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
  static auto lastIdleUiPresentTime =
      prevTime - std::chrono::milliseconds(kIdleUiFrameIntervalMs);

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

    const bool sceneIoActive = IsSceneStateLockedByIo();
    const bool sceneLoadWarmupActive =
        !sceneIoActive && g_currentRenderMode == RenderMode::DXR &&
        DxrRenderer::HasSceneLoadWarmup();
    if (!sceneIoActive) {
      if (!sceneLoadWarmupActive && !IsRenderExportActive()) {
        Input::Update(dt);
      }
      LiveLink::TickRuntime();
    } else {
      LiveLink::TickCoordinator();
    }

    D3D12_RECT previewRect = {0, 0, (LONG)DX12Context::g_windowWidth,
                  (LONG)DX12Context::g_windowHeight};
    GetSafeFramePreviewRect(DX12Context::g_windowWidth,
                DX12Context::g_windowHeight, previewRect);
    const UINT previewWidth =
      (std::max)(1u, static_cast<UINT>(previewRect.right - previewRect.left));
    const UINT previewHeight =
      (std::max)(1u, static_cast<UINT>(previewRect.bottom - previewRect.top));

    // Deferred export restore: the previous frame finished presentation/copy
    // work that can reference export-sized DXR resources.  Restore on the next
    // tick so tearing down the export pipeline cannot race the command list,
    // then submit a normal viewport frame.
    if (g_renderExportJob.active && g_renderExportJob.previewRestorePending) {
      const bool preservePreviewImage = g_renderExportJob.isPreview;
      const bool advanceAfterRestore = g_renderExportJob.completionAdvancePending;
      const bool restoredExportSucceeded =
          g_renderExportJob.completionExportSucceeded;
      g_renderExportJob.previewRestorePending = false;
      RestoreRenderExportState(preservePreviewImage);
      if (advanceAfterRestore) {
        AdvanceBatchRenderExport(restoredExportSucceeded);
        AdvanceAnimationRenderExport(restoredExportSucceeded);
      }
    }

    if (g_renderExportJob.active) {
      g_currentRenderMode = RenderMode::DXR;

      const UINT currentSpp = DxrRenderer::GetDisplayedSampleCount();
      const float currentNoise = DxrRenderer::GetCurrentNoiseLevel();
      const bool hasNoiseEstimate = DxrRenderer::HasNoiseEstimate();
      const bool sppDone = currentSpp >= (UINT)g_renderExportJob.targetMaxSpp;
      const bool noiseDone =
          (currentSpp >= g_renderExportJob.minSppBeforeNoiseStop) &&
          hasNoiseEstimate &&
          (currentNoise <= g_renderExportJob.targetNoiseThreshold);

        const bool reachedEnd =
          sppDone || (g_renderExportJob.allowNoiseThresholdStop && noiseDone);

      if (reachedEnd && !g_renderExportJob.completionArmed) {
        g_renderExportJob.completionArmed = true;
        g_renderExportJob.completionFrames = 0;
        const bool renderDenoiserActive =
            !g_renderExportJob.tileState.enabled &&
            (g_renderExportJob.targetDenoiserIndex != 0);
        g_renderExportJob.settleFramesRemaining =
            renderDenoiserActive ? 3 : 1;
      }

      if (g_renderExportJob.completionArmed) {
        ++g_renderExportJob.completionFrames;
        if (g_renderExportJob.settleFramesRemaining > 0) {
          --g_renderExportJob.settleFramesRemaining;
        } else {
          const bool finalDisplayReady = DxrRenderer::CanIdleWithoutRendering();
          // Wait until the renderer reports that the final displayable frame is
          // ready. This is stricter than "OIDN ran" because it also requires
          // the tonemapped output to be available.
          if (!finalDisplayReady && g_renderExportJob.completionFrames < 240) {
            // keep waiting
          } else {
            if (g_renderExportJob.isPreview) {
              // Latch camera state now, but DEFER RestoreRenderExportState to
              // the next frame.  The current command list still contains a
              // CopyPresentedTexture that references DXR resources
              // (s_tonemapOutputUAV).  RestoreRenderExportState recreates the
              // DXR pipeline which destroys those resources.  If we did it on
              // the same frame, the GPU would read freed memory and overwrite
              // the denoised image with garbage.
              LatchPreviewRenderImage();
              g_renderExportJob.previewRestorePending = true;
              g_renderExportStatus =
                  "Preview ready. Move camera or press ESC to dismiss.";
            } else if (g_renderExportJob.tileState.enabled) {
              RenderExportTileState &t = g_renderExportJob.tileState;
              std::vector<uint8_t> tileData;
              bool readbackOk = DxrRenderer::ReadbackBeautyTile(
                  tileData, t.tileWidth, t.tileHeight);
              if (readbackOk &&
                  tileData.size() >=
                      (size_t)t.tileWidth * t.tileHeight * 8) {
                CompositeTileToHdrPanorama(t, tileData);
                if (g_renderExportJob.targetDenoiserIndex != 0 &&
                    !t.cpuAlbedoGuideBuffer.empty() &&
                    !t.cpuNormalGuideBuffer.empty()) {
                  std::vector<uint8_t> tileAlbedo;
                  std::vector<uint8_t> tileNormal;
                  if (DxrRenderer::ReadbackGuideTiles(
                          tileAlbedo, tileNormal, t.tileWidth,
                          t.tileHeight) &&
                      CompositeTileToHalf4Buffer(
                          t, tileAlbedo, t.cpuAlbedoGuideBuffer) &&
                      CompositeTileToHalf4Buffer(
                          t, tileNormal, t.cpuNormalGuideBuffer)) {
                    t.guidesCaptured = !t.guideReadbackFailed;
                  } else {
                    t.guidesCaptured = false;
                    t.guideReadbackFailed = true;
                    fprintf(stderr,
                            "Tiled export: guide readback failed for tile "
                            "%u/%u; final panorama denoise will be skipped.\n",
                            t.currentTileIndex + 1,
                            t.tileCountX * t.tileCountY);
                  }
                }
                fprintf(stderr,
                        "Tiled export: tile %u/%u done (%ux%u at offset %u,%u)\n",
                        t.currentTileIndex + 1,
                        t.tileCountX * t.tileCountY,
                        t.tileWidth, t.tileHeight,
                        t.tileOffsetX, t.tileOffsetY);

                if (AdvanceToNextTile(g_renderExportJob)) {
                  g_renderExportJob.completionArmed = false;
                  g_renderExportJob.completionFrames = 0;
                  g_renderExportJob.settleFramesRemaining = 0;
                  g_renderExportJob.minSppBeforeNoiseStop =
                      (g_renderExportJob.targetMaxSpp < 32)
                          ? (UINT)g_renderExportJob.targetMaxSpp
                          : 32u;
                  DxrRenderer::WaitForAsyncRestirIdle();
                  DX12Context::WaitGPUIdle();
                  DxrRenderer::CreateRayTracingPipeline(
                      g_renderExportJob.targetWidth,
                      g_renderExportJob.targetHeight);
                  DxrRenderer::ResetAccumulation();
                  DxrRenderer::SetExportTileConstants(
                      t.fullWidth, t.fullHeight,
                      t.tileOffsetX, t.tileOffsetY);
                  g_renderExportStatus =
                      "Tiled export: tile " +
                      std::to_string(t.currentTileIndex + 1) + "/" +
                      std::to_string(t.tileCountX * t.tileCountY);
                } else {
                  fprintf(stderr,
                          "Tiled export: all %u tiles done, compositing %ux%u panorama\n",
                          t.tileCountX * t.tileCountY,
                          t.fullWidth, t.fullHeight);
                  const std::vector<uint8_t> *beautyForTonemap =
                      &t.cpuBeautyBuffer;
                  std::vector<uint8_t> denoisedBeauty;
                  if (g_renderExportJob.targetDenoiserIndex != 0) {
                    if (t.guidesCaptured && !t.guideReadbackFailed &&
                        t.cpuAlbedoGuideBuffer.size() ==
                            t.cpuBeautyBuffer.size() &&
                        t.cpuNormalGuideBuffer.size() ==
                            t.cpuBeautyBuffer.size()) {
                      g_renderExportStatus =
                          "Denoising panorama with guides...";
#ifdef USE_QT_UI
                      w.refreshRenderExportProgressUiNow();
                      QApplication::processEvents();
#endif
                      fprintf(stderr,
                              "Tiled export: running full panorama CPU OIDN "
                              "with stitched guides.\n");
                      if (DxrRenderer::DenoiseHostBeautyGuidedHalf4(
                              t.cpuBeautyBuffer, t.cpuAlbedoGuideBuffer,
                              t.cpuNormalGuideBuffer, t.fullWidth,
                              t.fullHeight, denoisedBeauty)) {
                        beautyForTonemap = &denoisedBeauty;
                      } else {
                        fprintf(stderr,
                                "Tiled export: guided CPU OIDN failed; "
                                "saving accumulated panorama.\n");
                      }
                    } else {
                      fprintf(stderr,
                              "Tiled export: stitched guides unavailable; "
                              "saving accumulated panorama without denoise.\n");
                    }
                  }
                  std::vector<uint8_t> rgbaOut;
                  TonemapHdrPanoramaToRgba8(
                      *beautyForTonemap, t.fullWidth, t.fullHeight, rgbaOut);
                  bool exported = DxrRenderer::SaveRgba8BufferToPng(
                      g_renderExportJob.outputPath,
                      t.fullWidth, t.fullHeight, rgbaOut);
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
                  g_renderExportJob.completionExportSucceeded = exported;
                  g_renderExportJob.completionAdvancePending = true;
                  g_renderExportJob.previewRestorePending = true;
                  g_renderExportJob.tileState = {};
                }
              } else {
                g_renderExportStatus = "Export failed: tile readback error.";
                fprintf(stderr, "Render export failed: tile readback error\n");
                g_renderExportJob.completionExportSucceeded = false;
                g_renderExportJob.completionAdvancePending = true;
                g_renderExportJob.previewRestorePending = true;
                g_renderExportJob.tileState = {};
              }
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
              g_renderExportJob.completionExportSucceeded = exported;
              g_renderExportJob.completionAdvancePending = true;
              g_renderExportJob.previewRestorePending = true;
            }
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
    const bool usingTiledExportAspect =
        usingExportAspect && g_renderExportJob.tileState.enabled &&
        g_renderExportJob.tileState.fullWidth > 0 &&
        g_renderExportJob.tileState.fullHeight > 0;
    const float targetWidth =
        usingTiledExportAspect
            ? (float)g_renderExportJob.tileState.fullWidth
            : (usingExportAspect ? (float)g_renderExportJob.targetWidth
                                 : (float)previewWidth);
    const float targetHeight =
        usingTiledExportAspect
            ? (float)g_renderExportJob.tileState.fullHeight
            : (usingExportAspect ? (float)g_renderExportJob.targetHeight
                                 : (float)previewHeight);
    if (targetHeight > 0.0f) {
      g_cameraData.aspect = targetWidth / targetHeight;
    }
    g_cameraData.debugMode = (float)g_debugMode;
    g_cameraData.lightCount = (float)DxrRenderer::GetLightCount();
    g_cameraData.frameCount = (float)DxrRenderer::GetDisplayedSampleCount();
    const bool fileIblActive =
        (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
    const bool effectiveCloudRendering =
        g_cloudRenderingEnabled && !fileIblActive;
    g_cameraData.cloudRenderingEnabled = effectiveCloudRendering ? 1.0f : 0.0f;
    g_cameraData.dxrProceduralSkyBoost = g_iblIntensity;
    g_cameraData.iblIndirectBoost = g_iblIndirectBoost;
    UpdateCameraCB();

    // Camera-relative scatter (min/max distance, fade) needs a full AS
    // rebuild whenever the camera moves, because the instance set
    // changes. The scatter cache check inside AppendScatterInstances
    // detects this but can't itself queue a rebuild — by the time it
    // runs, the renderer has already decided whether to call
    // Scene::GetInstances(). Tick here so the flag is set before the
    // next AS pass.
    Scene::TickScatterCameraInvalidation();
    Scene::TickVolumeAnimations(dt);

    if (!g_renderExportJob.active && g_currentRenderMode == RenderMode::DXR) {
      EnsureInteractiveDxrPipelineSize(previewWidth, previewHeight,
                                       "interactive preview resize");
    }

    // Update Cloud Manager (uploads changed params to GPU)
    g_cloudManager.Update(dt, DX12Context::g_frameIndex);

    // Editor UI (moved to editor_ui.cpp)
    DrawEditorUI(g_fps, g_timeOfDay, g_northOffset, g_latitudeDeg, g_dayOfYear);

    const bool previewOverlayHoldingViewport =
        !g_renderExportJob.active && HasPreviewRenderImage();
    if (previewOverlayHoldingViewport && !PreviewRenderNeedsPresent()) {
      prevTime = std::chrono::high_resolution_clock::now();
      WaitForSoftIdleMessage();
      prevTime = std::chrono::high_resolution_clock::now();
      continue;
    }

    const bool consumedDxrWake =
        !g_renderExportJob.active && g_currentRenderMode == RenderMode::DXR &&
        DxrRenderer::IsReady() && DxrRenderer::ConsumeInteractiveWake();
    const bool consumedSceneLoadWarmup =
        !g_renderExportJob.active && g_currentRenderMode == RenderMode::DXR &&
        DxrRenderer::IsReady() && DxrRenderer::ConsumeSceneLoadWarmupFrame();
  #ifdef USE_QT_UI
    const bool canIdleDxr =
      !g_renderExportJob.active && g_currentRenderMode == RenderMode::DXR &&
      DxrRenderer::IsReady() && !consumedDxrWake &&
      !consumedSceneLoadWarmup &&
      DxrRenderer::CanIdleWithoutRendering();
  #else
    const bool canIdleDxr =
      !handledWindowMessage && !g_renderExportJob.active &&
      g_currentRenderMode == RenderMode::DXR && DxrRenderer::IsReady() &&
      !consumedDxrWake && !consumedSceneLoadWarmup &&
      DxrRenderer::CanIdleWithoutRendering();
  #endif
    if (canIdleDxr) {
      const auto idleNow = std::chrono::high_resolution_clock::now();
      const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 idleNow - lastIdleUiPresentTime)
                                 .count();
      if (elapsedMs < kIdleUiFrameIntervalMs) {
        const DWORD waitMs = (std::max)(
            1L, static_cast<LONG>(kIdleUiFrameIntervalMs - elapsedMs));
        prevTime = std::chrono::high_resolution_clock::now();
        WaitForSoftIdleMessage(waitMs);
        prevTime = std::chrono::high_resolution_clock::now();
        continue;
      }

      // DXR has converged, but ImGui still needs periodic swapchain presents.
      // RenderFrame's end-condition path skips ray dispatch and sample
      // increments, so this refresh redraws the frozen image plus UI only.
      lastIdleUiPresentTime = idleNow;
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

    if (!g_renderExportJob.active && HasPreviewRenderImage() &&
        PreviewRenderNeedsPresent()) {
      MarkPreviewRenderPresented();
    }

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

#ifndef USE_QT_UI
  if (dropTarget) {
    RevokeDragDrop(hwnd);
    dropTarget->Release();
    dropTarget = nullptr;
  }
  if (oleInitialized) {
    OleUninitialize();
  }
#endif

  // Cleanup fence event
  CloseHandle(DX12Context::g_fenceEvent);

  return 0;
}
