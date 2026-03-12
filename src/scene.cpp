#define NOMINMAX
#include "scene.h"
#include "ImGuizmo.h"
#include "assets/asset_loader.h"
#include "camera.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "file_import.h"
#include "grass_manager.h"
#include "ibl_manager.h"
#include "imgui.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// Externals from main.cpp (global symbols)
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;
extern UINT g_textureDescriptorCount;
extern D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart;
extern D3D12_CPU_DESCRIPTOR_HANDLE g_texturesCpuStart;
extern UINT g_textureDescriptorCapacity;

using namespace DX12Context;

namespace Scene {

static std::vector<Node> s_nodes;
static std::vector<Light> s_lights;
// Import progress & pending results (for async import)
static std::atomic<bool> s_importInProgress(false);
static std::atomic<float> s_importProgress(0.0f);
static std::string s_importStatus;
static std::mutex s_importStatusMutex;

static std::vector<Asset::GpuMesh> s_pendingMeshes;
static std::vector<Asset::Material> s_pendingMaterials;
static std::vector<Asset::Texture> s_pendingTextures;
static std::string s_pendingPath;
static std::atomic<bool> s_pendingReady(false);
static std::mutex s_pendingMutex;
static ImGuizmo::OPERATION g_currentGizmoOp = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE g_currentGizmoMode = ImGuizmo::WORLD;

static ImGuizmo::OPERATION GetActiveGizmoOperation() {
  if (g_currentGizmoOp == ImGuizmo::ROTATE) {
    return static_cast<ImGuizmo::OPERATION>(ImGuizmo::ROTATE_X |
                                            ImGuizmo::ROTATE_Y |
                                            ImGuizmo::ROTATE_Z);
  }
  return g_currentGizmoOp;
}

static bool GetTextureDescriptorCpuHandle(UINT textureIndex,
                                          D3D12_CPU_DESCRIPTOR_HANDLE *outCpu) {
  if (!outCpu || !g_device || g_texturesCpuStart.ptr == 0 ||
      g_textureDescriptorCapacity == 0) {
    return false;
  }
  if (textureIndex >= g_textureDescriptorCapacity) {
    return false;
  }
  const UINT inc = g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (inc == 0) {
    return false;
  }
  *outCpu = g_texturesCpuStart;
  outCpu->ptr += (SIZE_T)textureIndex * (SIZE_T)inc;
  return true;
}

static bool WriteTextureSrv(UINT textureIndex, const Asset::Texture &tex) {
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
  if (!tex.resource || !GetTextureDescriptorCpuHandle(textureIndex, &cpu)) {
    return false;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = tex.format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = tex.mipLevels;
  g_device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpu);
  return true;
}

int AddTextureFromFile(const std::string &utf8path, bool isHDR) {
  if (!g_device) {
    fprintf(stderr, "AddTextureFromFile: no device\n");
    return -1;
  }
  // Serialize with renderer work to avoid re-entrant queue activity while
  // modal dialogs are active and the window may also be resizing.
  WaitGPUIdle();

  Asset::Texture tex = Asset::LoadTextureFromFile(utf8path, isHDR);
  if (!tex.resource) {
    fprintf(stderr, "AddTextureFromFile: failed to load '%s'\n",
            utf8path.c_str());
    return -1;
  }

  const int newIndex = (int)g_loadedTextures.size();
  if ((UINT)newIndex >= g_textureDescriptorCapacity) {
    fprintf(stderr,
            "AddTextureFromFile: descriptor capacity exceeded (%u) for '%s'\n",
            g_textureDescriptorCapacity, utf8path.c_str());
    return -1;
  }
  g_loadedTextures.push_back(std::move(tex));

  const Asset::Texture &t = g_loadedTextures.back();
  if (!WriteTextureSrv((UINT)newIndex, t)) {
    g_loadedTextures.pop_back();
    fprintf(stderr, "AddTextureFromFile: failed to create SRV for '%s'\n",
            utf8path.c_str());
    return -1;
  }

  g_textureDescriptorCount = (UINT)g_loadedTextures.size();
  fprintf(stderr,
          "AddTextureFromFile: added texture #%d '%s' (w=%u h=%u mips=%u)\n",
          newIndex, utf8path.c_str(), t.width, t.height, t.mipLevels);
  return newIndex;
}

// Helper: Simple matrix math for ImGuizmo
void BuildViewMatrix(float *mat) {
  float pos[3] = {g_cameraData.pos[0], g_cameraData.pos[1],
                  g_cameraData.pos[2]};
  float fwd[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                  g_cameraData.forward[2]};
  float up_in[3] = {0, 1, 0}; // Use world up as reference

  // R = F x U (as in shader)
  float R[3] = {fwd[1] * up_in[2] - fwd[2] * up_in[1],
                fwd[2] * up_in[0] - fwd[0] * up_in[2],
                fwd[0] * up_in[1] - fwd[1] * up_in[0]};
  float rlen = sqrtf(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
  if (rlen > 0) {
    R[0] /= rlen;
    R[1] /= rlen;
    R[2] /= rlen;
  }

  // U = R x F
  float U[3] = {R[1] * fwd[2] - R[2] * fwd[1], R[2] * fwd[0] - R[0] * fwd[2],
                R[0] * fwd[1] - R[1] * fwd[0]};
  float ulen = sqrtf(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]);
  if (ulen > 0) {
    U[0] /= ulen;
    U[1] /= ulen;
    U[2] /= ulen;
  }

  memset(mat, 0, 16 * sizeof(float));
  // Column 0
  mat[0] = R[0];
  mat[1] = U[0];
  mat[2] = fwd[0];
  // Column 1
  mat[4] = R[1];
  mat[5] = U[1];
  mat[6] = fwd[1];
  // Column 2
  mat[8] = R[2];
  mat[9] = U[2];
  mat[10] = fwd[2];
  // Column 3 (Trans)
  mat[12] = -(R[0] * pos[0] + R[1] * pos[1] + R[2] * pos[2]);
  mat[13] = -(U[0] * pos[0] + U[1] * pos[1] + U[2] * pos[2]);
  mat[14] = -(fwd[0] * pos[0] + fwd[1] * pos[1] + fwd[2] * pos[2]);
  mat[15] = 1.0f;
}

void BuildProjectionMatrix(float *mat) {
  float fovRad = g_cameraData.fov * 3.14159265359f / 180.0f;
  float aspect = g_cameraData.aspect;
  float n = g_cameraData.nearZ;
  float f = g_cameraData.farZ;
  float focalScale = 1.0f / tanf(fovRad * 0.5f);

  memset(mat, 0, 16 * sizeof(float));
  mat[0] = focalScale / aspect;
  mat[5] = focalScale;
  mat[10] = f / (f - n);
  mat[11] = 1.0f;
  mat[14] = -(f * n) / (f - n);
}

static std::string s_lastStatus;

Node::Node() {
  name = "New Node";
  // Identity matrix
  for (int i = 0; i < 16; ++i)
    transform[i] = 0.0f;
  transform[0] = transform[5] = transform[10] = transform[15] = 1.0f;
  selected = false;
  visible = true;
}

static void EnsureGpuBuffersForMeshes(std::vector<Asset::GpuMesh> &meshes) {
  for (auto &mesh : meshes) {
    if (mesh.vertexBuffer && mesh.indexBuffer)
      continue;
    if (mesh.cpuVertices.empty() || mesh.cpuIndices.empty())
      continue;
    const int materialIndex = mesh.materialIndex;
    Asset::GpuMesh uploaded =
        Asset::LoadMeshFromMemory(mesh.cpuVertices, mesh.cpuIndices);
    uploaded.materialIndex = materialIndex;
    mesh = std::move(uploaded);
  }
}

const std::string &LastStatus() { return s_lastStatus; }

bool ImportModel(const std::string &utf8path, const float *rootTranslation) {
  try {
    fprintf(stderr, "Scene::ImportModel: importing %s\n", utf8path.c_str());
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;
    bool ok = Asset::LoadModel(utf8path, meshes, &materials, &textures,
                               rootTranslation);
    if (!ok) {
      s_lastStatus = std::string("Load failed: ") + utf8path;
      fprintf(stderr, "%s\n", s_lastStatus.c_str());
      return false;
    }

    // If loader returned success but produced no meshes, treat as failure —
    // prevents UI from reporting success when nothing was created.
    if (meshes.empty()) {
      s_lastStatus =
          std::string("Load failed (no meshes returned): ") + utf8path;
      fprintf(stderr, "%s\n", s_lastStatus.c_str());
      return false;
    }
    EnsureGpuBuffersForMeshes(meshes);

    size_t meshBase = g_loadedMeshes.size();
    size_t materialBase = g_loadedMaterials.size();
    size_t textureBase = g_loadedTextures.size();

    g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(), meshes.end());
    g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(),
                             materials.end());
    g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(),
                            textures.end());

    // Adjust newly-inserted meshes' material indices to global material base
    for (size_t i = 0; i < meshes.size(); ++i) {
      int &mi = g_loadedMeshes[meshBase + i].materialIndex;
      if (mi >= 0)
        mi = mi + (int)materialBase;
    }

    // Adjust newly-inserted materials to reference global texture indices
    for (size_t i = 0; i < materials.size(); ++i) {
      Asset::Material &m = g_loadedMaterials[materialBase + i];
      if (m.diffuseTexture >= 0)
        m.diffuseTexture += (int)textureBase;
      if (m.normalTexture >= 0)
        m.normalTexture += (int)textureBase;
      if (m.occlusionTexture >= 0)
        m.occlusionTexture += (int)textureBase;
      if (m.emissiveTexture >= 0)
        m.emissiveTexture += (int)textureBase;
      if (m.metalRoughTexture >= 0)
        m.metalRoughTexture += (int)textureBase;
    }

    RegisterTextures(textures);

    // Create a single parent node for this import, named after the source
    // file. All imported meshes are attached to this node.
    Node node;
    node.name = std::filesystem::path(utf8path).filename().string();
    node.sourcePath = utf8path;
    for (size_t i = 0; i < meshes.size(); ++i)
      node.meshIndices.push_back(meshBase + i);
    s_nodes.push_back(node);

    s_lastStatus = std::string("Loaded: ") + utf8path;
    fprintf(stderr, "%s\n", s_lastStatus.c_str());

    // Rebuild AS for current active meshes
    fprintf(stderr, "RebuildAccelerationStructures: start\n");
    fflush(stderr);
    RebuildAccelerationStructures();
    fprintf(stderr, "RebuildAccelerationStructures: done\n");
    fflush(stderr);
    // Recreate DXR pipeline so it can merge texture descriptors (if any)
    fprintf(stderr, "CreateRayTracingPipeline: start\n");
    fflush(stderr);
    DxrRenderer::CreateRayTracingPipeline(0, 0);
    fprintf(stderr, "CreateRayTracingPipeline: done\n");
    fflush(stderr);
    return true;
  } catch (const std::exception &e) {
    s_lastStatus = std::string("Import exception: ") + e.what();
    fprintf(stderr, "%s\n", s_lastStatus.c_str());
    return false;
  }
}

