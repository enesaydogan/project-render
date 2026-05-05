#define NOMINMAX
#include "scene.h"
#include "animation_sequence.h"
#include "saved_views.h"
#include "ImGuizmo.h"
#include "assets/asset_loader.h"
#include "camera.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "livelink/livelink_scene_sync.h"
#include "file_import.h"
#include "grass_manager.h"
#include "ibl_manager.h"
#include "imgui.h"
#include "material/material_system.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// Externals from main.cpp (global symbols)
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;
extern UINT g_textureDescriptorCount;
extern HWND g_hwnd;
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

enum class PendingImportAction {
  Import,
  Reimport,
};

static std::vector<Asset::GpuMesh> s_pendingMeshes;
static std::vector<Asset::Material> s_pendingMaterials;
static std::vector<Asset::Texture> s_pendingTextures;
static std::vector<Asset::ImportedSceneNode> s_pendingSceneNodes;
static std::unordered_map<std::string, int> s_textureIndicesBySourceUri;
static std::vector<std::string> s_materialStableIds;
static std::unordered_map<std::string, int> s_materialIndicesByStableId;
static std::unordered_map<std::string, int> s_materialIndicesByName;
static bool s_materialMetadataDirty = true;
struct SharedImportedMeshEntry {
  std::vector<size_t> meshIndices;
  std::vector<int> linkedMaterialIndices;
  std::vector<std::string> linkedMaterialSourceNames;
  size_t refCount = 0;
};
static std::unordered_map<std::string, SharedImportedMeshEntry>
    s_sharedImportedMeshesBySourcePath;
static size_t s_nextImportGroupId = 1;
static size_t s_nextChangeListenerId = 1;
static std::unordered_map<size_t, std::function<void()>> s_changeListeners;
static std::string s_pendingPath;
static PendingImportAction s_pendingAction = PendingImportAction::Import;
static size_t s_pendingTargetNodeIndex = static_cast<size_t>(-1);
static std::string s_pendingTargetImportGroupKey;
static std::atomic<bool> s_pendingReady(false);
static std::mutex s_pendingMutex;
static ImGuizmo::OPERATION g_currentGizmoOp = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE g_currentGizmoMode = ImGuizmo::WORLD;
static int s_selectedLightIdx = -1;

struct ShiftCloneDragState {
  bool active = false;
  bool optionsPending = false;
  bool sawLeftMouseDown = false;
  size_t gizmoId = static_cast<size_t>(-1);
  std::vector<size_t> cloneRootIndices;
  std::vector<size_t> cloneNodeIndices;
};

static ShiftCloneDragState s_shiftCloneDrag;

static void EnsureGpuBuffersForMeshes(std::vector<Asset::GpuMesh> &meshes);

static void ResetGpuMeshEntry(Asset::GpuMesh &mesh) {
  mesh.vertexBuffer.Reset();
  mesh.indexBuffer.Reset();
  mesh.vertexCount = 0;
  mesh.indexCount = 0;
}

static std::vector<size_t> ReplaceOrAppendMeshes(
    const std::vector<size_t> &existingIndices,
    std::vector<Asset::GpuMesh> &incomingMeshes) {
  std::vector<size_t> result;
  result.reserve(incomingMeshes.size());

  size_t oldCount = existingIndices.size();
  size_t newCount = incomingMeshes.size();
  size_t reuseCount = std::min(oldCount, newCount);

  for (size_t i = 0; i < reuseCount; ++i) {
    size_t idx = existingIndices[i];
    if (idx < g_loadedMeshes.size()) {
      g_loadedMeshes[idx] = std::move(incomingMeshes[i]);
      result.push_back(idx);
    } else {
      g_loadedMeshes.push_back(std::move(incomingMeshes[i]));
      result.push_back(g_loadedMeshes.size() - 1);
    }
  }

  for (size_t i = reuseCount; i < oldCount; ++i) {
    size_t idx = existingIndices[i];
    if (idx < g_loadedMeshes.size()) {
      ResetGpuMeshEntry(g_loadedMeshes[idx]);
    }
  }

  for (size_t i = reuseCount; i < newCount; ++i) {
    g_loadedMeshes.push_back(std::move(incomingMeshes[i]));
    result.push_back(g_loadedMeshes.size() - 1);
  }

  return result;
}

enum class RendererInvalidationPlan {
  None,
  AccumulationOnly,
  TlasRefresh,
  FullAccelerationStructureRebuild,
};

static int s_batchedUpdateDepth = 0;
static RendererInvalidationPlan s_batchedInvalidationPlan =
    RendererInvalidationPlan::None;
static bool s_batchedLightsDirty = false;
static bool s_batchedSceneChanged = false;
static bool s_batchedGpuIdleSatisfied = false;
static bool s_batchedMeshUploadDirty = false;
static GpuUploadStats s_gpuUploadStats;

static void DispatchSceneChanged() {
  if (s_changeListeners.empty()) {
    return;
  }

  std::vector<std::function<void()>> callbacks;
  callbacks.reserve(s_changeListeners.size());
  for (const auto &[_, callback] : s_changeListeners) {
    if (callback) {
      callbacks.push_back(callback);
    }
  }

  for (const auto &callback : callbacks) {
    callback();
  }
}

static void NotifySceneChanged() {
  if (s_batchedUpdateDepth > 0) {
    s_batchedSceneChanged = true;
    return;
  }
  DispatchSceneChanged();
}

static void PrepareForDestructiveMeshMutation() {
  if (s_batchedUpdateDepth > 0) {
    if (!s_batchedGpuIdleSatisfied) {
      WaitGPUIdle();
      s_batchedGpuIdleSatisfied = true;
    }
    return;
  }

  WaitGPUIdle();
}

static size_t CountMeshesNeedingGpuUpload(const std::vector<Asset::GpuMesh> &meshes) {
  size_t count = 0;
  for (const Asset::GpuMesh &mesh : meshes) {
    if ((!mesh.vertexBuffer || !mesh.indexBuffer) &&
        !mesh.cpuVertices.empty() && !mesh.cpuIndices.empty()) {
      ++count;
    }
  }
  return count;
}

static void UploadGpuBuffersForMeshes(std::vector<Asset::GpuMesh> &meshes) {
  const size_t pendingMeshCount = CountMeshesNeedingGpuUpload(meshes);
  if (pendingMeshCount == 0) {
    return;
  }

  const auto uploadStart = std::chrono::steady_clock::now();
  Asset::UploadMeshes(meshes);
  const uint64_t uploadMs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - uploadStart)
          .count());
  ++s_gpuUploadStats.batchCount;
  s_gpuUploadStats.lastMeshCount = pendingMeshCount;
  s_gpuUploadStats.lastUploadMs = uploadMs;
  s_gpuUploadStats.totalUploadMs += uploadMs;
}

static void EnsureMaterialMetadataStorage() {
  if (!s_materialMetadataDirty &&
      s_materialStableIds.size() == g_loadedMaterials.size()) {
    return;
  }

  s_materialMetadataDirty = false;
  s_materialStableIds.resize(g_loadedMaterials.size());
  s_materialIndicesByStableId.clear();
  s_materialIndicesByName.clear();
  for (size_t materialIndex = 0; materialIndex < s_materialStableIds.size();
       ++materialIndex) {
    const std::string name = g_loadedMaterials[materialIndex].name;
    if (!name.empty()) {
      s_materialIndicesByName[name] = static_cast<int>(materialIndex);
    }
    const std::string &stableId = s_materialStableIds[materialIndex];
    if (stableId.empty()) {
      continue;
    }
    s_materialIndicesByStableId[stableId] = static_cast<int>(materialIndex);
  }
}

static std::string NormalizeMaterialStableId(const std::string &stableId) {
  if (stableId.empty()) {
    return {};
  }
  if (stableId.rfind("material:id:", 0) == 0) {
    return stableId.substr(strlen("material:id:"));
  }
  return stableId;
}