void RegisterTextures(const std::vector<Asset::Texture> &textures) {
  if (textures.empty())
    return;

  if (!g_device || g_texturesCpuStart.ptr == 0 || g_textureDescriptorCapacity == 0) {
    fprintf(stderr, "RegisterTextures: texture descriptor table unavailable\n");
    return;
  }

  size_t globalStart = 0;
  if (g_loadedTextures.size() >= textures.size()) {
    globalStart = g_loadedTextures.size() - textures.size();
  }

  if (globalStart >= (size_t)g_textureDescriptorCapacity) {
    fprintf(stderr,
            "RegisterTextures: descriptor capacity exceeded before upload "
            "(start=%zu cap=%u)\n",
            globalStart, g_textureDescriptorCapacity);
    g_textureDescriptorCount = g_textureDescriptorCapacity;
    return;
  }

  for (size_t i = 0; i < textures.size(); ++i) {
    const size_t globalIndexSizeT = globalStart + i;
    if (globalIndexSizeT >= (size_t)g_textureDescriptorCapacity) {
      fprintf(stderr,
              "RegisterTextures: descriptor capacity reached at texture %zu "
              "(cap=%u)\n",
              globalIndexSizeT, g_textureDescriptorCapacity);
      break;
    }

    const Asset::Texture &tex = textures[i];
    if (tex.resource) {
      fprintf(stderr,
              "RegisterTextures: texture %zu (res=%p w=%u h=%u "
              "mips=%u fmt=%u)\n",
              i, tex.resource.Get(), tex.width, tex.height, tex.mipLevels,
              (unsigned)tex.format);
      fflush(stderr);
      WriteTextureSrv((UINT)globalIndexSizeT, tex);
    }
  }
  const size_t clampedCount = (g_loadedTextures.size() < (size_t)g_textureDescriptorCapacity)
                                  ? g_loadedTextures.size()
                                  : (size_t)g_textureDescriptorCapacity;
  g_textureDescriptorCount = (UINT)clampedCount;
}

static int s_selectedLightIdx = -1;

bool ImportModelWithDialog(HWND hwnd) {
  std::wstring chosen;
  if (OpenModelFileDialog(hwnd, chosen)) {
    if (chosen.empty()) {
      s_lastStatus = "No file chosen";
      return false;
    }
    // convert wstring -> utf8
    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0, chosen.c_str(), (int)chosen.size(), NULL, 0, NULL, NULL);
    std::string utf8path(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, chosen.c_str(), (int)chosen.size(),
                        &utf8path[0], size_needed, NULL, NULL);

    if (s_importInProgress.load()) {
      s_lastStatus = "Import already in progress";
      return false;
    }

    // Reset progress state and set callback
    s_importInProgress = true;
    s_importProgress = 0.0f;
    {
      std::lock_guard<std::mutex> lg(s_importStatusMutex);
      s_importStatus = "Starting import...";
    }

    Asset::SetProgressCallback([&](float p, const std::string &msg) {
      s_importProgress = p;
      std::lock_guard<std::mutex> lg(s_importStatusMutex);
      s_importStatus = msg;
    });

    // Launch background thread to do CPU-side import. The main thread will
    // merge results when ready.
    std::thread([utf8path]() {
      std::vector<Asset::GpuMesh> meshes;
      std::vector<Asset::Material> materials;
      std::vector<Asset::Texture> textures;
      Asset::SetDeferGpuUpload(true);
      bool ok =
          Asset::LoadModel(utf8path, meshes, &materials, &textures, nullptr);
      Asset::SetDeferGpuUpload(false);

      // If the loader failed or produced no meshes, report error and do not
      // queue pending results for the main thread to merge.
      if (!ok || meshes.empty()) {
        std::string msg =
            !ok ? (std::string("Import failed: ") + utf8path)
                : (std::string("Import produced no meshes: ") + utf8path);
        fprintf(stderr, "Scene::ImportModel (async): %s\n", msg.c_str());
        {
          std::lock_guard<std::mutex> lg(s_importStatusMutex);
          s_importStatus = msg;
        }
        s_importInProgress = false;
        s_importProgress = 0.0f;
        Asset::ClearProgressCallback();
        return; // abort thread — nothing to merge
      }

      // Store pending results for main thread to pick up
      {
        std::lock_guard<std::mutex> lg(s_pendingMutex);
        s_pendingMeshes = std::move(meshes);
        s_pendingMaterials = std::move(materials);
        s_pendingTextures = std::move(textures);
        s_pendingPath = utf8path;
      }
      s_pendingReady = true;
      // Keep progress callback until main thread merges, but clear loader
      // callback now
      Asset::ClearProgressCallback();
    }).detach();

    return true;
  }
  s_lastStatus = "Open cancelled";
  return false;
}

bool ImportHDRWithDialog(HWND hwnd) {
  std::wstring chosen;
  if (OpenHDRFileDialog(hwnd, chosen)) {
    if (chosen.empty()) {
      s_lastStatus = "No file chosen";
      return false;
    }
    // convert wstring -> utf8
    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0, chosen.c_str(), (int)chosen.size(), NULL, 0, NULL, NULL);
    std::string utf8path(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, chosen.c_str(), (int)chosen.size(),
                        &utf8path[0], size_needed, NULL, NULL);

    if (IBLManager::Get().LoadEnvironmentMap(utf8path)) {
      s_lastStatus = "IBL Map loaded: " + utf8path;
      // Also need to push descriptors if necessary, but IBLManager handles it
      // internally usually. However, DXR should be notified if the descriptor
      // changed.
      DxrRenderer::CreateRayTracingPipeline(0, 0);
      return true;
    } else {
      s_lastStatus = "IBL Load failed: " + utf8path;
      return false;
    }
  }
  s_lastStatus = "Open cancelled";
  return false;
}

const std::vector<Node> &GetNodes() { return s_nodes; }

void SelectNode(size_t index) {
  for (size_t i = 0; i < s_nodes.size(); ++i)
    s_nodes[i].selected = false;
  s_selectedLightIdx = -1;
  if (index < s_nodes.size())
    s_nodes[index].selected = true;
}

void DeleteNode(size_t index) {
  if (index >= s_nodes.size())
    return;

  // Ensure GPU is not using the resources we are about to release
  WaitGPUIdle();

  // Mark meshes as empty by clearing their vertex/index resources
  for (size_t mi : s_nodes[index].meshIndices) {
    if (mi < g_loadedMeshes.size()) {
      g_loadedMeshes[mi].vertexBuffer.Reset();
      g_loadedMeshes[mi].indexBuffer.Reset();
      // mark counts zero
      g_loadedMeshes[mi].vertexCount = 0;
      g_loadedMeshes[mi].indexCount = 0;
    }
  }
  s_nodes.erase(s_nodes.begin() + index);

  // Rebuild AS after deletion
  RebuildAccelerationStructures();
  DxrRenderer::ResetAccumulation();
}

void AddDefaultPlane(float offset_y) {
  try {
    // plane 10x10 centered at origin on XZ plane (Y up)
    const float half = 5.0f;
    Asset::Vertex verts[4] = {{{-half, offset_y, -half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, 1},
                               {0.0f, 0.0f}},
                              {{half, offset_y, -half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, 1},
                               {1.0f, 0.0f}},
                              {{half, offset_y, half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, 1},
                               {1.0f, 1.0f}},
                              {{-half, offset_y, half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, 1},
                               {0.0f, 1.0f}}};
    // Use CCW winding that points UP (0-3-2 and 0-2-1)
    UINT indices[6] = {0, 3, 2, 0, 2, 1};

    // Create upload vertex buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(verts);
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Asset::GpuMesh gm;
    ThrowIfFailed(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&gm.vertexBuffer)));
    UINT8 *pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(
        gm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pData)));
    memcpy(pData, verts, sizeof(verts));
    gm.vertexBuffer->Unmap(0, nullptr);

    gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
    gm.vbView.StrideInBytes = sizeof(Asset::Vertex);
    gm.vbView.SizeInBytes = sizeof(verts);

    // Index buffer
    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = sizeof(indices);
    ThrowIfFailed(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&gm.indexBuffer)));
    pData = nullptr;
    ThrowIfFailed(
        gm.indexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pData)));
    memcpy(pData, indices, sizeof(indices));
    gm.indexBuffer->Unmap(0, nullptr);

    gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
    gm.ibView.Format = DXGI_FORMAT_R32_UINT;
    gm.ibView.SizeInBytes = sizeof(indices);

    gm.vertexCount = 4;
    gm.indexCount = 6;

    // Populate CPU copies for serialization/raypicking
    gm.cpuVertices.assign(verts, verts + 4);
    gm.cpuIndices.assign(indices, indices + 6);

    gm.minBound[0] = -half;
    gm.minBound[1] = offset_y;
    gm.minBound[2] = -half;
    gm.maxBound[0] = half;
    gm.maxBound[1] = offset_y;
    gm.maxBound[2] = half;

    // Default material
    Asset::Material mat;
    // Grey default
    mat.diffuseColor[0] = 0.8f;
    mat.diffuseColor[1] = 0.8f;
    mat.diffuseColor[2] = 0.8f;
    mat.diffuseColor[3] = 1.0f;
    mat.roughness = 1.0f;
    mat.specularWeight = 1.0f;
    mat.transmissionWeight = 0.0f;
    mat.transmissionColor[0] = 1.0f;
    mat.transmissionColor[1] = 1.0f;
    mat.transmissionColor[2] = 1.0f;
    mat.diffuseTexture = -1;
    mat.normalTexture = -1;
    mat.occlusionTexture = -1;
    mat.emissiveTexture = -1;
    mat.metalRoughTexture = -1;

    int matIndex = (int)g_loadedMaterials.size();
    g_loadedMaterials.push_back(mat);
    gm.materialIndex = matIndex;

    size_t meshIndex = g_loadedMeshes.size();
    g_loadedMeshes.push_back(gm);

    // Create a node for the plane
    Node node;
    node.name = "Ground Plane";
    node.meshIndices.push_back(meshIndex);
    s_nodes.push_back(node);

    fprintf(stderr, "AddDefaultPlane: added plane (10x10) with material %d\n",
            matIndex);

    // Rebuild AS
    RebuildAccelerationStructures();
  } catch (const std::exception &e) {
    fprintf(stderr, "AddDefaultPlane: exception: %s\n", e.what());
  }
}

void RebuildAccelerationStructures() {
  DxrRenderer::BuildAccelerationStructures(GetActiveMeshes(), GetInstances());
}

std::vector<const Asset::GpuMesh *> GetActiveMeshes() {
  std::vector<const Asset::GpuMesh *> active;
  for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
    const auto &m = g_loadedMeshes[i];
    if (m.vertexBuffer && m.indexBuffer && m.vertexCount > 0 &&
        m.indexCount > 0)
      active.push_back(&m);
  }
  // Include procedural grass patch mesh so DXR rebuilds can resolve its BLAS.
  const Asset::GpuMesh *patch = GrassManager::GetPatchMesh();
  if (patch && patch->vertexBuffer && patch->indexBuffer && patch->indexCount > 0) {
    const bool alreadyPresent = std::any_of(
        active.begin(), active.end(),
        [patch](const Asset::GpuMesh *m) {
          return m && m->vertexBuffer.Get() == patch->vertexBuffer.Get();
        });
    if (!alreadyPresent) {
      active.push_back(patch);
    }
  }
  return active;
}