static std::string NormalizeMaterialSourceNameForMatch(
    const std::string &name) {
  auto begin = name.begin();
  while (begin != name.end() &&
         std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }

  auto end = name.end();
  while (end != begin &&
         std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  std::string normalized(begin, end);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return normalized;
}

static bool MaterialSourceNamesMatch(const std::string &a,
                                     const std::string &b) {
  return NormalizeMaterialSourceNameForMatch(a) ==
         NormalizeMaterialSourceNameForMatch(b);
}

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

static void RefreshMaterialRuntimeTexture(Asset::Material &material) {
  if (!MaterialSystem::NeedsDerivedPackedSurfaceTexture(material)) {
    material.runtimeMetalRoughTexture = -1;
    return;
  }

  Asset::Texture derived;
  const Asset::Texture *metalnessTexture =
      material.metalnessTexture >= 0 &&
              material.metalnessTexture < static_cast<int>(g_loadedTextures.size())
          ? &g_loadedTextures[static_cast<size_t>(material.metalnessTexture)]
          : nullptr;
  const Asset::Texture *roughnessOrGlossinessTexture =
      material.roughnessGlossTexture >= 0 &&
              material.roughnessGlossTexture < static_cast<int>(g_loadedTextures.size())
          ? &g_loadedTextures[static_cast<size_t>(material.roughnessGlossTexture)]
          : nullptr;
  if (!MaterialSystem::BuildDerivedPackedSurfaceTexture(
          material, metalnessTexture, roughnessOrGlossinessTexture,
          &derived)) {
    material.runtimeMetalRoughTexture = -1;
    return;
  }

  if (material.runtimeMetalRoughTexture >= 0 &&
      material.runtimeMetalRoughTexture <
          static_cast<int>(g_loadedTextures.size()) &&
      g_loadedTextures[static_cast<size_t>(material.runtimeMetalRoughTexture)]
          .hiddenInEditor) {
    WaitGPUIdle();
    g_loadedTextures[static_cast<size_t>(material.runtimeMetalRoughTexture)] =
        std::move(derived);
    g_loadedTextures[static_cast<size_t>(material.runtimeMetalRoughTexture)]
        .hiddenInEditor = true;
    WriteTextureSrv(static_cast<UINT>(material.runtimeMetalRoughTexture),
                    g_loadedTextures[static_cast<size_t>(
                        material.runtimeMetalRoughTexture)]);
    return;
  }

  const int newTextureIndex = AddTexture(std::move(derived));
  material.runtimeMetalRoughTexture = newTextureIndex;
  if (newTextureIndex >= 0 &&
      newTextureIndex < static_cast<int>(g_loadedTextures.size())) {
    g_loadedTextures[static_cast<size_t>(newTextureIndex)].hiddenInEditor =
        true;
  }
}

int AddTexture(Asset::Texture texture) {
  if (!g_device) {
    fprintf(stderr, "AddTexture: no device\n");
    return -1;
  }
  if (!texture.resource) {
    fprintf(stderr, "AddTexture: invalid texture resource\n");
    return -1;
  }

  WaitGPUIdle();

  const int newIndex = static_cast<int>(g_loadedTextures.size());
  if (static_cast<UINT>(newIndex) >= g_textureDescriptorCapacity) {
    fprintf(stderr, "AddTexture: descriptor capacity exceeded (%u)\n",
            g_textureDescriptorCapacity);
    return -1;
  }

  g_loadedTextures.push_back(std::move(texture));
  const Asset::Texture &registeredTexture = g_loadedTextures.back();
  if (!WriteTextureSrv(static_cast<UINT>(newIndex), registeredTexture)) {
    g_loadedTextures.pop_back();
    fprintf(stderr, "AddTexture: failed to create SRV for texture #%d\n",
            newIndex);
    return -1;
  }

  g_textureDescriptorCount = static_cast<UINT>(g_loadedTextures.size());
  fprintf(stderr, "AddTexture: added texture #%d (w=%u h=%u mips=%u)\n",
          newIndex, registeredTexture.width, registeredTexture.height,
          registeredTexture.mipLevels);
  return newIndex;
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
  const int newIndex = AddTexture(std::move(tex));
  if (newIndex < 0) {
    fprintf(stderr, "AddTextureFromFile: failed to register '%s'\n",
            utf8path.c_str());
    return -1;
  }
  const Asset::Texture &t = g_loadedTextures[static_cast<size_t>(newIndex)];
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

static void GetRenderViewportRect(float *outX, float *outY, float *outWidth,
                                  float *outHeight) {
  float x = 0.0f;
  float y = 0.0f;
  float width = (float)ImGui::GetIO().DisplaySize.x;
  float height = (float)ImGui::GetIO().DisplaySize.y;

  if (g_hwnd) {
    RECT clientRect = {};
    if (GetClientRect(g_hwnd, &clientRect)) {
      width = (float)(clientRect.right - clientRect.left);
      height = (float)(clientRect.bottom - clientRect.top);

      POINT origin = {clientRect.left, clientRect.top};
      if (ClientToScreen(g_hwnd, &origin)) {
        x = (float)origin.x;
        y = (float)origin.y;
      }
    }
  }

  if (width <= 1.0f || height <= 1.0f) {
    ImGuiViewport *mainVp = ImGui::GetMainViewport();
    x = mainVp ? mainVp->Pos.x : 0.0f;
    y = mainVp ? mainVp->Pos.y : 0.0f;
    width = mainVp ? mainVp->Size.x : (float)ImGui::GetIO().DisplaySize.x;
    height = mainVp ? mainVp->Size.y : (float)ImGui::GetIO().DisplaySize.y;
  }

  if (outX)
    *outX = x;
  if (outY)
    *outY = y;
  if (outWidth)
    *outWidth = width;
  if (outHeight)
    *outHeight = height;
}

static ImDrawList *BeginRenderOverlayWindow(const char *name, float x, float y,
                                            float width, float height) {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_NoInputs |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoNav |
                           ImGuiWindowFlags_NoDocking;
  if (ImGuiViewport *mainViewport = ImGui::GetMainViewport()) {
    ImGui::SetNextWindowViewport(mainViewport->ID);
  }
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin(name, nullptr, flags);
  ImGui::PopStyleVar(2);
  return ImGui::GetWindowDrawList();
}

static std::string s_lastStatus;

static void AdjustMaterialTextureIndices(std::vector<Asset::Material> &materials,
                                         size_t textureBase) {
  for (size_t i = 0; i < materials.size(); ++i) {
    Asset::Material &m = materials[i];
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
    if (m.runtimeMetalRoughTexture >= 0)
      m.runtimeMetalRoughTexture += (int)textureBase;
    if (m.metalnessTexture >= 0)
      m.metalnessTexture += (int)textureBase;
    if (m.roughnessGlossTexture >= 0)
      m.roughnessGlossTexture += (int)textureBase;
  }
}

static std::string NormalizeTextureSourceUriKey(const std::string &uri) {
  std::string key = uri;
  std::replace(key.begin(), key.end(), '/', '\\');
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return key;
}

static bool IsHdrTextureSourceUri(const std::string &uri) {
  if (uri.empty()) {
    return false;
  }
  std::string extension = fs::path(uri).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return extension == ".hdr" || extension == ".exr";
}

static void RemapMaterialTextureIndex(int &textureIndex,
                                      const std::vector<int> &textureRemap) {
  if (textureIndex < 0) {
    return;
  }
  if (textureIndex >= static_cast<int>(textureRemap.size())) {
    textureIndex = -1;
    return;
  }
  textureIndex = textureRemap[static_cast<size_t>(textureIndex)];
}

static void RemapMaterialTextureIndices(std::vector<Asset::Material> &materials,
                                        const std::vector<int> &textureRemap) {
  for (Asset::Material &material : materials) {
    RemapMaterialTextureIndex(material.diffuseTexture, textureRemap);
    RemapMaterialTextureIndex(material.normalTexture, textureRemap);
    RemapMaterialTextureIndex(material.occlusionTexture, textureRemap);
    RemapMaterialTextureIndex(material.emissiveTexture, textureRemap);
    RemapMaterialTextureIndex(material.metalRoughTexture, textureRemap);
    RemapMaterialTextureIndex(material.runtimeMetalRoughTexture, textureRemap);
    RemapMaterialTextureIndex(material.metalnessTexture, textureRemap);
    RemapMaterialTextureIndex(material.roughnessGlossTexture, textureRemap);
  }
}

static std::vector<int> RegisterImportedTextures(
    std::vector<Asset::Texture> &textures,
    const std::vector<std::string> &textureSourceUris) {
  std::vector<int> textureRemap(textures.size(), -1);
  if (textures.empty()) {
    return textureRemap;
  }

  std::vector<Asset::Texture> texturesToAppend;
  texturesToAppend.reserve(textures.size());

  for (size_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex) {
    const bool hasSourceUri = textureIndex < textureSourceUris.size() &&
                              !textureSourceUris[textureIndex].empty();
    if (hasSourceUri) {
      const std::string key =
          NormalizeTextureSourceUriKey(textureSourceUris[textureIndex]);
      const auto cached = s_textureIndicesBySourceUri.find(key);
      if (cached != s_textureIndicesBySourceUri.end() && cached->second >= 0 &&
          cached->second < static_cast<int>(g_loadedTextures.size())) {
        textureRemap[textureIndex] = cached->second;
        continue;
      }
    }

    Asset::Texture textureToRegister = std::move(textures[textureIndex]);
    if (!textureToRegister.resource && hasSourceUri) {
      textureToRegister = Asset::LoadTextureFromFile(
          textureSourceUris[textureIndex],
          IsHdrTextureSourceUri(textureSourceUris[textureIndex]));
      if (!textureToRegister.resource) {
        fprintf(stderr,
                "RegisterImportedTextures: failed to load '%s'\n",
                textureSourceUris[textureIndex].c_str());
        continue;
      }
    }

    if (!textureToRegister.resource) {
      continue;
    }

    const int globalTextureIndex = static_cast<int>(g_loadedTextures.size());
    g_loadedTextures.push_back(std::move(textureToRegister));
    texturesToAppend.push_back(g_loadedTextures.back());
    textureRemap[textureIndex] = globalTextureIndex;

    if (hasSourceUri) {
      s_textureIndicesBySourceUri.emplace(
          NormalizeTextureSourceUriKey(textureSourceUris[textureIndex]),
          globalTextureIndex);
    }
  }

  RegisterTextures(texturesToAppend);
  return textureRemap;
}

static void InitializeNodeImportLinkage(Node &node,
                                        size_t materialBase,
                                        const std::vector<Asset::Material> &materials) {
  node.linkedMaterialIndices.clear();
  node.linkedMaterialSourceNames.clear();
  node.linkedMaterialIndices.reserve(materials.size());
  node.linkedMaterialSourceNames.reserve(materials.size());
  for (size_t i = 0; i < materials.size(); ++i) {
    node.linkedMaterialIndices.push_back((int)(materialBase + i));
    node.linkedMaterialSourceNames.emplace_back(materials[i].name);
  }
}

static Node BuildImportMaterialProbe(const ImportedNodePayload &payload) {
  Node probe;
  probe.linkedMaterialIndices = payload.preferredLinkedMaterialIndices;
  probe.linkedMaterialSourceNames = payload.preferredLinkedMaterialSourceNames;
  return probe;
}

static std::vector<int> BuildLegacyLinkedMaterialIndices(const Node &node);
static std::vector<std::string>
BuildLegacyLinkedMaterialNames(const std::vector<int> &indices);
static int FindLinkedMaterialByName(const std::vector<std::string> &sourceNames,
                                    const std::vector<int> &globalIndices,
                                    const std::string &name,
                                    std::vector<bool> *used);
static int FindLinkedMaterialByStableId(const std::vector<int> &globalIndices,
                                        const std::string &stableId,
                                        std::vector<bool> *used);
static bool IsShareableLiveLinkPayloadPath(const std::string &sourcePath);
static void ReleaseSharedImportedMeshEntry(const std::string &sourcePath);

static std::string ResolveNodeDisplayName(const ImportedNodePayload &payload) {
  if (!payload.displayName.empty()) {
    return payload.displayName;
  }
  if (!payload.sourcePath.empty()) {
    return fs::path(payload.sourcePath).filename().string();
  }
  return "Imported Node";
}

static RendererInvalidationPlan MergeRendererInvalidationPlan(
    RendererInvalidationPlan lhs, RendererInvalidationPlan rhs) {
  return static_cast<int>(rhs) > static_cast<int>(lhs) ? rhs : lhs;
}

static void ExecuteRendererInvalidation(RendererInvalidationPlan plan) {
  switch (plan) {
  case RendererInvalidationPlan::None:
    return;
  case RendererInvalidationPlan::AccumulationOnly:
    DxrRenderer::ResetAccumulation();
    return;
  case RendererInvalidationPlan::TlasRefresh:
    DxrRenderer::RequestAccelerationStructureUpdate();
    DxrRenderer::ResetAccumulation();
    return;
  case RendererInvalidationPlan::FullAccelerationStructureRebuild:
    DxrRenderer::RequestAccelerationStructureRebuild();
    DxrRenderer::ResetAccumulation();
    return;
  }
}

static void FlushBatchedRendererUpdates() {
  if (s_batchedMeshUploadDirty) {
    UploadGpuBuffersForMeshes(g_loadedMeshes);
    s_batchedMeshUploadDirty = false;
  }

  if (s_batchedLightsDirty) {
    const bool resetAccumulation =
        s_batchedInvalidationPlan == RendererInvalidationPlan::None;
    DxrRenderer::UpdateLights(s_lights, resetAccumulation);
    s_batchedLightsDirty = false;
  }

  if (s_batchedInvalidationPlan != RendererInvalidationPlan::None) {
    ExecuteRendererInvalidation(s_batchedInvalidationPlan);
    s_batchedInvalidationPlan = RendererInvalidationPlan::None;
  }
}

static void ApplyRendererInvalidation(RendererInvalidationPlan plan) {
  if (s_batchedUpdateDepth > 0) {
    s_batchedInvalidationPlan =
        MergeRendererInvalidationPlan(s_batchedInvalidationPlan, plan);
    return;
  }
  ExecuteRendererInvalidation(plan);
}

void BeginBatchedUpdates() {
  if (s_batchedUpdateDepth == 0) {
    s_batchedSceneChanged = false;
    s_batchedGpuIdleSatisfied = false;
  }
  ++s_batchedUpdateDepth;
}

void EndBatchedUpdates() {
  if (s_batchedUpdateDepth <= 0) {
    s_batchedUpdateDepth = 0;
    s_batchedSceneChanged = false;
    s_batchedGpuIdleSatisfied = false;
    s_batchedMeshUploadDirty = false;
    return;
  }

  --s_batchedUpdateDepth;
  if (s_batchedUpdateDepth == 0) {
    FlushBatchedRendererUpdates();
    const bool sceneChanged = s_batchedSceneChanged;
    s_batchedSceneChanged = false;
    s_batchedGpuIdleSatisfied = false;
    if (sceneChanged) {
      DispatchSceneChanged();
    }
  }
}

void RequestRendererFullRebuild() {
  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
}

void RequestRendererTlasRefresh() {
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
}

static void CopyMatrix4x4(const float *src, float *dst) {
  memcpy(dst, src, 16 * sizeof(float));
}

static void MulColumnMajor4x4(const float *a, const float *b, float *out) {
  float tmp[16];
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += a[k * 4 + row] * b[col * 4 + k];
      }
      tmp[col * 4 + row] = sum;
    }
  }
  CopyMatrix4x4(tmp, out);
}

static void TransformPointColumnMajor(const float m[16], const float p[3],
                                      float out[3]) {
  out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
  out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
  out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
}

static void TransformVectorColumnMajor(const float m[16], const float v[3],
                                       float out[3]) {
  out[0] = v[0] * m[0] + v[1] * m[4] + v[2] * m[8];
  out[1] = v[0] * m[1] + v[1] * m[5] + v[2] * m[9];
  out[2] = v[0] * m[2] + v[1] * m[6] + v[2] * m[10];
}

static size_t FindSceneTransformRootAncestor(size_t nodeIndex) {
  if (nodeIndex >= s_nodes.size()) {
    return static_cast<size_t>(-1);
  }
  size_t cursor = s_nodes[nodeIndex].parentIndex;
  for (size_t guard = 0; guard < s_nodes.size(); ++guard) {
    if (cursor == static_cast<size_t>(-1) || cursor >= s_nodes.size()) {
      return static_cast<size_t>(-1);
    }
    if (s_nodes[cursor].importGroupRoot) {
      return cursor;
    }
    cursor = s_nodes[cursor].parentIndex;
  }
  return static_cast<size_t>(-1);
}

static bool ResolveNodeWorldTransform(
    size_t nodeIndex, std::vector<std::array<float, 16>> &worldTransforms,
    std::vector<uint8_t> &visitState) {
  if (nodeIndex >= s_nodes.size()) {
    return false;
  }
  if (visitState[nodeIndex] == 2) {
    return true;
  }
  if (visitState[nodeIndex] == 1) {
    CopyMatrix4x4(s_nodes[nodeIndex].transform,
                  worldTransforms[nodeIndex].data());
    visitState[nodeIndex] = 2;
    return false;
  }

  visitState[nodeIndex] = 1;
  const size_t transformRootIndex = FindSceneTransformRootAncestor(nodeIndex);
  if (transformRootIndex != static_cast<size_t>(-1) &&
      transformRootIndex != nodeIndex &&
      ResolveNodeWorldTransform(transformRootIndex, worldTransforms,
                                visitState)) {
    MulColumnMajor4x4(worldTransforms[transformRootIndex].data(),
                      s_nodes[nodeIndex].transform,
                      worldTransforms[nodeIndex].data());
  } else {
    CopyMatrix4x4(s_nodes[nodeIndex].transform,
                  worldTransforms[nodeIndex].data());
  }
  visitState[nodeIndex] = 2;
  return true;
}

static std::vector<std::array<float, 16>> BuildNodeWorldTransforms() {
  std::vector<std::array<float, 16>> worldTransforms(s_nodes.size());
  std::vector<uint8_t> visitState(s_nodes.size(), 0);
  for (size_t i = 0; i < s_nodes.size(); ++i) {
    ResolveNodeWorldTransform(i, worldTransforms, visitState);
  }
  return worldTransforms;
}

static bool IsNodeDescendantOf(size_t nodeIndex, size_t ancestorIndex) {
  if (nodeIndex >= s_nodes.size() || ancestorIndex >= s_nodes.size()) {
    return false;
  }
  size_t cursor = nodeIndex;
  for (size_t guard = 0; guard < s_nodes.size(); ++guard) {
    if (cursor == ancestorIndex) {
      return true;
    }
    const size_t parentIndex = s_nodes[cursor].parentIndex;
    if (parentIndex == static_cast<size_t>(-1) ||
        parentIndex >= s_nodes.size()) {
      return false;
    }
    cursor = parentIndex;
  }
  return false;
}

static size_t ResolveSelectionTargetForHit(size_t hitNodeIndex) {
  if (hitNodeIndex >= s_nodes.size()) {
    return static_cast<size_t>(-1);
  }

  size_t selectionTarget = hitNodeIndex;
  size_t cursor = hitNodeIndex;
  for (size_t guard = 0; guard < s_nodes.size(); ++guard) {
    if (cursor >= s_nodes.size()) {
      break;
    }
    if (s_nodes[cursor].selectionLocked) {
      selectionTarget = cursor;
    }
    const size_t parentIndex = s_nodes[cursor].parentIndex;
    if (parentIndex == static_cast<size_t>(-1)) {
      break;
    }
    cursor = parentIndex;
  }
  return selectionTarget;
}

static bool IsModifierDown(bool ImGuiIO::*member, int virtualKey) {
  const ImGuiIO &io = ImGui::GetIO();
  return io.*member || ((GetKeyState(virtualKey) & 0x8000) != 0);
}

static bool IsCtrlDown() {
  return IsModifierDown(&ImGuiIO::KeyCtrl, VK_CONTROL);
}

static bool IsShiftDown() {
  return IsModifierDown(&ImGuiIO::KeyShift, VK_SHIFT);
}

static bool IsLeftMouseDown() {
  const ImGuiIO &io = ImGui::GetIO();
  return io.MouseDown[0] || ((GetKeyState(VK_LBUTTON) & 0x8000) != 0);
}

static std::vector<size_t> GetSelectedNodeIndices() {
  std::vector<size_t> selected;
  for (size_t i = 0; i < s_nodes.size(); ++i) {
    if (s_nodes[i].selected) {
      selected.push_back(i);
    }
  }
  return selected;
}

static std::vector<size_t> GetSelectedTransformRoots() {
  std::vector<size_t> selected = GetSelectedNodeIndices();
  std::vector<size_t> roots;
  roots.reserve(selected.size());
  for (size_t index : selected) {
    bool hasSelectedAncestor = false;
    size_t cursor = s_nodes[index].parentIndex;
    for (size_t guard = 0; guard < s_nodes.size(); ++guard) {
      if (cursor == static_cast<size_t>(-1) || cursor >= s_nodes.size()) {
        break;
      }
      if (s_nodes[cursor].selected) {
        hasSelectedAncestor = true;
        break;
      }
      cursor = s_nodes[cursor].parentIndex;
    }
    if (!hasSelectedAncestor) {
      roots.push_back(index);
    }
  }
  return roots;
}

static void SelectOnlyNodes(const std::vector<size_t> &indices) {
  for (Node &node : s_nodes) {
    node.selected = false;
  }
  for (size_t index : indices) {
    if (index < s_nodes.size()) {
      s_nodes[index].selected = true;
    }
  }
  s_selectedLightIdx = -1;
  NotifySceneChanged();
}

static void ToggleNodeSelection(size_t index) {
  if (index >= s_nodes.size()) {
    return;
  }
  s_nodes[index].selected = !s_nodes[index].selected;
  s_selectedLightIdx = -1;
  NotifySceneChanged();
}

static bool IsMeshReferencedOutsideNodes(
    size_t meshIndex, const std::vector<size_t> &excludedNodes) {
  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    if (std::find(excludedNodes.begin(), excludedNodes.end(), nodeIndex) !=
        excludedNodes.end()) {
      continue;
    }
    const Node &node = s_nodes[nodeIndex];
    if (std::find(node.meshIndices.begin(), node.meshIndices.end(),
                  meshIndex) != node.meshIndices.end()) {
      return true;
    }
  }
  return false;
}

static bool IsMeshReferencedByOtherNode(size_t meshIndex, const Node &owner) {
  for (const Node &node : s_nodes) {
    if (&node == &owner) {
      continue;
    }
    if (std::find(node.meshIndices.begin(), node.meshIndices.end(),
                  meshIndex) != node.meshIndices.end()) {
      return true;
    }
  }
  return false;
}

static void ClearGpuMeshEntry(size_t meshIndex) {
  if (meshIndex >= g_loadedMeshes.size()) {
    return;
  }
  g_loadedMeshes[meshIndex].vertexBuffer.Reset();
  g_loadedMeshes[meshIndex].indexBuffer.Reset();
  g_loadedMeshes[meshIndex].vertexCount = 0;
  g_loadedMeshes[meshIndex].indexCount = 0;
}

static std::vector<int> ResolveReplacementMaterialIndices(
    const Node &node, std::vector<Asset::Material> &materials,
    const std::vector<std::string> *materialStableIds = nullptr,
    bool allowSharedByNameReuse = true,
    bool overwriteResolvedMaterials = false) {
  EnsureMaterialMetadataStorage();
  std::vector<int> linkedMaterialIndices = node.linkedMaterialIndices;
  std::vector<std::string> linkedMaterialNames = node.linkedMaterialSourceNames;
  const bool allowGlobalNameReuse =
      allowSharedByNameReuse && materialStableIds && !materialStableIds->empty();
  if (linkedMaterialIndices.empty()) {
    linkedMaterialIndices = BuildLegacyLinkedMaterialIndices(node);
  }
  if (linkedMaterialNames.size() != linkedMaterialIndices.size()) {
    linkedMaterialNames = BuildLegacyLinkedMaterialNames(linkedMaterialIndices);
  }
  const bool hasSourceMaterialNames =
      std::any_of(linkedMaterialNames.begin(), linkedMaterialNames.end(),
                  [](const std::string &name) { return !name.empty(); });
  std::vector<std::string> incomingMaterialNames;
  incomingMaterialNames.reserve(materials.size());
  for (const Asset::Material &material : materials) {
    incomingMaterialNames.push_back(
        NormalizeMaterialSourceNameForMatch(material.name));
  }

  std::vector<bool> reused(linkedMaterialIndices.size(), false);
  std::vector<int> localToGlobal(materials.size(), -1);
  for (size_t i = 0; i < materials.size(); ++i) {
    const std::string importedName = materials[i].name;
    const std::string stableId =
        materialStableIds && i < materialStableIds->size()
            ? NormalizeMaterialStableId((*materialStableIds)[i])
            : std::string();
    int globalMaterialIndex = -1;

    if (!stableId.empty()) {
      globalMaterialIndex =
          FindLinkedMaterialByStableId(linkedMaterialIndices, stableId, &reused);
    }

    if (globalMaterialIndex < 0 && !stableId.empty()) {
      const auto existingStable = s_materialIndicesByStableId.find(stableId);
      if (existingStable != s_materialIndicesByStableId.end()) {
        globalMaterialIndex = existingStable->second;
      }
    }

    if (globalMaterialIndex < 0 && stableId.empty()) {
      globalMaterialIndex =
          FindLinkedMaterialByName(linkedMaterialNames, linkedMaterialIndices,
                                   importedName, &reused);
    }

    bool allowSlotFallback =
        !hasSourceMaterialNames || importedName.empty();
    if (!allowSlotFallback && i < linkedMaterialNames.size()) {
      const std::string linkedSlotName =
          NormalizeMaterialSourceNameForMatch(linkedMaterialNames[i]);
      const bool linkedSlotStillIncoming =
          !linkedSlotName.empty() &&
          std::find(incomingMaterialNames.begin(), incomingMaterialNames.end(),
                    linkedSlotName) != incomingMaterialNames.end();
      allowSlotFallback = !linkedSlotStillIncoming;
    }

    if (globalMaterialIndex < 0 && stableId.empty() && allowSlotFallback &&
        i < linkedMaterialIndices.size() && i < reused.size() && !reused[i]) {
      const int fallbackIndex = linkedMaterialIndices[i];
      if (fallbackIndex >= 0 && fallbackIndex < (int)g_loadedMaterials.size()) {
        globalMaterialIndex = fallbackIndex;
        reused[i] = true;
      }
    }

    if (allowGlobalNameReuse && globalMaterialIndex < 0 &&
      !importedName.empty()) {
      const auto sharedIt = s_materialIndicesByName.find(importedName);
      if (sharedIt != s_materialIndicesByName.end() &&
          sharedIt->second >= 0 &&
          sharedIt->second < static_cast<int>(g_loadedMaterials.size())) {
        globalMaterialIndex = sharedIt->second;
      }
    }

    if (globalMaterialIndex < 0) {
      globalMaterialIndex = (int)g_loadedMaterials.size();
      g_loadedMaterials.push_back(materials[i]);
      if (s_materialStableIds.size() < g_loadedMaterials.size()) {
        s_materialStableIds.resize(g_loadedMaterials.size());
      }
      if (!importedName.empty()) {
        s_materialIndicesByName[importedName] = globalMaterialIndex;
      }
    } else if (overwriteResolvedMaterials &&
               globalMaterialIndex < (int)g_loadedMaterials.size()) {
      const std::string previousName =
          g_loadedMaterials[(size_t)globalMaterialIndex].name;
      const std::string previousStableId =
          globalMaterialIndex < static_cast<int>(s_materialStableIds.size())
              ? s_materialStableIds[(size_t)globalMaterialIndex]
              : std::string();
      g_loadedMaterials[(size_t)globalMaterialIndex] = materials[i];
      if (!previousName.empty()) {
        const auto previousNameIt = s_materialIndicesByName.find(previousName);
        if (previousNameIt != s_materialIndicesByName.end() &&
            previousNameIt->second == globalMaterialIndex) {
          s_materialIndicesByName.erase(previousNameIt);
        }
      }
      if (!importedName.empty()) {
        s_materialIndicesByName[importedName] = globalMaterialIndex;
      }
      if (!previousStableId.empty() && previousStableId != stableId) {
        const auto previousStableIt =
            s_materialIndicesByStableId.find(previousStableId);
        if (previousStableIt != s_materialIndicesByStableId.end() &&
            previousStableIt->second == globalMaterialIndex) {
          s_materialIndicesByStableId.erase(previousStableIt);
        }
      }
    }

    if (globalMaterialIndex >= 0 &&
        globalMaterialIndex < static_cast<int>(g_loadedMaterials.size())) {
      if (s_materialStableIds.size() < g_loadedMaterials.size()) {
        s_materialStableIds.resize(g_loadedMaterials.size());
      }
      if (!stableId.empty()) {
        s_materialStableIds[static_cast<size_t>(globalMaterialIndex)] = stableId;
        s_materialIndicesByStableId[stableId] = globalMaterialIndex;
      } else if (overwriteResolvedMaterials) {
        s_materialStableIds[static_cast<size_t>(globalMaterialIndex)].clear();
      }
    }

    localToGlobal[i] = globalMaterialIndex;
  }

  return localToGlobal;
}

static std::vector<int> BuildLegacyLinkedMaterialIndices(const Node &node) {
  std::vector<int> indicesBySlot;
  std::vector<int> encounteredIndices;
  bool foundSourceSlots = false;

  auto visitNodeMeshes = [&](const Node &sceneNode) {
    for (size_t meshIndex : sceneNode.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
      const int materialIndex = mesh.materialIndex;
      if (materialIndex < 0 ||
          materialIndex >= static_cast<int>(g_loadedMaterials.size())) {
        continue;
      }

      if (mesh.materialSlot >= 0) {
        const size_t materialSlot = static_cast<size_t>(mesh.materialSlot);
        if (materialSlot >= indicesBySlot.size()) {
          indicesBySlot.resize(materialSlot + 1, -1);
        }
        if (indicesBySlot[materialSlot] < 0) {
          indicesBySlot[materialSlot] = materialIndex;
        }
        foundSourceSlots = true;
      }

      if (std::find(encounteredIndices.begin(), encounteredIndices.end(),
                    materialIndex) == encounteredIndices.end()) {
        encounteredIndices.push_back(materialIndex);
      }
    }
  };

  if (!node.importGroupKey.empty()) {
    for (const Node &sceneNode : s_nodes) {
      if (sceneNode.importGroupKey == node.importGroupKey) {
        visitNodeMeshes(sceneNode);
      }
    }
  } else {
    visitNodeMeshes(node);
  }

  if (foundSourceSlots) {
    return indicesBySlot;
  }

  return encounteredIndices;
}

static std::vector<std::string> BuildLegacyLinkedMaterialNames(const std::vector<int> &indices) {
  std::vector<std::string> names;
  names.reserve(indices.size());
  for (int index : indices) {
    if (index >= 0 && index < (int)g_loadedMaterials.size()) {
      names.emplace_back(g_loadedMaterials[index].name);
    } else {
      names.emplace_back();
    }
  }
  return names;
}

static void ClearNodeMeshes(const Node &node) {
  for (size_t meshIndex : node.meshIndices) {
    if (!IsMeshReferencedByOtherNode(meshIndex, node)) {
      ClearGpuMeshEntry(meshIndex);
    }
  }
}

static void ClearMeshIndices(const std::vector<size_t> &meshIndices) {
  static const std::vector<size_t> emptyExcludedNodes;
  for (size_t meshIndex : meshIndices) {
    if (!IsMeshReferencedOutsideNodes(meshIndex, emptyExcludedNodes)) {
      ClearGpuMeshEntry(meshIndex);
    }
  }
}

static void ClearMeshIndicesAfterRemovingNodes(
    const std::vector<size_t> &meshIndices,
    const std::vector<size_t> &removingNodes) {
  for (size_t meshIndex : meshIndices) {
    if (!IsMeshReferencedOutsideNodes(meshIndex, removingNodes)) {
      ClearGpuMeshEntry(meshIndex);
    }
  }
}

static std::string GenerateImportGroupKey(const std::string &sourcePath) {
  const std::string base = sourcePath.empty() ? "imported-scene" : sourcePath;
  return base + "#group-" + std::to_string(s_nextImportGroupId++);
}

static bool IsImportedSceneGroupNode(const Node &node) {
  return !node.importGroupKey.empty();
}

static size_t FindImportGroupRootIndex(const std::string &groupKey) {
  if (groupKey.empty()) {
    return static_cast<size_t>(-1);
  }
  for (size_t index = 0; index < s_nodes.size(); ++index) {
    if (s_nodes[index].importGroupKey == groupKey &&
        s_nodes[index].importGroupRoot) {
      return index;
    }
  }
  return static_cast<size_t>(-1);
}

static std::vector<size_t> CollectImportGroupNodeIndices(
    const std::string &groupKey) {
  std::vector<size_t> indices;
  if (groupKey.empty()) {
    return indices;
  }
  for (size_t index = 0; index < s_nodes.size(); ++index) {
    if (s_nodes[index].importGroupKey == groupKey) {
      indices.push_back(index);
    }
  }
  return indices;
}

static void RemoveNodesByIndexSet(std::vector<size_t> indices,
                                  bool notifyScene = true) {
  if (indices.empty()) {
    return;
  }

  PrepareForDestructiveMeshMutation();

  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  std::vector<size_t> uniqueMeshIndices;
  for (size_t nodeIndex : indices) {
    if (nodeIndex >= s_nodes.size()) {
      continue;
    }
    if (IsShareableLiveLinkPayloadPath(s_nodes[nodeIndex].sourcePath)) {
      continue;
    }
    for (size_t meshIndex : s_nodes[nodeIndex].meshIndices) {
      if (std::find(uniqueMeshIndices.begin(), uniqueMeshIndices.end(),
                    meshIndex) == uniqueMeshIndices.end()) {
        uniqueMeshIndices.push_back(meshIndex);
      }
    }
  }
  ClearMeshIndicesAfterRemovingNodes(uniqueMeshIndices, indices);

  for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
    const size_t nodeIndex = *it;
    if (nodeIndex >= s_nodes.size()) {
      continue;
    }
    if (IsShareableLiveLinkPayloadPath(s_nodes[nodeIndex].sourcePath)) {
      ReleaseSharedImportedMeshEntry(s_nodes[nodeIndex].sourcePath);
    }
    s_nodes.erase(s_nodes.begin() + nodeIndex);

    for (Node &node : s_nodes) {
      if (node.parentIndex == nodeIndex) {
        node.parentIndex = static_cast<size_t>(-1);
      } else if (node.parentIndex != static_cast<size_t>(-1) &&
                 node.parentIndex > nodeIndex) {
        --node.parentIndex;
      }
    }
    LiveLink::GetSceneSync().ReindexSceneNodeBindingsAfterRemoval(nodeIndex);
  }

  if (notifyScene) {
    DxrRenderer::RequestAccelerationStructureRebuild();
    DxrRenderer::ResetAccumulation();
    NotifySceneChanged();
  }
}

static bool IsShareableLiveLinkPayloadPath(const std::string &sourcePath) {
  return !sourcePath.empty() && fs::path(sourcePath).extension() == ".prmesh";
}