std::vector<Instance> GetInstances() {
  std::vector<Instance> instances;
  instances.reserve(1280); // Heuristic
  for (size_t ni = 0; ni < s_nodes.size(); ++ni) {
    const auto &node = s_nodes[ni];
    if (!node.visible)
      continue;
    for (size_t mi : node.meshIndices) {
      if (mi < g_loadedMeshes.size()) {
        Instance inst;
        inst.name = node.name;
        inst.mesh = &g_loadedMeshes[mi];
        inst.transform = DirectX::XMLoadFloat4x4(
            reinterpret_cast<const DirectX::XMFLOAT4X4 *>(node.transform));
        inst.id = (int)ni;
        instances.push_back(inst);
      }
    }
  }
  return instances;
}

std::vector<Light> &GetLights() { return s_lights; }

void UpdateLights() { DxrRenderer::UpdateLights(s_lights); }

void AddLight(LightType type) {
  Light l = {};
  memset(&l, 0, sizeof(Light));
  l.type = (uint32_t)type;
  l.position[0] = 0;
  l.position[1] = 2;
  l.position[2] = 0;
  l.emission[0] = 1000;
  l.emission[1] = 1000;
  l.emission[2] = 1000;
  l.direction[0] = 0;
  l.direction[1] = -1;
  l.direction[2] = 0;
  l.radius = 0.1f;
  l.innerConeAngle = cosf(DirectX::XMConvertToRadians(30.0f));
  l.outerConeAngle = cosf(DirectX::XMConvertToRadians(45.0f));
  l.areaExtents[0] = 1;
  l.areaExtents[1] = 1;
  l.iesAtlasIndex = -1;
  s_lights.push_back(l);
  UpdateLights();
}

void RemoveLight(size_t index) {
  if (index < s_lights.size()) {
    s_lights.erase(s_lights.begin() + index);
    UpdateLights();
  }
}

void DrawLightsPanel(bool &visible) {
  if (!visible)
    return;
  if (ImGui::Begin("Global Lights", &visible)) {
    if (ImGui::Button("Add Point Light"))
      AddLight(LightType::Omni);
    ImGui::SameLine();
    if (ImGui::Button("Add Spot Light"))
      AddLight(LightType::Spot);
    ImGui::SameLine();
    if (ImGui::Button("Add Rect Area"))
      AddLight(LightType::AreaRect);
    ImGui::SameLine();
    if (ImGui::Button("Add Disk Area"))
      AddLight(LightType::AreaDisk);

    ImGui::Separator();

    for (size_t i = 0; i < s_lights.size(); ++i) {
      ImGui::PushID((int)i);
      Light &l = s_lights[i];
      char buf[64];
      const char *typeStr = "Unknown";
      switch ((LightType)l.type) {
      case LightType::Directional:
        typeStr = "Sun";
        break;
      case LightType::Omni:
        typeStr = "Omni";
        break;
      case LightType::Spot:
        typeStr = "Spot";
        break;
      case LightType::AreaRect:
        typeStr = "Rect";
        break;
      case LightType::AreaDisk:
        typeStr = "Disk";
        break;
      case LightType::IES:
        typeStr = "IES";
        break;
      }
      sprintf(buf, "Light %zu (%s)", i, typeStr);

      bool isSelected = (s_selectedLightIdx == (int)i);
      if (isSelected)
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.7f));

      bool headerOpen = ImGui::CollapsingHeader(buf);

      // Select light when clicking the header
      if (ImGui::IsItemClicked(0)) {
        s_selectedLightIdx = (int)i;
        for (auto &n : s_nodes)
          n.selected = false;
      }

      if (isSelected)
        ImGui::PopStyleColor();

      if (headerOpen) {
        bool changed = false;
        changed |= ImGui::DragFloat3("Position", l.position, 0.1f);

        // Color + Intensity (separate controls)
        // Compute current intensity as max component
        float maxComp =
            std::max({l.emission[0], l.emission[1], l.emission[2], 0.001f});
        float color[3] = {l.emission[0] / maxComp, l.emission[1] / maxComp,
                          l.emission[2] / maxComp};
        float intensity = maxComp;

        if (ImGui::ColorEdit3("Color", color)) {
          l.emission[0] = color[0] * intensity;
          l.emission[1] = color[1] * intensity;
          l.emission[2] = color[2] * intensity;
          changed = true;
        }
        if (ImGui::DragFloat("Intensity", &intensity, 10.0f, 0.0f, 1000000.0f,
                             "%.2f")) {
          l.emission[0] = color[0] * intensity;
          l.emission[1] = color[1] * intensity;
          l.emission[2] = color[2] * intensity;
          changed = true;
        }

        if (l.type != (uint32_t)LightType::Omni) {
          changed |= ImGui::DragFloat3("Direction", l.direction, 0.01f);
        }

        changed |= ImGui::DragFloat("Radius", &l.radius, 0.01f, 0.0f, 10.0f);

        if (l.type == (uint32_t)LightType::Spot) {
          float inner = acosf(l.innerConeAngle) * 180.0f / 3.14159f;
          float outer = acosf(l.outerConeAngle) * 180.0f / 3.14159f;
          if (ImGui::SliderFloat("Inner Angle", &inner, 0, 90)) {
            l.innerConeAngle = cosf(DirectX::XMConvertToRadians(inner));
            changed = true;
          }
          if (ImGui::SliderFloat("Outer Angle", &outer, inner, 90)) {
            l.outerConeAngle = cosf(DirectX::XMConvertToRadians(outer));
            changed = true;
          }
        }

        if (l.type == (uint32_t)LightType::AreaRect ||
            l.type == (uint32_t)LightType::AreaDisk) {
          changed |=
              ImGui::DragFloat2("Extents", l.areaExtents, 0.1f, 0.01f, 50.0f);
        }

        if (l.type == (uint32_t)LightType::IES) {
          ImGui::Text("IES Atlas Index: %d (placeholder)", l.iesAtlasIndex);
        }

        ImGui::Spacing();
        if (ImGui::Button("Select for Gizmo")) {
          s_selectedLightIdx = (int)i;
          for (auto &n : s_nodes)
            n.selected = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
          if (s_selectedLightIdx == (int)i)
            s_selectedLightIdx = -1;
          else if (s_selectedLightIdx > (int)i)
            s_selectedLightIdx--;
          RemoveLight(i);
          ImGui::PopID();
          break;
        }
        if (changed)
          UpdateLights();
      }
      ImGui::PopID();
    }
  }
  ImGui::End();
}

void DrawLightGizmo() {
  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);

  ImGuiViewport *mainVp = ImGui::GetMainViewport();
  float windowX = mainVp ? mainVp->Pos.x : 0.0f;
  float windowY = mainVp ? mainVp->Pos.y : 0.0f;
  float windowWidth =
      mainVp ? mainVp->Size.x : (float)ImGui::GetIO().DisplaySize.x;
  float windowHeight =
      mainVp ? mainVp->Size.y : (float)ImGui::GetIO().DisplaySize.y;

  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  auto WorldToScreen = [&](const float *wp, ImVec2 &outSp) -> bool {
    float viewPos[4];
    for (int i = 0; i < 4; i++) {
      viewPos[i] = wp[0] * view[0 * 4 + i] + wp[1] * view[1 * 4 + i] +
                   wp[2] * view[2 * 4 + i] + 1.0f * view[3 * 4 + i];
    }
    float clipPos[4];
    for (int i = 0; i < 4; i++) {
      clipPos[i] = viewPos[0] * proj[0 * 4 + i] + viewPos[1] * proj[1 * 4 + i] +
                   viewPos[2] * proj[2 * 4 + i] + viewPos[3] * proj[3 * 4 + i];
    }
    if (clipPos[3] < 0.001f) // Behind camera
      return false;
    outSp.x = windowX + (clipPos[0] / clipPos[3] + 1.0f) * 0.5f * windowWidth;
    outSp.y = windowY + (1.0f - clipPos[1] / clipPos[3]) * 0.5f * windowHeight;
    return true;
  };

  // 1. Draw wireframes / icons for ALL lights to visualize their layout
  for (size_t i = 0; i < s_lights.size(); ++i) {
    Light &l = s_lights[i];
    bool isSelected = (s_selectedLightIdx == (int)i);
    ImU32 col =
        isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(200, 200, 200, 150);
    float thick = isSelected ? 3.0f : 1.5f;

    ImVec2 screenPos;
    if (!WorldToScreen(l.position, screenPos))
      continue;

    // Draw little center circle for the light
    drawList->AddCircleFilled(screenPos, 4.0f, col);

    if (l.type == (uint32_t)LightType::Omni) {
      drawList->AddCircle(screenPos, l.radius * 5.0f + 10.0f, col, 16, thick);
    } else {
      // spot, directional, area: draw an arrow representing 'direction'
      float fwd[3] = {l.direction[0], l.direction[1], l.direction[2]};
      float arrowEnd[3] = {l.position[0] + fwd[0] * 1.0f,
                           l.position[1] + fwd[1] * 1.0f,
                           l.position[2] + fwd[2] * 1.0f};

      ImVec2 sEnd;
      if (WorldToScreen(arrowEnd, sEnd)) {
        drawList->AddLine(screenPos, sEnd, col, thick);
      }

      if (l.type == (uint32_t)LightType::AreaRect ||
          l.type == (uint32_t)LightType::AreaDisk) {
        // Draw the wireframe of the area light surface
        float up_ref[3] = {0, 1, 0};
        if (fabsf(fwd[1]) > 0.99f) {
          up_ref[0] = 1;
          up_ref[1] = 0;
        }

        // right = up_ref x fwd
        float right[3] = {up_ref[1] * fwd[2] - up_ref[2] * fwd[1],
                          up_ref[2] * fwd[0] - up_ref[0] * fwd[2],
                          up_ref[0] * fwd[1] - up_ref[1] * fwd[0]};
        float rlen = sqrtf(right[0] * right[0] + right[1] * right[1] +
                           right[2] * right[2]);
        if (rlen > 0.001f) {
          right[0] /= rlen;
          right[1] /= rlen;
          right[2] /= rlen;
        }

        // up = fwd x right
        float up[3] = {fwd[1] * right[2] - fwd[2] * right[1],
                       fwd[2] * right[0] - fwd[0] * right[2],
                       fwd[0] * right[1] - fwd[1] * right[0]};

        float hw = l.areaExtents[0] * 0.5f;
        float hh = l.areaExtents[1] * 0.5f;

        // 4 corners of the quad
        float c[4][3];
        c[0][0] = l.position[0] + right[0] * hw + up[0] * hh;
        c[0][1] = l.position[1] + right[1] * hw + up[1] * hh;
        c[0][2] = l.position[2] + right[2] * hw + up[2] * hh;

        c[1][0] = l.position[0] - right[0] * hw + up[0] * hh;
        c[1][1] = l.position[1] - right[1] * hw + up[1] * hh;
        c[1][2] = l.position[2] - right[2] * hw + up[2] * hh;

        c[2][0] = l.position[0] - right[0] * hw - up[0] * hh;
        c[2][1] = l.position[1] - right[1] * hw - up[1] * hh;
        c[2][2] = l.position[2] - right[2] * hw - up[2] * hh;

        c[3][0] = l.position[0] + right[0] * hw - up[0] * hh;
        c[3][1] = l.position[1] + right[1] * hw - up[1] * hh;
        c[3][2] = l.position[2] + right[2] * hw - up[2] * hh;

        ImVec2 sc[4];
        bool ok = true;
        for (int k = 0; k < 4; k++) {
          if (!WorldToScreen(c[k], sc[k]))
            ok = false;
        }

        if (ok) {
          if (l.type == (uint32_t)LightType::AreaDisk) {
            // Draw a quick octagon for the disk
            drawList->AddLine(sc[0], sc[1], col, thick);
            drawList->AddLine(sc[1], sc[2], col, thick);
            drawList->AddLine(sc[2], sc[3], col, thick);
            drawList->AddLine(sc[3], sc[0], col, thick);
          } else {
            // Draw rect
            drawList->AddLine(sc[0], sc[1], col, thick);
            drawList->AddLine(sc[1], sc[2], col, thick);
            drawList->AddLine(sc[2], sc[3], col, thick);
            drawList->AddLine(sc[3], sc[0], col, thick);
          }
        }
      }
    }
  }

  // 2. Do ImGuizmo manipulate for the selected light
  if (s_selectedLightIdx < 0 || s_selectedLightIdx >= (int)s_lights.size())
    return;

  Light &l = s_lights[s_selectedLightIdx];

  ImGuizmo::AllowAxisFlip(false);
  ImGuizmo::GetStyle().TranslationLineThickness = 6.0f;

  // Build a matrix containing both position and direction
  float up_ref[3] = {0, 1, 0};
  float fwd[3] = {l.direction[0], l.direction[1], l.direction[2]};
  if (fabsf(fwd[1]) > 0.99f) {
    up_ref[0] = 1;
    up_ref[1] = 0;
  }
  float right[3] = {up_ref[1] * fwd[2] - up_ref[2] * fwd[1],
                    up_ref[2] * fwd[0] - up_ref[0] * fwd[2],
                    up_ref[0] * fwd[1] - up_ref[1] * fwd[0]};
  float rlen =
      sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
  if (rlen > 0.001f) {
    right[0] /= rlen;
    right[1] /= rlen;
    right[2] /= rlen;
  } else {
    right[0] = 1;
    right[1] = 0;
    right[2] = 0;
  }
  float up[3] = {fwd[1] * right[2] - fwd[2] * right[1],
                 fwd[2] * right[0] - fwd[0] * right[2],
                 fwd[0] * right[1] - fwd[1] * right[0]};

  float matrix[16] = {right[0],      right[1],      right[2],      0,
                      up[0],         up[1],         up[2],         0,
                      fwd[0],        fwd[1],        fwd[2],        0,
                      l.position[0], l.position[1], l.position[2], 1};

  // Keyboard toggles for Translate / Rotate / Scale
  if (ImGui::IsKeyPressed(ImGuiKey_G))
    g_currentGizmoOp = ImGuizmo::TRANSLATE;
  if (ImGui::IsKeyPressed(ImGuiKey_R))
    g_currentGizmoOp = ImGuizmo::ROTATE;
  if (ImGui::IsKeyPressed(ImGuiKey_T))
    g_currentGizmoOp = ImGuizmo::SCALE;

  // Only allow uniform scale mode for lights if T is pressed
  ImGuizmo::OPERATION op = (g_currentGizmoOp == ImGuizmo::SCALE)
                               ? ImGuizmo::SCALE
                               : GetActiveGizmoOperation();

  ImGuizmo::SetID(10000 + s_selectedLightIdx);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(drawList);
  ImGuizmo::SetRect(windowX, windowY, windowWidth, windowHeight);

  if (ImGuizmo::Manipulate(view, proj, op, ImGuizmo::WORLD, matrix)) {
    // Read back position
    l.position[0] = matrix[12];
    l.position[1] = matrix[13];
    l.position[2] = matrix[14];

    // Read back direction (Z axis of matrix)
    float nfwd[3] = {matrix[8], matrix[9], matrix[10]};
    float nlen =
        sqrtf(nfwd[0] * nfwd[0] + nfwd[1] * nfwd[1] + nfwd[2] * nfwd[2]);
    if (nlen > 0.001f) {
      l.direction[0] = nfwd[0] / nlen;
      l.direction[1] = nfwd[1] / nlen;
      l.direction[2] = nfwd[2] / nlen;
    }

    // Read back scale (if SCALE Op)
    if (op == ImGuizmo::SCALE) {
      // Approximate scale change (just based on X axis magnitude of matrix)
      float currentScaleX =
          sqrtf(matrix[0] * matrix[0] + matrix[1] * matrix[1] +
                matrix[2] * matrix[2]);
      if (currentScaleX > 0.001f) {
        l.areaExtents[0] *= currentScaleX;
        l.areaExtents[1] *= currentScaleX;
        l.radius *= currentScaleX;
      }
    }

    UpdateLights();
  }
}