static void ReleaseSharedImportedMeshEntry(const std::string &sourcePath) {
  auto entryIt = s_sharedImportedMeshesBySourcePath.find(sourcePath);
  if (entryIt == s_sharedImportedMeshesBySourcePath.end()) {
    return;
  }

  SharedImportedMeshEntry &entry = entryIt->second;
  if (entry.refCount > 0) {
    --entry.refCount;
  }
  if (entry.refCount != 0) {
    return;
  }

  for (size_t meshIndex : entry.meshIndices) {
    if (meshIndex >= g_loadedMeshes.size()) {
      continue;
    }
    g_loadedMeshes[meshIndex].vertexBuffer.Reset();
    g_loadedMeshes[meshIndex].indexBuffer.Reset();
    g_loadedMeshes[meshIndex].vertexCount = 0;
    g_loadedMeshes[meshIndex].indexCount = 0;
  }

  s_sharedImportedMeshesBySourcePath.erase(entryIt);
}

static void ReleaseNodeMeshes(const Node &node) {
  if (IsShareableLiveLinkPayloadPath(node.sourcePath)) {
    ReleaseSharedImportedMeshEntry(node.sourcePath);
    return;
  }
  if (IsImportedSceneGroupNode(node)) {
    return;
  }
  ClearNodeMeshes(node);
}

struct ClonedNodeSet {
  std::vector<size_t> rootIndices;
  std::vector<size_t> nodeIndices;
};

static std::vector<size_t> CollectNodeSubtree(size_t rootIndex) {
  std::vector<size_t> subtree;
  if (rootIndex >= s_nodes.size()) {
    return subtree;
  }
  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    if (IsNodeDescendantOf(nodeIndex, rootIndex)) {
      subtree.push_back(nodeIndex);
    }
  }
  return subtree;
}

static ClonedNodeSet CloneNodesAsInstances(const std::vector<size_t> &roots) {
  ClonedNodeSet result;
  if (roots.empty()) {
    return result;
  }

  std::vector<size_t> sourceNodes;
  for (size_t rootIndex : roots) {
    std::vector<size_t> subtree = CollectNodeSubtree(rootIndex);
    sourceNodes.insert(sourceNodes.end(), subtree.begin(), subtree.end());
  }
  std::sort(sourceNodes.begin(), sourceNodes.end());
  sourceNodes.erase(std::unique(sourceNodes.begin(), sourceNodes.end()),
                    sourceNodes.end());
  if (sourceNodes.empty()) {
    return result;
  }

  std::unordered_map<size_t, size_t> remap;
  remap.reserve(sourceNodes.size());
  std::unordered_map<std::string, std::string> groupKeyRemap;

  for (size_t sourceIndex : sourceNodes) {
    const Node &source = s_nodes[sourceIndex];
    Node clone = source;
    clone.selected = false;
    clone.liveLinkManaged = false;
    clone.sourcePath.clear();
    clone.name = source.name + " Instance";

    if (!source.importGroupKey.empty()) {
      auto groupIt = groupKeyRemap.find(source.importGroupKey);
      if (groupIt == groupKeyRemap.end()) {
        groupIt = groupKeyRemap
                      .emplace(source.importGroupKey,
                               GenerateImportGroupKey(source.sourcePath))
                      .first;
      }
      clone.importGroupKey = groupIt->second;
    }

    const auto parentIt = remap.find(source.parentIndex);
    if (parentIt != remap.end()) {
      clone.parentIndex = parentIt->second;
    } else {
      clone.parentIndex = static_cast<size_t>(-1);
    }

    const size_t cloneIndex = s_nodes.size();
    s_nodes.push_back(std::move(clone));
    remap[sourceIndex] = cloneIndex;
    result.nodeIndices.push_back(cloneIndex);
  }

  result.rootIndices.reserve(roots.size());
  for (size_t rootIndex : roots) {
    const auto it = remap.find(rootIndex);
    if (it != remap.end()) {
      result.rootIndices.push_back(it->second);
    }
  }
  return result;
}

static void ConvertClonedInstancesToCopies(
    const std::vector<size_t> &cloneNodeIndices) {
  if (cloneNodeIndices.empty()) {
    return;
  }

  PrepareForDestructiveMeshMutation();

  std::unordered_map<size_t, size_t> meshRemap;
  for (size_t nodeIndex : cloneNodeIndices) {
    if (nodeIndex >= s_nodes.size()) {
      continue;
    }
    Node &node = s_nodes[nodeIndex];
    std::vector<size_t> copiedMeshIndices;
    copiedMeshIndices.reserve(node.meshIndices.size());
    for (size_t meshIndex : node.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      auto remapIt = meshRemap.find(meshIndex);
      if (remapIt == meshRemap.end()) {
        Asset::GpuMesh meshCopy = g_loadedMeshes[meshIndex];
        if (!meshCopy.cpuVertices.empty() && !meshCopy.cpuIndices.empty()) {
          meshCopy.vertexBuffer.Reset();
          meshCopy.indexBuffer.Reset();
          meshCopy.vbView = {};
          meshCopy.ibView = {};
        }
        const size_t newMeshIndex = g_loadedMeshes.size();
        g_loadedMeshes.push_back(std::move(meshCopy));
        remapIt = meshRemap.emplace(meshIndex, newMeshIndex).first;
      }
      copiedMeshIndices.push_back(remapIt->second);
    }
    node.meshIndices = std::move(copiedMeshIndices);
    const std::string suffix = " Instance";
    if (node.name.size() >= suffix.size() &&
        node.name.compare(node.name.size() - suffix.size(), suffix.size(),
                          suffix) == 0) {
      node.name.replace(node.name.size() - suffix.size(), suffix.size(),
                        " Copy");
    }
  }

  UploadGpuBuffersForMeshes(g_loadedMeshes);
  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
  NotifySceneChanged();
}

static bool FinalizeImportedNode(const std::string &srcPath,
                                 std::vector<Asset::GpuMesh> meshes,
                                 std::vector<Asset::Material> materials,
                                 std::vector<Asset::Texture> textures,
                                 std::vector<Asset::ImportedSceneNode> sceneNodes) {
  ImportedNodePayload payload;
  payload.sourcePath = srcPath;
  payload.meshes = std::move(meshes);
  payload.materials = std::move(materials);
  payload.textures = std::move(textures);
  payload.sceneNodes = std::move(sceneNodes);
  return AddImportedNode(std::move(payload));
}

static std::string BuildSceneLoadStatusPrefix(PendingImportAction action) {
  return action == PendingImportAction::Reimport ? "Reimporting "
                                                 : "Importing ";
}

static std::string BuildSceneLoadStartStatus(PendingImportAction action) {
  return action == PendingImportAction::Reimport ? "Starting reimport..."
                                                 : "Starting import...";
}

static std::string BuildSceneLoadFinishedStatus(PendingImportAction action) {
  return action == PendingImportAction::Reimport ? "Reimport finished"
                                                 : "Import finished";
}

static std::string BuildSceneLoadFailedStatus(PendingImportAction action,
                                              const std::string &path,
                                              bool producedNoMeshes) {
  const char *verb = action == PendingImportAction::Reimport ? "Reimport"
                                                              : "Import";
  return producedNoMeshes
             ? (std::string(verb) + " produced no meshes: " + path)
             : (std::string(verb) + " failed: " + path);
}

static std::string FormatSceneLoadProgressStatus(PendingImportAction action,
                                                 const std::string &message,
                                                 const std::string &path) {
  if (action != PendingImportAction::Reimport) {
    return message;
  }

  const std::string importPrefix = "Importing ";
  if (message.rfind(importPrefix, 0) == 0) {
    return BuildSceneLoadStatusPrefix(action) + message.substr(importPrefix.size());
  }
  if (message.empty()) {
    return BuildSceneLoadStatusPrefix(action) + path + "...";
  }
  return message;
}

static bool StartAsyncSceneLoadJob(const std::string &path,
                                   PendingImportAction action,
                                   size_t targetNodeIndex =
                                       static_cast<size_t>(-1),
                                   const std::string &targetImportGroupKey = {}) {
  if (s_importInProgress.load()) {
    s_lastStatus = "Import already in progress";
    return false;
  }

  s_importInProgress = true;
  s_importProgress = 0.0f;
  {
    std::lock_guard<std::mutex> lg(s_importStatusMutex);
    s_importStatus = BuildSceneLoadStartStatus(action);
  }

  Asset::SetProgressCallback([action, path](float progress,
                                            const std::string &message) {
    s_importProgress = progress;
    std::lock_guard<std::mutex> lg(s_importStatusMutex);
    s_importStatus = FormatSceneLoadProgressStatus(action, message, path);
  });

  std::thread([path, action, targetNodeIndex, targetImportGroupKey]() {
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;
    std::vector<Asset::ImportedSceneNode> sceneNodes;
    Asset::SetDeferGpuUpload(true);
    const bool ok =
        Asset::LoadModel(path, meshes, &materials, &textures, nullptr, &sceneNodes);
    Asset::SetDeferGpuUpload(false);

    if (!ok || meshes.empty()) {
      const std::string msg =
          BuildSceneLoadFailedStatus(action, path, ok && meshes.empty());
      fprintf(stderr, "Scene::StartAsyncSceneLoadJob: %s\n", msg.c_str());
      s_lastStatus = msg;
      {
        std::lock_guard<std::mutex> lg(s_importStatusMutex);
        s_importStatus = msg;
      }
      s_importInProgress = false;
      s_importProgress = 0.0f;
      Asset::ClearProgressCallback();
      return;
    }

    {
      std::lock_guard<std::mutex> lg(s_pendingMutex);
      s_pendingMeshes = std::move(meshes);
      s_pendingMaterials = std::move(materials);
      s_pendingTextures = std::move(textures);
      s_pendingSceneNodes = std::move(sceneNodes);
      s_pendingPath = path;
      s_pendingAction = action;
      s_pendingTargetNodeIndex = targetNodeIndex;
      s_pendingTargetImportGroupKey = targetImportGroupKey;
    }
    s_pendingReady = true;
    Asset::ClearProgressCallback();
  }).detach();

  return true;
}

static int FindLinkedMaterialByName(const std::vector<std::string> &sourceNames,
                                    const std::vector<int> &globalIndices,
                                    const std::string &name,
                                    std::vector<bool> *used) {
  if (!used) {
    return -1;
  }
  for (size_t i = 0; i < sourceNames.size() && i < globalIndices.size(); ++i) {
    if ((*used)[i]) {
      continue;
    }
    if (MaterialSourceNamesMatch(sourceNames[i], name) && globalIndices[i] >= 0 &&
        globalIndices[i] < (int)g_loadedMaterials.size()) {
      (*used)[i] = true;
      return globalIndices[i];
    }
  }
  return -1;
}

static int FindLinkedMaterialByStableId(const std::vector<int> &globalIndices,
                                        const std::string &stableId,
                                        std::vector<bool> *used) {
  if (!used || stableId.empty()) {
    return -1;
  }

  EnsureMaterialMetadataStorage();
  for (size_t i = 0; i < globalIndices.size(); ++i) {
    if (i < used->size() && (*used)[i]) {
      continue;
    }

    const int globalIndex = globalIndices[i];
    if (globalIndex < 0 ||
        globalIndex >= static_cast<int>(g_loadedMaterials.size()) ||
        globalIndex >= static_cast<int>(s_materialStableIds.size()) ||
        s_materialStableIds[static_cast<size_t>(globalIndex)] != stableId) {
      continue;
    }

    if (i < used->size()) {
      (*used)[i] = true;
    }
    return globalIndex;
  }
  return -1;
}

Node::Node() {
  name = "New Node";
  parentIndex = static_cast<size_t>(-1);
  // Identity matrix
  for (int i = 0; i < 16; ++i)
    transform[i] = 0.0f;
  transform[0] = transform[5] = transform[10] = transform[15] = 1.0f;
  selected = false;
  visible = true;
  liveLinkManaged = false;
}

static void EnsureGpuBuffersForMeshes(std::vector<Asset::GpuMesh> &meshes) {
  if (s_batchedUpdateDepth > 0) {
    s_batchedMeshUploadDirty = true;
    return;
  }
  UploadGpuBuffersForMeshes(meshes);
}

const std::string &LastStatus() { return s_lastStatus; }

GpuUploadStats GetGpuUploadStats() { return s_gpuUploadStats; }

size_t AddNode(Node node) {
  s_nodes.push_back(std::move(node));
  NotifySceneChanged();
  return s_nodes.empty() ? (size_t)-1 : (s_nodes.size() - 1);
}