void MatMul(const float *a, const float *b, float *out) {
  float tmp[16];
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        sum += a[k * 4 + row] * b[col * 4 + k];
      }
      tmp[col * 4 + row] = sum;
    }
  }
  memcpy(out, tmp, 16 * sizeof(float));
}

void DrawGizmo() {
  size_t selectedIdx = (size_t)-1;
  for (size_t i = 0; i < s_nodes.size(); ++i) {
    if (s_nodes[i].selected) {
      selectedIdx = i;
      break;
    }
  }

  // ImGuizmo::BeginFrame() called in main.cpp
  if (selectedIdx == (size_t)-1)
    return;
  auto &node = s_nodes[selectedIdx];

  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);

  if (ImGui::IsKeyPressed(ImGuiKey_G))
    g_currentGizmoOp = ImGuizmo::TRANSLATE;
  if (ImGui::IsKeyPressed(ImGuiKey_R))
    g_currentGizmoOp = ImGuizmo::ROTATE;
  if (ImGui::IsKeyPressed(ImGuiKey_T))
    g_currentGizmoOp = ImGuizmo::SCALE;
  if (ImGui::IsKeyPressed(ImGuiKey_L)) {
    g_currentGizmoMode = (g_currentGizmoMode == ImGuizmo::WORLD)
                             ? ImGuizmo::LOCAL
                             : ImGuizmo::WORLD;
  }

  // Scaling is almost always performed in Local space
  ImGuizmo::MODE actualMode = (g_currentGizmoOp == ImGuizmo::SCALE)
                                  ? ImGuizmo::LOCAL
                                  : g_currentGizmoMode;
  // Default to WORLD for Rotation if implied by user request, but respect the
  // toggle If the user feels it "looks weird", ensuring AxisFlip is off can
  // help stability
  ImGuizmo::AllowAxisFlip(false);

  // Make gizmo lines thicker for easier clicking
  ImGuizmo::GetStyle().TranslationLineThickness = 6.0f;
  ImGuizmo::GetStyle().RotationLineThickness = 6.0f;

  ImGuizmo::SetID((int)selectedIdx);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());

  ImGuiViewport *mainVp = ImGui::GetMainViewport();
  float windowX = mainVp ? mainVp->Pos.x : 0.0f;
  float windowY = mainVp ? mainVp->Pos.y : 0.0f;
  float windowWidth =
      mainVp ? mainVp->Size.x : (float)ImGui::GetIO().DisplaySize.x;
  float windowHeight =
      mainVp ? mainVp->Size.y : (float)ImGui::GetIO().DisplaySize.y;
  ImGuizmo::SetRect(windowX, windowY, windowWidth, windowHeight);

  // Compute mesh local center to position gizmo at center of the object
  float localCenter[3] = {0, 0, 0};
  int count = 0;
  for (size_t mi : node.meshIndices) {
    if (mi < g_loadedMeshes.size()) {
      const auto &m = g_loadedMeshes[mi];
      for (int a = 0; a < 3; ++a)
        localCenter[a] += (m.minBound[a] + m.maxBound[a]) * 0.5f;
      count++;
    }
  }
  if (count > 0) {
    localCenter[0] /= count;
    localCenter[1] /= count;
    localCenter[2] /= count;
  }

  float pivotMatrix[16];
  memcpy(pivotMatrix, node.transform, 16 * sizeof(float));
  float translationMat[16] = {1,
                              0,
                              0,
                              0,
                              0,
                              1,
                              0,
                              0,
                              0,
                              0,
                              1,
                              0,
                              localCenter[0],
                              localCenter[1],
                              localCenter[2],
                              1};
  MatMul(pivotMatrix, translationMat, pivotMatrix);

  ImGuizmo::OPERATION op = GetActiveGizmoOperation();

  if (ImGuizmo::Manipulate(view, proj, op, actualMode,
                           pivotMatrix)) {
    // NodeTransform = pivotMatrix * Translation(-localCenter)
    float invTranslationMat[16] = {1,
                                   0,
                                   0,
                                   0,
                                   0,
                                   1,
                                   0,
                                   0,
                                   0,
                                   0,
                                   1,
                                   0,
                                   -localCenter[0],
                                   -localCenter[1],
                                   -localCenter[2],
                                   1};
    MatMul(pivotMatrix, invTranslationMat, node.transform);

    // Ensure raytracing acceleration structures are updated to reflect
    // transform changes
    RebuildAccelerationStructures();
    DxrRenderer::ResetAccumulation();
  }
}

// Simple Ray-AABB intersection for node selection
static bool RayAABBIntersection(const float *rayOrigin, const float *rayDir,
                                const float *minP, const float *maxP,
                                float &t) {
  float tmin = -FLT_MAX, tmax = FLT_MAX;
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(rayDir[i]) < 1e-6f) {
      if (rayOrigin[i] < minP[i] || rayOrigin[i] > maxP[i])
        return false;
    } else {
      float invD = 1.0f / rayDir[i];
      float t1 = (minP[i] - rayOrigin[i]) * invD;
      float t2 = (maxP[i] - rayOrigin[i]) * invD;
      if (t1 > t2)
        std::swap(t1, t2);
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
    }
  }
  t = tmin;
  return tmax >= std::max(0.0f, tmin);
}

// Moeller-Trumbore Ray-Triangle Intersection
static bool RayTriangleIntersection(const float *orig, const float *dir,
                                    const float *v0, const float *v1,
                                    const float *v2, float &t) {
  const float EPSILON = 1e-7f;
  float edge1[3], edge2[3], h[3], s[3], q[3];
  float a, f, u, v;

  for (int i = 0; i < 3; i++) {
    edge1[i] = v1[i] - v0[i];
    edge2[i] = v2[i] - v0[i];
  }

  // h = dir x edge2
  h[0] = dir[1] * edge2[2] - dir[2] * edge2[1];
  h[1] = dir[2] * edge2[0] - dir[0] * edge2[2];
  h[2] = dir[0] * edge2[1] - dir[1] * edge2[0];

  a = edge1[0] * h[0] + edge1[1] * h[1] + edge1[2] * h[2];
  if (a > -EPSILON && a < EPSILON)
    return false;

  f = 1.0f / a;
  for (int i = 0; i < 3; i++)
    s[i] = orig[i] - v0[i];

  u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
  if (u < 0.0f || u > 1.0f)
    return false;

  // q = s x edge1
  q[0] = s[1] * edge1[2] - s[2] * edge1[1];
  q[1] = s[2] * edge1[0] - s[0] * edge1[2];
  q[2] = s[0] * edge1[1] - s[1] * edge1[0];

  v = f * (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]);
  if (v < 0.0f || u + v > 1.0f)
    return false;

  t = f * (edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2]);
  if (t > EPSILON)
    return true;

  return false;
}

void MatMul(const float *a, const float *b, float *out);

// Helper: Inverse 4x4 matrix (simplified, assumes affine/orthonormal part)
bool Inverse4x4(const float *m, float *out) {
  float inv[16];
  float det;
  int i;

  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (det == 0)
    return false;

  det = 1.0f / det;
  for (i = 0; i < 16; i++)
    out[i] = inv[i] * det;
  return true;
}

int UpdateSelection(float screenWidth, float screenHeight) {
  if (ImGuizmo::IsOver() || ImGuizmo::IsUsing() || ImGui::IsAnyItemHovered())
    return -1;

  if (screenWidth <= 1.0f || screenHeight <= 1.0f)
    return -1;

  // Use viewport-relative mouse coordinates.
  ImVec2 mposAbs = ImGui::GetIO().MousePos;
  ImGuiViewport *vp = ImGui::GetMainViewport();
  float vpX = vp ? vp->Pos.x : 0.0f;
  float vpY = vp ? vp->Pos.y : 0.0f;
  float mx = mposAbs.x - vpX;
  float my = mposAbs.y - vpY;
  if (mx < 0.0f || my < 0.0f || mx > screenWidth || my > screenHeight)
    return -1;

  // NDC [-1, 1]
  float ndcX = (mx / screenWidth) * 2.0f - 1.0f;
  float ndcY = 1.0f - (my / screenHeight) * 2.0f;

  // Build ray from the same camera basis used by DXR raygen.
  const float kPi = 3.14159265359f;
  float fovRad = g_cameraData.fov * (kPi / 180.0f);
  float tanHalfFov = tanf(fovRad * 0.5f);
  float aspect = (screenHeight > 0.0f) ? (screenWidth / screenHeight)
                                       : g_cameraData.aspect;

  float forward[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                      g_cameraData.forward[2]};
  float upHint[3] = {g_cameraData.up[0], g_cameraData.up[1], g_cameraData.up[2]};

  auto Normalize3 = [](float v[3]) -> bool {
    float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1e-12f)
      return false;
    float invLen = 1.0f / sqrtf(len2);
    v[0] *= invLen;
    v[1] *= invLen;
    v[2] *= invLen;
    return true;
  };

  auto Cross3 = [](const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
  };

  if (!Normalize3(forward))
    return -1;
  float right[3];
  Cross3(forward, upHint, right);
  if (!Normalize3(right))
    return -1;
  float up[3];
  Cross3(right, forward, up);
  if (!Normalize3(up))
    return -1;

  float xView = ndcX * aspect * tanHalfFov;
  float yView = ndcY * tanHalfFov;
  float dir[3] = {xView * right[0] + yView * up[0] + forward[0],
                  xView * right[1] + yView * up[1] + forward[1],
                  xView * right[2] + yView * up[2] + forward[2]};
  if (!Normalize3(dir))
    return -1;

  float orig[3] = {g_cameraData.pos[0], g_cameraData.pos[1], g_cameraData.pos[2]};

  auto TransformPoint = [](const float m[16], const float p[3], float out[3]) {
    out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
  };
  auto TransformVector = [](const float m[16], const float v[3], float out[3]) {
    out[0] = v[0] * m[0] + v[1] * m[4] + v[2] * m[8];
    out[1] = v[0] * m[1] + v[1] * m[5] + v[2] * m[9];
    out[2] = v[0] * m[2] + v[1] * m[6] + v[2] * m[10];
  };

  float minWorldDist2 = FLT_MAX;
  int hitNode = -1;
  int hitMaterial = -1;

  for (size_t i = 0; i < s_nodes.size(); ++i) {
    auto &node = s_nodes[i];
    if (!node.visible)
      continue;

    float invNode[16];
    if (!Inverse4x4(node.transform, invNode))
      continue;

    // Transform ray to local space
    float localOrig[3], localDir[3];
    TransformPoint(invNode, orig, localOrig);
    TransformVector(invNode, dir, localDir);
    float localDirLen2 = localDir[0] * localDir[0] + localDir[1] * localDir[1] +
                         localDir[2] * localDir[2];
    if (localDirLen2 <= 1e-12f)
      continue;

    for (size_t mIdx : node.meshIndices) {
      if (mIdx >= g_loadedMeshes.size())
        continue;
      const auto &mesh = g_loadedMeshes[mIdx];

      float boxT = 1e30f;
      if (!RayAABBIntersection(localOrig, localDir, mesh.minBound, mesh.maxBound,
                               boxT)) {
        continue;
      }

      // Broad phase hit: refine with triangle tests when CPU geometry exists.
      if (!mesh.cpuVertices.empty() && !mesh.cpuIndices.empty()) {
        bool triHit = false;
        float bestMeshDist2 = FLT_MAX;

        for (size_t k = 0; k + 2 < mesh.cpuIndices.size(); k += 3) {
          uint32_t i0 = mesh.cpuIndices[k];
          uint32_t i1 = mesh.cpuIndices[k + 1];
          uint32_t i2 = mesh.cpuIndices[k + 2];
          if (i0 >= mesh.cpuVertices.size() || i1 >= mesh.cpuVertices.size() ||
              i2 >= mesh.cpuVertices.size()) {
            continue;
          }

          float tVal = 0.0f;
          if (!RayTriangleIntersection(localOrig, localDir, mesh.cpuVertices[i0].pos,
                                       mesh.cpuVertices[i1].pos,
                                       mesh.cpuVertices[i2].pos, tVal)) {
            continue;
          }

          float localHit[3] = {localOrig[0] + localDir[0] * tVal,
                               localOrig[1] + localDir[1] * tVal,
                               localOrig[2] + localDir[2] * tVal};
          float worldHit[3];
          TransformPoint(node.transform, localHit, worldHit);
          float dx = worldHit[0] - orig[0];
          float dy = worldHit[1] - orig[1];
          float dz = worldHit[2] - orig[2];
          float worldDist2 = dx * dx + dy * dy + dz * dz;

          if (worldDist2 < bestMeshDist2) {
            bestMeshDist2 = worldDist2;
            triHit = true;
          }
        }

        if (triHit && bestMeshDist2 < minWorldDist2) {
          minWorldDist2 = bestMeshDist2;
          hitNode = (int)i;
          hitMaterial = mesh.materialIndex;
        }
      } else {
        // Fallback to AABB hit point in world-space distance.
        float localHit[3] = {localOrig[0] + localDir[0] * boxT,
                             localOrig[1] + localDir[1] * boxT,
                             localOrig[2] + localDir[2] * boxT};
        float worldHit[3];
        TransformPoint(node.transform, localHit, worldHit);
        float dx = worldHit[0] - orig[0];
        float dy = worldHit[1] - orig[1];
        float dz = worldHit[2] - orig[2];
        float worldDist2 = dx * dx + dy * dy + dz * dz;

        if (worldDist2 < minWorldDist2) {
          minWorldDist2 = worldDist2;
          hitNode = (int)i;
          hitMaterial = mesh.materialIndex;
        }
      }
    }
  }

  if (hitNode != -1) {
    SelectNode((size_t)hitNode);
    fprintf(stderr, "Scene: Picked Node '%s' (ID %d), Material ID %d\n",
            s_nodes[hitNode].name.c_str(), hitNode, hitMaterial);
  }

  return hitMaterial;
}