bool AddImportedNode(ImportedNodePayload payload, size_t *outNodeIndex) {
  if (payload.meshes.empty()) {
    s_lastStatus = "AddImportedNode failed: no meshes provided";
    return false;
  }

  const bool isLiveLinkPayload =
      IsShareableLiveLinkPayloadPath(payload.sourcePath);

  if (!payload.sceneNodes.empty()) {
    EnsureGpuBuffersForMeshes(payload.meshes);

    const std::vector<int> textureRemap =
        RegisterImportedTextures(payload.textures, payload.textureSourceUris);
    RemapMaterialTextureIndices(payload.materials, textureRemap);

    Node importNodeProbe = BuildImportMaterialProbe(payload);
    const bool overwriteResolvedMaterials =
        isLiveLinkPayload && payload.materialsContainFullDefinitions;
    std::vector<int> localToGlobal =
        ResolveReplacementMaterialIndices(importNodeProbe, payload.materials,
                                          &payload.materialStableIds,
                                          !isLiveLinkPayload,
                                          overwriteResolvedMaterials);

    const size_t meshBase = g_loadedMeshes.size();
    g_loadedMeshes.insert(g_loadedMeshes.end(), payload.meshes.begin(),
                          payload.meshes.end());
    for (size_t meshOffset = 0; meshOffset < payload.meshes.size();
         ++meshOffset) {
      int &materialIndex = g_loadedMeshes[meshBase + meshOffset].materialIndex;
      if (materialIndex >= 0 &&
          materialIndex < static_cast<int>(localToGlobal.size())) {
        materialIndex = localToGlobal[(size_t)materialIndex];
      } else {
        materialIndex = -1;
      }
    }

    const std::string groupKey =
        payload.importGroupKey.empty()
            ? GenerateImportGroupKey(payload.sourcePath)
            : payload.importGroupKey;

    Node rootNode;
    rootNode.name = ResolveNodeDisplayName(payload);
    rootNode.sourcePath = payload.sourcePath;
    rootNode.importGroupKey = groupKey;
    rootNode.importGroupRoot = true;
    rootNode.selectionLocked = true;
    rootNode.linkedMaterialIndices = localToGlobal;
    rootNode.linkedMaterialSourceNames.reserve(payload.materials.size());
    for (const Asset::Material &material : payload.materials) {
      rootNode.linkedMaterialSourceNames.emplace_back(material.name);
    }

    const size_t rootIndex = s_nodes.size();
    s_nodes.push_back(std::move(rootNode));

    std::vector<size_t> importedToSceneIndex(payload.sceneNodes.size(),
                                             static_cast<size_t>(-1));
    for (size_t importedIndex = 0; importedIndex < payload.sceneNodes.size();
         ++importedIndex) {
      const Asset::ImportedSceneNode &importedNode =
          payload.sceneNodes[importedIndex];
      Node sceneNode;
      sceneNode.name =
          importedNode.name.empty() ? "Imported Node" : importedNode.name;
      sceneNode.importGroupKey = groupKey;
      sceneNode.linkedMaterialIndices = localToGlobal;
      sceneNode.linkedMaterialSourceNames =
          s_nodes[rootIndex].linkedMaterialSourceNames;
      memcpy(sceneNode.transform, importedNode.transform,
             sizeof(sceneNode.transform));

      const size_t importedParentIndex = importedNode.parentIndex;
      if (importedParentIndex != static_cast<size_t>(-1) &&
          importedParentIndex < importedToSceneIndex.size() &&
          importedToSceneIndex[importedParentIndex] !=
              static_cast<size_t>(-1)) {
        sceneNode.parentIndex = importedToSceneIndex[importedParentIndex];
      } else {
        sceneNode.parentIndex = rootIndex;
      }

      sceneNode.meshIndices.reserve(importedNode.meshIndices.size());
      for (size_t localMeshIndex : importedNode.meshIndices) {
        if (localMeshIndex < payload.meshes.size()) {
          sceneNode.meshIndices.push_back(meshBase + localMeshIndex);
        }
      }

      importedToSceneIndex[importedIndex] = s_nodes.size();
      s_nodes.push_back(std::move(sceneNode));
    }

    s_lastStatus = std::string("Loaded instanced: ") +
                   (payload.sourcePath.empty() ? ResolveNodeDisplayName(payload)
                                               : payload.sourcePath);
    fprintf(stderr, "%s\n", s_lastStatus.c_str());
    ApplyRendererInvalidation(
        RendererInvalidationPlan::FullAccelerationStructureRebuild);
    NotifySceneChanged();
    if (outNodeIndex) {
      *outNodeIndex = rootIndex;
    }
    return true;
  }

  if (isLiveLinkPayload) {
    auto sharedEntryIt =
        s_sharedImportedMeshesBySourcePath.find(payload.sourcePath);
    if (sharedEntryIt != s_sharedImportedMeshesBySourcePath.end()) {
      ++sharedEntryIt->second.refCount;

      Node node;
      node.name = ResolveNodeDisplayName(payload);
      node.sourcePath = payload.sourcePath;
      node.selectionLocked = true;
      node.meshIndices = sharedEntryIt->second.meshIndices;
      node.linkedMaterialIndices = sharedEntryIt->second.linkedMaterialIndices;
      node.linkedMaterialSourceNames =
          sharedEntryIt->second.linkedMaterialSourceNames;
      const size_t nodeIndex = AddNode(std::move(node));

      s_lastStatus = std::string("Loaded shared: ") + payload.sourcePath;
      fprintf(stderr, "%s\n", s_lastStatus.c_str());
      ApplyRendererInvalidation(
          RendererInvalidationPlan::FullAccelerationStructureRebuild);
      NotifySceneChanged();
      if (outNodeIndex) {
        *outNodeIndex = nodeIndex;
      }
      return true;
    }
  }

  EnsureGpuBuffersForMeshes(payload.meshes);

  const size_t meshBase = g_loadedMeshes.size();
  const std::vector<int> textureRemap =
      RegisterImportedTextures(payload.textures, payload.textureSourceUris);
  RemapMaterialTextureIndices(payload.materials, textureRemap);
    Node importNodeProbe = BuildImportMaterialProbe(payload);
  const bool overwriteResolvedMaterials =
      isLiveLinkPayload && payload.materialsContainFullDefinitions;
  std::vector<int> localToGlobal =
      ResolveReplacementMaterialIndices(importNodeProbe, payload.materials,
               &payload.materialStableIds,
                       !isLiveLinkPayload,
                       overwriteResolvedMaterials);
  g_loadedMeshes.insert(g_loadedMeshes.end(), payload.meshes.begin(),
                        payload.meshes.end());

  for (size_t i = 0; i < payload.meshes.size(); ++i) {
    int &materialIndex = g_loadedMeshes[meshBase + i].materialIndex;
    if (materialIndex >= 0 && materialIndex < (int)localToGlobal.size()) {
      materialIndex = localToGlobal[(size_t)materialIndex];
    } else {
      materialIndex = -1;
    }
  }

  Node node;
  node.name = ResolveNodeDisplayName(payload);
  node.sourcePath = payload.sourcePath;
  node.selectionLocked = true;
  node.meshIndices.reserve(payload.meshes.size());
  for (size_t i = 0; i < payload.meshes.size(); ++i) {
    node.meshIndices.push_back(meshBase + i);
  }
  node.linkedMaterialIndices = std::move(localToGlobal);
  node.linkedMaterialSourceNames.clear();
  node.linkedMaterialSourceNames.reserve(payload.materials.size());
  for (const Asset::Material &material : payload.materials) {
    node.linkedMaterialSourceNames.emplace_back(material.name);
  }
  const size_t nodeIndex = AddNode(std::move(node));

  if (isLiveLinkPayload) {
    SharedImportedMeshEntry entry;
    entry.refCount = 1;
    entry.meshIndices.reserve(payload.meshes.size());
    for (size_t i = 0; i < payload.meshes.size(); ++i) {
      entry.meshIndices.push_back(meshBase + i);
    }
    if (nodeIndex < s_nodes.size()) {
      entry.linkedMaterialIndices = s_nodes[nodeIndex].linkedMaterialIndices;
      entry.linkedMaterialSourceNames =
          s_nodes[nodeIndex].linkedMaterialSourceNames;
    }
    s_sharedImportedMeshesBySourcePath[payload.sourcePath] = std::move(entry);
  }

  s_lastStatus = std::string("Loaded: ") +
                 (payload.sourcePath.empty() ? ResolveNodeDisplayName(payload)
                                             : payload.sourcePath);
  fprintf(stderr, "%s\n", s_lastStatus.c_str());

  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
  NotifySceneChanged();
  if (outNodeIndex) {
    *outNodeIndex = nodeIndex;
  }
  return true;
}

bool ReplaceNodeImportedContent(size_t index, ImportedNodePayload payload) {
  if (index >= s_nodes.size()) {
    s_lastStatus = "ReplaceNodeImportedContent failed: node index out of range";
    return false;
  }
  if (payload.meshes.empty()) {
    s_lastStatus = "ReplaceNodeImportedContent failed: no meshes provided";
    return false;
  }

  PrepareForDestructiveMeshMutation();
  EnsureGpuBuffersForMeshes(payload.meshes);

  Node &node = s_nodes[index];
  const std::string previousSourcePath = node.sourcePath;
  const std::string effectiveSourcePath =
      payload.sourcePath.empty() ? node.sourcePath : payload.sourcePath;

  if (!payload.sceneNodes.empty()) {
    std::string groupKey = payload.importGroupKey;
    if (groupKey.empty() && IsImportedSceneGroupNode(node)) {
      groupKey = node.importGroupKey;
    }
    payload.sourcePath = effectiveSourcePath;
    payload.importGroupKey =
        groupKey.empty() ? GenerateImportGroupKey(effectiveSourcePath)
                         : groupKey;
    payload.preferredLinkedMaterialIndices = node.linkedMaterialIndices;
    if (payload.preferredLinkedMaterialIndices.empty()) {
      payload.preferredLinkedMaterialIndices = BuildLegacyLinkedMaterialIndices(node);
    }
    payload.preferredLinkedMaterialSourceNames = node.linkedMaterialSourceNames;
    if (payload.preferredLinkedMaterialSourceNames.size() !=
        payload.preferredLinkedMaterialIndices.size()) {
      payload.preferredLinkedMaterialSourceNames =
          BuildLegacyLinkedMaterialNames(payload.preferredLinkedMaterialIndices);
    }

    std::vector<size_t> nodesToRemove =
        IsImportedSceneGroupNode(node)
            ? CollectImportGroupNodeIndices(node.importGroupKey)
            : std::vector<size_t>{index};
    RemoveNodesByIndexSet(std::move(nodesToRemove), false);

    size_t newRootIndex = static_cast<size_t>(-1);
    const bool ok = AddImportedNode(std::move(payload), &newRootIndex);
    if (!ok) {
      s_lastStatus =
          "ReplaceNodeImportedContent failed: unable to rebuild import group";
    }
    return ok;
  }

  const bool isLiveLinkPayload =
      node.liveLinkManaged || IsShareableLiveLinkPayloadPath(effectiveSourcePath);
  const bool useSharedImportedMesh =
      IsShareableLiveLinkPayloadPath(effectiveSourcePath);
  const std::vector<int> textureRemap =
      RegisterImportedTextures(payload.textures, payload.textureSourceUris);
  RemapMaterialTextureIndices(payload.materials, textureRemap);
  const bool overwriteResolvedMaterials =
      isLiveLinkPayload && payload.materialsContainFullDefinitions;

  std::vector<int> localToGlobal =
      ResolveReplacementMaterialIndices(node, payload.materials,
               &payload.materialStableIds,
                       !isLiveLinkPayload,
                       overwriteResolvedMaterials);

  const size_t meshBase = g_loadedMeshes.size();
  for (size_t i = 0; i < payload.meshes.size(); ++i) {
    int &materialIndex = payload.meshes[i].materialIndex;
    if (materialIndex >= 0 && materialIndex < (int)localToGlobal.size()) {
      materialIndex = localToGlobal[(size_t)materialIndex];
    } else {
      materialIndex = -1;
    }
  }

  if (useSharedImportedMesh) {
    auto sharedEntryIt =
        s_sharedImportedMeshesBySourcePath.find(effectiveSourcePath);
    const size_t previousRefCount =
        sharedEntryIt != s_sharedImportedMeshesBySourcePath.end()
            ? sharedEntryIt->second.refCount
            : 0;
    std::vector<size_t> previousMeshIndices;
    if (sharedEntryIt != s_sharedImportedMeshesBySourcePath.end()) {
      previousMeshIndices = sharedEntryIt->second.meshIndices;
    }

    std::vector<size_t> newMeshIndices;
    if (previousSourcePath == effectiveSourcePath &&
        !previousMeshIndices.empty()) {
      // Reuse existing mesh table slots for repeated same-source LiveLink updates
      newMeshIndices = ReplaceOrAppendMeshes(previousMeshIndices, payload.meshes);
      // payload.meshes have been consumed by replace-or-append.
    } else {
      if (previousSourcePath != effectiveSourcePath) {
        ReleaseNodeMeshes(node);
      }

      g_loadedMeshes.insert(g_loadedMeshes.end(), payload.meshes.begin(),
                            payload.meshes.end());

      newMeshIndices.reserve(payload.meshes.size());
      for (size_t i = 0; i < payload.meshes.size(); ++i) {
        newMeshIndices.push_back(meshBase + i);
      }
    }

    SharedImportedMeshEntry entry;
    entry.refCount =
        previousSourcePath == effectiveSourcePath
            ? (std::max)(size_t(1), previousRefCount)
            : (std::max)(size_t(1), previousRefCount + 1);
    entry.linkedMaterialIndices = localToGlobal;
    entry.linkedMaterialSourceNames.reserve(payload.materials.size());
    for (const Asset::Material &material : payload.materials) {
      entry.linkedMaterialSourceNames.emplace_back(material.name);
    }
    entry.meshIndices = newMeshIndices;

    s_sharedImportedMeshesBySourcePath[effectiveSourcePath] = entry;

    if (previousSourcePath != effectiveSourcePath) {
      for (size_t meshIndex : previousMeshIndices) {
        if (meshIndex >= g_loadedMeshes.size()) {
          continue;
        }
        ResetGpuMeshEntry(g_loadedMeshes[meshIndex]);
      }
    }

    for (Node &sceneNode : s_nodes) {
      if (sceneNode.sourcePath != effectiveSourcePath &&
          &sceneNode != &node) {
        continue;
      }
      sceneNode.name = (&sceneNode == &node) ? ResolveNodeDisplayName(payload)
                                             : sceneNode.name;
      sceneNode.sourcePath = effectiveSourcePath;
      sceneNode.meshIndices =
          s_sharedImportedMeshesBySourcePath[effectiveSourcePath].meshIndices;
      sceneNode.linkedMaterialIndices =
          s_sharedImportedMeshesBySourcePath[effectiveSourcePath]
              .linkedMaterialIndices;
      sceneNode.linkedMaterialSourceNames =
          s_sharedImportedMeshesBySourcePath[effectiveSourcePath]
              .linkedMaterialSourceNames;
    }
  } else {
    std::vector<size_t> previousNodeMeshIndices = node.meshIndices;
    std::vector<size_t> newNodeMeshIndices;
    if (!previousNodeMeshIndices.empty()) {
      newNodeMeshIndices = ReplaceOrAppendMeshes(previousNodeMeshIndices,
                                                 payload.meshes);
    } else {
      ReleaseNodeMeshes(node);
      g_loadedMeshes.insert(g_loadedMeshes.end(), payload.meshes.begin(),
                            payload.meshes.end());
      newNodeMeshIndices.reserve(payload.meshes.size());
      for (size_t i = 0; i < payload.meshes.size(); ++i) {
        newNodeMeshIndices.push_back(meshBase + i);
      }
    }

    node.meshIndices = std::move(newNodeMeshIndices);
    node.name = ResolveNodeDisplayName(payload);
    node.sourcePath = effectiveSourcePath;
    node.linkedMaterialIndices = std::move(localToGlobal);
    node.linkedMaterialSourceNames.clear();
    node.linkedMaterialSourceNames.reserve(payload.materials.size());
    for (const Asset::Material &material : payload.materials) {
      node.linkedMaterialSourceNames.emplace_back(material.name);
    }
  }

  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
  NotifySceneChanged();

  s_lastStatus = std::string("Reimported: ") +
                 (effectiveSourcePath.empty() ? node.name : effectiveSourcePath);
  fprintf(stderr, "%s\n", s_lastStatus.c_str());
  return true;
}