void DrawScenePanel(HWND hwnd, bool &visible) {
  if (!visible)
    return;
  if (ImGui::Begin("Scene", &visible)) {
    bool uiChanged = false;
    // If background import finished CPU-side, merge results on main thread (GPU
    // uploads, descriptors, AS rebuild)
    if (s_pendingReady.load()) {
      std::vector<Asset::GpuMesh> meshes;
      std::vector<Asset::Material> materials;
      std::vector<Asset::Texture> textures;
      std::string srcPath;
      {
        std::lock_guard<std::mutex> lg(s_pendingMutex);
        meshes = std::move(s_pendingMeshes);
        materials = std::move(s_pendingMaterials);
        textures = std::move(s_pendingTextures);
        srcPath = std::move(s_pendingPath);
        s_pendingMeshes.clear();
        s_pendingMaterials.clear();
        s_pendingTextures.clear();
        s_pendingPath.clear();
      }
      s_pendingReady = false;
      EnsureGpuBuffersForMeshes(meshes);

      // Merge into global lists (same logic as ImportModel)
      size_t meshBase = g_loadedMeshes.size();
      size_t materialBase = g_loadedMaterials.size();
      size_t textureBase = g_loadedTextures.size();

      g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(), meshes.end());
      g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(),
                               materials.end());
      g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(),
                              textures.end());

      for (size_t i = 0; i < meshes.size(); ++i) {
        int &mi = g_loadedMeshes[meshBase + i].materialIndex;
        if (mi >= 0)
          mi = mi + (int)materialBase;
      }

      for (size_t i = 0; i < materials.size(); ++i) {
        Asset::Material &m = g_loadedMaterials[materialBase + i];
        if (m.diffuseTexture >= 0)
          m.diffuseTexture += (int)textureBase;
        if (m.normalTexture >= 0)
          m.normalTexture += (int)textureBase;
        if (m.occlusionTexture >= 0)
          m.occlusionTexture += (int)textureBase;
        if (m.emissiveTexture >= 0)
          m.emissiveTexture += (int)textureBase;
        if (m.metalRoughTexture >= 0)
          m.metalRoughTexture += (int)textureBase;
      }

      RegisterTextures(textures);

      // Create a scene node for this import
      Node node;
      node.name = fs::path(srcPath).filename().string();
      node.sourcePath = srcPath;
      for (size_t i = 0; i < meshes.size(); ++i)
        node.meshIndices.push_back(meshBase + i);
      s_nodes.push_back(node);

      s_lastStatus = std::string("Loaded: ") + srcPath;
      fprintf(stderr, "%s\n", s_lastStatus.c_str());

      // Rebuild AS and pipeline on main thread
      fprintf(stderr, "RebuildAccelerationStructures: start\n");
      fflush(stderr);
      RebuildAccelerationStructures();
      fprintf(stderr, "RebuildAccelerationStructures: done\n");
      fflush(stderr);
      fprintf(stderr, "CreateRayTracingPipeline: start\n");
      fflush(stderr);
      DxrRenderer::CreateRayTracingPipeline(0, 0);
      fprintf(stderr, "CreateRayTracingPipeline: done\n");
      fflush(stderr);

      // Clear import-in-progress flag
      s_importInProgress = false;
      s_importProgress = 1.0f;
      {
        std::lock_guard<std::mutex> lg(s_importStatusMutex);
        s_importStatus = "Import finished";
      }
    }

    // Show progress bar if an import is in progress
    if (s_importInProgress.load()) {
      float p = s_importProgress.load();
      std::string status;
      {
        std::lock_guard<std::mutex> lg(s_importStatusMutex);
        status = s_importStatus;
      }
      ImGui::ProgressBar(p, ImVec2(-FLT_MIN, 0));
      ImGui::Text("%s", status.c_str());
    }
    // Action area
    float btnWidth = ImGui::GetContentRegionAvail().x * 0.33f;
    if (ImGui::Button("Import Model...", ImVec2(btnWidth, 0))) {
      ImportModelWithDialog(hwnd);
      uiChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Import HDR...", ImVec2(btnWidth, 0))) {
      ImportHDRWithDialog(hwnd);
      uiChanged = true;
    }
    ImGui::SameLine();
    const char *spaceNames[] = {"Local", "World"};
    int currentSpace = (g_currentGizmoMode == ImGuizmo::WORLD) ? 1 : 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Space", &currentSpace, spaceNames, 2)) {
      g_currentGizmoMode =
          (currentSpace == 1) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
      uiChanged = true;
    }

    ImGui::Separator();
    ImGui::Text("Hierarchy");

    // Use a child for the list area to allow scrolling independently of the
    // header/footer
    if (ImGui::BeginChild("HierarchyRegion",
                          ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2),
                          true)) {
      if (ImGui::BeginTable("HierarchyTable", 2,
                            ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                65.0f);
        // ImGui::TableHeadersRow();

        for (size_t i = 0; i < s_nodes.size(); ++i) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);

          ImGui::PushID((int)i);
          bool selected = s_nodes[i].selected;
          if (ImGui::Selectable(s_nodes[i].name.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
            SelectNode(i);
          }

          ImGui::TableSetColumnIndex(1);
          // Minimalist red button for deletion
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
          if (ImGui::Button("Delete", ImVec2(-FLT_MIN, 0))) {
            DeleteNode(i);
            // Ensure accumulation is reset immediately for delete operations
            DxrRenderer::ResetAccumulation();
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            ImGui::EndTable();
            ImGui::EndChild();
            ImGui::End();
            return; // Refresh state next frame
          }
          ImGui::PopStyleColor(2);
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }
    ImGui::EndChild();

    // Reset accumulation once per Scene window when any UI widget changed
    if (uiChanged) {
      DxrRenderer::ResetAccumulation();
    }

    if (!s_lastStatus.empty()) {
      ImGui::Separator();
      ImGui::TextDisabled("Status:");
      ImGui::TextWrapped("%s", s_lastStatus.c_str());
    }
  }
  ImGui::End();
}

void ResetScene() {
  WaitGPUIdle();
  s_nodes.clear();
  g_loadedMeshes.clear();
  g_loadedMaterials.clear();
  g_loadedTextures.clear();
  g_textureDescriptorCount = 0;
  // Note: In a full implementation, we should also release GPU
  // resources/descriptors and reset the IBL manager, but for now this clears
  // the CPU state which is then rebuilt by LoadScene.

  // Reset DXR state if it's active
  DxrRenderer::ResetAccumulation();
  // Ensure camera/exposure defaults are restored when starting a fresh scene
  DxrRenderer::SetAutoExposure(false);
  DxrRenderer::SetPhysicalCameraExposure(true);
  DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 30.0f, 2.8f);
}

} // namespace Scene