bool RenameNode(size_t index, const std::string &name) {
  if (index >= s_nodes.size()) {
    return false;
  }
  s_nodes[index].name = name;
  NotifySceneChanged();
  return true;
}

bool UpdateNodeTransform(size_t index, const float *columnMajor4x4) {
  if (index >= s_nodes.size() || !columnMajor4x4) {
    return false;
  }
  if (memcmp(s_nodes[index].transform, columnMajor4x4,
             sizeof(s_nodes[index].transform)) == 0) {
    return true;
  }
  memcpy(s_nodes[index].transform, columnMajor4x4,
         sizeof(s_nodes[index].transform));
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  return true;
}

bool SetNodeVisibility(size_t index, bool visible) {
  if (index >= s_nodes.size()) {
    return false;
  }
  if (s_nodes[index].visible == visible) {
    return true;
  }
  s_nodes[index].visible = visible;
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  return true;
}

bool SetNodeSelectionLocked(size_t index, bool locked) {
  if (index >= s_nodes.size()) {
    return false;
  }
  if (s_nodes[index].selectionLocked == locked) {
    return true;
  }
  s_nodes[index].selectionLocked = locked;
  NotifySceneChanged();
  return true;
}

bool SetNodeLiveLinkManaged(size_t index, bool liveLinkManaged) {
  if (index >= s_nodes.size()) {
    return false;
  }
  s_nodes[index].liveLinkManaged = liveLinkManaged;
  return true;
}

bool SetNodeParent(size_t index, size_t parentIndex) {
  if (index >= s_nodes.size()) {
    return false;
  }

  const size_t resolvedParent =
      parentIndex < s_nodes.size() ? parentIndex : static_cast<size_t>(-1);
  if (resolvedParent == index) {
    return false;
  }

  size_t cursor = resolvedParent;
  while (cursor != static_cast<size_t>(-1) && cursor < s_nodes.size()) {
    if (cursor == index) {
      return false;
    }
    cursor = s_nodes[cursor].parentIndex;
  }

  s_nodes[index].parentIndex = resolvedParent;
  return true;
}

bool IsImportInProgress() { return s_importInProgress.load(); }

float GetImportProgress() { return s_importProgress.load(); }

std::string GetImportStatus() {
  std::lock_guard<std::mutex> lg(s_importStatusMutex);
  return s_importStatus;
}

void ProcessPendingImport() {
  if (!s_pendingReady.load()) {
    return;
  }

  std::vector<Asset::GpuMesh> meshes;
  std::vector<Asset::Material> materials;
  std::vector<Asset::Texture> textures;
  std::vector<Asset::ImportedSceneNode> sceneNodes;
  std::string srcPath;
  PendingImportAction action = PendingImportAction::Import;
  size_t targetNodeIndex = static_cast<size_t>(-1);
  std::string targetImportGroupKey;
  {
    std::lock_guard<std::mutex> lg(s_pendingMutex);
    meshes = std::move(s_pendingMeshes);
    materials = std::move(s_pendingMaterials);
    textures = std::move(s_pendingTextures);
    sceneNodes = std::move(s_pendingSceneNodes);
    srcPath = std::move(s_pendingPath);
    action = s_pendingAction;
    targetNodeIndex = s_pendingTargetNodeIndex;
    targetImportGroupKey = std::move(s_pendingTargetImportGroupKey);
    s_pendingMeshes.clear();
    s_pendingMaterials.clear();
    s_pendingTextures.clear();
    s_pendingSceneNodes.clear();
    s_pendingPath.clear();
    s_pendingAction = PendingImportAction::Import;
    s_pendingTargetNodeIndex = static_cast<size_t>(-1);
    s_pendingTargetImportGroupKey.clear();
  }
  s_pendingReady = false;

  bool ok = false;
  if (action == PendingImportAction::Import) {
    ok = FinalizeImportedNode(srcPath, std::move(meshes), std::move(materials),
                              std::move(textures), std::move(sceneNodes));
  } else {
    if (!targetImportGroupKey.empty()) {
      targetNodeIndex = FindImportGroupRootIndex(targetImportGroupKey);
    }

    if (targetNodeIndex >= s_nodes.size()) {
      s_lastStatus = "Reimport failed: target node no longer exists.";
    } else {
      ImportedNodePayload payload;
      payload.sourcePath = srcPath;
      payload.importGroupKey = targetImportGroupKey;
      payload.meshes = std::move(meshes);
      payload.materials = std::move(materials);
      payload.textures = std::move(textures);
      payload.sceneNodes = std::move(sceneNodes);
      ok = ReplaceNodeImportedContent(targetNodeIndex, std::move(payload));
    }
  }

  // Clear import-in-progress flag
  s_importInProgress = false;
  s_importProgress = ok ? 1.0f : 0.0f;
  {
    std::lock_guard<std::mutex> lg(s_importStatusMutex);
    s_importStatus = ok ? BuildSceneLoadFinishedStatus(action) : s_lastStatus;
  }
}

bool ImportModel(const std::string &utf8path, const float *rootTranslation) {
  try {
    fprintf(stderr, "Scene::ImportModel: importing %s\n", utf8path.c_str());
    std::vector<Asset::GpuMesh> meshes;
    std::vector<Asset::Material> materials;
    std::vector<Asset::Texture> textures;
    std::vector<Asset::ImportedSceneNode> sceneNodes;
    bool ok = Asset::LoadModel(utf8path, meshes, &materials, &textures,
                               rootTranslation, &sceneNodes);
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
    return FinalizeImportedNode(utf8path, std::move(meshes),
                                std::move(materials), std::move(textures),
                                std::move(sceneNodes));
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

    return StartAsyncSceneLoadJob(utf8path, PendingImportAction::Import);
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

bool CanReimportNode(size_t index) {
  if (index >= s_nodes.size()) {
    return false;
  }
  if (!s_nodes[index].importGroupKey.empty()) {
    const size_t rootIndex =
        FindImportGroupRootIndex(s_nodes[index].importGroupKey);
    return rootIndex < s_nodes.size() && !s_nodes[rootIndex].sourcePath.empty();
  }
  return !s_nodes[index].sourcePath.empty();
}

bool ReimportNode(size_t index) {
  if (!CanReimportNode(index)) {
    s_lastStatus = "Reimport failed: node has no linked source.";
    return false;
  }

  if (!s_nodes[index].importGroupKey.empty()) {
    const size_t rootIndex =
        FindImportGroupRootIndex(s_nodes[index].importGroupKey);
    if (rootIndex < s_nodes.size()) {
      index = rootIndex;
    }
  }

  Node &node = s_nodes[index];
  const std::string srcPath = node.sourcePath;
  if (!fs::exists(fs::path(srcPath))) {
    s_lastStatus = std::string("Reimport failed: source missing: ") + srcPath;
    fprintf(stderr, "%s\n", s_lastStatus.c_str());
    return false;
  }

  return StartAsyncSceneLoadJob(srcPath, PendingImportAction::Reimport, index,
                                node.importGroupKey);
}

void SelectNode(size_t index) {
  if (index < s_nodes.size()) {
    SelectOnlyNodes({index});
  } else {
    SelectOnlyNodes({});
  }
}

void SelectNodes(const std::vector<size_t> &indices) {
  SelectOnlyNodes(indices);
}

bool HasPendingCloneOptions() { return s_shiftCloneDrag.optionsPending; }

void ResolvePendingCloneAsCopy() {
  if (!s_shiftCloneDrag.optionsPending) {
    return;
  }
  const std::vector<size_t> cloneRootIndices = s_shiftCloneDrag.cloneRootIndices;
  ConvertClonedInstancesToCopies(s_shiftCloneDrag.cloneNodeIndices);
  SelectOnlyNodes(cloneRootIndices);
  s_shiftCloneDrag = {};
}

void ResolvePendingCloneAsInstance() {
  if (!s_shiftCloneDrag.optionsPending) {
    return;
  }
  const std::vector<size_t> cloneRootIndices = s_shiftCloneDrag.cloneRootIndices;
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  NotifySceneChanged();
  SelectOnlyNodes(cloneRootIndices);
  s_shiftCloneDrag = {};
}

size_t RegisterChangeListener(std::function<void()> callback) {
  const size_t listenerId = s_nextChangeListenerId++;
  s_changeListeners.emplace(listenerId, std::move(callback));
  return listenerId;
}

void UnregisterChangeListener(size_t listenerId) {
  s_changeListeners.erase(listenerId);
}

bool RemoveNode(size_t index) {
  if (index >= s_nodes.size())
    return false;

  PrepareForDestructiveMeshMutation();

  if (IsImportedSceneGroupNode(s_nodes[index])) {
    RemoveNodesByIndexSet(
        CollectImportGroupNodeIndices(s_nodes[index].importGroupKey), true);
    return true;
  }

  ReleaseNodeMeshes(s_nodes[index]);
  s_nodes.erase(s_nodes.begin() + index);

  for (Node &node : s_nodes) {
    if (node.parentIndex == index) {
      node.parentIndex = static_cast<size_t>(-1);
    } else if (node.parentIndex != static_cast<size_t>(-1) &&
               node.parentIndex > index) {
      --node.parentIndex;
    }
  }

  LiveLink::GetSceneSync().ReindexSceneNodeBindingsAfterRemoval(index);
  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
  NotifySceneChanged();
  return true;
}

void DeleteNode(size_t index) {
  RemoveNode(index);
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
    AddNode(std::move(node));

    fprintf(stderr, "AddDefaultPlane: added plane (10x10) with material %d\n",
            matIndex);

    ApplyRendererInvalidation(
        RendererInvalidationPlan::FullAccelerationStructureRebuild);
  } catch (const std::exception &e) {
    fprintf(stderr, "AddDefaultPlane: exception: %s\n", e.what());
  }
}

void RebuildAccelerationStructures() {
  DxrRenderer::BuildAccelerationStructures(GetActiveMeshes(), GetInstances());
}

std::vector<const Asset::GpuMesh *> GetActiveMeshes() {
  std::vector<const Asset::GpuMesh *> active;
  active.reserve(256);

  std::vector<bool> used;
  used.resize(g_loadedMeshes.size());
  for (const auto &node : s_nodes) {
    if (!node.visible)
      continue;
    for (size_t meshIndex : node.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size() || used[meshIndex])
        continue;
      const auto &m = g_loadedMeshes[meshIndex];
      if (m.vertexBuffer && m.indexBuffer && m.vertexCount > 0 &&
          m.indexCount > 0) {
        active.push_back(&m);
        used[meshIndex] = true;
      }
    }
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
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();
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
            reinterpret_cast<const DirectX::XMFLOAT4X4 *>(
                worldTransforms[ni].data()));
        inst.id = (int)ni;
        instances.push_back(inst);
      }
    }
  }
  return instances;
}

std::vector<Light> &GetLights() { return s_lights; }

void UpdateLights() {
  if (s_batchedUpdateDepth > 0) {
    s_batchedLightsDirty = true;
    return;
  }
  DxrRenderer::UpdateLights(s_lights);
}

int GetSelectedLightIndex() { return s_selectedLightIdx; }

void SelectLight(int index) {
  if (index < 0 || index >= (int)s_lights.size()) {
    s_selectedLightIdx = -1;
    NotifySceneChanged();
    return;
  }
  s_selectedLightIdx = index;
  for (auto &n : s_nodes)
    n.selected = false;
  NotifySceneChanged();
}

size_t AddLight(LightType type) {
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
  return s_lights.empty() ? (size_t)-1 : (s_lights.size() - 1);
}

bool UpdateLight(size_t index, const Light &light) {
  if (index >= s_lights.size()) {
    return false;
  }
  if (memcmp(&s_lights[index], &light, sizeof(Light)) == 0) {
    return true;
  }
  s_lights[index] = light;
  UpdateLights();
  return true;
}

size_t GetMaterialCount() { return g_loadedMaterials.size(); }

size_t GetTextureCount() { return g_loadedTextures.size(); }

int FindMaterialByName(const std::string &name) {
  if (name.empty()) {
    return -1;
  }
  EnsureMaterialMetadataStorage();
  const auto it = s_materialIndicesByName.find(name);
  if (it == s_materialIndicesByName.end()) {
    return -1;
  }
  return it->second;
}

int FindMaterialByStableId(const std::string &stableId) {
  const std::string normalized = NormalizeMaterialStableId(stableId);
  if (normalized.empty()) {
    return -1;
  }
  EnsureMaterialMetadataStorage();
  const auto it = s_materialIndicesByStableId.find(normalized);
  if (it == s_materialIndicesByStableId.end()) {
    return -1;
  }
  return it->second;
}

bool GetMaterial(size_t index, Asset::Material *outMaterial) {
  if (!outMaterial || index >= g_loadedMaterials.size()) {
    return false;
  }
  *outMaterial = g_loadedMaterials[index];
  return true;
}

int FindOrCreateMaterial(const Asset::Material &material,
                         const std::string &stableId) {
  EnsureMaterialMetadataStorage();

  const std::string normalizedStableId = NormalizeMaterialStableId(stableId);
  if (!normalizedStableId.empty()) {
    const auto stableIt = s_materialIndicesByStableId.find(normalizedStableId);
    if (stableIt != s_materialIndicesByStableId.end()) {
      return stableIt->second;
    }
  }

  const std::string materialName = material.name;
  if (!materialName.empty()) {
    const auto nameIt = s_materialIndicesByName.find(materialName);
    if (nameIt != s_materialIndicesByName.end()) {
      return nameIt->second;
    }
  }

  const int materialIndex = static_cast<int>(g_loadedMaterials.size());
  Asset::Material storedMaterial = material;
  RefreshMaterialRuntimeTexture(storedMaterial);
  g_loadedMaterials.push_back(std::move(storedMaterial));
  if (s_materialStableIds.size() < g_loadedMaterials.size()) {
    s_materialStableIds.resize(g_loadedMaterials.size());
  }
  if (!materialName.empty()) {
    s_materialIndicesByName[materialName] = materialIndex;
  }
  if (!normalizedStableId.empty()) {
    s_materialStableIds[static_cast<size_t>(materialIndex)] = normalizedStableId;
    s_materialIndicesByStableId[normalizedStableId] = materialIndex;
  }
  return materialIndex;
}

bool SetMaterialStableId(size_t index, const std::string &stableId) {
  if (index >= g_loadedMaterials.size()) {
    return false;
  }

  EnsureMaterialMetadataStorage();
  const std::string normalized = NormalizeMaterialStableId(stableId);
  if (normalized.empty()) {
    if (index < s_materialStableIds.size()) {
      const std::string previousStableId = s_materialStableIds[index];
      s_materialStableIds[index].clear();
      if (!previousStableId.empty()) {
        const auto it = s_materialIndicesByStableId.find(previousStableId);
        if (it != s_materialIndicesByStableId.end() &&
            it->second == static_cast<int>(index)) {
          s_materialIndicesByStableId.erase(it);
        }
      }
    }
    return true;
  }

  if (s_materialStableIds.size() < g_loadedMaterials.size()) {
    s_materialStableIds.resize(g_loadedMaterials.size());
  }
  s_materialStableIds[index] = normalized;
  s_materialIndicesByStableId[normalized] = static_cast<int>(index);
  return true;
}

bool RebindNodeMaterialSlot(size_t nodeIndex, size_t materialSlot,
                            int materialIndex) {
  if (nodeIndex >= s_nodes.size() || materialIndex < 0 ||
      materialIndex >= static_cast<int>(g_loadedMaterials.size())) {
    return false;
  }

  Node &node = s_nodes[nodeIndex];
  if (materialSlot >= node.linkedMaterialIndices.size()) {
    return false;
  }

  const int previousMaterialIndex = node.linkedMaterialIndices[materialSlot];
  if (previousMaterialIndex == materialIndex) {
    return true;
  }

  node.linkedMaterialIndices[materialSlot] = materialIndex;
  for (size_t meshIndex : node.meshIndices) {
    if (meshIndex >= g_loadedMeshes.size()) {
      continue;
    }
    if (g_loadedMeshes[meshIndex].materialSlot == static_cast<int>(materialSlot)) {
      g_loadedMeshes[meshIndex].materialIndex = materialIndex;
    }
  }

  ApplyRendererInvalidation(RendererInvalidationPlan::AccumulationOnly);
  NotifySceneChanged();
  return true;
}

bool UpdateNodeMaterialSourceName(size_t nodeIndex, size_t materialSlot,
                                  const std::string &materialName) {
  if (nodeIndex >= s_nodes.size()) {
    return false;
  }

  Node &node = s_nodes[nodeIndex];
  if (materialSlot >= node.linkedMaterialIndices.size()) {
    return false;
  }

  if (node.linkedMaterialSourceNames.size() < node.linkedMaterialIndices.size()) {
    node.linkedMaterialSourceNames.resize(node.linkedMaterialIndices.size());
  }

  if (node.linkedMaterialSourceNames[materialSlot].empty()) {
    node.linkedMaterialSourceNames[materialSlot] = materialName;
  }

  if (node.linkedMaterialSourceNames[materialSlot] == materialName) {
    return true;
  }

  return true;
}

bool UpdateMaterial(size_t index, const Asset::Material &material) {
  if (index >= g_loadedMaterials.size()) {
    return false;
  }

  Asset::Material &dst = g_loadedMaterials[index];
  Asset::Material updatedMaterial = material;
  RefreshMaterialRuntimeTexture(updatedMaterial);
  if (memcmp(&dst, &updatedMaterial, sizeof(Asset::Material)) == 0) {
    return true;
  }
  const std::string previousName = dst.name;
  dst = std::move(updatedMaterial);
  EnsureMaterialMetadataStorage();
  if (!previousName.empty()) {
    const auto previousNameIt = s_materialIndicesByName.find(previousName);
    if (previousNameIt != s_materialIndicesByName.end() &&
        previousNameIt->second == static_cast<int>(index)) {
      s_materialIndicesByName.erase(previousNameIt);
    }
  }
  const std::string updatedName = dst.name;
  if (!updatedName.empty()) {
    s_materialIndicesByName[updatedName] = static_cast<int>(index);
  }
  DxrRenderer::MarkMaterialDirty(static_cast<int>(index));
  ApplyRendererInvalidation(RendererInvalidationPlan::AccumulationOnly);
  NotifySceneChanged();
  return true;
}

void RefreshAllMaterialRuntimeTextures() {
  for (Asset::Material &material : g_loadedMaterials) {
    RefreshMaterialRuntimeTexture(material);
  }
}

void RemoveLight(size_t index) {
  if (index < s_lights.size()) {
    s_lights.erase(s_lights.begin() + index);
    LiveLink::GetSceneSync().ReindexSceneLightBindingsAfterRemoval(index);
    NotifySceneChanged();
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
      sprintf_s(buf, sizeof(buf), "Light %zu (%s)", i, typeStr);

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

  float windowX = 0.0f;
  float windowY = 0.0f;
  float windowWidth = 0.0f;
  float windowHeight = 0.0f;
  GetRenderViewportRect(&windowX, &windowY, &windowWidth, &windowHeight);
  ImDrawList *drawList =
      BeginRenderOverlayWindow("##LightViewportOverlay", windowX, windowY,
                               windowWidth, windowHeight);

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

    char label[64];
    snprintf(label, sizeof(label), "L%zu", i);
    drawList->AddText(ImVec2(screenPos.x + 8.0f, screenPos.y - 8.0f), col,
                      label);

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
  if (s_selectedLightIdx < 0 || s_selectedLightIdx >= (int)s_lights.size()) {
    ImGui::End();
    return;
  }

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

  ImGui::End();
}

void MatMul(const float *a, const float *b, float *out) {
  MulColumnMajor4x4(a, b, out);
}

bool Inverse4x4(const float *m, float *out);

static void ApplyNodeWorldTransform(
    size_t nodeIndex, const float newWorld[16],
    const std::vector<std::array<float, 16>> &worldTransforms) {
  if (nodeIndex >= s_nodes.size()) {
    return;
  }
  Node &node = s_nodes[nodeIndex];
  const size_t transformRootIndex = FindSceneTransformRootAncestor(nodeIndex);
  if (transformRootIndex != static_cast<size_t>(-1) &&
      transformRootIndex < worldTransforms.size()) {
    float invRootWorld[16];
    if (Inverse4x4(worldTransforms[transformRootIndex].data(),
                   invRootWorld)) {
      MatMul(invRootWorld, newWorld, node.transform);
      return;
    }
  }
  CopyMatrix4x4(newWorld, node.transform);
}

static bool ComputeSelectionPivot(
    const std::vector<size_t> &roots,
    const std::vector<std::array<float, 16>> &worldTransforms,
    float pivotMatrix[16]) {
  if (roots.empty() || roots.front() >= worldTransforms.size()) {
    return false;
  }

  CopyMatrix4x4(worldTransforms[roots.front()].data(), pivotMatrix);

  float center[3] = {0.0f, 0.0f, 0.0f};
  int centerCount = 0;
  for (size_t rootIndex : roots) {
    if (rootIndex >= s_nodes.size()) {
      continue;
    }
    for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
      if (!IsNodeDescendantOf(nodeIndex, rootIndex) ||
          nodeIndex >= worldTransforms.size()) {
        continue;
      }
      const Node &meshNode = s_nodes[nodeIndex];
      if (!meshNode.visible) {
        continue;
      }
      for (size_t meshIndex : meshNode.meshIndices) {
        if (meshIndex >= g_loadedMeshes.size()) {
          continue;
        }
        const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
        float meshLocalCenter[3] = {
            (mesh.minBound[0] + mesh.maxBound[0]) * 0.5f,
            (mesh.minBound[1] + mesh.maxBound[1]) * 0.5f,
            (mesh.minBound[2] + mesh.maxBound[2]) * 0.5f};
        float worldCenter[3];
        TransformPointColumnMajor(worldTransforms[nodeIndex].data(),
                                  meshLocalCenter, worldCenter);
        center[0] += worldCenter[0];
        center[1] += worldCenter[1];
        center[2] += worldCenter[2];
        ++centerCount;
      }
    }
  }

  if (centerCount > 0) {
    center[0] /= static_cast<float>(centerCount);
    center[1] /= static_cast<float>(centerCount);
    center[2] /= static_cast<float>(centerCount);
  } else {
    for (size_t rootIndex : roots) {
      if (rootIndex >= worldTransforms.size()) {
        continue;
      }
      const float *rootWorld = worldTransforms[rootIndex].data();
      center[0] += rootWorld[12];
      center[1] += rootWorld[13];
      center[2] += rootWorld[14];
      ++centerCount;
    }
    if (centerCount > 0) {
      center[0] /= static_cast<float>(centerCount);
      center[1] /= static_cast<float>(centerCount);
      center[2] /= static_cast<float>(centerCount);
    }
  }

  pivotMatrix[12] = center[0];
  pivotMatrix[13] = center[1];
  pivotMatrix[14] = center[2];
  return true;
}

static bool WorldToScreenPoint(const float worldPoint[3], const float view[16],
                               const float proj[16], float windowX,
                               float windowY, float windowWidth,
                               float windowHeight, ImVec2 &outScreen) {
  float viewPos[4];
  for (int i = 0; i < 4; i++) {
    viewPos[i] = worldPoint[0] * view[0 * 4 + i] +
                 worldPoint[1] * view[1 * 4 + i] +
                 worldPoint[2] * view[2 * 4 + i] + view[3 * 4 + i];
  }

  float clipPos[4];
  for (int i = 0; i < 4; i++) {
    clipPos[i] = viewPos[0] * proj[0 * 4 + i] +
                 viewPos[1] * proj[1 * 4 + i] +
                 viewPos[2] * proj[2 * 4 + i] +
                 viewPos[3] * proj[3 * 4 + i];
  }
  if (clipPos[3] < 0.001f) {
    return false;
  }

  outScreen.x =
      windowX + (clipPos[0] / clipPos[3] + 1.0f) * 0.5f * windowWidth;
  outScreen.y =
      windowY + (1.0f - clipPos[1] / clipPos[3]) * 0.5f * windowHeight;
  return true;
}

static bool ComputeSelectedRootWorldBounds(
    size_t rootIndex, const std::vector<std::array<float, 16>> &worldTransforms,
    float outMin[3], float outMax[3]) {
  if (rootIndex >= s_nodes.size()) {
    return false;
  }

  outMin[0] = outMin[1] = outMin[2] = FLT_MAX;
  outMax[0] = outMax[1] = outMax[2] = -FLT_MAX;
  bool hasBounds = false;

  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    if (!IsNodeDescendantOf(nodeIndex, rootIndex) ||
        nodeIndex >= worldTransforms.size()) {
      continue;
    }
    const Node &node = s_nodes[nodeIndex];
    if (!node.visible) {
      continue;
    }

    for (size_t meshIndex : node.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
      const float corners[8][3] = {
          {mesh.minBound[0], mesh.minBound[1], mesh.minBound[2]},
          {mesh.maxBound[0], mesh.minBound[1], mesh.minBound[2]},
          {mesh.maxBound[0], mesh.maxBound[1], mesh.minBound[2]},
          {mesh.minBound[0], mesh.maxBound[1], mesh.minBound[2]},
          {mesh.minBound[0], mesh.minBound[1], mesh.maxBound[2]},
          {mesh.maxBound[0], mesh.minBound[1], mesh.maxBound[2]},
          {mesh.maxBound[0], mesh.maxBound[1], mesh.maxBound[2]},
          {mesh.minBound[0], mesh.maxBound[1], mesh.maxBound[2]},
      };
      for (const auto &corner : corners) {
        float worldCorner[3];
        TransformPointColumnMajor(worldTransforms[nodeIndex].data(), corner,
                                  worldCorner);
        for (int axis = 0; axis < 3; ++axis) {
          outMin[axis] = (std::min)(outMin[axis], worldCorner[axis]);
          outMax[axis] = (std::max)(outMax[axis], worldCorner[axis]);
        }
        hasBounds = true;
      }
    }
  }

  return hasBounds;
}

static void DrawSelectedRootOutline(
    ImDrawList *drawList, size_t rootIndex,
    const std::vector<std::array<float, 16>> &worldTransforms,
    const float view[16], const float proj[16], float windowX, float windowY,
    float windowWidth, float windowHeight) {
  if (!drawList) {
    return;
  }

  float minB[3];
  float maxB[3];
  if (!ComputeSelectedRootWorldBounds(rootIndex, worldTransforms, minB, maxB)) {
    return;
  }

  const float corners[8][3] = {
      {minB[0], minB[1], minB[2]}, {maxB[0], minB[1], minB[2]},
      {maxB[0], maxB[1], minB[2]}, {minB[0], maxB[1], minB[2]},
      {minB[0], minB[1], maxB[2]}, {maxB[0], minB[1], maxB[2]},
      {maxB[0], maxB[1], maxB[2]}, {minB[0], maxB[1], maxB[2]},
  };

  ImVec2 screen[8];
  for (int i = 0; i < 8; ++i) {
    if (!WorldToScreenPoint(corners[i], view, proj, windowX, windowY,
                            windowWidth, windowHeight, screen[i])) {
      return;
    }
  }

  const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                            {4, 5}, {5, 6}, {6, 7}, {7, 4},
                            {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const ImU32 shadow = IM_COL32(0, 0, 0, 180);
  const ImU32 accent = IM_COL32(255, 210, 80, 255);
  for (const auto &edge : edges) {
    drawList->AddLine(screen[edge[0]], screen[edge[1]], shadow, 5.0f);
  }
  for (const auto &edge : edges) {
    drawList->AddLine(screen[edge[0]], screen[edge[1]], accent, 2.5f);
  }
}

void DrawGizmo() {
  std::vector<size_t> selectedRoots = GetSelectedTransformRoots();
  if (s_shiftCloneDrag.active &&
      !s_shiftCloneDrag.cloneRootIndices.empty()) {
    selectedRoots = s_shiftCloneDrag.cloneRootIndices;
  }

  // ImGuizmo::BeginFrame() called in main.cpp
  if (selectedRoots.empty())
    return;

  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);
  float windowX = 0.0f;
  float windowY = 0.0f;
  float windowWidth = 0.0f;
  float windowHeight = 0.0f;
  GetRenderViewportRect(&windowX, &windowY, &windowWidth, &windowHeight);
  ImDrawList *overlayDrawList =
      BeginRenderOverlayWindow("##SceneViewportOverlay", windowX, windowY,
                               windowWidth, windowHeight);

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

  const size_t gizmoId =
      s_shiftCloneDrag.active &&
              s_shiftCloneDrag.gizmoId != static_cast<size_t>(-1)
          ? s_shiftCloneDrag.gizmoId
          : selectedRoots.front();
  ImGuizmo::SetID((int)gizmoId);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(overlayDrawList);
  ImGuizmo::SetRect(windowX, windowY, windowWidth, windowHeight);

  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();

  for (size_t rootIndex : selectedRoots) {
    DrawSelectedRootOutline(overlayDrawList, rootIndex, worldTransforms, view,
                            proj, windowX, windowY, windowWidth,
                            windowHeight);
  }

  float pivotMatrix[16];
  if (!ComputeSelectionPivot(selectedRoots, worldTransforms, pivotMatrix)) {
    ImGui::End();
    return;
  }

  float originalPivotMatrix[16];
  CopyMatrix4x4(pivotMatrix, originalPivotMatrix);
  std::vector<std::array<float, 16>> originalRootWorlds;
  originalRootWorlds.reserve(selectedRoots.size());
  for (size_t rootIndex : selectedRoots) {
    std::array<float, 16> rootWorld{};
    if (rootIndex < worldTransforms.size()) {
      CopyMatrix4x4(worldTransforms[rootIndex].data(), rootWorld.data());
    }
    originalRootWorlds.push_back(rootWorld);
  }

  ImGuizmo::OPERATION op = GetActiveGizmoOperation();

  if (ImGuizmo::Manipulate(view, proj, op, actualMode,
                           pivotMatrix)) {
    if (IsShiftDown() && !s_shiftCloneDrag.active &&
        !s_shiftCloneDrag.optionsPending) {
      ClonedNodeSet clones = CloneNodesAsInstances(selectedRoots);
      if (!clones.rootIndices.empty()) {
        s_shiftCloneDrag.active = true;
        s_shiftCloneDrag.sawLeftMouseDown = IsLeftMouseDown();
        s_shiftCloneDrag.gizmoId = selectedRoots.front();
        s_shiftCloneDrag.cloneRootIndices = clones.rootIndices;
        s_shiftCloneDrag.cloneNodeIndices = clones.nodeIndices;
        selectedRoots = clones.rootIndices;
      }
    }

    float invOriginalPivot[16];
    if (Inverse4x4(originalPivotMatrix, invOriginalPivot)) {
      float delta[16];
      MatMul(pivotMatrix, invOriginalPivot, delta);

      const std::vector<std::array<float, 16>> currentWorldTransforms =
          BuildNodeWorldTransforms();
      for (size_t i = 0; i < selectedRoots.size() &&
                         i < originalRootWorlds.size();
           ++i) {
        float newWorld[16];
        MatMul(delta, originalRootWorlds[i].data(), newWorld);
        ApplyNodeWorldTransform(selectedRoots[i], newWorld,
                                currentWorldTransforms);
      }

      ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
    }
  }

  if (s_shiftCloneDrag.active && IsLeftMouseDown()) {
    s_shiftCloneDrag.sawLeftMouseDown = true;
  }
  if (s_shiftCloneDrag.active && s_shiftCloneDrag.sawLeftMouseDown &&
      !IsLeftMouseDown()) {
    s_shiftCloneDrag.active = false;
    s_shiftCloneDrag.optionsPending = true;
  }

  ImGui::End();
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
  float vpX = 0.0f;
  float vpY = 0.0f;
  float vpWidth = screenWidth;
  float vpHeight = screenHeight;
  GetRenderViewportRect(&vpX, &vpY, &vpWidth, &vpHeight);
  float mx = mposAbs.x - vpX;
  float my = mposAbs.y - vpY;
  if (mx < 0.0f || my < 0.0f || mx > vpWidth || my > vpHeight)
    return -1;

  // NDC [-1, 1]
  float ndcX = (mx / vpWidth) * 2.0f - 1.0f;
  float ndcY = 1.0f - (my / vpHeight) * 2.0f;

  // Build ray from the same camera basis used by DXR raygen.
  const float kPi = 3.14159265359f;
  float fovRad = g_cameraData.fov * (kPi / 180.0f);
  float tanHalfFov = tanf(fovRad * 0.5f);
  float aspect = (vpHeight > 0.0f) ? (vpWidth / vpHeight)
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

  float minWorldDist2 = FLT_MAX;
  int hitNode = -1;
  int hitMaterial = -1;
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();

  for (size_t i = 0; i < s_nodes.size(); ++i) {
    auto &node = s_nodes[i];
    if (!node.visible)
      continue;

    const float *nodeWorld = worldTransforms[i].data();
    float invNode[16];
    if (!Inverse4x4(nodeWorld, invNode))
      continue;

    // Transform ray to local space
    float localOrig[3], localDir[3];
    TransformPointColumnMajor(invNode, orig, localOrig);
    TransformVectorColumnMajor(invNode, dir, localDir);
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
          TransformPointColumnMajor(nodeWorld, localHit, worldHit);
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
        TransformPointColumnMajor(nodeWorld, localHit, worldHit);
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
    const size_t selectionTarget =
        ResolveSelectionTargetForHit(static_cast<size_t>(hitNode));
    if (IsCtrlDown()) {
      ToggleNodeSelection(selectionTarget);
    } else {
      SelectNode(selectionTarget);
    }
    fprintf(stderr, "Scene: Picked Node '%s' (ID %d), Material ID %d\n",
            s_nodes[selectionTarget].name.c_str(),
            static_cast<int>(selectionTarget), hitMaterial);
  } else if (!IsCtrlDown()) {
    SelectNode(static_cast<size_t>(-1));
  }

  return hitMaterial;
}

int PickMaterialAtCursor(float screenWidth, float screenHeight) {
  if (ImGuizmo::IsUsing()) {
    return -1;
  }

  if (screenWidth <= 1.0f || screenHeight <= 1.0f) {
    return -1;
  }

  ImVec2 mposAbs = ImGui::GetIO().MousePos;
  float vpX = 0.0f;
  float vpY = 0.0f;
  float vpWidth = screenWidth;
  float vpHeight = screenHeight;
  GetRenderViewportRect(&vpX, &vpY, &vpWidth, &vpHeight);
  float mx = mposAbs.x - vpX;
  float my = mposAbs.y - vpY;
  if (mx < 0.0f || my < 0.0f || mx > vpWidth || my > vpHeight) {
    return -1;
  }

  const float ndcX = (mx / vpWidth) * 2.0f - 1.0f;
  const float ndcY = 1.0f - (my / vpHeight) * 2.0f;
  const float kPi = 3.14159265359f;
  const float fovRad = g_cameraData.fov * (kPi / 180.0f);
  const float tanHalfFov = tanf(fovRad * 0.5f);
  const float aspect = (vpHeight > 0.0f) ? (vpWidth / vpHeight)
                                         : g_cameraData.aspect;

  float forward[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                      g_cameraData.forward[2]};
  float upHint[3] = {g_cameraData.up[0], g_cameraData.up[1],
                     g_cameraData.up[2]};

  auto Normalize3 = [](float v[3]) -> bool {
    float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1e-12f) {
      return false;
    }
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

  if (!Normalize3(forward)) {
    return -1;
  }
  float right[3];
  Cross3(forward, upHint, right);
  if (!Normalize3(right)) {
    return -1;
  }
  float up[3];
  Cross3(right, forward, up);
  if (!Normalize3(up)) {
    return -1;
  }

  const float xView = ndcX * aspect * tanHalfFov;
  const float yView = ndcY * tanHalfFov;
  float dir[3] = {xView * right[0] + yView * up[0] + forward[0],
                  xView * right[1] + yView * up[1] + forward[1],
                  xView * right[2] + yView * up[2] + forward[2]};
  if (!Normalize3(dir)) {
    return -1;
  }

  float orig[3] = {g_cameraData.pos[0], g_cameraData.pos[1],
                   g_cameraData.pos[2]};

  float minWorldDist2 = FLT_MAX;
  int hitNode = -1;
  int hitMaterial = -1;
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();

  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    const Node &node = s_nodes[nodeIndex];
    if (!node.visible) {
      continue;
    }

    const float *nodeWorld = worldTransforms[nodeIndex].data();
    float invNode[16];
    if (!Inverse4x4(nodeWorld, invNode)) {
      continue;
    }

    float localOrig[3], localDir[3];
    TransformPointColumnMajor(invNode, orig, localOrig);
    TransformVectorColumnMajor(invNode, dir, localDir);
    const float localDirLen2 = localDir[0] * localDir[0] +
                               localDir[1] * localDir[1] +
                               localDir[2] * localDir[2];
    if (localDirLen2 <= 1e-12f) {
      continue;
    }

    for (size_t meshIndex : node.meshIndices) {
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }
      const Asset::GpuMesh &mesh = g_loadedMeshes[meshIndex];
      if (mesh.materialIndex < 0 ||
          mesh.materialIndex >= static_cast<int>(g_loadedMaterials.size())) {
        continue;
      }

      float boxT = 1e30f;
      if (!RayAABBIntersection(localOrig, localDir, mesh.minBound,
                               mesh.maxBound, boxT)) {
        continue;
      }

      float localHit[3] = {localOrig[0] + localDir[0] * boxT,
                           localOrig[1] + localDir[1] * boxT,
                           localOrig[2] + localDir[2] * boxT};
      float worldHit[3];
      TransformPointColumnMajor(nodeWorld, localHit, worldHit);
      const float dx = worldHit[0] - orig[0];
      const float dy = worldHit[1] - orig[1];
      const float dz = worldHit[2] - orig[2];
      const float worldDist2 = dx * dx + dy * dy + dz * dz;
      if (worldDist2 < minWorldDist2) {
        minWorldDist2 = worldDist2;
        hitNode = static_cast<int>(nodeIndex);
        hitMaterial = mesh.materialIndex;
      }
    }
  }

  if (hitNode >= 0) {
    SelectNode(static_cast<size_t>(hitNode));
  }

  return hitMaterial;
}

void DrawScenePanel(HWND hwnd, bool &visible) {
  if (!visible)
    return;
  if (ImGui::Begin("Scene", &visible)) {
    bool uiChanged = false;
    ProcessPendingImport();

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

        auto drawNodeRow = [&](size_t index, bool indentChild) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);

          ImGui::PushID((int)index);
          if (indentChild) {
            ImGui::Indent();
          }

          bool selected = s_nodes[index].selected;
          if (ImGui::Selectable(s_nodes[index].name.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
            if (IsCtrlDown()) {
              ToggleNodeSelection(index);
            } else {
              SelectNode(index);
            }
          }

          if (indentChild) {
            ImGui::Unindent();
          }

          ImGui::TableSetColumnIndex(1);
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
          if (ImGui::Button("Delete", ImVec2(-FLT_MIN, 0))) {
            DeleteNode(index);
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            ImGui::EndTable();
            ImGui::EndChild();
            ImGui::End();
            return false;
          }
          ImGui::PopStyleColor(2);
          ImGui::PopID();
          return true;
        };

        auto isGroupRoot = [&](size_t index, bool liveLinkGroup) {
          if (index >= s_nodes.size() || s_nodes[index].liveLinkManaged != liveLinkGroup) {
            return false;
          }
          const size_t parentIndex = s_nodes[index].parentIndex;
          return parentIndex == static_cast<size_t>(-1) ||
                 parentIndex >= s_nodes.size() ||
                 s_nodes[parentIndex].liveLinkManaged != liveLinkGroup;
        };

        std::function<bool(size_t, bool)> drawHierarchyRecursive;
        drawHierarchyRecursive = [&](size_t index, bool indentChild) {
          if (!drawNodeRow(index, indentChild)) {
            return false;
          }
          for (size_t childIndex = 0; childIndex < s_nodes.size(); ++childIndex) {
            if (s_nodes[childIndex].parentIndex != index ||
                s_nodes[childIndex].liveLinkManaged != s_nodes[index].liveLinkManaged) {
              continue;
            }
            if (!drawHierarchyRecursive(childIndex, true)) {
              return false;
            }
          }
          return true;
        };

        std::vector<size_t> regularNodeIndices;
        std::vector<size_t> liveLinkNodeIndices;
        regularNodeIndices.reserve(s_nodes.size());
        liveLinkNodeIndices.reserve(s_nodes.size());
        for (size_t i = 0; i < s_nodes.size(); ++i) {
          if (s_nodes[i].liveLinkManaged) {
            if (isGroupRoot(i, true)) {
              liveLinkNodeIndices.push_back(i);
            }
          } else if (isGroupRoot(i, false)) {
            regularNodeIndices.push_back(i);
          }
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID("LiveSyncRoot");
        const std::string rootLabel =
            "Live Sync (" + std::to_string(liveLinkNodeIndices.size()) + ")";
        ImGuiTreeNodeFlags rootFlags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (liveLinkNodeIndices.empty()) {
          rootFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const bool rootOpen = ImGui::TreeNodeEx(rootLabel.c_str(), rootFlags);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Group");

        if (!liveLinkNodeIndices.empty() && rootOpen) {
          for (size_t index : liveLinkNodeIndices) {
            if (!drawHierarchyRecursive(index, true)) {
              ImGui::TreePop();
              ImGui::PopID();
              return;
            }
          }
          ImGui::TreePop();
        }
        ImGui::PopID();

        for (size_t index : regularNodeIndices) {
          if (!drawHierarchyRecursive(index, false)) {
            return;
          }
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
  PrepareForDestructiveMeshMutation();
  s_nodes.clear();
  g_loadedMeshes.clear();
  g_loadedMaterials.clear();
  g_loadedTextures.clear();
  s_materialStableIds.clear();
  s_materialIndicesByStableId.clear();
  s_materialIndicesByName.clear();
  s_materialMetadataDirty = false;
  s_sharedImportedMeshesBySourcePath.clear();
  s_textureIndicesBySourceUri.clear();
  s_lights.clear();
  s_selectedLightIdx = -1;
  AnimationSequence::Clear();
  SavedViews::Clear();
  g_textureDescriptorCount = 0;
  // Note: In a full implementation, we should also release GPU
  // resources/descriptors and reset the IBL manager, but for now this clears
  // the CPU state which is then rebuilt by LoadScene.

  // Reset DXR state if it's active
  UpdateLights();
  DxrRenderer::ResetAccumulation();
  // Ensure camera/exposure defaults are restored when starting a fresh scene
  DxrRenderer::SetAutoExposure(false);
  DxrRenderer::SetPhysicalCameraExposure(true);
  DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 125.0f, 16.0f);
  NotifySceneChanged();
}

} // namespace Scene
