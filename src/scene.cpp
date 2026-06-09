#define NOMINMAX
#include "scene.h"
#include "scene_internal.h"
#include "animation_sequence.h"
#include "saved_views.h"
#include "ImGuizmo.h"
#include "asset_library/asset_cooker.h"
#include "asset_library/asset_runtime.h"
#include "asset_library/cook_jobs.h"
#include "asset_library/global_registry.h"
#include "asset_library/import_hook.h"
#include "assets/asset_loader.h"
#include "camera.h"
#include "volumetric_renderer.h"
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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
static std::vector<LightPrototype> s_lightPrototypes;
static std::vector<LightInstance> s_lightInstances;
static std::vector<Light> s_flattenedLights;
static std::vector<std::pair<size_t, size_t>> s_lightFlattenMapping;
static std::vector<IESProfile> s_iesProfiles;
// Scatter state lives in scatter.cpp (see scatter.h API). scene.cpp talks to
// it only via Scene:: public API plus the scatter-side hooks declared in
// scatter.h (AppendScatterInstances, ReindexScatterNodeReferencesAfterRemoval,
// RemapScatterTargetMaterialIndices, OnSceneStateChanged).
static LightPlacementMode s_lightPlacementMode = LightPlacementMode::None;
static LightType s_lightPlacementCreateType = LightType::Omni;
static int s_lightPlacementCreateIesProfile = -1;
static int s_lightPlacementMoveInstance = -1;
static bool s_lightGizmosVisible = true;
// Import progress & pending results (for async import)
static std::atomic<bool> s_importInProgress(false);
static std::atomic<float> s_importProgress(0.0f);
static std::string s_importStatus;
static std::mutex s_importStatusMutex;
static std::mutex s_recentImportMutex;
static std::string s_recentImportPath;
static std::chrono::steady_clock::time_point s_recentImportTime;

enum class PendingImportAction {
  Import,
  Reimport,
};

static std::vector<Asset::GpuMesh> s_pendingMeshes;
static std::vector<Asset::Material> s_pendingMaterials;
static std::vector<Asset::Texture> s_pendingTextures;
static std::vector<Asset::ImportedSceneNode> s_pendingSceneNodes;
static std::array<float, 3> s_pendingRootTranslation = {};
static bool s_pendingHasRootTranslation = false;
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

static const char *LightTypeLabel(LightType type) {
  switch (type) {
  case LightType::Directional: return "Sun";
  case LightType::Omni: return "Point";
  case LightType::Spot: return "Spot";
  case LightType::AreaRect: return "Rect";
  case LightType::AreaDisk: return "Disk";
  case LightType::IES: return "IES";
  }
  return "Light";
}

static void EnsureLightPrototypeName(LightPrototype &proto, size_t index) {
  if (proto.name[0] != '\0') {
    proto.name[sizeof(proto.name) - 1] = '\0';
    return;
  }
  std::snprintf(proto.name, sizeof(proto.name), "%s %zu",
                LightTypeLabel(static_cast<LightType>(proto.type)),
                index + 1);
}

static bool LightPrototypeSharedFieldsEqual(const LightPrototype &a,
                                            const LightPrototype &b) {
  LightPrototype lhs = a;
  LightPrototype rhs = b;
  std::memset(lhs.name, 0, sizeof(lhs.name));
  std::memset(rhs.name, 0, sizeof(rhs.name));
  return std::memcmp(&lhs, &rhs, sizeof(LightPrototype)) == 0;
}

static std::wstring WidePathFromUtf8(const std::string &path) {
  if (path.empty()) {
    return {};
  }

  int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      path.data(),
                                      static_cast<int>(path.size()), nullptr,
                                      0);
  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (wideCount <= 0) {
    // Older saved scenes may contain paths encoded with the process ANSI code
    // page. Keep those reimportable while treating new paths as UTF-8.
    codePage = CP_ACP;
    flags = 0;
    wideCount = MultiByteToWideChar(codePage, flags, path.data(),
                                    static_cast<int>(path.size()), nullptr, 0);
  }
  if (wideCount <= 0) {
    return {};
  }

  std::wstring widePath(static_cast<size_t>(wideCount), L'\0');
  MultiByteToWideChar(codePage, flags, path.data(),
                      static_cast<int>(path.size()), widePath.data(),
                      wideCount);
  return widePath;
}

static fs::path NativePathFromStoredPath(const std::string &path) {
  std::wstring widePath = WidePathFromUtf8(path);
  return widePath.empty() ? fs::path(path) : fs::path(widePath);
}

static bool StoredPathExists(const std::string &path) {
  std::error_code ec;
  return fs::exists(NativePathFromStoredPath(path), ec);
}
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
static SelectionToolMode s_selectionToolMode = SelectionToolMode::Pointer;
static SelectionFilter s_selectionFilter = SelectionFilter::MeshesAndLights;
static int s_selectedLightIdx = -1;

struct TransformNodeHistory {
  size_t nodeIndex = static_cast<size_t>(-1);
  std::string nodeName;
  std::array<float, 16> before{};
  std::array<float, 16> after{};
};

struct TransformHistoryEntry {
  std::vector<TransformNodeHistory> nodes;
};

struct ActiveTransformEdit {
  bool active = false;
  std::vector<TransformNodeHistory> nodes;
};

static constexpr size_t kMaxTransformHistoryEntries = 128;
static std::vector<TransformHistoryEntry> s_transformUndoStack;
static std::vector<TransformHistoryEntry> s_transformRedoStack;
static ActiveTransformEdit s_activeTransformEdit;
static std::string s_lastStatus;

struct ShiftCloneDragState {
  bool active = false;
  bool optionsPending = false;
  bool sawLeftMouseDown = false;
  int releaseFramesArmed = 0;
  size_t gizmoId = static_cast<size_t>(-1);
  std::vector<size_t> cloneRootIndices;
  std::vector<size_t> cloneNodeIndices;
  std::vector<size_t> cloneLightIndices;
};

static ShiftCloneDragState s_shiftCloneDrag;

static void SelectNodesAndLights(const std::vector<size_t> &nodeIndices,
                                 const std::vector<size_t> &lightIndices);

static bool ShiftCloneHasMeshChoice() {
  return !s_shiftCloneDrag.cloneNodeIndices.empty();
}

static void SelectShiftCloneResult() {
  SelectNodesAndLights(s_shiftCloneDrag.cloneRootIndices,
                       s_shiftCloneDrag.cloneLightIndices);
}

static void EnsureGpuBuffersForMeshes(std::vector<Asset::GpuMesh> &meshes);
bool Inverse4x4(const float *m, float *out);
void MatMul(const float *a, const float *b, float *out);

void SetGizmoOperation(GizmoOperation operation) {
  switch (operation) {
  case GizmoOperation::Translate:
    g_currentGizmoOp = ImGuizmo::TRANSLATE;
    break;
  case GizmoOperation::Rotate:
    g_currentGizmoOp = ImGuizmo::ROTATE;
    break;
  case GizmoOperation::Scale:
    g_currentGizmoOp = ImGuizmo::SCALE;
    break;
  }
}

GizmoOperation GetGizmoOperation() {
  if (g_currentGizmoOp == ImGuizmo::ROTATE) {
    return GizmoOperation::Rotate;
  }
  if (g_currentGizmoOp == ImGuizmo::SCALE) {
    return GizmoOperation::Scale;
  }
  return GizmoOperation::Translate;
}

void SetGizmoSpace(GizmoSpace space) {
  g_currentGizmoMode =
      (space == GizmoSpace::World) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

GizmoSpace GetGizmoSpace() {
  return (g_currentGizmoMode == ImGuizmo::WORLD) ? GizmoSpace::World
                                                 : GizmoSpace::Local;
}

void SetSelectionToolMode(SelectionToolMode mode) {
  s_selectionToolMode = mode;
}

SelectionToolMode GetSelectionToolMode() { return s_selectionToolMode; }

void SetSelectionFilter(SelectionFilter filter) {
  s_selectionFilter = filter;
}

SelectionFilter GetSelectionFilter() { return s_selectionFilter; }

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

// RendererInvalidationPlan now lives in scene_internal.h so scatter.cpp can
// participate in the same invalidation taxonomy.

static int s_batchedUpdateDepth = 0;
static RendererInvalidationPlan s_batchedInvalidationPlan =
    RendererInvalidationPlan::None;
static bool s_batchedLightsDirty = false;
static bool s_batchedSceneChanged = false;
static bool s_batchedGpuIdleSatisfied = false;
static bool s_batchedMeshUploadDirty = false;
static bool s_batchedIESAtlasDirty = false;
static bool s_rebuildingIESAtlas = false;
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

void NotifySceneChanged() {
  // Inform scatter so its instance cache treats the scene state as dirty.
  OnSceneStateChanged();
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
  DxrRenderer::MarkTextureDescriptorTableDirty();
  return true;
}

static Asset::TextureUsageSemantic MergeTextureSemantic(
    Asset::TextureUsageSemantic existing,
    Asset::TextureUsageSemantic incoming) {
  using Semantic = Asset::TextureUsageSemantic;
  if (existing == Semantic::Unknown) {
    return incoming;
  }
  if (incoming == Semantic::Unknown || incoming == existing) {
    return existing;
  }
  const auto priority = [](Semantic semantic) {
    switch (semantic) {
    case Semantic::Normal:
      return 70;
    case Semantic::PackedSurface:
      return 60;
    case Semantic::ColorAlpha:
      return 50;
    case Semantic::Color:
    case Semantic::Emissive:
      return 40;
    case Semantic::Scalar:
      return 30;
    case Semantic::Hdr:
      return 20;
    default:
      return 0;
    }
  };
  return priority(incoming) > priority(existing) ? incoming : existing;
}

static void MarkTextureSemantic(std::vector<Asset::TextureUsageSemantic> &usage,
                                int textureIndex,
                                Asset::TextureUsageSemantic semantic) {
  if (textureIndex < 0 ||
      textureIndex >= static_cast<int>(usage.size())) {
    return;
  }
  usage[static_cast<size_t>(textureIndex)] =
      MergeTextureSemantic(usage[static_cast<size_t>(textureIndex)], semantic);
}

static Asset::TextureUsageSemantic TextureSemanticFromMaterialAlpha(
    const Asset::Material &material) {
  if (material.alphaMode != "OPAQUE" || material.opacityTexture >= 0) {
    return Asset::TextureUsageSemantic::ColorAlpha;
  }
  return Asset::TextureUsageSemantic::Color;
}

static void RefreshTextureCompressionForMaterials(bool resetAccumulation) {
  if (g_loadedTextures.empty()) {
    return;
  }

  std::vector<Asset::TextureUsageSemantic> usage(
      g_loadedTextures.size(), Asset::TextureUsageSemantic::Unknown);
  for (const Asset::Material &material : g_loadedMaterials) {
    MarkTextureSemantic(usage, material.diffuseTexture,
                        TextureSemanticFromMaterialAlpha(material));
    MarkTextureSemantic(usage, material.opacityTexture,
                        Asset::TextureUsageSemantic::Scalar);
    MarkTextureSemantic(usage, material.normalTexture,
                        Asset::TextureUsageSemantic::Normal);
    MarkTextureSemantic(usage, material.coatNormalTexture,
                        Asset::TextureUsageSemantic::Normal);
    MarkTextureSemantic(usage, material.occlusionTexture,
                        Asset::TextureUsageSemantic::Scalar);
    MarkTextureSemantic(usage, material.emissiveTexture,
                        Asset::TextureUsageSemantic::Emissive);
    MarkTextureSemantic(usage, material.metalRoughTexture,
                        Asset::TextureUsageSemantic::PackedSurface);
    MarkTextureSemantic(usage, material.runtimeMetalRoughTexture,
                        Asset::TextureUsageSemantic::PackedSurface);
    MarkTextureSemantic(usage, material.metalnessTexture,
                        Asset::TextureUsageSemantic::Scalar);
    MarkTextureSemantic(usage, material.roughnessGlossTexture,
                        Asset::TextureUsageSemantic::Scalar);
    MarkTextureSemantic(usage, material.specularColorTexture,
                        Asset::TextureUsageSemantic::Color);
    MarkTextureSemantic(usage, material.thicknessTexture,
                        Asset::TextureUsageSemantic::Scalar);
    MarkTextureSemantic(usage, material.parallaxTexture,
                        Asset::TextureUsageSemantic::Scalar);
  }

  bool changed = false;
  for (size_t textureIndex = 0; textureIndex < g_loadedTextures.size();
       ++textureIndex) {
    Asset::Texture &texture = g_loadedTextures[textureIndex];
    const Asset::TextureUsageSemantic semantic = usage[textureIndex];
    if (!texture.resource || semantic == Asset::TextureUsageSemantic::Unknown) {
      continue;
    }
    const DXGI_FORMAT oldFormat = texture.format;
    const UINT oldMipLevels = texture.mipLevels;
    const bool oldCompressed = texture.gpuCompressed;
    if (Asset::ApplyTextureCompressionForUsage(texture, semantic) &&
        (texture.format != oldFormat || texture.mipLevels != oldMipLevels ||
         texture.gpuCompressed != oldCompressed)) {
      WriteTextureSrv(static_cast<UINT>(textureIndex), texture);
      changed = true;
    }
  }

  if (changed) {
    DxrRenderer::MarkTextureDescriptorTableDirty();
    if (resetAccumulation) {
      DxrRenderer::ResetAccumulation();
    }
  }
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
    Asset::ApplyTextureCompressionForUsage(
        derived, Asset::TextureUsageSemantic::PackedSurface);
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
    Asset::ApplyTextureCompressionForUsage(
        g_loadedTextures[static_cast<size_t>(newTextureIndex)],
        Asset::TextureUsageSemantic::PackedSurface);
    WriteTextureSrv(static_cast<UINT>(newTextureIndex),
                    g_loadedTextures[static_cast<size_t>(newTextureIndex)]);
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
  return AddTextureFromFile(utf8path, isHDR,
                            isHDR ? Asset::TextureUsageSemantic::Hdr
                                  : Asset::TextureUsageSemantic::Unknown);
}

int AddTextureFromFile(const std::string &utf8path, bool isHDR,
                       Asset::TextureUsageSemantic semantic) {
  if (!g_device) {
    fprintf(stderr, "AddTextureFromFile: no device\n");
    return -1;
  }
  // Serialize with renderer work to avoid re-entrant queue activity while
  // modal dialogs are active and the window may also be resizing.
  WaitGPUIdle();

  Asset::Texture tex = Asset::LoadTextureFromFile(utf8path, isHDR, semantic);
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

static bool BuildSceneCameraPerspectiveBasis(float forward[3],
                                             const float upHint[3],
                                             float right[3], float up[3],
                                             float &verticalCenterShift) {
  auto Normalize3 = [](float v[3]) -> bool {
    const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1.0e-12f)
      return false;
    const float invLen = 1.0f / sqrtf(len2);
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

  verticalCenterShift = 0.0f;
  if (!Normalize3(forward))
    return false;
  Cross3(forward, upHint, right);
  if (!Normalize3(right))
    return false;
  Cross3(right, forward, up);
  if (!Normalize3(up))
    return false;

  if (g_cameraData.verticalTiltCorrection <= 0.5f)
    return true;

  const float pitchedForward[3] = {forward[0], forward[1], forward[2]};
  float levelForward[3] = {forward[0], 0.0f, forward[2]};
  if (!Normalize3(levelForward))
    return true;
  const float worldUp[3] = {0.0f, 1.0f, 0.0f};
  Cross3(levelForward, worldUp, right);
  if (!Normalize3(right))
    return false;
  Cross3(right, levelForward, up);
  if (!Normalize3(up))
    return false;
  forward[0] = levelForward[0];
  forward[1] = levelForward[1];
  forward[2] = levelForward[2];
  const float numerator = pitchedForward[0] * up[0] +
                          pitchedForward[1] * up[1] +
                          pitchedForward[2] * up[2];
  const float denominator = std::max(
      0.025f, pitchedForward[0] * forward[0] +
                  pitchedForward[1] * forward[1] +
                  pitchedForward[2] * forward[2]);
  verticalCenterShift = std::clamp(numerator / denominator, -40.0f, 40.0f);
  return true;
}

// Helper: Simple matrix math for ImGuizmo
void BuildViewMatrix(float *mat) {
  float pos[3] = {g_cameraData.pos[0], g_cameraData.pos[1],
                  g_cameraData.pos[2]};
  float fwd[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                  g_cameraData.forward[2]};
  const float upHint[3] = {g_cameraData.up[0], g_cameraData.up[1],
                           g_cameraData.up[2]};
  float R[3] = {};
  float U[3] = {};
  float verticalCenterShift = 0.0f;
  if (!BuildSceneCameraPerspectiveBasis(fwd, upHint, R, U,
                                        verticalCenterShift)) {
    return;
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
  float fwd[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                  g_cameraData.forward[2]};
  const float upHint[3] = {g_cameraData.up[0], g_cameraData.up[1],
                           g_cameraData.up[2]};
  float R[3] = {};
  float U[3] = {};
  float verticalCenterShift = 0.0f;
  BuildSceneCameraPerspectiveBasis(fwd, upHint, R, U, verticalCenterShift);

  memset(mat, 0, 16 * sizeof(float));
  mat[0] = focalScale / aspect;
  mat[5] = focalScale;
  mat[9] = -verticalCenterShift * focalScale;
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
    if (m.parallaxTexture >= 0)
      m.parallaxTexture += (int)textureBase;
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
    RemapMaterialTextureIndex(material.opacityTexture, textureRemap);
    RemapMaterialTextureIndex(material.normalTexture, textureRemap);
    RemapMaterialTextureIndex(material.coatNormalTexture, textureRemap);
    RemapMaterialTextureIndex(material.occlusionTexture, textureRemap);
    RemapMaterialTextureIndex(material.emissiveTexture, textureRemap);
    RemapMaterialTextureIndex(material.metalRoughTexture, textureRemap);
    RemapMaterialTextureIndex(material.runtimeMetalRoughTexture, textureRemap);
    RemapMaterialTextureIndex(material.metalnessTexture, textureRemap);
    RemapMaterialTextureIndex(material.roughnessGlossTexture, textureRemap);
    RemapMaterialTextureIndex(material.specularColorTexture, textureRemap);
    RemapMaterialTextureIndex(material.thicknessTexture, textureRemap);
    RemapMaterialTextureIndex(material.parallaxTexture, textureRemap);
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
static bool MergeMissingParallaxMaterialChannels(Asset::Material &existing,
                                                 const Asset::Material &incoming);
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

  if (s_batchedIESAtlasDirty) {
    RebuildIESAtlas();
    s_batchedIESAtlasDirty = false;
  }

  if (s_batchedLightsDirty) {
    const bool resetAccumulation =
        s_batchedInvalidationPlan == RendererInvalidationPlan::None;
    DxrRenderer::UpdateLights(s_flattenedLights, resetAccumulation);
    s_batchedLightsDirty = false;
  }

  if (s_batchedInvalidationPlan != RendererInvalidationPlan::None) {
    ExecuteRendererInvalidation(s_batchedInvalidationPlan);
    s_batchedInvalidationPlan = RendererInvalidationPlan::None;
  }
}

void ApplyRendererInvalidation(RendererInvalidationPlan plan) {
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

static bool MatrixEquals(const std::array<float, 16> &a,
                         const std::array<float, 16> &b) {
  return memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static bool CaptureNodeTransform(size_t nodeIndex,
                                 TransformNodeHistory *outSnapshot) {
  if (!outSnapshot || nodeIndex >= s_nodes.size()) {
    return false;
  }
  outSnapshot->nodeIndex = nodeIndex;
  outSnapshot->nodeName = s_nodes[nodeIndex].name;
  CopyMatrix4x4(s_nodes[nodeIndex].transform, outSnapshot->before.data());
  outSnapshot->after = outSnapshot->before;
  return true;
}

static void PushTransformHistoryEntry(TransformHistoryEntry entry) {
  entry.nodes.erase(
      std::remove_if(entry.nodes.begin(), entry.nodes.end(),
                     [](const TransformNodeHistory &node) {
                       return MatrixEquals(node.before, node.after);
                     }),
      entry.nodes.end());
  if (entry.nodes.empty()) {
    return;
  }

  s_transformUndoStack.push_back(std::move(entry));
  if (s_transformUndoStack.size() > kMaxTransformHistoryEntries) {
    s_transformUndoStack.erase(s_transformUndoStack.begin());
  }
  s_transformRedoStack.clear();
}

static void BeginTransformHistoryEdit(const std::vector<size_t> &nodeIndices) {
  if (s_activeTransformEdit.active || nodeIndices.empty()) {
    return;
  }

  s_activeTransformEdit.nodes.clear();
  s_activeTransformEdit.nodes.reserve(nodeIndices.size());
  for (size_t nodeIndex : nodeIndices) {
    TransformNodeHistory snapshot;
    if (CaptureNodeTransform(nodeIndex, &snapshot)) {
      s_activeTransformEdit.nodes.push_back(std::move(snapshot));
    }
  }
  s_activeTransformEdit.active = !s_activeTransformEdit.nodes.empty();
}

static void CancelTransformHistoryEdit() {
  s_activeTransformEdit = {};
}

static void CommitTransformHistoryEdit() {
  if (!s_activeTransformEdit.active) {
    return;
  }

  TransformHistoryEntry entry;
  entry.nodes = std::move(s_activeTransformEdit.nodes);
  s_activeTransformEdit = {};

  for (TransformNodeHistory &node : entry.nodes) {
    if (node.nodeIndex < s_nodes.size() &&
        s_nodes[node.nodeIndex].name == node.nodeName) {
      CopyMatrix4x4(s_nodes[node.nodeIndex].transform, node.after.data());
    } else {
      node.after = node.before;
    }
  }
  PushTransformHistoryEntry(std::move(entry));
}

static bool IsTransformHistoryEntryValid(const TransformHistoryEntry &entry) {
  return std::all_of(entry.nodes.begin(), entry.nodes.end(),
                     [](const TransformNodeHistory &node) {
                       return node.nodeIndex < s_nodes.size() &&
                              s_nodes[node.nodeIndex].name == node.nodeName;
                     });
}

static void ApplyTransformHistoryEntry(const TransformHistoryEntry &entry,
                                       bool useAfter) {
  for (const TransformNodeHistory &node : entry.nodes) {
    if (node.nodeIndex >= s_nodes.size()) {
      continue;
    }
    const std::array<float, 16> &matrix = useAfter ? node.after : node.before;
    CopyMatrix4x4(matrix.data(), s_nodes[node.nodeIndex].transform);
  }
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  NotifySceneChanged();
}

bool CanUndoTransform() { return !s_transformUndoStack.empty(); }

bool CanRedoTransform() { return !s_transformRedoStack.empty(); }

bool UndoTransform() {
  CommitTransformHistoryEdit();
  while (!s_transformUndoStack.empty()) {
    TransformHistoryEntry entry = std::move(s_transformUndoStack.back());
    s_transformUndoStack.pop_back();
    if (!IsTransformHistoryEntryValid(entry)) {
      continue;
    }
    ApplyTransformHistoryEntry(entry, false);
    s_transformRedoStack.push_back(std::move(entry));
    s_lastStatus = "Undo transform";
    return true;
  }
  return false;
}

bool RedoTransform() {
  CommitTransformHistoryEdit();
  while (!s_transformRedoStack.empty()) {
    TransformHistoryEntry entry = std::move(s_transformRedoStack.back());
    s_transformRedoStack.pop_back();
    if (!IsTransformHistoryEntryValid(entry)) {
      continue;
    }
    ApplyTransformHistoryEntry(entry, true);
    s_transformUndoStack.push_back(std::move(entry));
    s_lastStatus = "Redo transform";
    return true;
  }
  return false;
}

void ClearTransformHistory() {
  s_transformUndoStack.clear();
  s_transformRedoStack.clear();
  s_activeTransformEdit = {};
}

void MulColumnMajor4x4(const float *a, const float *b, float *out) {
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

std::vector<std::array<float, 16>> BuildNodeWorldTransforms() {
  std::vector<std::array<float, 16>> worldTransforms(s_nodes.size());
  std::vector<uint8_t> visitState(s_nodes.size(), 0);
  for (size_t i = 0; i < s_nodes.size(); ++i) {
    ResolveNodeWorldTransform(i, worldTransforms, visitState);
  }
  return worldTransforms;
}

bool IsNodeDescendantOf(size_t nodeIndex, size_t ancestorIndex) {
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
  return ImGuizmo::IsUsing() || io.MouseDown[0] ||
         ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
         ((GetKeyState(VK_LBUTTON) & 0x8000) != 0);
}

std::vector<size_t> GetSelectedNodeIndices() {
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

static void SelectNodesAndLights(const std::vector<size_t> &nodeIndices,
                                 const std::vector<size_t> &lightIndices) {
  for (Node &node : s_nodes) {
    node.selected = false;
  }
  for (LightInstance &inst : s_lightInstances) {
    inst.selected = false;
  }

  for (size_t index : nodeIndices) {
    if (index < s_nodes.size()) {
      s_nodes[index].selected = true;
    }
  }

  s_selectedLightIdx = -1;
  for (size_t index : lightIndices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    s_lightInstances[index].selected = true;
    s_selectedLightIdx = static_cast<int>(index);
  }

  NotifySceneChanged();
}

static void SelectOnlyNodes(const std::vector<size_t> &indices) {
  SelectNodesAndLights(indices, {});
}

static void ToggleNodeSelection(size_t index) {
  if (index >= s_nodes.size()) {
    return;
  }
  s_nodes[index].selected = !s_nodes[index].selected;
  s_selectedLightIdx = -1;
  for (LightInstance &inst : s_lightInstances) {
    inst.selected = false;
  }
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

static void AddUniqueInt(std::vector<int> &values, int value) {
  if (value < 0) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

static void AddUniqueSize(std::vector<size_t> &values, size_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

static void CollectMaterialTextureCandidates(
    const Asset::Material &material, std::vector<size_t> &textureIndices) {
  auto addTexture = [&](int textureIndex) {
    if (textureIndex >= 0 &&
        textureIndex < static_cast<int>(g_loadedTextures.size())) {
      AddUniqueSize(textureIndices, static_cast<size_t>(textureIndex));
    }
  };

  addTexture(material.diffuseTexture);
  addTexture(material.opacityTexture);
  addTexture(material.normalTexture);
  addTexture(material.coatNormalTexture);
  addTexture(material.emissiveTexture);
  addTexture(material.occlusionTexture);
  addTexture(material.metalRoughTexture);
  addTexture(material.runtimeMetalRoughTexture);
  addTexture(material.metalnessTexture);
  addTexture(material.roughnessGlossTexture);
  addTexture(material.specularColorTexture);
  addTexture(material.thicknessTexture);
  addTexture(material.parallaxTexture);
}

static void RemapMaterialTextureIndicesInPlace(
    Asset::Material &material, const std::vector<int> &textureRemap) {
  RemapMaterialTextureIndex(material.diffuseTexture, textureRemap);
  RemapMaterialTextureIndex(material.opacityTexture, textureRemap);
  RemapMaterialTextureIndex(material.normalTexture, textureRemap);
  RemapMaterialTextureIndex(material.coatNormalTexture, textureRemap);
  RemapMaterialTextureIndex(material.emissiveTexture, textureRemap);
  RemapMaterialTextureIndex(material.occlusionTexture, textureRemap);
  RemapMaterialTextureIndex(material.metalRoughTexture, textureRemap);
  RemapMaterialTextureIndex(material.runtimeMetalRoughTexture, textureRemap);
  RemapMaterialTextureIndex(material.metalnessTexture, textureRemap);
  RemapMaterialTextureIndex(material.roughnessGlossTexture, textureRemap);
  RemapMaterialTextureIndex(material.specularColorTexture, textureRemap);
  RemapMaterialTextureIndex(material.thicknessTexture, textureRemap);
  RemapMaterialTextureIndex(material.parallaxTexture, textureRemap);
}

static void CollectNodeMaterialCandidates(
    const Node &node, std::vector<int> &materialIndices) {
  for (int materialIndex : node.linkedMaterialIndices) {
    AddUniqueInt(materialIndices, materialIndex);
  }
  for (size_t meshIndex : node.meshIndices) {
    if (meshIndex >= g_loadedMeshes.size()) {
      continue;
    }
    AddUniqueInt(materialIndices, g_loadedMeshes[meshIndex].materialIndex);
  }
}

static std::vector<int> CollectMaterialCandidatesForNodes(
    const std::vector<size_t> &nodeIndices) {
  std::vector<int> materialIndices;
  for (size_t nodeIndex : nodeIndices) {
    if (nodeIndex < s_nodes.size()) {
      CollectNodeMaterialCandidates(s_nodes[nodeIndex], materialIndices);
    }
  }
  return materialIndices;
}

static std::vector<int> BuildLiveMaterialReferenceCounts() {
  std::vector<int> refCounts(g_loadedMaterials.size(), 0);
  for (const Asset::GpuMesh &mesh : g_loadedMeshes) {
    if (mesh.vertexCount == 0 || mesh.indexCount == 0 ||
        mesh.materialIndex < 0 ||
        mesh.materialIndex >= static_cast<int>(refCounts.size())) {
      continue;
    }
    ++refCounts[static_cast<size_t>(mesh.materialIndex)];
  }
  return refCounts;
}

static void RemapMaterialIndexValue(int &materialIndex,
                                    const std::vector<int> &materialRemap) {
  if (materialIndex < 0) {
    return;
  }
  if (materialIndex >= static_cast<int>(materialRemap.size())) {
    materialIndex = -1;
    return;
  }
  materialIndex = materialRemap[static_cast<size_t>(materialIndex)];
}

static void RemapMaterialIndexVector(std::vector<int> &indices,
                                     const std::vector<int> &materialRemap) {
  for (int &materialIndex : indices) {
    RemapMaterialIndexValue(materialIndex, materialRemap);
  }
}

static void CompactImportedMaterialCandidates(
    const std::vector<int> &candidateMaterialIndices,
    std::vector<size_t> &candidateTextureIndices) {
  if (candidateMaterialIndices.empty() || g_loadedMaterials.empty()) {
    return;
  }

  const std::vector<int> liveRefCounts = BuildLiveMaterialReferenceCounts();
  std::vector<uint8_t> removeMaterial(g_loadedMaterials.size(), 0);
  for (int materialIndex : candidateMaterialIndices) {
    if (materialIndex < 0 ||
        materialIndex >= static_cast<int>(g_loadedMaterials.size())) {
      continue;
    }
    const size_t index = static_cast<size_t>(materialIndex);
    if (index < liveRefCounts.size() && liveRefCounts[index] > 0) {
      continue;
    }
    removeMaterial[index] = 1;
    CollectMaterialTextureCandidates(g_loadedMaterials[index],
                                     candidateTextureIndices);
  }

  if (std::none_of(removeMaterial.begin(), removeMaterial.end(),
                   [](uint8_t remove) { return remove != 0; })) {
    return;
  }

  std::vector<int> materialRemap(g_loadedMaterials.size(), -1);
  std::vector<Asset::Material> compactedMaterials;
  std::vector<std::string> compactedStableIds;
  compactedMaterials.reserve(g_loadedMaterials.size());
  compactedStableIds.reserve(s_materialStableIds.size());
  for (size_t materialIndex = 0; materialIndex < g_loadedMaterials.size();
       ++materialIndex) {
    if (removeMaterial[materialIndex]) {
      continue;
    }
    materialRemap[materialIndex] = static_cast<int>(compactedMaterials.size());
    compactedMaterials.push_back(std::move(g_loadedMaterials[materialIndex]));
    compactedStableIds.push_back(
        materialIndex < s_materialStableIds.size()
            ? s_materialStableIds[materialIndex]
            : std::string());
  }

  for (Asset::GpuMesh &mesh : g_loadedMeshes) {
    RemapMaterialIndexValue(mesh.materialIndex, materialRemap);
  }
  for (Node &node : s_nodes) {
    RemapMaterialIndexVector(node.linkedMaterialIndices, materialRemap);
  }
  for (auto &[_, entry] : s_sharedImportedMeshesBySourcePath) {
    RemapMaterialIndexVector(entry.linkedMaterialIndices, materialRemap);
  }
  RemapScatterTargetMaterialIndices(materialRemap);

  g_loadedMaterials = std::move(compactedMaterials);
  s_materialStableIds = std::move(compactedStableIds);
  s_materialMetadataDirty = true;
  EnsureMaterialMetadataStorage();
}

static std::vector<uint8_t> BuildLiveTextureReferenceMask() {
  std::vector<uint8_t> live(g_loadedTextures.size(), 0);
  for (const Asset::Material &material : g_loadedMaterials) {
    std::vector<size_t> textureIndices;
    CollectMaterialTextureCandidates(material, textureIndices);
    for (size_t textureIndex : textureIndices) {
      if (textureIndex < live.size()) {
        live[textureIndex] = 1;
      }
    }
  }
  return live;
}

static void CompactImportedTextureCandidates(
    const std::vector<size_t> &candidateTextureIndices) {
  if (candidateTextureIndices.empty() || g_loadedTextures.empty()) {
    return;
  }

  const std::vector<uint8_t> liveTextures = BuildLiveTextureReferenceMask();
  std::vector<uint8_t> removeTexture(g_loadedTextures.size(), 0);
  for (size_t textureIndex : candidateTextureIndices) {
    if (textureIndex >= g_loadedTextures.size()) {
      continue;
    }
    if (textureIndex < liveTextures.size() && liveTextures[textureIndex]) {
      continue;
    }
    removeTexture[textureIndex] = 1;
  }

  if (std::none_of(removeTexture.begin(), removeTexture.end(),
                   [](uint8_t remove) { return remove != 0; })) {
    return;
  }

  std::vector<int> textureRemap(g_loadedTextures.size(), -1);
  std::vector<Asset::Texture> compactedTextures;
  compactedTextures.reserve(g_loadedTextures.size());
  for (size_t textureIndex = 0; textureIndex < g_loadedTextures.size();
       ++textureIndex) {
    if (removeTexture[textureIndex]) {
      continue;
    }
    textureRemap[textureIndex] = static_cast<int>(compactedTextures.size());
    compactedTextures.push_back(std::move(g_loadedTextures[textureIndex]));
  }

  for (Asset::Material &material : g_loadedMaterials) {
    RemapMaterialTextureIndicesInPlace(material, textureRemap);
  }

  for (auto it = s_textureIndicesBySourceUri.begin();
       it != s_textureIndicesBySourceUri.end();) {
    const int textureIndex = it->second;
    if (textureIndex < 0 ||
        textureIndex >= static_cast<int>(textureRemap.size()) ||
        textureRemap[static_cast<size_t>(textureIndex)] < 0) {
      it = s_textureIndicesBySourceUri.erase(it);
      continue;
    }
    it->second = textureRemap[static_cast<size_t>(textureIndex)];
    ++it;
  }

  g_loadedTextures = std::move(compactedTextures);
  RegisterTextures(g_loadedTextures);
  DxrRenderer::MarkTextureDescriptorTableDirty();
}

static void PruneOrphanedImportAssets(
    const std::vector<int> &candidateMaterialIndices) {
  if (candidateMaterialIndices.empty()) {
    return;
  }

  std::vector<size_t> candidateTextureIndices;
  const size_t materialCountBefore = g_loadedMaterials.size();
  const size_t textureCountBefore = g_loadedTextures.size();
  CompactImportedMaterialCandidates(candidateMaterialIndices,
                                    candidateTextureIndices);
  CompactImportedTextureCandidates(candidateTextureIndices);

  const size_t removedMaterials = materialCountBefore - g_loadedMaterials.size();
  const size_t removedTextures = textureCountBefore - g_loadedTextures.size();
  if (removedMaterials > 0 || removedTextures > 0) {
    RefreshAllMaterialRuntimeTextures();
    fprintf(stderr,
            "Scene: pruned orphaned import assets (materials=%zu textures=%zu)\n",
            removedMaterials, removedTextures);
  }
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
    } else if (globalMaterialIndex >= 0 &&
               globalMaterialIndex < (int)g_loadedMaterials.size()) {
      if (MergeMissingParallaxMaterialChannels(
              g_loadedMaterials[(size_t)globalMaterialIndex], materials[i])) {
        fprintf(stderr,
                "Scene: merged missing parallax texture slots into material "
                "%s (index=%d)\n",
                importedName.c_str(), globalMaterialIndex);
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

static bool MergeMissingParallaxMaterialChannels(Asset::Material &existing,
                                                 const Asset::Material &incoming) {
  if (incoming.parallaxTexture < 0) {
    return false;
  }

  bool changed = false;
  if (existing.diffuseTexture < 0 && incoming.diffuseTexture >= 0) {
    existing.diffuseTexture = incoming.diffuseTexture;
    existing.diffuseTextureAmount = incoming.diffuseTextureAmount;
    changed = true;
  }
  if (existing.emissiveTexture < 0 && incoming.emissiveTexture >= 0) {
    existing.emissiveTexture = incoming.emissiveTexture;
    existing.emissiveTextureAmount = incoming.emissiveTextureAmount;
    std::copy(std::begin(incoming.emissiveColor),
              std::end(incoming.emissiveColor),
              std::begin(existing.emissiveColor));
    existing.emissiveIntensity = incoming.emissiveIntensity;
    changed = true;
  }
  if (existing.opacityTexture < 0 && incoming.opacityTexture >= 0) {
    existing.opacityTexture = incoming.opacityTexture;
    existing.opacityTextureAmount = incoming.opacityTextureAmount;
    existing.alphaCutoff = incoming.alphaCutoff;
    changed = true;
  }
  if (existing.parallaxTexture < 0) {
    existing.parallaxTexture = incoming.parallaxTexture;
    existing.parallaxMode = incoming.parallaxMode;
    existing.parallaxDepthScale = incoming.parallaxDepthScale;
    existing.parallaxRoomDepth = incoming.parallaxRoomDepth;
    existing.parallaxWindowAspect = incoming.parallaxWindowAspect;
    existing.parallaxUvScale[0] = incoming.parallaxUvScale[0];
    existing.parallaxUvScale[1] = incoming.parallaxUvScale[1];
    existing.parallaxUvOffset[0] = incoming.parallaxUvOffset[0];
    existing.parallaxUvOffset[1] = incoming.parallaxUvOffset[1];
    existing.parallaxBackFace = incoming.parallaxBackFace;
    changed = true;
  } else if (incoming.parallaxMode == Asset::Material::kParallaxModeWindowBox &&
             existing.parallaxMode != Asset::Material::kParallaxModeWindowBox) {
    existing.parallaxTexture = incoming.parallaxTexture;
    existing.parallaxMode = incoming.parallaxMode;
    existing.parallaxDepthScale = incoming.parallaxDepthScale;
    existing.parallaxRoomDepth = incoming.parallaxRoomDepth;
    existing.parallaxWindowAspect = incoming.parallaxWindowAspect;
    existing.parallaxUvScale[0] = incoming.parallaxUvScale[0];
    existing.parallaxUvScale[1] = incoming.parallaxUvScale[1];
    existing.parallaxUvOffset[0] = incoming.parallaxUvOffset[0];
    existing.parallaxUvOffset[1] = incoming.parallaxUvOffset[1];
    existing.parallaxBackFace = incoming.parallaxBackFace;
    changed = true;
  } else if (existing.parallaxDepthScale <= 1.0e-5f &&
             incoming.parallaxDepthScale > 1.0e-5f) {
    if (existing.parallaxMode == Asset::Material::kParallaxModeOff) {
      existing.parallaxMode = incoming.parallaxMode;
    }
    existing.parallaxDepthScale = incoming.parallaxDepthScale;
    existing.parallaxRoomDepth = incoming.parallaxRoomDepth;
    existing.parallaxWindowAspect = incoming.parallaxWindowAspect;
    existing.parallaxUvScale[0] = incoming.parallaxUvScale[0];
    existing.parallaxUvScale[1] = incoming.parallaxUvScale[1];
    existing.parallaxUvOffset[0] = incoming.parallaxUvOffset[0];
    existing.parallaxUvOffset[1] = incoming.parallaxUvOffset[1];
    existing.parallaxBackFace = incoming.parallaxBackFace;
    changed = true;
  }

  if (changed) {
    existing.doubleSided = existing.doubleSided || incoming.doubleSided;
    if (existing.alphaMode.empty() || existing.alphaMode == "OPAQUE") {
      existing.alphaMode = incoming.alphaMode;
    }
  }
  return changed;
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
                                  bool notifyScene = true,
                                  bool pruneAssets = true) {
  if (indices.empty()) {
    return;
  }

  ClearTransformHistory();
  PrepareForDestructiveMeshMutation();

  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  std::vector<int> candidateMaterialIndices =
      CollectMaterialCandidatesForNodes(indices);

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
    ReindexScatterNodeReferencesAfterRemoval(nodeIndex);
  }

  if (pruneAssets) {
    PruneOrphanedImportAssets(candidateMaterialIndices);
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
                                 std::vector<Asset::ImportedSceneNode> sceneNodes,
                                 const float *rootTranslation = nullptr) {
  ImportedNodePayload payload;
  payload.sourcePath = srcPath;
  if (rootTranslation) {
    payload.rootTranslation = {rootTranslation[0], rootTranslation[1],
                               rootTranslation[2]};
    payload.hasRootTranslation = true;
  }
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
                                   const std::string &targetImportGroupKey = {},
                                   const float *rootTranslation = nullptr) {
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

  std::array<float, 3> asyncRootTranslation = {};
  const bool hasRootTranslation = rootTranslation != nullptr;
  if (rootTranslation) {
    asyncRootTranslation = {rootTranslation[0], rootTranslation[1],
                            rootTranslation[2]};
  }

  std::thread([path, action, targetNodeIndex, targetImportGroupKey,
               asyncRootTranslation, hasRootTranslation]() {
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
      s_pendingRootTranslation = asyncRootTranslation;
      s_pendingHasRootTranslation = hasRootTranslation;
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

  // Register the imported model into the asset library (Phase 2). Skipped for
  // streamed live-link payloads, library re-instantiations, and sources that
  // are not real files on disk.
  if (!payload.skipLibraryRegister && !isLiveLinkPayload &&
      !payload.sourcePath.empty()) {
    std::error_code regEc;
    if (std::filesystem::exists(std::filesystem::path(payload.sourcePath),
                                regEc)) {
      std::string libName =
          payload.displayName.empty()
              ? std::filesystem::path(payload.sourcePath).stem().string()
              : payload.displayName;
      assetlib::RegisterImportedModel(libName, payload.sourcePath,
                                      payload.meshes, payload.materials,
                                      payload.textures);
    }
  }

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
    RefreshTextureCompressionForMaterials(false);

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
    if (payload.hasRootTranslation) {
      rootNode.transform[12] = payload.rootTranslation[0];
      rootNode.transform[13] = payload.rootTranslation[1];
      rootNode.transform[14] = payload.rootTranslation[2];
    }
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
      if (payload.hasRootTranslation) {
        node.transform[12] = payload.rootTranslation[0];
        node.transform[13] = payload.rootTranslation[1];
        node.transform[14] = payload.rootTranslation[2];
      }
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
  RefreshTextureCompressionForMaterials(false);
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
  if (payload.hasRootTranslation) {
    node.transform[12] = payload.rootTranslation[0];
    node.transform[13] = payload.rootTranslation[1];
    node.transform[14] = payload.rootTranslation[2];
  }
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
    std::vector<int> candidateMaterialIndices =
        CollectMaterialCandidatesForNodes(nodesToRemove);
    // Keep material indices stable until the rebuilt import has rebound its
    // source slots; pruning here would invalidate edited engine materials.
    RemoveNodesByIndexSet(std::move(nodesToRemove), false, false);

    size_t newRootIndex = static_cast<size_t>(-1);
    const bool ok = AddImportedNode(std::move(payload), &newRootIndex);
    if (!ok) {
      s_lastStatus =
          "ReplaceNodeImportedContent failed: unable to rebuild import group";
    } else {
      PruneOrphanedImportAssets(candidateMaterialIndices);
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
  RefreshTextureCompressionForMaterials(false);

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
  if (!s_nodes[index].volumeAssetId.empty())
    UpdateLights();
  InvalidateScatterRuntimeCache();
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
  if (!s_nodes[index].volumeAssetId.empty())
    UpdateLights();
  InvalidateScatterRuntimeCache();
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

  if (s_nodes[index].parentIndex == resolvedParent) {
    return true;
  }
  ClearTransformHistory();
  s_nodes[index].parentIndex = resolvedParent;
  InvalidateScatterRuntimeCache();
  return true;
}

bool CanExplodeNodeMeshes(size_t index) {
  if (index >= s_nodes.size() || s_nodes[index].liveLinkManaged) {
    return false;
  }
  size_t meshCount = 0;
  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    if (!IsNodeDescendantOf(nodeIndex, index)) {
      continue;
    }
    for (size_t meshIndex : s_nodes[nodeIndex].meshIndices) {
      if (meshIndex < g_loadedMeshes.size() && ++meshCount >= 2) {
        return true;
      }
    }
  }
  return false;
}

size_t ExplodeNodeMeshes(size_t index) {
  if (!CanExplodeNodeMeshes(index)) {
    s_lastStatus =
        "Explode failed: select a non-Live-Sync branch containing multiple "
        "meshes.";
    return 0;
  }

  ClearTransformHistory();
  const std::string sourceName = s_nodes[index].name;
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();
  std::vector<size_t> sourceIndices;
  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    if (IsNodeDescendantOf(nodeIndex, index)) {
      sourceIndices.push_back(nodeIndex);
    }
  }

  size_t createdCount = 0;
  for (size_t sourceIndex : sourceIndices) {
    if (sourceIndex >= s_nodes.size() ||
        sourceIndex >= worldTransforms.size()) {
      continue;
    }
    const Node source = s_nodes[sourceIndex];
    for (size_t meshOffset = 0; meshOffset < source.meshIndices.size();
         ++meshOffset) {
      const size_t meshIndex = source.meshIndices[meshOffset];
      if (meshIndex >= g_loadedMeshes.size()) {
        continue;
      }

      Node part;
      part.name = source.name.empty() ? sourceName : source.name;
      if (source.meshIndices.size() > 1) {
        part.name += " / Mesh " + std::to_string(meshOffset + 1);
      }
      const int materialIndex = g_loadedMeshes[meshIndex].materialIndex;
      if (materialIndex >= 0 &&
          materialIndex < static_cast<int>(g_loadedMaterials.size()) &&
          g_loadedMaterials[static_cast<size_t>(materialIndex)].name[0] !=
              '\0') {
        part.name +=
            " - " +
            std::string(
                g_loadedMaterials[static_cast<size_t>(materialIndex)].name);
      }
      part.meshIndices = {meshIndex};
      memcpy(part.transform, worldTransforms[sourceIndex].data(),
             sizeof(part.transform));
      part.parentIndex = static_cast<size_t>(-1);
      part.linkedMaterialIndices = source.linkedMaterialIndices;
      part.linkedMaterialSourceNames = source.linkedMaterialSourceNames;
      part.visible = source.visible;
      part.selected = true;
      part.selectionLocked = false;
      part.liveLinkManaged = false;
      part.importGroupRoot = false;
      s_nodes.push_back(std::move(part));
      ++createdCount;
    }
  }

  if (createdCount == 0) {
    s_lastStatus = "Explode failed: branch has no valid mesh references.";
    return 0;
  }

  RemoveNodesByIndexSet(std::move(sourceIndices), false, true);
  InvalidateScatterRuntimeCache();
  ApplyRendererInvalidation(
      RendererInvalidationPlan::FullAccelerationStructureRebuild);
  NotifySceneChanged();
  s_lastStatus = "Exploded '" + sourceName + "' into " +
                 std::to_string(createdCount) + " independent mesh nodes.";
  return createdCount;
}

// Scatter API moved to scatter.cpp. The Scene namespace surface
// (GetScatterModels, AddScatterModel, AddSelectedNodesAsScatter*, ...)
// is declared in scatter.h and stays callable as Scene::* unchanged.

bool IsImportInProgress() { return s_importInProgress.load(); }

float GetImportProgress() { return s_importProgress.load(); }

std::string GetImportStatus() {
  std::lock_guard<std::mutex> lg(s_importStatusMutex);
  return s_importStatus;
}

void ProcessPendingImport() {
  // Apply any finished background cook results to the registry every frame
  // (runs regardless of whether an import is pending).
  if (assetlib::AssetRegistry *reg = assetlib::GlobalRegistry())
    assetlib::CookService::Get().Pump(*reg);

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
  std::array<float, 3> rootTranslation = {};
  bool hasRootTranslation = false;
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
    rootTranslation = s_pendingRootTranslation;
    hasRootTranslation = s_pendingHasRootTranslation;
    s_pendingMeshes.clear();
    s_pendingMaterials.clear();
    s_pendingTextures.clear();
    s_pendingSceneNodes.clear();
    s_pendingPath.clear();
    s_pendingAction = PendingImportAction::Import;
    s_pendingTargetNodeIndex = static_cast<size_t>(-1);
    s_pendingTargetImportGroupKey.clear();
    s_pendingRootTranslation = {};
    s_pendingHasRootTranslation = false;
  }
  s_pendingReady = false;

  bool ok = false;
  if (action == PendingImportAction::Import) {
    ok = FinalizeImportedNode(srcPath, std::move(meshes), std::move(materials),
                              std::move(textures), std::move(sceneNodes),
                              hasRootTranslation ? rootTranslation.data()
                                                 : nullptr);
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

bool InstantiateAssetModel(const assetlib::AssetId &id,
                           const float *rootTranslation, size_t *outNodeIndex) {
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg) {
    s_lastStatus = "Instantiate failed: asset library unavailable";
    return false;
  }
  assetlib::ResolvedModel rm = assetlib::ResolveModel(*reg, reg->paths(), id);
  if (!rm.valid || rm.meshes.empty()) {
    s_lastStatus = "Instantiate failed: could not resolve cooked asset";
    return false;
  }

  // Cooked geometry comes back CPU-only; create GPU buffers now.
  Asset::UploadMeshes(rm.meshes);

  ImportedNodePayload payload;
  payload.materials = std::move(rm.materials);
  payload.textures = std::move(rm.textures);
  payload.meshes = std::move(rm.meshes);
  payload.materialsContainFullDefinitions = true;
  payload.skipLibraryRegister = true; // already a library asset
  if (const assetlib::AssetMetadata *meta = reg->Get(id)) {
    payload.displayName = meta->displayName;
    payload.sourcePath = meta->sourcePath;
  }
  if (rootTranslation) {
    payload.rootTranslation = {rootTranslation[0], rootTranslation[1],
                               rootTranslation[2]};
    payload.hasRootTranslation = true;
  }

  // Wrap all meshes in a single synthetic scene node so the well-tested
  // hierarchical import path handles material/texture registration. Phase 2
  // does not yet cook the original node hierarchy.
  Asset::ImportedSceneNode rootNode;
  rootNode.name = payload.displayName.empty() ? "Asset" : payload.displayName;
  for (size_t i = 0; i < payload.meshes.size(); ++i)
    rootNode.meshIndices.push_back(i);
  payload.sceneNodes.push_back(std::move(rootNode));

  reg->TouchRecent(id);
  return AddImportedNode(std::move(payload), outNodeIndex);
}

bool AssignMaterialAssetToSelection(const assetlib::AssetId &id) {
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg)
    return false;
  std::vector<size_t> selection = GetSelectedNodeIndices();
  if (selection.empty()) {
    s_lastStatus = "Drop a material onto a selected object";
    return false;
  }
  assetlib::ResolvedMaterial rm =
      assetlib::ResolveMaterial(*reg, reg->paths(), id);
  if (!rm.valid) {
    s_lastStatus = "Material assign failed: could not resolve cooked material";
    return false;
  }

  // Register the material's textures into the global array and remap its slots
  // from local (resolved) indices to global texture indices.
  std::vector<int> localToGlobal(rm.textures.size(), -1);
  for (size_t i = 0; i < rm.textures.size(); ++i)
    localToGlobal[i] = AddTexture(std::move(rm.textures[i]));

  Asset::Material mat = rm.material;
  int *slots[] = {&mat.diffuseTexture,        &mat.normalTexture,
                  &mat.opacityTexture,        &mat.emissiveTexture,
                  &mat.occlusionTexture,      &mat.metalRoughTexture,
                  &mat.metalnessTexture,      &mat.roughnessGlossTexture,
                  &mat.specularColorTexture,  &mat.thicknessTexture,
                  &mat.coatNormalTexture,     &mat.parallaxTexture};
  for (int *s : slots) {
    if (*s >= 0 && *s < static_cast<int>(localToGlobal.size()))
      *s = localToGlobal[static_cast<size_t>(*s)];
    else
      *s = -1;
  }
  mat.runtimeMetalRoughTexture = -1; // regenerated by the runtime refresh below

  int materialIndex = FindOrCreateMaterial(mat);
  if (materialIndex < 0) {
    s_lastStatus = "Material assign failed";
    return false;
  }

  bool any = false;
  for (size_t nodeIndex : selection) {
    if (nodeIndex >= s_nodes.size())
      continue;
    size_t slotCount = s_nodes[nodeIndex].linkedMaterialIndices.size();
    if (slotCount == 0)
      slotCount = 1;
    for (size_t slot = 0; slot < slotCount; ++slot)
      any |= RebindNodeMaterialSlot(nodeIndex, slot, materialIndex);
  }
  if (any) {
    RefreshAllMaterialRuntimeTextures();
    s_lastStatus = "Assigned material to selection";
  }
  return any;
}

assetlib::AssetId ExtractModelAssetFromMeshes(
    const std::vector<size_t> &meshIndices,
    const std::vector<std::array<float, 16>> &localTransforms,
    const std::string &displayName, const std::string &folder,
    const std::string &sourcePath) {
  using namespace DirectX;
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg)
    return {};

  std::vector<Asset::GpuMesh> meshes;
  std::vector<Asset::Material> materials;
  std::vector<Asset::Texture> textures;
  std::unordered_map<int, int> matG2L, texG2L;

  auto addTexture = [&](int g) -> int {
    if (g < 0 || g >= static_cast<int>(g_loadedTextures.size()))
      return -1;
    auto it = texG2L.find(g);
    if (it != texG2L.end())
      return it->second;
    int local = static_cast<int>(textures.size());
    textures.push_back(g_loadedTextures[static_cast<size_t>(g)]);
    texG2L[g] = local;
    return local;
  };
  auto remapMaterialTextures = [&](Asset::Material &m) {
    int *slots[] = {&m.diffuseTexture,       &m.normalTexture,
                    &m.opacityTexture,       &m.emissiveTexture,
                    &m.occlusionTexture,     &m.metalRoughTexture,
                    &m.metalnessTexture,     &m.roughnessGlossTexture,
                    &m.specularColorTexture, &m.thicknessTexture,
                    &m.coatNormalTexture,    &m.parallaxTexture};
    for (int *s : slots)
      *s = addTexture(*s);
    m.runtimeMetalRoughTexture = -1; // regenerated by the runtime refresh
  };
  auto addMaterial = [&](int g) -> int {
    if (g < 0 || g >= static_cast<int>(g_loadedMaterials.size()))
      return -1;
    auto it = matG2L.find(g);
    if (it != matG2L.end())
      return it->second;
    Asset::Material m = g_loadedMaterials[static_cast<size_t>(g)];
    remapMaterialTextures(m);
    int local = static_cast<int>(materials.size());
    materials.push_back(std::move(m));
    matG2L[g] = local;
    return local;
  };

  for (size_t i = 0; i < meshIndices.size(); ++i) {
    const size_t gi = meshIndices[i];
    if (gi >= g_loadedMeshes.size())
      continue;
    Asset::GpuMesh mesh = g_loadedMeshes[gi]; // copies CPU geometry + buffers

    // Bake the prototype's per-mesh local transform into the geometry so the
    // extracted model reproduces the original orientation/arrangement.
    if (i < localTransforms.size() && !mesh.cpuVertices.empty()) {
      const XMMATRIX M = XMLoadFloat4x4(
          reinterpret_cast<const XMFLOAT4X4 *>(localTransforms[i].data()));
      const XMMATRIX nrm = XMMatrixTranspose(XMMatrixInverse(nullptr, M));
      float mn[3] = {std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
      float mx[3] = {-std::numeric_limits<float>::max(),
                     -std::numeric_limits<float>::max(),
                     -std::numeric_limits<float>::max()};
      for (Asset::Vertex &v : mesh.cpuVertices) {
        XMFLOAT3 pf;
        XMStoreFloat3(&pf, XMVector3Transform(
                               XMVectorSet(v.pos[0], v.pos[1], v.pos[2], 1.0f),
                               M));
        v.pos[0] = pf.x;
        v.pos[1] = pf.y;
        v.pos[2] = pf.z;
        XMFLOAT3 nf;
        XMStoreFloat3(&nf, XMVector3Normalize(XMVector3TransformNormal(
                               XMVectorSet(v.normal[0], v.normal[1],
                                           v.normal[2], 0.0f),
                               nrm)));
        v.normal[0] = nf.x;
        v.normal[1] = nf.y;
        v.normal[2] = nf.z;
        XMFLOAT3 tf;
        XMStoreFloat3(&tf, XMVector3Normalize(XMVector3TransformNormal(
                               XMVectorSet(v.tangent[0], v.tangent[1],
                                           v.tangent[2], 0.0f),
                               M)));
        v.tangent[0] = tf.x;
        v.tangent[1] = tf.y;
        v.tangent[2] = tf.z;
        for (int c = 0; c < 3; ++c) {
          mn[c] = std::min(mn[c], v.pos[c]);
          mx[c] = std::max(mx[c], v.pos[c]);
        }
      }
      for (int c = 0; c < 3; ++c) {
        mesh.minBound[c] = mn[c];
        mesh.maxBound[c] = mx[c];
      }
    }

    mesh.materialIndex = addMaterial(mesh.materialIndex);
    mesh.materialSlot = -1;
    meshes.push_back(std::move(mesh));
  }
  if (meshes.empty())
    return {};

  assetlib::ImportedAssetSet set = assetlib::RegisterAndCookImport(
      *reg, reg->paths(), displayName, sourcePath, meshes, materials, textures);
  if (set.modelId.valid() && !folder.empty()) {
    if (const assetlib::AssetMetadata *meta = reg->Get(set.modelId)) {
      assetlib::AssetMetadata updated = *meta;
      updated.virtualPath = folder;
      reg->Update(updated);
    }
  }
  reg->Save();
  return set.modelId;
}

size_t AddVolumeNode(const assetlib::AssetId &id) {
  // Load the volume into the renderer (also gives us its cooked bounds).
  if (!VolumetricRenderer::SetActiveVolume(id))
    return static_cast<size_t>(-1);
  const DirectX::XMFLOAT3 bmin = VolumetricRenderer::BoundsMin();
  const DirectX::XMFLOAT3 bmax = VolumetricRenderer::BoundsMax();

  // The volume occupies a unit cube [0,1]^3 in local space; this transform maps
  // that cube to a world box matching the volume's real proportions, with its
  // largest dimension ~10 units, centered at the world origin. The user can then
  // move/scale/rotate it with the gizmo.
  const float ex = bmax.x - bmin.x, ey = bmax.y - bmin.y, ez = bmax.z - bmin.z;
  const float maxExtent = (std::max)(ex, (std::max)(ey, ez));
  const float norm = maxExtent > 1e-6f ? 10.0f / maxExtent : 10.0f;
  const float sxv = (std::max)(ex * norm, 0.5f);
  const float syv = (std::max)(ey * norm, 0.5f);
  const float szv = (std::max)(ez * norm, 0.5f);

  Node node;
  const assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  const assetlib::AssetMetadata *meta = reg ? reg->Get(id) : nullptr;
  node.name = meta && !meta->displayName.empty() ? meta->displayName : "Volume";
  node.volumeAssetId = id.ToString();
  // Column-major diagonal scale + translation. Rest the volume ON the ground
  // (bottom at Y=0), centered in X/Z. Centering on the origin would bury the
  // lower half below the ground plane, where the scene-depth clip hides it —
  // and for fire VDBs the flame lives at the bottom, so it would vanish.
  for (float &f : node.transform)
    f = 0.0f;
  node.transform[0] = sxv;
  node.transform[5] = syv;
  node.transform[10] = szv;
  node.transform[12] = -sxv * 0.5f;
  node.transform[13] = 0.0f;
  node.transform[14] = -szv * 0.5f;
  node.transform[15] = 1.0f;

  const size_t index = AddNode(std::move(node));
  SelectNode(index);
  UpdateLights();
  NotifySceneChanged();
  return index;
}

bool FindVolumeNodeTransform(const assetlib::AssetId &id, float out[16]) {
  const std::string hex = id.ToString();
  for (const Node &node : s_nodes) {
    if (node.volumeAssetId == hex) {
      for (int i = 0; i < 16; ++i)
        out[i] = node.transform[i];
      return true;
    }
  }
  return false;
}

bool FindVolumeNodeMaterial(const assetlib::AssetId &id,
                            VolumeMaterial &out) {
  const std::string hex = id.ToString();
  for (const Node &node : s_nodes) {
    if (node.volumeAssetId == hex) {
      out = node.volumeMaterial;
      return true;
    }
  }
  return false;
}

bool SetVolumeNodeMaterial(size_t nodeIndex, const VolumeMaterial &material) {
  if (nodeIndex >= s_nodes.size() ||
      s_nodes[nodeIndex].volumeAssetId.empty()) {
    return false;
  }
  s_nodes[nodeIndex].volumeMaterial = material;
  UpdateLights();
  DxrRenderer::ResetAccumulation();
  NotifySceneChanged();
  return true;
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

bool ImportModelAsync(const std::string &utf8path, const float *rootTranslation) {
  if (utf8path.empty()) {
    s_lastStatus = "Import failed: empty path";
    return false;
  }
  if (!StoredPathExists(utf8path)) {
    s_lastStatus = "Import failed: path not found: " + utf8path;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(s_recentImportMutex);
    const auto now = std::chrono::steady_clock::now();
    if (!s_recentImportPath.empty() && s_recentImportPath == utf8path) {
      const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - s_recentImportTime);
      if (elapsedMs.count() < 500) { // lowered from 1500 to 500, true cause fixed in browser
        s_lastStatus = "Duplicate import suppressed: " + utf8path;
        fprintf(stderr, "Scene::ImportModelAsync: suppressed duplicate import for %s\n",
                utf8path.c_str());
        return false;
      }
    }
    s_recentImportPath = utf8path;
    s_recentImportTime = now;
  }

  return StartAsyncSceneLoadJob(utf8path, PendingImportAction::Import,
                                static_cast<size_t>(-1), {}, rootTranslation);
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

std::vector<Node> &GetMutableNodes() { return s_nodes; }

void InvalidateScatterRuntimeCache() {
  // The scatter cache lives in scatter.cpp; bumping the revision there is
  // what flushes the next AppendScatterInstances call.
  OnSceneStateChanged();
}

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
  if (!StoredPathExists(srcPath)) {
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
  ConvertClonedInstancesToCopies(s_shiftCloneDrag.cloneNodeIndices);
  SelectShiftCloneResult();
  s_shiftCloneDrag = {};
}

void ResolvePendingCloneAsInstance() {
  if (!s_shiftCloneDrag.optionsPending) {
    return;
  }
  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  NotifySceneChanged();
  SelectShiftCloneResult();
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

  ClearTransformHistory();
  PrepareForDestructiveMeshMutation();

  if (IsImportedSceneGroupNode(s_nodes[index])) {
    RemoveNodesByIndexSet(
        CollectImportGroupNodeIndices(s_nodes[index].importGroupKey), true);
    return true;
  }

  const bool removedVolume = !s_nodes[index].volumeAssetId.empty();
  std::vector<int> candidateMaterialIndices;
  CollectNodeMaterialCandidates(s_nodes[index], candidateMaterialIndices);
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
  ReindexScatterNodeReferencesAfterRemoval(index);
  if (removedVolume)
    UpdateLights();
  PruneOrphanedImportAssets(candidateMaterialIndices);
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
                               {1, 0, 0, -1},
                               {0.0f, 0.0f}},
                              {{half, offset_y, -half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, -1},
                               {1.0f, 0.0f}},
                              {{half, offset_y, half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, -1},
                               {1.0f, 1.0f}},
                              {{-half, offset_y, half},
                               {0.0f, 1.0f, 0.0f},
                               {1, 0, 0, -1},
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

  for (const ScatterModel &model : GetScatterModels()) {
    if (!model.enabled) {
      continue;
    }
    for (const ScatterObject &object : model.objects) {
      if (!object.enabled) {
        continue;
      }
      for (size_t meshIndex : object.meshIndices) {
        if (meshIndex >= g_loadedMeshes.size() || used[meshIndex]) {
          continue;
        }
        const auto &m = g_loadedMeshes[meshIndex];
        if (m.vertexBuffer && m.indexBuffer && m.vertexCount > 0 &&
            m.indexCount > 0) {
          active.push_back(&m);
          used[meshIndex] = true;
        }
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
  const Asset::GpuMesh *midPatch = GrassManager::GetMidPatchMesh();
  if (midPatch && midPatch->vertexBuffer && midPatch->indexBuffer &&
      midPatch->indexCount > 0) {
    const bool alreadyPresent = std::any_of(
        active.begin(), active.end(),
        [midPatch](const Asset::GpuMesh *m) {
          return m && m->vertexBuffer.Get() == midPatch->vertexBuffer.Get();
        });
    if (!alreadyPresent) {
      active.push_back(midPatch);
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
  AppendScatterInstances(instances);
  return instances;
}

const std::vector<LightPrototype> &GetLightPrototypes() { return s_lightPrototypes; }

const std::vector<LightInstance> &GetLightInstances() { return s_lightInstances; }

const std::vector<Light> &GetLights() { return s_flattenedLights; }

void UpdateLights() {
  // Resolve IES profile indices to atlas slice indices
  for (auto &proto : s_lightPrototypes) {
    if (proto.iesProfileIndex >= 0 &&
        proto.iesProfileIndex < static_cast<int>(s_iesProfiles.size()) &&
        s_iesProfiles[proto.iesProfileIndex].gpuReady) {
      proto.iesAtlasIndex = s_iesProfiles[proto.iesProfileIndex].atlasSlice;
    } else {
      proto.iesAtlasIndex = -1;
    }
  }

  FlattenLights(s_lightPrototypes, s_lightInstances, s_flattenedLights,
                s_lightFlattenMapping);
  VolumetricRenderer::AppendEmissionLights(s_flattenedLights);
  if (s_batchedUpdateDepth > 0) {
    s_batchedLightsDirty = true;
    return;
  }
  DxrRenderer::UpdateLights(s_flattenedLights);
}

static bool ParseIESStream(std::istream &file, const std::string &path,
                           const std::string &displayName,
                           const std::string &sourceText, IESProfile &out) {
  auto trimAscii = [](std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](char ch) { return !isSpace((unsigned char)ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](char ch) { return !isSpace((unsigned char)ch); })
                    .base(),
                value.end());
    return value;
  };
  auto upperAscii = [](std::string value) {
    for (char &ch : value) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
  };
  auto extractTiltValue = [&](const std::string &sourceLine,
                              std::string &tiltValue) {
    const std::string upper = upperAscii(sourceLine);
    const size_t tiltPos = upper.find("TILT");
    if (tiltPos == std::string::npos) {
      return false;
    }
    size_t cursor = tiltPos + 4;
    while (cursor < sourceLine.size() &&
           std::isspace(static_cast<unsigned char>(sourceLine[cursor]))) {
      ++cursor;
    }
    if (cursor >= sourceLine.size() || sourceLine[cursor] != '=') {
      return false;
    }
    ++cursor;
    tiltValue = trimAscii(sourceLine.substr(cursor));
    if (tiltValue.size() >= 2 &&
        ((tiltValue.front() == '"' && tiltValue.back() == '"') ||
         (tiltValue.front() == '\'' && tiltValue.back() == '\''))) {
      tiltValue = tiltValue.substr(1, tiltValue.size() - 2);
    }
    return true;
  };
  auto skipInlineTiltData = [&](const std::string &tiltValue) {
    const std::string normalizedTilt = upperAscii(trimAscii(tiltValue));
    if (normalizedTilt.empty() || normalizedTilt == "NONE") {
      return true;
    }
    if (normalizedTilt != "INCLUDE") {
      // External tilt files do not consume numeric records from the IES stream.
      return true;
    }

    int pairCount = 0;
    file >> pairCount;
    if (!file || pairCount < 0 || pairCount > 4096) {
      return false;
    }
    float ignored = 0.0f;
    for (int i = 0; i < pairCount * 2; ++i) {
      file >> ignored;
      if (!file) {
        return false;
      }
    }
    return true;
  };

  std::string line;
  if (!std::getline(file, line))
    return false;

  bool foundTilt = false;
  std::string tiltValue;
  while (std::getline(file, line)) {
    if (extractTiltValue(line, tiltValue)) {
      foundTilt = true;
      break;
    }
  }
  if (!foundTilt)
    return false;

  if (!skipInlineTiltData(tiltValue)) {
    return false;
  }

  int numLamps = 0;
  int numVerticalAngles = 0;
  int numHorizontalAngles = 0;
  int phototype = 0;
  int unitType = 0;
  float lumens = 0.0f;
  float multiplier = 1.0f;
  float width = 0.0f;
  float length = 0.0f;
  float height = 0.0f;

  file >> numLamps >> lumens >> multiplier >> numVerticalAngles >>
      numHorizontalAngles >> phototype >> unitType >> width >> length >> height;
  if (!file || numVerticalAngles <= 0 || numHorizontalAngles <= 0 ||
      numVerticalAngles > 4096 || numHorizontalAngles > 4096) {
    return false;
  }
  const int64_t candelaCount =
      static_cast<int64_t>(numVerticalAngles) * numHorizontalAngles;
  if (candelaCount <= 0 || candelaCount > 4 * 1024 * 1024)
    return false;

  float ballMult = 1.0f;
  float volt = 0.0f;
  float watts = 0.0f;
  file >> ballMult >> volt >> watts;
  if (!file)
    return false;

  std::vector<float> verticalAngles(numVerticalAngles);
  for (int i = 0; i < numVerticalAngles; ++i) {
    file >> verticalAngles[i];
    if (!file)
      return false;
  }

  std::vector<float> horizontalAngles(numHorizontalAngles);
  for (int i = 0; i < numHorizontalAngles; ++i) {
    file >> horizontalAngles[i];
    if (!file)
      return false;
  }

  std::vector<float> candelaValues(static_cast<size_t>(candelaCount));
  for (int64_t i = 0; i < candelaCount; ++i) {
    file >> candelaValues[i];
    if (!file)
      return false;
  }

  if (!std::isfinite(multiplier))
    multiplier = 1.0f;
  for (float &value : candelaValues) {
    value = std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
  }

  out.filePath = path;
  out.displayName =
      displayName.empty()
          ? (path.empty() ? std::string("Embedded IES")
                          : fs::path(path).stem().string())
          : displayName;
  out.sourceText = sourceText;
  out.verticalAngles = std::move(verticalAngles);
  out.horizontalAngles = std::move(horizontalAngles);
  out.candela = std::move(candelaValues);
  out.multiplier = multiplier;
  out.numVerticalAngles = numVerticalAngles;
  out.numHorizontalAngles = numHorizontalAngles;
  out.loaded = true;
  return true;
}

static bool ReadIESSourceText(const std::string &path, std::string &outText) {
  std::ifstream file(NativePathFromStoredPath(path), std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  std::ostringstream text;
  text << file.rdbuf();
  outText = text.str();
  return !outText.empty();
}

// Parse an .ies file into raw profile data (no GPU bake).
// Reuses the parsing logic from Asset::LoadIES but stores raw candela arrays.
static bool ParseIESFile(const std::string &path, IESProfile &out) {
  std::string sourceText;
  if (!ReadIESSourceText(path, sourceText)) {
    return false;
  }
  std::istringstream stream(sourceText);
  return ParseIESStream(stream, path, fs::path(path).stem().string(),
                        sourceText, out);
}

const std::vector<IESProfile> &GetIESProfiles() { return s_iesProfiles; }

int LoadIESProfile(const std::string &path) {
  for (size_t i = 0; i < s_iesProfiles.size(); ++i) {
    if (s_iesProfiles[i].filePath == path) {
      // Profile already loaded, but ensure the GPU atlas is current.
      // This fixes the case where the atlas was lost across save/load
      // or was never built for this profile's slice.
      RebuildIESAtlas();
      return static_cast<int>(i);
    }
  }

  if (s_iesProfiles.size() >= static_cast<size_t>(kMaxIESSlices))
    return -1;

  IESProfile profile;
  if (!ParseIESFile(path, profile))
    return -1;

  profile.atlasSlice = static_cast<int>(s_iesProfiles.size());
  s_iesProfiles.push_back(std::move(profile));

  RebuildIESAtlas();
  return static_cast<int>(s_iesProfiles.size()) - 1;
}

int AddIESProfileFromSource(const std::string &sourceText,
                            const std::string &originalPath,
                            const std::string &displayName) {
  if (sourceText.empty()) {
    return -1;
  }
  IESProfile profile;
  std::istringstream stream(sourceText);
  if (!ParseIESStream(stream, originalPath, displayName, sourceText, profile)) {
    return -1;
  }
  return AddIESProfile(std::move(profile));
}

int AddIESProfile(IESProfile profile) {
  if (profile.numVerticalAngles <= 0 ||
      profile.numHorizontalAngles <= 0 ||
      profile.verticalAngles.size() !=
          static_cast<size_t>(profile.numVerticalAngles) ||
      profile.horizontalAngles.size() !=
          static_cast<size_t>(profile.numHorizontalAngles) ||
      profile.candela.size() !=
          static_cast<size_t>(profile.numVerticalAngles) *
              static_cast<size_t>(profile.numHorizontalAngles)) {
    return -1;
  }
  if (s_iesProfiles.size() >= static_cast<size_t>(kMaxIESSlices)) {
    return -1;
  }
  if (!profile.filePath.empty()) {
    for (size_t i = 0; i < s_iesProfiles.size(); ++i) {
      if (s_iesProfiles[i].filePath == profile.filePath) {
        // Profile already loaded, but ensure the GPU atlas is current.
        RebuildIESAtlas();
        return static_cast<int>(i);
      }
    }
  }
  if (profile.displayName.empty()) {
    profile.displayName = profile.filePath.empty()
                              ? "Embedded IES"
                              : fs::path(profile.filePath).stem().string();
  }
  profile.atlasSlice = static_cast<int>(s_iesProfiles.size());
  profile.loaded = true;
  profile.gpuReady = false;
  s_iesProfiles.push_back(std::move(profile));
  RebuildIESAtlas();
  return static_cast<int>(s_iesProfiles.size()) - 1;
}

void ClearIESProfiles() {
  s_iesProfiles.clear();
  for (auto &proto : s_lightPrototypes) {
    proto.iesProfileIndex = -1;
    proto.iesAtlasIndex = -1;
  }
  DxrRenderer::UpdateIESAtlas(nullptr, 0);
  UpdateLights();
}

void ClearIESProfile(int profileIndex) {
  if (profileIndex < 0 || profileIndex >= static_cast<int>(s_iesProfiles.size()))
    return;

  s_iesProfiles.erase(s_iesProfiles.begin() + profileIndex);

  // Shift remaining profiles' atlas slices
  for (size_t i = 0; i < s_iesProfiles.size(); ++i)
    s_iesProfiles[i].atlasSlice = static_cast<int>(i);

  // Update prototypes that referenced shifted profiles
  for (auto &proto : s_lightPrototypes) {
    if (proto.iesProfileIndex == profileIndex) {
      proto.iesProfileIndex = -1;
      proto.iesAtlasIndex = -1;
    } else if (proto.iesProfileIndex > profileIndex)
      proto.iesProfileIndex--;
  }

  RebuildIESAtlas();
}

void RebuildIESAtlas() {
  if (s_batchedUpdateDepth > 0) {
    s_batchedIESAtlasDirty = true;
    return;
  }
  if (s_rebuildingIESAtlas) {
    return;
  }

  s_rebuildingIESAtlas = true;

  const int sliceCount = static_cast<int>(s_iesProfiles.size());
  if (sliceCount == 0) {
    DxrRenderer::UpdateIESAtlas(nullptr, 0);
    UpdateLights();
    s_rebuildingIESAtlas = false;
    return;
  }

  const int texW = kIESAtlasResolution;
  const int texH = kIESAtlasResolution;
  std::vector<float> atlasData(texW * texH * sliceCount * 4, 0.0f);

  for (int slice = 0; slice < sliceCount; ++slice) {
    IESProfile &profile = s_iesProfiles[slice];
    profile.gpuReady = false;
    const int numVA = profile.numVerticalAngles;
    const int numHA = profile.numHorizontalAngles;
    const float mult = profile.multiplier;
    const auto &vAngles = profile.verticalAngles;
    const auto &hAngles = profile.horizontalAngles;
    const auto &candela = profile.candela;
    if (numVA <= 0 || numHA <= 0 ||
        candela.size() < static_cast<size_t>(numVA) *
                             static_cast<size_t>(numHA)) {
      profile.gpuReady = false;
      continue;
    }

    float *sliceData = atlasData.data() + (slice * texW * texH * 4);

    for (int y = 0; y < texH; ++y) {
      float theta = static_cast<float>(y) / static_cast<float>(texH - 1) * 180.0f;
      for (int x = 0; x < texW; ++x) {
        float phi = static_cast<float>(x) / static_cast<float>(texW - 1) * 360.0f;

        // Wrap phi for symmetry (IES often stores 0-90 or 0-180)
        float lookPhi = phi;
        if (numHA > 1) {
          float maxH = hAngles.back();
          if (maxH == 90.0f) {
            lookPhi = fmodf(phi, 90.0f);
            if ((static_cast<int>(phi / 90.0f) % 2) == 1)
              lookPhi = 90.0f - lookPhi;
          } else if (maxH == 180.0f) {
            lookPhi = fmodf(phi, 180.0f);
            if ((static_cast<int>(phi / 180.0f) % 2) == 1)
              lookPhi = 180.0f - lookPhi;
          }
        } else {
          lookPhi = 0.0f;
        }

        // Linear interpolation for vertical
        int v0 = 0;
        while (v0 < numVA - 2 && vAngles[v0 + 1] < theta)
          v0++;
        int v1 = std::min(v0 + 1, numVA - 1);
        float vLerp = (theta - vAngles[v0]) /
                      std::max(1e-5f, vAngles[v1] - vAngles[v0]);
        vLerp = std::clamp(vLerp, 0.0f, 1.0f);

        // Linear interpolation for horizontal
        int h0 = 0;
        while (h0 < numHA - 2 && hAngles[h0 + 1] < lookPhi)
          h0++;
        int h1 = std::min(h0 + 1, numHA - 1);
        float hLerp = (lookPhi - hAngles[h0]) /
                      std::max(1e-5f, hAngles[h1] - hAngles[h0]);
        hLerp = std::clamp(hLerp, 0.0f, 1.0f);

        float c00 = candela[h0 * numVA + v0];
        float c01 = candela[h0 * numVA + v1];
        float c10 = candela[h1 * numVA + v0];
        float c11 = candela[h1 * numVA + v1];

        float val = (c00 * (1.0f - vLerp) * (1.0f - hLerp) +
                     c01 * vLerp * (1.0f - hLerp) +
                     c10 * (1.0f - vLerp) * hLerp +
                     c11 * vLerp * hLerp) * mult;

        int pixelIdx = (y * texW + x) * 4;
        sliceData[pixelIdx + 0] = val;
        sliceData[pixelIdx + 1] = val;
        sliceData[pixelIdx + 2] = val;
        sliceData[pixelIdx + 3] = 1.0f;
      }
    }

  }

  const bool atlasUploaded = DxrRenderer::UpdateIESAtlas(atlasData.data(), sliceCount);
  if (atlasUploaded) {
    for (IESProfile &profile : s_iesProfiles) {
      const int numVA = profile.numVerticalAngles;
      const int numHA = profile.numHorizontalAngles;
      profile.gpuReady =
          numVA > 0 && numHA > 0 &&
          profile.candela.size() >=
              static_cast<size_t>(numVA) * static_cast<size_t>(numHA);
    }
  }
  UpdateLights();
  s_rebuildingIESAtlas = false;
}

bool EnsureIESAtlasReady() {
  if (s_rebuildingIESAtlas || s_iesProfiles.empty()) {
    return false;
  }

  bool needsRebuild = false;
  for (const LightPrototype &proto : s_lightPrototypes) {
    if (proto.iesProfileIndex < 0 ||
        proto.iesProfileIndex >= static_cast<int>(s_iesProfiles.size())) {
      continue;
    }
    const IESProfile &profile = s_iesProfiles[proto.iesProfileIndex];
    const bool hasValidProfile =
        profile.numVerticalAngles > 0 && profile.numHorizontalAngles > 0 &&
        profile.verticalAngles.size() ==
            static_cast<size_t>(profile.numVerticalAngles) &&
        profile.horizontalAngles.size() ==
            static_cast<size_t>(profile.numHorizontalAngles) &&
        profile.candela.size() >=
            static_cast<size_t>(profile.numVerticalAngles) *
                static_cast<size_t>(profile.numHorizontalAngles);
    if (hasValidProfile && !profile.gpuReady) {
      needsRebuild = true;
      break;
    }
  }
  if (!needsRebuild) {
    return false;
  }

  RebuildIESAtlas();
  return true;
}

int GetSelectedLightIndex() { return s_selectedLightIdx; }

std::vector<size_t> GetSelectedLightIndices() {
  std::vector<size_t> selected;
  for (size_t i = 0; i < s_lightInstances.size(); ++i) {
    if (s_lightInstances[i].selected) {
      selected.push_back(i);
    }
  }
  if (selected.empty() && s_selectedLightIdx >= 0 &&
      s_selectedLightIdx < static_cast<int>(s_lightInstances.size())) {
    selected.push_back(static_cast<size_t>(s_selectedLightIdx));
  }
  return selected;
}

static void ClearNodeSelectionForLightSelection() {
  for (auto &n : s_nodes) {
    n.selected = false;
  }
}

void SelectLight(int instanceIndex) {
  for (LightInstance &inst : s_lightInstances) {
    inst.selected = false;
  }
  if (instanceIndex < 0 || instanceIndex >= (int)s_lightInstances.size()) {
    s_selectedLightIdx = -1;
    NotifySceneChanged();
    return;
  }
  s_selectedLightIdx = instanceIndex;
  s_lightInstances[static_cast<size_t>(instanceIndex)].selected = true;
  ClearNodeSelectionForLightSelection();
  NotifySceneChanged();
}

void SelectLights(const std::vector<size_t> &instanceIndices) {
  for (LightInstance &inst : s_lightInstances) {
    inst.selected = false;
  }

  s_selectedLightIdx = -1;
  for (size_t index : instanceIndices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    s_lightInstances[index].selected = true;
    s_selectedLightIdx = static_cast<int>(index);
  }

  if (s_selectedLightIdx >= 0) {
    ClearNodeSelectionForLightSelection();
  }
  NotifySceneChanged();
}

void ToggleLightSelection(size_t instanceIndex) {
  if (instanceIndex >= s_lightInstances.size()) {
    return;
  }

  s_lightInstances[instanceIndex].selected =
      !s_lightInstances[instanceIndex].selected;
  if (s_lightInstances[instanceIndex].selected) {
    s_selectedLightIdx = static_cast<int>(instanceIndex);
    ClearNodeSelectionForLightSelection();
  } else if (s_selectedLightIdx == static_cast<int>(instanceIndex)) {
    s_selectedLightIdx = -1;
    for (size_t i = 0; i < s_lightInstances.size(); ++i) {
      if (s_lightInstances[i].selected) {
        s_selectedLightIdx = static_cast<int>(i);
      }
    }
  }
  NotifySceneChanged();
}

size_t AddLightPrototype(LightType type) {
  LightPrototype proto;
  proto.type = (uint32_t)type;
  switch (type) {
  case LightType::Omni:
  case LightType::IES:
    proto.radius = 0.1f;
    break;
  case LightType::Spot:
    proto.innerConeAngle = cosf(DirectX::XMConvertToRadians(30.0f));
    proto.outerConeAngle = cosf(DirectX::XMConvertToRadians(45.0f));
    break;
  case LightType::AreaRect:
  case LightType::AreaDisk:
    proto.areaExtents[0] = 1.0f;
    proto.areaExtents[1] = 1.0f;
    break;
  default:
    break;
  }
  size_t protoIdx = s_lightPrototypes.size();
  EnsureLightPrototypeName(proto, protoIdx);
  s_lightPrototypes.push_back(proto);

  // Auto-create one instance
  LightInstance inst;
  inst.prototypeIndex = protoIdx;
  s_lightInstances.push_back(inst);

  UpdateLights();
  return protoIdx;
}

size_t AddLightPrototypeRaw(const LightPrototype &proto) {
  size_t idx = s_lightPrototypes.size();
  LightPrototype namedProto = proto;
  EnsureLightPrototypeName(namedProto, idx);
  s_lightPrototypes.push_back(namedProto);
  return idx;
}

bool UpdateLightPrototype(size_t index, const LightPrototype &proto) {
  if (index >= s_lightPrototypes.size())
    return false;
  LightPrototype namedProto = proto;
  EnsureLightPrototypeName(namedProto, index);
  if (memcmp(&s_lightPrototypes[index], &namedProto, sizeof(LightPrototype)) == 0)
    return true;
  s_lightPrototypes[index] = namedProto;
  UpdateLights();
  return true;
}

void RemoveLightPrototype(size_t index) {
  if (index >= s_lightPrototypes.size())
    return;

  // Remove all instances belonging to this prototype (reverse order)
  for (int i = (int)s_lightInstances.size() - 1; i >= 0; --i) {
    if (s_lightInstances[i].prototypeIndex == index) {
      if (s_selectedLightIdx == i) {
        s_selectedLightIdx = -1;
      } else if (s_selectedLightIdx > i) {
        --s_selectedLightIdx;
      }
      LiveLink::GetSceneSync().ReindexSceneLightBindingsAfterRemoval(i);
      s_lightInstances.erase(s_lightInstances.begin() + i);
    }
  }

  // Fix up prototype indices for instances that referenced prototypes after the removed one
  for (auto &inst : s_lightInstances) {
    if (inst.prototypeIndex > index)
      --inst.prototypeIndex;
  }

  s_lightPrototypes.erase(s_lightPrototypes.begin() + index);
  UpdateLights();
  NotifySceneChanged();
}

int FindLightPrototypeByStableId(const std::string &stableId) {
  if (stableId.empty()) return -1;
  for (size_t i = 0; i < s_lightPrototypes.size(); ++i) {
    if (s_lightPrototypes[i].stableId[0] != '\0' &&
        stableId == s_lightPrototypes[i].stableId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool SetLightPrototypeStableId(size_t index, const std::string &stableId) {
  if (index >= s_lightPrototypes.size() || stableId.empty()) return false;
  const size_t maxLen = sizeof(LightPrototype::stableId) - 1;
  const size_t copyLen = (std::min)(stableId.size(), maxLen);
  std::memcpy(s_lightPrototypes[index].stableId, stableId.c_str(), copyLen);
  s_lightPrototypes[index].stableId[copyLen] = '\0';
  return true;
}

size_t DuplicateLightInstanceAsInstance(size_t instanceIndex) {
  if (instanceIndex >= s_lightInstances.size()) {
    return static_cast<size_t>(-1);
  }
  LightInstance inst = s_lightInstances[instanceIndex];
  const size_t newIndex = s_lightInstances.size();
  inst.position[0] += 0.5f;
  s_lightInstances.push_back(inst);
  SelectLight(static_cast<int>(newIndex));
  UpdateLights();
  NotifySceneChanged();
  return newIndex;
}

size_t DuplicateLightInstanceAsCopy(size_t instanceIndex) {
  if (instanceIndex >= s_lightInstances.size()) {
    return static_cast<size_t>(-1);
  }
  const LightInstance &sourceInst = s_lightInstances[instanceIndex];
  if (sourceInst.prototypeIndex >= s_lightPrototypes.size()) {
    return static_cast<size_t>(-1);
  }

  LightPrototype proto = s_lightPrototypes[sourceInst.prototypeIndex];
  std::snprintf(proto.name, sizeof(proto.name), "%s Copy",
                s_lightPrototypes[sourceInst.prototypeIndex].name[0] != '\0'
                    ? s_lightPrototypes[sourceInst.prototypeIndex].name
                    : LightTypeLabel(static_cast<LightType>(proto.type)));
  const size_t protoIndex = s_lightPrototypes.size();
  s_lightPrototypes.push_back(proto);

  LightInstance inst = sourceInst;
  inst.prototypeIndex = protoIndex;
  inst.position[0] += 0.5f;
  const size_t newIndex = s_lightInstances.size();
  s_lightInstances.push_back(inst);
  SelectLight(static_cast<int>(newIndex));
  UpdateLights();
  NotifySceneChanged();
  return newIndex;
}

int MergeCompatibleLightPrototypes(size_t targetPrototypeIndex) {
  if (targetPrototypeIndex >= s_lightPrototypes.size()) {
    return 0;
  }

  int movedInstances = 0;
  for (int protoIndex = static_cast<int>(s_lightPrototypes.size()) - 1;
       protoIndex >= 0; --protoIndex) {
    if (static_cast<size_t>(protoIndex) == targetPrototypeIndex) {
      continue;
    }
    if (!LightPrototypeSharedFieldsEqual(
            s_lightPrototypes[targetPrototypeIndex],
            s_lightPrototypes[static_cast<size_t>(protoIndex)])) {
      continue;
    }

    const size_t removedIndex = static_cast<size_t>(protoIndex);
    const size_t remappedTarget =
        (targetPrototypeIndex > removedIndex) ? targetPrototypeIndex - 1
                                              : targetPrototypeIndex;
    for (auto &inst : s_lightInstances) {
      if (inst.prototypeIndex == static_cast<size_t>(protoIndex)) {
        inst.prototypeIndex = remappedTarget;
        ++movedInstances;
      } else if (inst.prototypeIndex > static_cast<size_t>(protoIndex)) {
        --inst.prototypeIndex;
      }
    }
    s_lightPrototypes.erase(s_lightPrototypes.begin() + protoIndex);
    if (targetPrototypeIndex > static_cast<size_t>(protoIndex)) {
      --targetPrototypeIndex;
    }
  }

  if (movedInstances > 0) {
    UpdateLights();
    NotifySceneChanged();
  }
  return movedInstances;
}

size_t AddLightInstance(size_t prototypeIndex) {
  if (prototypeIndex >= s_lightPrototypes.size())
    return (size_t)-1;

  LightInstance inst;
  inst.prototypeIndex = prototypeIndex;
  size_t idx = s_lightInstances.size();
  s_lightInstances.push_back(inst);
  UpdateLights();
  return idx;
}

size_t AddLightInstanceRaw(const LightInstance &inst) {
  size_t idx = s_lightInstances.size();
  s_lightInstances.push_back(inst);
  return idx;
}

bool UpdateLightInstance(size_t index, const LightInstance &inst) {
  if (index >= s_lightInstances.size())
    return false;
  if (inst.prototypeIndex >= s_lightPrototypes.size())
    return false;
  s_lightInstances[index] = inst;
  UpdateLights();
  return true;
}

void RemoveLightInstance(size_t index) {
  if (index >= s_lightInstances.size())
    return;

  size_t protoIdx = s_lightInstances[index].prototypeIndex;

  // Update selection
  if (s_selectedLightIdx == (int)index) {
    s_selectedLightIdx = -1;
  } else if (s_selectedLightIdx > (int)index) {
    --s_selectedLightIdx;
  }

  LiveLink::GetSceneSync().ReindexSceneLightBindingsAfterRemoval(index);
  s_lightInstances.erase(s_lightInstances.begin() + index);

  // If no more instances reference this prototype, remove the prototype too
  bool hasOtherInstances = false;
  for (const auto &inst : s_lightInstances) {
    if (inst.prototypeIndex == protoIdx) {
      hasOtherInstances = true;
      break;
    }
  }
  if (!hasOtherInstances) {
    s_lightPrototypes.erase(s_lightPrototypes.begin() + protoIdx);
    // Fix up prototype indices
    for (auto &inst : s_lightInstances) {
      if (inst.prototypeIndex > protoIdx)
        --inst.prototypeIndex;
    }
  }

  UpdateLights();
  NotifySceneChanged();
}

void RemoveLightInstances(const std::vector<size_t> &indices) {
  std::vector<size_t> removeIndices;
  removeIndices.reserve(indices.size());
  for (size_t index : indices) {
    if (index < s_lightInstances.size()) {
      removeIndices.push_back(index);
    }
  }
  std::sort(removeIndices.begin(), removeIndices.end());
  removeIndices.erase(std::unique(removeIndices.begin(), removeIndices.end()),
                      removeIndices.end());
  if (removeIndices.empty()) {
    return;
  }

  std::vector<size_t> affectedPrototypes;
  affectedPrototypes.reserve(removeIndices.size());
  for (size_t index : removeIndices) {
    const size_t protoIdx = s_lightInstances[index].prototypeIndex;
    if (protoIdx < s_lightPrototypes.size()) {
      affectedPrototypes.push_back(protoIdx);
    }
  }

  for (auto it = removeIndices.rbegin(); it != removeIndices.rend(); ++it) {
    const size_t index = *it;
    if (index >= s_lightInstances.size()) {
      continue;
    }
    LiveLink::GetSceneSync().ReindexSceneLightBindingsAfterRemoval(
        static_cast<int>(index));
    s_lightInstances.erase(s_lightInstances.begin() + index);
  }

  std::sort(affectedPrototypes.begin(), affectedPrototypes.end());
  affectedPrototypes.erase(
      std::unique(affectedPrototypes.begin(), affectedPrototypes.end()),
      affectedPrototypes.end());
  for (auto it = affectedPrototypes.rbegin(); it != affectedPrototypes.rend();
       ++it) {
    const size_t protoIdx = *it;
    if (protoIdx >= s_lightPrototypes.size()) {
      continue;
    }
    const bool hasOtherInstances = std::any_of(
        s_lightInstances.begin(), s_lightInstances.end(),
        [protoIdx](const LightInstance &inst) {
          return inst.prototypeIndex == protoIdx;
        });
    if (hasOtherInstances) {
      continue;
    }

    s_lightPrototypes.erase(s_lightPrototypes.begin() + protoIdx);
    for (auto &inst : s_lightInstances) {
      if (inst.prototypeIndex > protoIdx) {
        --inst.prototypeIndex;
      }
    }
  }

  for (LightInstance &inst : s_lightInstances) {
    inst.selected = false;
  }
  s_selectedLightIdx = -1;

  UpdateLights();
  NotifySceneChanged();
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
  RefreshTextureCompressionForMaterials(false);
  DxrRenderer::MarkMaterialDirty(static_cast<int>(index));
  ApplyRendererInvalidation(RendererInvalidationPlan::AccumulationOnly);
  NotifySceneChanged();
  return true;
}

void RefreshAllMaterialRuntimeTextures() {
  for (Asset::Material &material : g_loadedMaterials) {
    RefreshMaterialRuntimeTexture(material);
  }
  RefreshTextureCompressionForMaterials(true);
}

void RefreshTextureCompression(bool resetAccumulation) {
  WaitGPUIdle();
  RefreshTextureCompressionForMaterials(resetAccumulation);
}

void DrawLightsPanel(bool &visible) {
  if (!visible)
    return;
  if (ImGui::Begin("Global Lights", &visible)) {
    if (ImGui::Button("Add Point Light"))
      AddLightPrototype(LightType::Omni);
    ImGui::SameLine();
    if (ImGui::Button("Add Spot Light"))
      AddLightPrototype(LightType::Spot);
    ImGui::SameLine();
    if (ImGui::Button("Add Rect Area"))
      AddLightPrototype(LightType::AreaRect);
    ImGui::SameLine();
    if (ImGui::Button("Add Disk Area"))
      AddLightPrototype(LightType::AreaDisk);

    ImGui::Separator();

    for (size_t p = 0; p < s_lightPrototypes.size(); ++p) {
      ImGui::PushID((int)p);
      LightPrototype &proto = s_lightPrototypes[p];

      const char *typeStr = "Unknown";
      switch ((LightType)proto.type) {
      case LightType::Directional: typeStr = "Sun"; break;
      case LightType::Omni:        typeStr = "Omni"; break;
      case LightType::Spot:        typeStr = "Spot"; break;
      case LightType::AreaRect:    typeStr = "Rect"; break;
      case LightType::AreaDisk:    typeStr = "Disk"; break;
      case LightType::IES:         typeStr = "IES";  break;
      }

      // Count instances for this prototype
      int instCount = 0;
      for (const auto &inst : s_lightInstances) {
        if (inst.prototypeIndex == p) ++instCount;
      }

      char buf[128];
      snprintf(buf, sizeof(buf), "Proto %zu (%s) [%d inst]", p, typeStr, instCount);
      bool headerOpen = ImGui::CollapsingHeader(buf);

      if (headerOpen) {
        bool protoChanged = false;

        // Shared properties
        float color[3] = {proto.color[0], proto.color[1], proto.color[2]};
        if (ImGui::ColorEdit3("Color", color)) {
          proto.color[0] = color[0]; proto.color[1] = color[1]; proto.color[2] = color[2];
          protoChanged = true;
        }
        if (ImGui::DragFloat("Intensity", &proto.intensity, 10.0f, 0.0f, 1000000.0f, "%.2f"))
          protoChanged = true;
        if (ImGui::DragFloat("Radius", &proto.radius, 0.01f, 0.0f, 10.0f))
          protoChanged = true;

        if (proto.type == (uint32_t)LightType::Spot) {
          float inner = acosf(proto.innerConeAngle) * 180.0f / 3.14159f;
          float outer = acosf(proto.outerConeAngle) * 180.0f / 3.14159f;
          if (ImGui::SliderFloat("Inner Angle", &inner, 0, 90)) {
            proto.innerConeAngle = cosf(DirectX::XMConvertToRadians(inner));
            protoChanged = true;
          }
          if (ImGui::SliderFloat("Outer Angle", &outer, inner, 90)) {
            proto.outerConeAngle = cosf(DirectX::XMConvertToRadians(outer));
            protoChanged = true;
          }
        }

        if (proto.type == (uint32_t)LightType::AreaRect ||
            proto.type == (uint32_t)LightType::AreaDisk) {
          protoChanged |= ImGui::DragFloat2("Extents", proto.areaExtents, 0.1f, 0.01f, 50.0f);
        }

        if (proto.type == (uint32_t)LightType::IES) {
          if (proto.iesProfileIndex >= 0 &&
              proto.iesProfileIndex < static_cast<int>(s_iesProfiles.size())) {
            const auto &profile = s_iesProfiles[proto.iesProfileIndex];
            ImGui::Text("IES Profile: %s", profile.displayName.c_str());
            ImGui::Text("Atlas Slice: %d", proto.iesAtlasIndex);
          } else {
            ImGui::Text("IES Profile: None");
          }

          if (ImGui::Button("Load IES File...")) {
            char filename[MAX_PATH] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "IES Files (*.ies)\0*.ies\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
              int profIdx = LoadIESProfile(filename);
              if (profIdx >= 0) {
                proto.iesProfileIndex = profIdx;
                protoChanged = true;
              }
            }
          }
          ImGui::SameLine();
          if (ImGui::Button("Clear IES")) {
            proto.iesProfileIndex = -1;
            proto.iesAtlasIndex = -1;
            protoChanged = true;
          }
        }

        ImGui::Checkbox("Enabled", &proto.enabled);
        if (ImGui::IsItemDeactivatedAfterEdit()) protoChanged = true;

        if (protoChanged)
          UpdateLightPrototype(p, proto);

        ImGui::Separator();
        ImGui::Text("Instances:");

        // List instances for this prototype
        for (size_t i = 0; i < s_lightInstances.size(); ++i) {
          LightInstance &inst = s_lightInstances[i];
          if (inst.prototypeIndex != p) continue;

          ImGui::PushID((int)(i + 100000));
          bool isSelected = (s_selectedLightIdx == (int)i);
          if (isSelected)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.7f));

          char instLabel[64];
          snprintf(instLabel, sizeof(instLabel), "Instance %zu", i);
          bool instOpen = ImGui::TreeNodeEx(instLabel,
              ImGuiTreeNodeFlags_DefaultOpen | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));

          if (ImGui::IsItemClicked(0)) {
            s_selectedLightIdx = (int)i;
            for (auto &n : s_nodes) n.selected = false;
          }

          if (isSelected)
            ImGui::PopStyleColor();

          if (instOpen) {
            bool instChanged = false;
            instChanged |= ImGui::DragFloat3("Position", inst.position, 0.1f);
            instChanged |= ImGui::DragFloat3("Direction", inst.direction, 0.01f);
            ImGui::Checkbox("Enabled", &inst.enabled);
            if (ImGui::IsItemDeactivatedAfterEdit()) instChanged = true;

            if (ImGui::Button("Select for Gizmo")) {
              s_selectedLightIdx = (int)i;
              for (auto &n : s_nodes) n.selected = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Instance")) {
              RemoveLightInstance(i);
              ImGui::TreePop();
              ImGui::PopID();
              ImGui::PopID();
              break;
            }
            if (instChanged)
              UpdateLightInstance(i, inst);
            ImGui::TreePop();
          }
          ImGui::PopID();
        }

        // Add instance button
        if (ImGui::Button("Add Instance")) {
          size_t newIdx = AddLightInstance(p);
          s_selectedLightIdx = (int)newIdx;
          for (auto &n : s_nodes) n.selected = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Prototype")) {
          RemoveLightPrototype(p);
          ImGui::PopID();
          break;
        }
      }
      ImGui::PopID();
    }
  }
  ImGui::End();
}

static bool NormalizeLightVec(float v[3]) {
  const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (len2 <= 1e-12f) {
    return false;
  }
  const float invLen = 1.0f / sqrtf(len2);
  v[0] *= invLen;
  v[1] *= invLen;
  v[2] *= invLen;
  return true;
}

static void CrossLightVec(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static void BuildLightBasis(const LightInstance &inst, float right[3],
                            float up[3], float fwd[3]) {
  fwd[0] = inst.direction[0];
  fwd[1] = inst.direction[1];
  fwd[2] = inst.direction[2];
  if (!NormalizeLightVec(fwd)) {
    fwd[0] = 0.0f;
    fwd[1] = -1.0f;
    fwd[2] = 0.0f;
  }

  float upRef[3] = {0.0f, 1.0f, 0.0f};
  if (fabsf(fwd[1]) > 0.96f) {
    upRef[0] = 1.0f;
    upRef[1] = 0.0f;
    upRef[2] = 0.0f;
  }
  CrossLightVec(upRef, fwd, right);
  if (!NormalizeLightVec(right)) {
    right[0] = 1.0f;
    right[1] = 0.0f;
    right[2] = 0.0f;
  }
  CrossLightVec(fwd, right, up);
  NormalizeLightVec(up);
}

static bool ProjectLightPoint(const float wp[3], const float view[16],
                              const float proj[16], float windowX,
                              float windowY, float windowWidth,
                              float windowHeight, ImVec2 &outSp) {
  float viewPos[4];
  for (int i = 0; i < 4; i++) {
    viewPos[i] = wp[0] * view[0 * 4 + i] + wp[1] * view[1 * 4 + i] +
                 wp[2] * view[2 * 4 + i] + view[3 * 4 + i];
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

  outSp.x = windowX + (clipPos[0] / clipPos[3] + 1.0f) * 0.5f * windowWidth;
  outSp.y = windowY + (1.0f - clipPos[1] / clipPos[3]) * 0.5f * windowHeight;
  return true;
}

static ImU32 LightGizmoColor(const LightPrototype &proto,
                             const LightInstance &inst, bool selected,
                             int alphaOverride = -1) {
  if (selected) {
    return IM_COL32(255, 202, 72, alphaOverride >= 0 ? alphaOverride : 255);
  }
  const bool enabled = proto.enabled && inst.enabled;
  const int alpha = alphaOverride >= 0 ? alphaOverride : (enabled ? 185 : 72);
  if (!enabled) {
    return IM_COL32(145, 145, 145, alpha);
  }
  const int r = static_cast<int>(std::clamp(proto.color[0], 0.0f, 1.0f) * 210.0f + 35.0f);
  const int g = static_cast<int>(std::clamp(proto.color[1], 0.0f, 1.0f) * 210.0f + 35.0f);
  const int b = static_cast<int>(std::clamp(proto.color[2], 0.0f, 1.0f) * 210.0f + 35.0f);
  return IM_COL32(std::min(r, 255), std::min(g, 255), std::min(b, 255), alpha);
}

static float DistanceToSegment2D(ImVec2 p, ImVec2 a, ImVec2 b) {
  const float vx = b.x - a.x;
  const float vy = b.y - a.y;
  const float wx = p.x - a.x;
  const float wy = p.y - a.y;
  const float len2 = vx * vx + vy * vy;
  float t = len2 > 1e-5f ? (wx * vx + wy * vy) / len2 : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  const float dx = p.x - (a.x + vx * t);
  const float dy = p.y - (a.y + vy * t);
  return sqrtf(dx * dx + dy * dy);
}

static float LightGizmoReach(const LightPrototype &proto) {
  const float intensityScale = sqrtf(std::max(0.0f, proto.intensity)) * 0.035f;
  return std::clamp(0.9f + intensityScale, 0.9f, 4.5f);
}

static void AddWorldLine(ImDrawList *drawList, const float a[3], const float b[3],
                         const float view[16], const float proj[16],
                         float windowX, float windowY, float windowWidth,
                         float windowHeight, ImU32 col, float thickness) {
  ImVec2 sa;
  ImVec2 sb;
  if (ProjectLightPoint(a, view, proj, windowX, windowY, windowWidth,
                        windowHeight, sa) &&
      ProjectLightPoint(b, view, proj, windowX, windowY, windowWidth,
                        windowHeight, sb)) {
    drawList->AddLine(sa, sb, col, thickness);
  }
}

static void AddWorldCircle(ImDrawList *drawList, const float center[3],
                           const float axisA[3], const float axisB[3],
                           float radius, int segments, const float view[16],
                           const float proj[16], float windowX, float windowY,
                           float windowWidth, float windowHeight, ImU32 col,
                           float thickness) {
  ImVec2 first;
  ImVec2 prev;
  bool haveFirst = false;
  bool havePrev = false;
  for (int s = 0; s <= segments; ++s) {
    const float t = (static_cast<float>(s) / static_cast<float>(segments)) *
                    6.28318530718f;
    const float p[3] = {
        center[0] + (axisA[0] * cosf(t) + axisB[0] * sinf(t)) * radius,
        center[1] + (axisA[1] * cosf(t) + axisB[1] * sinf(t)) * radius,
        center[2] + (axisA[2] * cosf(t) + axisB[2] * sinf(t)) * radius};
    ImVec2 sp;
    if (!ProjectLightPoint(p, view, proj, windowX, windowY, windowWidth,
                           windowHeight, sp)) {
      havePrev = false;
      continue;
    }
    if (!haveFirst) {
      first = sp;
      haveFirst = true;
    }
    if (havePrev) {
      drawList->AddLine(prev, sp, col, thickness);
    }
    prev = sp;
    havePrev = true;
  }
  if (haveFirst && havePrev) {
    drawList->AddLine(prev, first, col, thickness);
  }
}

static void AddScreenArrow(ImDrawList *drawList, ImVec2 a, ImVec2 b, ImU32 col,
                           float thickness) {
  drawList->AddLine(a, b, col, thickness);
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) {
    return;
  }
  const float ux = dx / len;
  const float uy = dy / len;
  const float px = -uy;
  const float py = ux;
  const float head = 8.0f;
  drawList->AddLine(b, ImVec2(b.x - ux * head + px * head * 0.45f,
                              b.y - uy * head + py * head * 0.45f),
                    col, thickness);
  drawList->AddLine(b, ImVec2(b.x - ux * head - px * head * 0.45f,
                              b.y - uy * head - py * head * 0.45f),
                    col, thickness);
}

static int CountLightPrototypeInstances(size_t prototypeIndex) {
  int count = 0;
  for (const LightInstance &inst : s_lightInstances) {
    if (inst.prototypeIndex == prototypeIndex) {
      ++count;
    }
  }
  return count;
}

static void DrawLightModel(ImDrawList *drawList, size_t instanceIndex,
                           const LightPrototype &proto,
                           const LightInstance &inst, const float view[16],
                           const float proj[16], float windowX, float windowY,
                           float windowWidth, float windowHeight,
                           bool selected, bool simple) {
  ImVec2 center;
  if (!ProjectLightPoint(inst.position, view, proj, windowX, windowY,
                         windowWidth, windowHeight, center)) {
    return;
  }

  const ImU32 col = LightGizmoColor(proto, inst, selected);
  const ImU32 dimCol = LightGizmoColor(proto, inst, selected, selected ? 120 : 84);
  const float thick = selected ? 2.6f : 1.35f;
  drawList->AddCircleFilled(center, selected ? 4.8f : 3.6f, col, 16);
  drawList->AddCircle(center, selected ? 8.5f : 6.5f, col, 16, thick);

  float right[3], up[3], fwd[3];
  BuildLightBasis(inst, right, up, fwd);
  const LightType type = static_cast<LightType>(proto.type);
  const int segments = selected ? 36 : 20;

  if (!simple) {
    if (type == LightType::Omni) {
      const float radius = std::max(0.18f, proto.radius);
      AddWorldCircle(drawList, inst.position, right, up, radius, segments, view,
                     proj, windowX, windowY, windowWidth, windowHeight, col,
                     thick);
      AddWorldCircle(drawList, inst.position, right, fwd, radius, segments, view,
                     proj, windowX, windowY, windowWidth, windowHeight, dimCol,
                     1.0f);
      AddWorldCircle(drawList, inst.position, up, fwd, radius, segments, view,
                     proj, windowX, windowY, windowWidth, windowHeight, dimCol,
                     1.0f);
    } else if (type == LightType::Spot || type == LightType::IES) {
      const float length = LightGizmoReach(proto);
      const float outerAngle =
          acosf(std::clamp(proto.outerConeAngle, -0.999f, 0.999f));
      const float innerAngle =
          acosf(std::clamp(proto.innerConeAngle, -0.999f, 0.999f));
      const float outerRadius = tanf(outerAngle) * length;
      const float innerRadius = tanf(innerAngle) * length;
      const float ringCenter[3] = {inst.position[0] + fwd[0] * length,
                                   inst.position[1] + fwd[1] * length,
                                   inst.position[2] + fwd[2] * length};
      AddWorldCircle(drawList, ringCenter, right, up, outerRadius, segments,
                     view, proj, windowX, windowY, windowWidth, windowHeight,
                     col, thick);
      AddWorldCircle(drawList, ringCenter, right, up, innerRadius, segments,
                     view, proj, windowX, windowY, windowWidth, windowHeight,
                     dimCol, 1.0f);
      for (int k = 0; k < 4; ++k) {
        const float sx = (k == 0) ? 1.0f : ((k == 1) ? -1.0f : 0.0f);
        const float sy = (k == 2) ? 1.0f : ((k == 3) ? -1.0f : 0.0f);
        const float edge[3] = {ringCenter[0] + (right[0] * sx + up[0] * sy) * outerRadius,
                               ringCenter[1] + (right[1] * sx + up[1] * sy) * outerRadius,
                               ringCenter[2] + (right[2] * sx + up[2] * sy) * outerRadius};
        AddWorldLine(drawList, inst.position, edge, view, proj, windowX, windowY,
                     windowWidth, windowHeight, col, 1.0f);
      }
      AddWorldLine(drawList, inst.position, ringCenter, view, proj, windowX,
                   windowY, windowWidth, windowHeight, col, thick);
      if (type == LightType::IES || proto.iesProfileIndex >= 0) {
        for (int k = 0; k < 10; ++k) {
          const float t = (static_cast<float>(k) / 10.0f) * 6.28318530718f;
          const float lobe = 0.18f + ((k % 2) ? 0.12f : 0.28f);
          const float a[3] = {inst.position[0] + fwd[0] * 0.18f,
                              inst.position[1] + fwd[1] * 0.18f,
                              inst.position[2] + fwd[2] * 0.18f};
          const float b[3] = {a[0] + (right[0] * cosf(t) + up[0] * sinf(t)) * lobe,
                              a[1] + (right[1] * cosf(t) + up[1] * sinf(t)) * lobe,
                              a[2] + (right[2] * cosf(t) + up[2] * sinf(t)) * lobe};
          AddWorldLine(drawList, a, b, view, proj, windowX, windowY,
                       windowWidth, windowHeight, dimCol, 1.0f);
        }
      }
    } else if (type == LightType::AreaRect || type == LightType::AreaDisk) {
      const float hw = std::max(0.02f, proto.areaExtents[0] * 0.5f);
      const float hh = std::max(0.02f, proto.areaExtents[1] * 0.5f);
      if (type == LightType::AreaDisk) {
        AddWorldCircle(drawList, inst.position, right, up, std::max(hw, hh),
                       selected ? 40 : 24, view, proj, windowX, windowY,
                       windowWidth, windowHeight, col, thick);
        AddWorldCircle(drawList, inst.position, right, up, std::min(hw, hh),
                       selected ? 32 : 18, view, proj, windowX, windowY,
                       windowWidth, windowHeight, dimCol, 1.0f);
      } else {
        float c[4][3];
        for (int k = 0; k < 4; ++k) {
          const float sx = (k == 0 || k == 3) ? 1.0f : -1.0f;
          const float sy = (k < 2) ? 1.0f : -1.0f;
          c[k][0] = inst.position[0] + right[0] * hw * sx + up[0] * hh * sy;
          c[k][1] = inst.position[1] + right[1] * hw * sx + up[1] * hh * sy;
          c[k][2] = inst.position[2] + right[2] * hw * sx + up[2] * hh * sy;
        }
        for (int k = 0; k < 4; ++k) {
          AddWorldLine(drawList, c[k], c[(k + 1) % 4], view, proj, windowX,
                       windowY, windowWidth, windowHeight, col, thick);
        }
        AddWorldLine(drawList, c[0], c[2], view, proj, windowX, windowY,
                     windowWidth, windowHeight, dimCol, 1.0f);
        AddWorldLine(drawList, c[1], c[3], view, proj, windowX, windowY,
                     windowWidth, windowHeight, dimCol, 1.0f);
      }
      const float arrowEnd[3] = {inst.position[0] + fwd[0] * 0.7f,
                                 inst.position[1] + fwd[1] * 0.7f,
                                 inst.position[2] + fwd[2] * 0.7f};
      AddWorldLine(drawList, inst.position, arrowEnd, view, proj, windowX,
                   windowY, windowWidth, windowHeight, col, thick);
    } else if (type == LightType::Directional) {
      AddWorldCircle(drawList, inst.position, right, up, 0.28f, 24, view, proj,
                     windowX, windowY, windowWidth, windowHeight, col, thick);
      for (int k = -1; k <= 1; ++k) {
        const float base[3] = {inst.position[0] + right[0] * 0.22f * k,
                               inst.position[1] + right[1] * 0.22f * k,
                               inst.position[2] + right[2] * 0.22f * k};
        const float end[3] = {base[0] + fwd[0] * 0.75f,
                              base[1] + fwd[1] * 0.75f,
                              base[2] + fwd[2] * 0.75f};
        AddWorldLine(drawList, base, end, view, proj, windowX, windowY,
                     windowWidth, windowHeight, col, thick);
      }
    }
  }

  if (selected) {
    const int groupCount = CountLightPrototypeInstances(inst.prototypeIndex);
    char label[128];
    std::snprintf(label, sizeof(label), "%s  #%zu / %d",
                  proto.name[0] ? proto.name : LightTypeLabel(type),
                  instanceIndex, groupCount);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 textPos(center.x + 12.0f, center.y - textSize.y * 0.5f);
    drawList->AddRectFilled(ImVec2(textPos.x - 5.0f, textPos.y - 3.0f),
                            ImVec2(textPos.x + textSize.x + 5.0f,
                                   textPos.y + textSize.y + 3.0f),
                            IM_COL32(18, 20, 22, 190), 4.0f);
    drawList->AddText(textPos, col, label);
  }
}

void SetLightGizmosVisible(bool visible) { s_lightGizmosVisible = visible; }

bool AreLightGizmosVisible() { return s_lightGizmosVisible; }

bool IsTransformGizmoActiveOrHovered() {
  return ImGuizmo::IsUsing() || ImGuizmo::IsOver();
}

int PickLightGizmoAt(float screenX, float screenY, float screenWidth,
                     float screenHeight) {
  if (ImGuizmo::IsUsing() || screenWidth <= 1.0f || screenHeight <= 1.0f) {
    return -1;
  }

  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);
  float windowX = 0.0f, windowY = 0.0f, windowWidth = screenWidth,
        windowHeight = screenHeight;
  GetRenderViewportRect(&windowX, &windowY, &windowWidth, &windowHeight);
  const ImVec2 mouse(screenX, screenY);
  if (mouse.x < windowX || mouse.y < windowY ||
      mouse.x > windowX + windowWidth || mouse.y > windowY + windowHeight) {
    return -1;
  }

  int bestIndex = -1;
  float bestDistance = 18.0f;
  for (size_t i = 0; i < s_lightInstances.size(); ++i) {
    const LightInstance &inst = s_lightInstances[i];
    if (inst.prototypeIndex >= s_lightPrototypes.size()) {
      continue;
    }
    const LightPrototype &proto = s_lightPrototypes[inst.prototypeIndex];
    ImVec2 center;
    if (!ProjectLightPoint(inst.position, view, proj, windowX, windowY,
                           windowWidth, windowHeight, center)) {
      continue;
    }

    float distance = sqrtf((mouse.x - center.x) * (mouse.x - center.x) +
                           (mouse.y - center.y) * (mouse.y - center.y));
    float right[3], up[3], fwd[3];
    BuildLightBasis(inst, right, up, fwd);
    const LightType type = static_cast<LightType>(proto.type);
    const float length =
        (type == LightType::Spot || type == LightType::IES)
            ? LightGizmoReach(proto)
            : 0.8f;
    const float end[3] = {inst.position[0] + fwd[0] * length,
                          inst.position[1] + fwd[1] * length,
                          inst.position[2] + fwd[2] * length};
    ImVec2 endScreen;
    if (ProjectLightPoint(end, view, proj, windowX, windowY, windowWidth,
                          windowHeight, endScreen)) {
      distance = std::min(distance, DistanceToSegment2D(mouse, center, endScreen));
    }

    if (type == LightType::AreaRect || type == LightType::AreaDisk) {
      const float hw = std::max(0.02f, proto.areaExtents[0] * 0.5f);
      const float hh = std::max(0.02f, proto.areaExtents[1] * 0.5f);
      ImVec2 prev;
      bool havePrev = false;
      const int samples = (type == LightType::AreaDisk) ? 32 : 4;
      for (int k = 0; k <= samples; ++k) {
        float sx = 0.0f;
        float sy = 0.0f;
        if (type == LightType::AreaDisk) {
          const float t = (static_cast<float>(k) / static_cast<float>(samples)) *
                          6.28318530718f;
          sx = cosf(t);
          sy = sinf(t);
        } else {
          const int corner = k % 4;
          sx = (corner == 0 || corner == 3) ? 1.0f : -1.0f;
          sy = (corner < 2) ? 1.0f : -1.0f;
        }
        const float p[3] = {
            inst.position[0] + right[0] * hw * sx + up[0] * hh * sy,
            inst.position[1] + right[1] * hw * sx + up[1] * hh * sy,
            inst.position[2] + right[2] * hw * sx + up[2] * hh * sy};
        ImVec2 sp;
        if (!ProjectLightPoint(p, view, proj, windowX, windowY, windowWidth,
                               windowHeight, sp)) {
          havePrev = false;
          continue;
        }
        if (havePrev) {
          distance = std::min(distance, DistanceToSegment2D(mouse, prev, sp));
        }
        prev = sp;
        havePrev = true;
      }
    }

    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = static_cast<int>(i);
    }
  }
  return bestIndex;
}

static void BuildLightTransformMatrix(const LightInstance &inst,
                                      float matrix[16]) {
  float right[3], up[3], fwd[3];
  BuildLightBasis(inst, right, up, fwd);
  matrix[0] = right[0];
  matrix[1] = right[1];
  matrix[2] = right[2];
  matrix[3] = 0.0f;
  matrix[4] = up[0];
  matrix[5] = up[1];
  matrix[6] = up[2];
  matrix[7] = 0.0f;
  matrix[8] = fwd[0];
  matrix[9] = fwd[1];
  matrix[10] = fwd[2];
  matrix[11] = 0.0f;
  matrix[12] = inst.position[0];
  matrix[13] = inst.position[1];
  matrix[14] = inst.position[2];
  matrix[15] = 1.0f;
}

static void ApplyLightTransformMatrix(size_t instanceIndex,
                                      const float matrix[16]) {
  if (instanceIndex >= s_lightInstances.size()) {
    return;
  }
  LightInstance &inst = s_lightInstances[instanceIndex];
  inst.position[0] = matrix[12];
  inst.position[1] = matrix[13];
  inst.position[2] = matrix[14];

  float nfwd[3] = {matrix[8], matrix[9], matrix[10]};
  const float nlen =
      sqrtf(nfwd[0] * nfwd[0] + nfwd[1] * nfwd[1] + nfwd[2] * nfwd[2]);
  if (nlen > 0.001f) {
    inst.direction[0] = nfwd[0] / nlen;
    inst.direction[1] = nfwd[1] / nlen;
    inst.direction[2] = nfwd[2] / nlen;
  }
}

static bool ComputeLightSelectionPivot(const std::vector<size_t> &indices,
                                       size_t activeIndex,
                                       float outPivot[16]) {
  if (indices.empty() || activeIndex >= s_lightInstances.size()) {
    return false;
  }

  LightInstance pivotInst = s_lightInstances[activeIndex];
  pivotInst.position[0] = 0.0f;
  pivotInst.position[1] = 0.0f;
  pivotInst.position[2] = 0.0f;

  int count = 0;
  for (size_t index : indices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    pivotInst.position[0] += s_lightInstances[index].position[0];
    pivotInst.position[1] += s_lightInstances[index].position[1];
    pivotInst.position[2] += s_lightInstances[index].position[2];
    ++count;
  }
  if (count == 0) {
    return false;
  }

  const float invCount = 1.0f / static_cast<float>(count);
  pivotInst.position[0] *= invCount;
  pivotInst.position[1] *= invCount;
  pivotInst.position[2] *= invCount;
  BuildLightTransformMatrix(pivotInst, outPivot);
  return true;
}

static void ApplyLightScaleDeltaToPrototypes(
    const std::vector<size_t> &selectedIndices, const float delta[16]) {
  const float currentScaleX =
      sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
  if (currentScaleX <= 0.001f) {
    return;
  }

  std::vector<size_t> scaledPrototypes;
  for (size_t index : selectedIndices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    const size_t protoIndex = s_lightInstances[index].prototypeIndex;
    if (protoIndex >= s_lightPrototypes.size() ||
        std::find(scaledPrototypes.begin(), scaledPrototypes.end(),
                  protoIndex) != scaledPrototypes.end()) {
      continue;
    }
    LightPrototype &proto = s_lightPrototypes[protoIndex];
    proto.areaExtents[0] *= currentScaleX;
    proto.areaExtents[1] *= currentScaleX;
    proto.radius *= currentScaleX;
    scaledPrototypes.push_back(protoIndex);
  }
}

static std::vector<size_t>
CloneLightInstancesAsInstances(const std::vector<size_t> &indices) {
  std::vector<size_t> clones;
  clones.reserve(indices.size());
  for (size_t index : indices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    LightInstance clone = s_lightInstances[index];
    clone.selected = false;
    clones.push_back(s_lightInstances.size());
    s_lightInstances.push_back(clone);
  }
  return clones;
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

  if (s_lightGizmosVisible) {
    const bool manyLights = s_lightInstances.size() > 512;
    for (size_t i = 0; i < s_lightInstances.size(); ++i) {
      const LightInstance &inst = s_lightInstances[i];
      if (inst.prototypeIndex >= s_lightPrototypes.size()) {
        continue;
      }
      const LightPrototype &proto = s_lightPrototypes[inst.prototypeIndex];
      const bool isSelected = inst.selected ||
                              (s_selectedLightIdx == static_cast<int>(i));
      DrawLightModel(drawList, i, proto, inst, view, proj, windowX, windowY,
                     windowWidth, windowHeight, isSelected,
                     manyLights && !isSelected);
    }
  }

  std::vector<size_t> selectedIndices = GetSelectedLightIndices();
  if (s_shiftCloneDrag.active && s_shiftCloneDrag.cloneRootIndices.empty() &&
      !s_shiftCloneDrag.cloneLightIndices.empty()) {
    selectedIndices = s_shiftCloneDrag.cloneLightIndices;
  }
  if (selectedIndices.empty() || !GetSelectedTransformRoots().empty()) {
    ImGui::End();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
      !s_shiftCloneDrag.active && !s_shiftCloneDrag.optionsPending) {
    RemoveLightInstances(selectedIndices);
    ImGui::End();
    return;
  }

  size_t activeIndex = selectedIndices.front();
  if (s_selectedLightIdx >= 0 &&
      s_selectedLightIdx < static_cast<int>(s_lightInstances.size()) &&
      s_lightInstances[static_cast<size_t>(s_selectedLightIdx)].selected) {
    activeIndex = static_cast<size_t>(s_selectedLightIdx);
  }
  if (activeIndex >= s_lightInstances.size() ||
      s_lightInstances[activeIndex].prototypeIndex >= s_lightPrototypes.size()) {
    ImGui::End();
    return;
  }

  ImGuizmo::AllowAxisFlip(false);
  ImGuizmo::GetStyle().TranslationLineThickness = 6.0f;
  ImGuizmo::GetStyle().RotationLineThickness = 6.0f;

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

  ImGuizmo::OPERATION op = (g_currentGizmoOp == ImGuizmo::SCALE)
                               ? ImGuizmo::SCALE
                               : GetActiveGizmoOperation();
  ImGuizmo::MODE mode = (op == ImGuizmo::SCALE) ? ImGuizmo::LOCAL
                                                : g_currentGizmoMode;

  const int gizmoId =
      s_shiftCloneDrag.active &&
              s_shiftCloneDrag.gizmoId != static_cast<size_t>(-1)
          ? static_cast<int>(s_shiftCloneDrag.gizmoId)
          : static_cast<int>(activeIndex);
  ImGuizmo::SetID(10000 + gizmoId);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(drawList);
  ImGuizmo::SetRect(windowX, windowY, windowWidth, windowHeight);

  float pivotMatrix[16];
  if (!ComputeLightSelectionPivot(selectedIndices, activeIndex, pivotMatrix)) {
    ImGui::End();
    return;
  }

  float originalPivotMatrix[16];
  CopyMatrix4x4(pivotMatrix, originalPivotMatrix);
  std::vector<std::array<float, 16>> originalLightMatrices;
  originalLightMatrices.reserve(selectedIndices.size());
  for (size_t index : selectedIndices) {
    std::array<float, 16> matrix{};
    if (index < s_lightInstances.size()) {
      BuildLightTransformMatrix(s_lightInstances[index], matrix.data());
    }
    originalLightMatrices.push_back(matrix);
  }

  bool startedShiftCloneThisFrame = false;
  if (ImGuizmo::Manipulate(view, proj, op, mode, pivotMatrix)) {
    if (IsShiftDown() && !s_shiftCloneDrag.active &&
        !s_shiftCloneDrag.optionsPending) {
      const std::vector<size_t> clones =
          CloneLightInstancesAsInstances(selectedIndices);
      if (!clones.empty()) {
        s_shiftCloneDrag.active = true;
        s_shiftCloneDrag.sawLeftMouseDown = IsLeftMouseDown();
        s_shiftCloneDrag.releaseFramesArmed = 2;
        s_shiftCloneDrag.gizmoId = activeIndex;
        s_shiftCloneDrag.cloneLightIndices = clones;
        startedShiftCloneThisFrame = true;
        selectedIndices = clones;
        activeIndex = selectedIndices.front();
        UpdateLights();
      }
    }

    float invOriginalPivot[16];
    if (Inverse4x4(originalPivotMatrix, invOriginalPivot)) {
      float delta[16];
      MatMul(pivotMatrix, invOriginalPivot, delta);

      for (size_t i = 0; i < selectedIndices.size() &&
                         i < originalLightMatrices.size();
           ++i) {
        float newMatrix[16];
        MatMul(delta, originalLightMatrices[i].data(), newMatrix);
        ApplyLightTransformMatrix(selectedIndices[i], newMatrix);
      }

      if (op == ImGuizmo::SCALE) {
        ApplyLightScaleDeltaToPrototypes(selectedIndices, delta);
      }

      UpdateLights();
      NotifySceneChanged();
    }
  }

  if (s_shiftCloneDrag.active && s_shiftCloneDrag.cloneRootIndices.empty() &&
      IsLeftMouseDown()) {
    s_shiftCloneDrag.sawLeftMouseDown = true;
  }
  if (s_shiftCloneDrag.active && s_shiftCloneDrag.cloneRootIndices.empty() &&
      !startedShiftCloneThisFrame && s_shiftCloneDrag.releaseFramesArmed > 0) {
    --s_shiftCloneDrag.releaseFramesArmed;
  }
  if (s_shiftCloneDrag.active && s_shiftCloneDrag.cloneRootIndices.empty() &&
      s_shiftCloneDrag.sawLeftMouseDown &&
      s_shiftCloneDrag.releaseFramesArmed <= 0 && !ImGuizmo::IsUsing() &&
      !IsLeftMouseDown()) {
    s_shiftCloneDrag = ShiftCloneDragState{};
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

static bool ComputeSelectedRootWorldBounds(
    size_t rootIndex, const std::vector<std::array<float, 16>> &worldTransforms,
    float outMin[3], float outMax[3]);

static bool ComputeMixedSelectionPivot(
    const std::vector<size_t> &roots,
    const std::vector<size_t> &lightIndices,
    const std::vector<std::array<float, 16>> &worldTransforms,
    float pivotMatrix[16]) {
  if (!ComputeSelectionPivot(roots, worldTransforms, pivotMatrix)) {
    return false;
  }

  float minPoint[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
  float maxPoint[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  bool hasBounds = false;

  auto includePoint = [&](const float point[3]) {
    for (int axis = 0; axis < 3; ++axis) {
      minPoint[axis] = (std::min)(minPoint[axis], point[axis]);
      maxPoint[axis] = (std::max)(maxPoint[axis], point[axis]);
    }
    hasBounds = true;
  };

  for (size_t rootIndex : roots) {
    float rootMin[3] = {};
    float rootMax[3] = {};
    if (ComputeSelectedRootWorldBounds(rootIndex, worldTransforms, rootMin,
                                       rootMax)) {
      includePoint(rootMin);
      includePoint(rootMax);
      continue;
    }
    if (rootIndex < worldTransforms.size()) {
      const float *rootWorld = worldTransforms[rootIndex].data();
      const float rootPos[3] = {rootWorld[12], rootWorld[13], rootWorld[14]};
      includePoint(rootPos);
    }
  }

  for (size_t index : lightIndices) {
    if (index >= s_lightInstances.size()) {
      continue;
    }
    includePoint(s_lightInstances[index].position);
  }

  if (!hasBounds) {
    return true;
  }

  pivotMatrix[12] = (minPoint[0] + maxPoint[0]) * 0.5f;
  pivotMatrix[13] = (minPoint[1] + maxPoint[1]) * 0.5f;
  pivotMatrix[14] = (minPoint[2] + maxPoint[2]) * 0.5f;
  return true;
}

static void SetIdentityMatrix4x4(float matrix[16]) {
  for (int i = 0; i < 16; ++i) {
    matrix[i] = 0.0f;
  }
  matrix[0] = 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  matrix[15] = 1.0f;
}

static const char *MirrorAxisName(MirrorAxis axis) {
  switch (axis) {
  case MirrorAxis::X:
    return "X";
  case MirrorAxis::Y:
    return "Y";
  case MirrorAxis::Z:
    return "Z";
  }
  return "?";
}

static bool BuildMirrorPivotMatrix(
    const std::vector<size_t> &roots,
    const std::vector<std::array<float, 16>> &worldTransforms,
    MirrorPivot pivot, MirrorSpace space, float outPivot[16]) {
  if (roots.empty() || roots.front() >= worldTransforms.size()) {
    return false;
  }

  float pivotPoint[3] = {0.0f, 0.0f, 0.0f};
  switch (pivot) {
  case MirrorPivot::SelectionCenter: {
    float selectionPivot[16];
    if (!ComputeSelectionPivot(roots, worldTransforms, selectionPivot)) {
      return false;
    }
    pivotPoint[0] = selectionPivot[12];
    pivotPoint[1] = selectionPivot[13];
    pivotPoint[2] = selectionPivot[14];
    break;
  }
  case MirrorPivot::WorldOrigin:
    break;
  case MirrorPivot::ActiveNode: {
    const float *activeWorld = worldTransforms[roots.front()].data();
    pivotPoint[0] = activeWorld[12];
    pivotPoint[1] = activeWorld[13];
    pivotPoint[2] = activeWorld[14];
    break;
  }
  }

  if (space == MirrorSpace::Local) {
    CopyMatrix4x4(worldTransforms[roots.front()].data(), outPivot);
  } else {
    SetIdentityMatrix4x4(outPivot);
  }
  outPivot[12] = pivotPoint[0];
  outPivot[13] = pivotPoint[1];
  outPivot[14] = pivotPoint[2];
  return true;
}

bool MirrorSelectedNodes(MirrorAxis axis, MirrorPivot pivot,
                         MirrorSpace space) {
  CommitTransformHistoryEdit();
  const std::vector<size_t> selectedRoots = GetSelectedTransformRoots();
  if (selectedRoots.empty()) {
    return false;
  }

  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();
  float pivotMatrix[16];
  if (!BuildMirrorPivotMatrix(selectedRoots, worldTransforms, pivot, space,
                              pivotMatrix)) {
    return false;
  }
  float invPivot[16];
  if (!Inverse4x4(pivotMatrix, invPivot)) {
    return false;
  }

  float reflection[16];
  SetIdentityMatrix4x4(reflection);
  switch (axis) {
  case MirrorAxis::X:
    reflection[0] = -1.0f;
    break;
  case MirrorAxis::Y:
    reflection[5] = -1.0f;
    break;
  case MirrorAxis::Z:
    reflection[10] = -1.0f;
    break;
  }

  float pivotReflection[16];
  float mirrorDelta[16];
  MatMul(pivotMatrix, reflection, pivotReflection);
  MatMul(pivotReflection, invPivot, mirrorDelta);

  TransformHistoryEntry historyEntry;
  historyEntry.nodes.reserve(selectedRoots.size());
  for (size_t rootIndex : selectedRoots) {
    TransformNodeHistory snapshot;
    if (CaptureNodeTransform(rootIndex, &snapshot)) {
      historyEntry.nodes.push_back(std::move(snapshot));
    }
  }
  if (historyEntry.nodes.empty()) {
    return false;
  }

  for (size_t rootIndex : selectedRoots) {
    if (rootIndex >= worldTransforms.size()) {
      continue;
    }
    float newWorld[16];
    MatMul(mirrorDelta, worldTransforms[rootIndex].data(), newWorld);
    ApplyNodeWorldTransform(rootIndex, newWorld, worldTransforms);
  }

  for (TransformNodeHistory &node : historyEntry.nodes) {
    if (node.nodeIndex < s_nodes.size() &&
        node.nodeName == s_nodes[node.nodeIndex].name) {
      CopyMatrix4x4(s_nodes[node.nodeIndex].transform, node.after.data());
    } else {
      node.after = node.before;
    }
  }
  PushTransformHistoryEntry(std::move(historyEntry));

  ApplyRendererInvalidation(RendererInvalidationPlan::TlasRefresh);
  NotifySceneChanged();
  s_lastStatus = std::string("Mirrored selected nodes on ") +
                 MirrorAxisName(axis) + " axis";
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

bool IsTransformGizmoHitAt(float screenX, float screenY, float screenWidth,
                           float screenHeight) {
  if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
    return true;
  }
  if (screenWidth <= 1.0f || screenHeight <= 1.0f) {
    return false;
  }

  std::vector<size_t> selectedRoots = GetSelectedTransformRoots();
  std::vector<size_t> selectedLightIndices = GetSelectedLightIndices();
  if (selectedRoots.empty() && selectedLightIndices.empty()) {
    return false;
  }

  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);

  float pivotMatrix[16] = {};
  bool hasPivot = false;
  if (!selectedRoots.empty()) {
    const std::vector<std::array<float, 16>> worldTransforms =
        BuildNodeWorldTransforms();
    hasPivot = ComputeMixedSelectionPivot(selectedRoots, selectedLightIndices,
                                          worldTransforms, pivotMatrix);
  } else {
    size_t activeIndex = selectedLightIndices.front();
    if (s_selectedLightIdx >= 0 &&
        s_selectedLightIdx < static_cast<int>(s_lightInstances.size()) &&
        s_lightInstances[static_cast<size_t>(s_selectedLightIdx)].selected) {
      activeIndex = static_cast<size_t>(s_selectedLightIdx);
    }
    hasPivot = ComputeLightSelectionPivot(selectedLightIndices, activeIndex,
                                          pivotMatrix);
  }
  if (!hasPivot) {
    return false;
  }

  float windowX = 0.0f;
  float windowY = 0.0f;
  float windowWidth = screenWidth;
  float windowHeight = screenHeight;
  GetRenderViewportRect(&windowX, &windowY, &windowWidth, &windowHeight);

  const float pivotPoint[3] = {pivotMatrix[12], pivotMatrix[13],
                               pivotMatrix[14]};
  ImVec2 pivotScreen;
  if (!WorldToScreenPoint(pivotPoint, view, proj, windowX, windowY,
                          windowWidth, windowHeight, pivotScreen)) {
    return false;
  }

  constexpr float kGizmoCenterHitRadius = 54.0f;
  const float dx = screenX - pivotScreen.x;
  const float dy = screenY - pivotScreen.y;
  return dx * dx + dy * dy <=
         kGizmoCenterHitRadius * kGizmoCenterHitRadius;
}

void DrawGizmo() {
  std::vector<size_t> selectedRoots = GetSelectedTransformRoots();
  std::vector<size_t> selectedLightIndices = GetSelectedLightIndices();
  if (s_shiftCloneDrag.active &&
      !s_shiftCloneDrag.cloneRootIndices.empty()) {
    selectedRoots = s_shiftCloneDrag.cloneRootIndices;
    selectedLightIndices = s_shiftCloneDrag.cloneLightIndices;
  }

  // ImGuizmo::BeginFrame() called in main.cpp
  if (selectedRoots.empty()) {
    if (s_activeTransformEdit.active && !ImGuizmo::IsUsing()) {
      CommitTransformHistoryEdit();
    }
    return;
  }

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
  if (!ComputeMixedSelectionPivot(selectedRoots, selectedLightIndices,
                                  worldTransforms, pivotMatrix)) {
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
  std::vector<std::array<float, 16>> originalLightMatrices;
  originalLightMatrices.reserve(selectedLightIndices.size());
  for (size_t index : selectedLightIndices) {
    std::array<float, 16> matrix{};
    if (index < s_lightInstances.size()) {
      BuildLightTransformMatrix(s_lightInstances[index], matrix.data());
    }
    originalLightMatrices.push_back(matrix);
  }

  ImGuizmo::OPERATION op = GetActiveGizmoOperation();
  bool startedShiftCloneThisFrame = false;
  if (ImGuizmo::Manipulate(view, proj, op, actualMode,
                           pivotMatrix)) {
    if (!IsShiftDown() && !s_shiftCloneDrag.active &&
        !s_shiftCloneDrag.optionsPending) {
      BeginTransformHistoryEdit(selectedRoots);
    } else {
      CancelTransformHistoryEdit();
    }

    if (IsShiftDown() && !s_shiftCloneDrag.active &&
        !s_shiftCloneDrag.optionsPending) {
      ClonedNodeSet clones = CloneNodesAsInstances(selectedRoots);
      std::vector<size_t> lightClones =
          CloneLightInstancesAsInstances(selectedLightIndices);
      if (!clones.rootIndices.empty() || !lightClones.empty()) {
        s_shiftCloneDrag.active = true;
        s_shiftCloneDrag.sawLeftMouseDown = IsLeftMouseDown();
        s_shiftCloneDrag.releaseFramesArmed = 2;
        s_shiftCloneDrag.gizmoId = selectedRoots.front();
        s_shiftCloneDrag.cloneRootIndices = clones.rootIndices;
        s_shiftCloneDrag.cloneNodeIndices = clones.nodeIndices;
        s_shiftCloneDrag.cloneLightIndices = lightClones;
        startedShiftCloneThisFrame = true;
        selectedRoots = clones.rootIndices;
        selectedLightIndices = lightClones;
        if (!selectedLightIndices.empty()) {
          UpdateLights();
        }
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

      for (size_t i = 0; i < selectedLightIndices.size() &&
                         i < originalLightMatrices.size();
           ++i) {
        float newMatrix[16];
        MatMul(delta, originalLightMatrices[i].data(), newMatrix);
        ApplyLightTransformMatrix(selectedLightIndices[i], newMatrix);
      }
      if (!selectedLightIndices.empty()) {
        if (op == ImGuizmo::SCALE) {
          ApplyLightScaleDeltaToPrototypes(selectedLightIndices, delta);
        }
        UpdateLights();
        NotifySceneChanged();
      }
    }
  }

  if (s_activeTransformEdit.active && !ImGuizmo::IsUsing()) {
    CommitTransformHistoryEdit();
  }

  if (s_shiftCloneDrag.active && IsLeftMouseDown()) {
    s_shiftCloneDrag.sawLeftMouseDown = true;
  }
  if (s_shiftCloneDrag.active && !startedShiftCloneThisFrame &&
      s_shiftCloneDrag.releaseFramesArmed > 0) {
    --s_shiftCloneDrag.releaseFramesArmed;
  }
  if (s_shiftCloneDrag.active && s_shiftCloneDrag.sawLeftMouseDown &&
      s_shiftCloneDrag.releaseFramesArmed <= 0 && !ImGuizmo::IsUsing() &&
      !IsLeftMouseDown()) {
    s_shiftCloneDrag.active = false;
    if (!ShiftCloneHasMeshChoice()) {
      SelectShiftCloneResult();
      s_shiftCloneDrag = {};
    } else {
      s_shiftCloneDrag.optionsPending = true;
    }
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

static bool ScreenRectsIntersect(float aMinX, float aMinY, float aMaxX,
                                 float aMaxY, float bMinX, float bMinY,
                                 float bMaxX, float bMaxY) {
  return aMinX <= bMaxX && aMaxX >= bMinX && aMinY <= bMaxY &&
         aMaxY >= bMinY;
}

static void AppendUniqueIndex(std::vector<size_t> &indices, size_t index) {
  if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
    indices.push_back(index);
  }
}

static void ApplySelectionSets(const std::vector<size_t> &nodeIndices,
                               const std::vector<size_t> &lightIndices) {
  SelectNodesAndLights(nodeIndices, lightIndices);
}

static bool ProjectNodeBoundsToScreen(
    size_t nodeIndex, const std::vector<std::array<float, 16>> &worldTransforms,
    const float view[16], const float proj[16], float windowX, float windowY,
    float windowWidth, float windowHeight, float &outMinX, float &outMinY,
    float &outMaxX, float &outMaxY) {
  if (nodeIndex >= s_nodes.size() || nodeIndex >= worldTransforms.size()) {
    return false;
  }

  const Node &node = s_nodes[nodeIndex];
  if (!node.visible) {
    return false;
  }

  bool hasPoint = false;
  outMinX = outMinY = FLT_MAX;
  outMaxX = outMaxY = -FLT_MAX;
  const float *nodeWorld = worldTransforms[nodeIndex].data();
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
      TransformPointColumnMajor(nodeWorld, corner, worldCorner);
      ImVec2 screenPoint;
      if (!WorldToScreenPoint(worldCorner, view, proj, windowX, windowY,
                              windowWidth, windowHeight, screenPoint)) {
        continue;
      }
      outMinX = (std::min)(outMinX, screenPoint.x);
      outMinY = (std::min)(outMinY, screenPoint.y);
      outMaxX = (std::max)(outMaxX, screenPoint.x);
      outMaxY = (std::max)(outMaxY, screenPoint.y);
      hasPoint = true;
    }
  }
  return hasPoint;
}

bool BoxSelect(float startScreenX, float startScreenY, float endScreenX,
               float endScreenY, float screenWidth, float screenHeight,
               bool additive) {
  if (screenWidth <= 1.0f || screenHeight <= 1.0f) {
    return false;
  }

  float vpX = 0.0f;
  float vpY = 0.0f;
  float vpWidth = screenWidth;
  float vpHeight = screenHeight;
  GetRenderViewportRect(&vpX, &vpY, &vpWidth, &vpHeight);

  float boxMinX = (std::min)(startScreenX, endScreenX);
  float boxMinY = (std::min)(startScreenY, endScreenY);
  float boxMaxX = (std::max)(startScreenX, endScreenX);
  float boxMaxY = (std::max)(startScreenY, endScreenY);

  boxMinX = std::clamp(boxMinX, vpX, vpX + vpWidth);
  boxMaxX = std::clamp(boxMaxX, vpX, vpX + vpWidth);
  boxMinY = std::clamp(boxMinY, vpY, vpY + vpHeight);
  boxMaxY = std::clamp(boxMaxY, vpY, vpY + vpHeight);
  if (boxMaxX - boxMinX < 1.0f || boxMaxY - boxMinY < 1.0f) {
    return false;
  }

  const bool includeMeshes =
      s_selectionFilter == SelectionFilter::Meshes ||
      s_selectionFilter == SelectionFilter::MeshesAndLights;
  const bool includeLights =
      s_selectionFilter == SelectionFilter::Lights ||
      s_selectionFilter == SelectionFilter::MeshesAndLights;

  std::vector<size_t> selectedNodes =
      additive ? GetSelectedNodeIndices() : std::vector<size_t>{};
  std::vector<size_t> selectedLights =
      additive ? GetSelectedLightIndices() : std::vector<size_t>{};

  float view[16], proj[16];
  BuildViewMatrix(view);
  BuildProjectionMatrix(proj);

  if (includeMeshes) {
    const std::vector<std::array<float, 16>> worldTransforms =
        BuildNodeWorldTransforms();
    for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
      float meshMinX = 0.0f;
      float meshMinY = 0.0f;
      float meshMaxX = 0.0f;
      float meshMaxY = 0.0f;
      if (!ProjectNodeBoundsToScreen(nodeIndex, worldTransforms, view, proj,
                                     vpX, vpY, vpWidth, vpHeight, meshMinX,
                                     meshMinY, meshMaxX, meshMaxY)) {
        continue;
      }
      if (!ScreenRectsIntersect(boxMinX, boxMinY, boxMaxX, boxMaxY, meshMinX,
                                meshMinY, meshMaxX, meshMaxY)) {
        continue;
      }
      const size_t target = ResolveSelectionTargetForHit(nodeIndex);
      if (target < s_nodes.size()) {
        AppendUniqueIndex(selectedNodes, target);
      }
    }
  }

  if (includeLights) {
    constexpr float kLightPickRadiusPixels = 12.0f;
    for (size_t lightIndex = 0; lightIndex < s_lightInstances.size();
         ++lightIndex) {
      const LightInstance &inst = s_lightInstances[lightIndex];
      if (inst.prototypeIndex >= s_lightPrototypes.size()) {
        continue;
      }
      ImVec2 screenPoint;
      if (!WorldToScreenPoint(inst.position, view, proj, vpX, vpY, vpWidth,
                              vpHeight, screenPoint)) {
        continue;
      }
      if (!ScreenRectsIntersect(boxMinX, boxMinY, boxMaxX, boxMaxY,
                                screenPoint.x - kLightPickRadiusPixels,
                                screenPoint.y - kLightPickRadiusPixels,
                                screenPoint.x + kLightPickRadiusPixels,
                                screenPoint.y + kLightPickRadiusPixels)) {
        continue;
      }
      AppendUniqueIndex(selectedLights, lightIndex);
    }
  }

  ApplySelectionSets(selectedNodes, selectedLights);
  return !selectedNodes.empty() || !selectedLights.empty();
}

int UpdateSelection(float screenWidth, float screenHeight) {
  if (s_selectionToolMode == SelectionToolMode::Box) {
    return -1;
  }
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

  const int pickedLight =
      PickLightGizmoAt(mposAbs.x, mposAbs.y, screenWidth, screenHeight);
  if (pickedLight >= 0) {
    if (IsCtrlDown()) {
      ToggleLightSelection(static_cast<size_t>(pickedLight));
    } else {
      SelectLight(pickedLight);
    }
    fprintf(stderr, "Scene: Picked Light %d\n", pickedLight);
    return -1;
  }

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

  float right[3] = {};
  float up[3] = {};
  float verticalCenterShift = 0.0f;
  if (!BuildSceneCameraPerspectiveBasis(forward, upHint, right, up,
                                        verticalCenterShift))
    return -1;

  float xView = ndcX * aspect * tanHalfFov;
  float yView = ndcY * tanHalfFov + verticalCenterShift;
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

int PickMaterialAt(float screenX, float screenY, float screenWidth,
                   float screenHeight) {
  if (ImGuizmo::IsUsing()) {
    return -1;
  }

  if (screenWidth <= 1.0f || screenHeight <= 1.0f) {
    return -1;
  }

  float vpX = 0.0f;
  float vpY = 0.0f;
  float vpWidth = screenWidth;
  float vpHeight = screenHeight;
  GetRenderViewportRect(&vpX, &vpY, &vpWidth, &vpHeight);
  float mx = screenX - vpX;
  float my = screenY - vpY;
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

  float right[3] = {};
  float up[3] = {};
  float verticalCenterShift = 0.0f;
  if (!BuildSceneCameraPerspectiveBasis(forward, upHint, right, up,
                                        verticalCenterShift)) {
    return -1;
  }

  const float xView = ndcX * aspect * tanHalfFov;
  const float yView = ndcY * tanHalfFov + verticalCenterShift;
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

      const float boxLocalHit[3] = {localOrig[0] + localDir[0] * boxT,
                                    localOrig[1] + localDir[1] * boxT,
                                    localOrig[2] + localDir[2] * boxT};
      float boxWorldHit[3];
      TransformPointColumnMajor(nodeWorld, boxLocalHit, boxWorldHit);
      const float boxDx = boxWorldHit[0] - orig[0];
      const float boxDy = boxWorldHit[1] - orig[1];
      const float boxDz = boxWorldHit[2] - orig[2];
      const float boxWorldDist2 = boxDx * boxDx + boxDy * boxDy +
                                  boxDz * boxDz;
      if (boxWorldDist2 >= minWorldDist2) {
        continue;
      }

      if (!mesh.cpuVertices.empty() && !mesh.cpuIndices.empty()) {
        bool triHit = false;
        float bestMeshDist2 = FLT_MAX;

        for (size_t k = 0; k + 2 < mesh.cpuIndices.size(); k += 3) {
          const uint32_t i0 = mesh.cpuIndices[k];
          const uint32_t i1 = mesh.cpuIndices[k + 1];
          const uint32_t i2 = mesh.cpuIndices[k + 2];
          if (i0 >= mesh.cpuVertices.size() || i1 >= mesh.cpuVertices.size() ||
              i2 >= mesh.cpuVertices.size()) {
            continue;
          }

          float tVal = 0.0f;
          if (!RayTriangleIntersection(localOrig, localDir,
                                       mesh.cpuVertices[i0].pos,
                                       mesh.cpuVertices[i1].pos,
                                       mesh.cpuVertices[i2].pos, tVal)) {
            continue;
          }

          float localHit[3] = {localOrig[0] + localDir[0] * tVal,
                               localOrig[1] + localDir[1] * tVal,
                               localOrig[2] + localDir[2] * tVal};
          float worldHit[3];
          TransformPointColumnMajor(nodeWorld, localHit, worldHit);
          const float dx = worldHit[0] - orig[0];
          const float dy = worldHit[1] - orig[1];
          const float dz = worldHit[2] - orig[2];
          const float worldDist2 = dx * dx + dy * dy + dz * dz;

          if (worldDist2 < bestMeshDist2) {
            bestMeshDist2 = worldDist2;
            triHit = true;
          }
        }

        if (triHit && bestMeshDist2 < minWorldDist2) {
          minWorldDist2 = bestMeshDist2;
          hitNode = static_cast<int>(nodeIndex);
          hitMaterial = mesh.materialIndex;
        }
      } else {
        minWorldDist2 = boxWorldDist2;
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

int PickMaterialAtCursor(float screenWidth, float screenHeight) {
  const ImVec2 mousePos = ImGui::GetIO().MousePos;
  return PickMaterialAt(mousePos.x, mousePos.y, screenWidth, screenHeight);
}

// SceneMeshPickHit is declared in scene_internal.h.

static bool BuildViewportRayAt(float screenX, float screenY, float screenWidth,
                               float screenHeight, float outOrigin[3],
                               float outDirection[3]) {
  if (!outOrigin || !outDirection || screenWidth <= 1.0f ||
      screenHeight <= 1.0f) {
    return false;
  }

  float vpX = 0.0f;
  float vpY = 0.0f;
  float vpWidth = screenWidth;
  float vpHeight = screenHeight;
  GetRenderViewportRect(&vpX, &vpY, &vpWidth, &vpHeight);
  const float mx = screenX - vpX;
  const float my = screenY - vpY;
  if (mx < 0.0f || my < 0.0f || mx > vpWidth || my > vpHeight) {
    return false;
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
    const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1e-12f) {
      return false;
    }
    const float invLen = 1.0f / sqrtf(len2);
    v[0] *= invLen;
    v[1] *= invLen;
    v[2] *= invLen;
    return true;
  };
  float right[3] = {};
  float up[3] = {};
  float verticalCenterShift = 0.0f;
  if (!BuildSceneCameraPerspectiveBasis(forward, upHint, right, up,
                                        verticalCenterShift)) {
    return false;
  }

  const float xView = ndcX * aspect * tanHalfFov;
  const float yView = ndcY * tanHalfFov + verticalCenterShift;
  float dir[3] = {xView * right[0] + yView * up[0] + forward[0],
                  xView * right[1] + yView * up[1] + forward[1],
                  xView * right[2] + yView * up[2] + forward[2]};
  if (!Normalize3(dir)) {
    return false;
  }

  outOrigin[0] = g_cameraData.pos[0];
  outOrigin[1] = g_cameraData.pos[1];
  outOrigin[2] = g_cameraData.pos[2];
  outDirection[0] = dir[0];
  outDirection[1] = dir[1];
  outDirection[2] = dir[2];
  return true;
}

bool PickSceneMeshAt(float screenX, float screenY, float screenWidth,
                            float screenHeight, SceneMeshPickHit &outHit) {
  outHit = {};
  if (ImGuizmo::IsUsing()) {
    return false;
  }

  float orig[3] = {};
  float dir[3] = {};
  if (!BuildViewportRayAt(screenX, screenY, screenWidth, screenHeight, orig,
                          dir)) {
    return false;
  }

  float minWorldDist2 = FLT_MAX;
  auto normalize3 = [](float v[3]) {
    const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1e-12f) {
      return false;
    }
    const float invLen = 1.0f / sqrtf(len2);
    v[0] *= invLen;
    v[1] *= invLen;
    v[2] *= invLen;
    return true;
  };
  const std::vector<std::array<float, 16>> worldTransforms =
      BuildNodeWorldTransforms();

  for (size_t nodeIndex = 0; nodeIndex < s_nodes.size(); ++nodeIndex) {
    const Node &node = s_nodes[nodeIndex];
    if (!node.visible || nodeIndex >= worldTransforms.size()) {
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
      float boxT = 1e30f;
      if (!RayAABBIntersection(localOrig, localDir, mesh.minBound,
                               mesh.maxBound, boxT)) {
        continue;
      }

      float bestMeshDist2 = FLT_MAX;
      float bestMeshWorldHit[3] = {};
      float bestMeshWorldNormal[3] = {0.0f, 1.0f, 0.0f};
      bool meshHit = false;
      if (!mesh.cpuVertices.empty() && !mesh.cpuIndices.empty()) {
        for (size_t k = 0; k + 2 < mesh.cpuIndices.size(); k += 3) {
          const uint32_t i0 = mesh.cpuIndices[k];
          const uint32_t i1 = mesh.cpuIndices[k + 1];
          const uint32_t i2 = mesh.cpuIndices[k + 2];
          if (i0 >= mesh.cpuVertices.size() || i1 >= mesh.cpuVertices.size() ||
              i2 >= mesh.cpuVertices.size()) {
            continue;
          }
          float tVal = 0.0f;
          if (!RayTriangleIntersection(localOrig, localDir,
                                       mesh.cpuVertices[i0].pos,
                                       mesh.cpuVertices[i1].pos,
                                       mesh.cpuVertices[i2].pos, tVal)) {
            continue;
          }
          const float localHit[3] = {localOrig[0] + localDir[0] * tVal,
                                     localOrig[1] + localDir[1] * tVal,
                                     localOrig[2] + localDir[2] * tVal};
          float worldHit[3];
          TransformPointColumnMajor(nodeWorld, localHit, worldHit);
          const float dx = worldHit[0] - orig[0];
          const float dy = worldHit[1] - orig[1];
          const float dz = worldHit[2] - orig[2];
          const float worldDist2 = dx * dx + dy * dy + dz * dz;
          if (worldDist2 < bestMeshDist2) {
            float localNormal[3] = {
                mesh.cpuVertices[i0].normal[0] + mesh.cpuVertices[i1].normal[0] +
                    mesh.cpuVertices[i2].normal[0],
                mesh.cpuVertices[i0].normal[1] + mesh.cpuVertices[i1].normal[1] +
                    mesh.cpuVertices[i2].normal[1],
                mesh.cpuVertices[i0].normal[2] + mesh.cpuVertices[i1].normal[2] +
                    mesh.cpuVertices[i2].normal[2]};
            if (!normalize3(localNormal)) {
              const float *p0 = mesh.cpuVertices[i0].pos;
              const float *p1 = mesh.cpuVertices[i1].pos;
              const float *p2 = mesh.cpuVertices[i2].pos;
              const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1],
                                   p1[2] - p0[2]};
              const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1],
                                   p2[2] - p0[2]};
              localNormal[0] = e1[1] * e2[2] - e1[2] * e2[1];
              localNormal[1] = e1[2] * e2[0] - e1[0] * e2[2];
              localNormal[2] = e1[0] * e2[1] - e1[1] * e2[0];
            }
            float worldNormal[3] = {0.0f, 1.0f, 0.0f};
            if (normalize3(localNormal)) {
              TransformVectorColumnMajor(nodeWorld, localNormal, worldNormal);
              if (!normalize3(worldNormal)) {
                worldNormal[0] = 0.0f;
                worldNormal[1] = 1.0f;
                worldNormal[2] = 0.0f;
              }
            }
            bestMeshDist2 = worldDist2;
            bestMeshWorldHit[0] = worldHit[0];
            bestMeshWorldHit[1] = worldHit[1];
            bestMeshWorldHit[2] = worldHit[2];
            bestMeshWorldNormal[0] = worldNormal[0];
            bestMeshWorldNormal[1] = worldNormal[1];
            bestMeshWorldNormal[2] = worldNormal[2];
            meshHit = true;
          }
        }
      } else {
        const float localHit[3] = {localOrig[0] + localDir[0] * boxT,
                                   localOrig[1] + localDir[1] * boxT,
                                   localOrig[2] + localDir[2] * boxT};
        float worldHit[3];
        TransformPointColumnMajor(nodeWorld, localHit, worldHit);
        const float dx = worldHit[0] - orig[0];
        const float dy = worldHit[1] - orig[1];
        const float dz = worldHit[2] - orig[2];
        bestMeshDist2 = dx * dx + dy * dy + dz * dz;
        bestMeshWorldHit[0] = worldHit[0];
        bestMeshWorldHit[1] = worldHit[1];
        bestMeshWorldHit[2] = worldHit[2];
        bestMeshWorldNormal[0] = -dir[0];
        bestMeshWorldNormal[1] = -dir[1];
        bestMeshWorldNormal[2] = -dir[2];
        normalize3(bestMeshWorldNormal);
        meshHit = true;
      }

      if (meshHit && bestMeshDist2 < minWorldDist2) {
        minWorldDist2 = bestMeshDist2;
        outHit.nodeIndex = nodeIndex;
        outHit.meshIndex = meshIndex;
        outHit.worldPosition[0] = bestMeshWorldHit[0];
        outHit.worldPosition[1] = bestMeshWorldHit[1];
        outHit.worldPosition[2] = bestMeshWorldHit[2];
        outHit.worldNormal[0] = bestMeshWorldNormal[0];
        outHit.worldNormal[1] = bestMeshWorldNormal[1];
        outHit.worldNormal[2] = bestMeshWorldNormal[2];
      }
    }
  }

  return outHit.nodeIndex < s_nodes.size() &&
         outHit.meshIndex < g_loadedMeshes.size();
}

bool ResolveViewportImportPlacement(float screenX, float screenY,
                                    float screenWidth, float screenHeight,
                                    float outTranslation[3]) {
  if (!outTranslation) {
    return false;
  }

  SceneMeshPickHit hit;
  if (PickSceneMeshAt(screenX, screenY, screenWidth, screenHeight, hit)) {
    outTranslation[0] = hit.worldPosition[0];
    outTranslation[1] = hit.worldPosition[1];
    outTranslation[2] = hit.worldPosition[2];
    return true;
  }

  float origin[3] = {};
  float direction[3] = {};
  if (!BuildViewportRayAt(screenX, screenY, screenWidth, screenHeight, origin,
                          direction)) {
    return false;
  }

  float forward[3] = {g_cameraData.forward[0], g_cameraData.forward[1],
                      g_cameraData.forward[2]};
  const float forwardLen2 =
      forward[0] * forward[0] + forward[1] * forward[1] +
      forward[2] * forward[2];
  if (forwardLen2 <= 1e-12f) {
    return false;
  }
  const float invForwardLen = 1.0f / sqrtf(forwardLen2);
  forward[0] *= invForwardLen;
  forward[1] *= invForwardLen;
  forward[2] *= invForwardLen;

  // Keep empty-view drops visible under the cursor at a stable view depth.
  const float fallbackDepth = std::max(5.0f, g_cameraData.nearZ * 4.0f);
  const float denominator = direction[0] * forward[0] +
                            direction[1] * forward[1] +
                            direction[2] * forward[2];
  float rayDistance = fallbackDepth;
  if (std::fabs(denominator) > 1e-5f) {
    rayDistance = fallbackDepth / denominator;
  }
  if (rayDistance <= g_cameraData.nearZ) {
    rayDistance = fallbackDepth;
  }

  outTranslation[0] = origin[0] + direction[0] * rayDistance;
  outTranslation[1] = origin[1] + direction[1] * rayDistance;
  outTranslation[2] = origin[2] + direction[2] * rayDistance;
  return true;
}

static bool ResolveLightPlacement(float screenX, float screenY,
                                  float screenWidth, float screenHeight,
                                  float outPosition[3], float outDirection[3]) {
  if (!outPosition || !outDirection) {
    return false;
  }

  SceneMeshPickHit hit;
  if (PickSceneMeshAt(screenX, screenY, screenWidth, screenHeight, hit)) {
    const float offset = 0.05f;
    outPosition[0] = hit.worldPosition[0] + hit.worldNormal[0] * offset;
    outPosition[1] = hit.worldPosition[1] + hit.worldNormal[1] * offset;
    outPosition[2] = hit.worldPosition[2] + hit.worldNormal[2] * offset;
    outDirection[0] = hit.worldNormal[0];
    outDirection[1] = hit.worldNormal[1];
    outDirection[2] = hit.worldNormal[2];
    return true;
  }

  float origin[3] = {};
  float direction[3] = {};
  if (!BuildViewportRayAt(screenX, screenY, screenWidth, screenHeight, origin,
                          direction)) {
    return false;
  }

  const float fallbackDepth = std::max(5.0f, g_cameraData.nearZ * 4.0f);
  outPosition[0] = origin[0] + direction[0] * fallbackDepth;
  outPosition[1] = origin[1] + direction[1] * fallbackDepth;
  outPosition[2] = origin[2] + direction[2] * fallbackDepth;
  outDirection[0] = direction[0];
  outDirection[1] = direction[1];
  outDirection[2] = direction[2];
  return true;
}

void BeginCreateLightAtClick(LightType type, int iesProfileIndex) {
  s_lightPlacementCreateType = type;
  s_lightPlacementCreateIesProfile = iesProfileIndex;
  s_lightPlacementMoveInstance = -1;
  s_lightPlacementMode = LightPlacementMode::Create;
}

void BeginMoveLightToSurface(int instanceIndex) {
  if (instanceIndex < 0 ||
      instanceIndex >= static_cast<int>(s_lightInstances.size())) {
    s_lightPlacementMoveInstance = -1;
    s_lightPlacementMode = LightPlacementMode::None;
    return;
  }

  s_lightPlacementMoveInstance = instanceIndex;
  SelectLight(instanceIndex);
  s_lightPlacementMode = LightPlacementMode::MoveSelected;
}

void BeginMoveSelectedLightToSurface() {
  BeginMoveLightToSurface(s_selectedLightIdx);
}

bool IsLightPlacementActive() {
  return s_lightPlacementMode != LightPlacementMode::None;
}

LightPlacementMode GetLightPlacementMode() { return s_lightPlacementMode; }

void CancelLightPlacement() {
  s_lightPlacementMoveInstance = -1;
  s_lightPlacementCreateIesProfile = -1;
  s_lightPlacementMode = LightPlacementMode::None;
}

bool HandleLightPlacement(float screenX, float screenY, float screenWidth,
                          float screenHeight) {
  if (s_lightPlacementMode == LightPlacementMode::None) {
    return false;
  }

  float position[3] = {};
  float direction[3] = {};
  if (!ResolveLightPlacement(screenX, screenY, screenWidth, screenHeight,
                             position, direction)) {
    return false;
  }

  if (s_lightPlacementMode == LightPlacementMode::Create) {
    const size_t prototypeIndex = AddLightPrototype(s_lightPlacementCreateType);
    if (prototypeIndex >= s_lightPrototypes.size() || s_lightInstances.empty()) {
      s_lightPlacementMode = LightPlacementMode::None;
      return false;
    }
    if (s_lightPlacementCreateIesProfile >= 0) {
      LightPrototype proto = s_lightPrototypes[prototypeIndex];
      proto.iesProfileIndex = s_lightPlacementCreateIesProfile;
      UpdateLightPrototype(prototypeIndex, proto);
    }
    const size_t instanceIndex = s_lightInstances.size() - 1;
    LightInstance inst = s_lightInstances[instanceIndex];
    inst.position[0] = position[0];
    inst.position[1] = position[1];
    inst.position[2] = position[2];
    inst.direction[0] = direction[0];
    inst.direction[1] = direction[1];
    inst.direction[2] = direction[2];
    UpdateLightInstance(instanceIndex, inst);
    SelectLight(static_cast<int>(instanceIndex));
    s_lightPlacementCreateIesProfile = -1;
    s_lightPlacementMoveInstance = -1;
    s_lightPlacementMode = LightPlacementMode::None;
    return true;
  }

  const int moveInstance = s_lightPlacementMoveInstance;
  if (moveInstance < 0 ||
      moveInstance >= static_cast<int>(s_lightInstances.size())) {
    s_lightPlacementMoveInstance = -1;
    s_lightPlacementMode = LightPlacementMode::None;
    return false;
  }

  LightInstance inst = s_lightInstances[static_cast<size_t>(moveInstance)];
  inst.position[0] = position[0];
  inst.position[1] = position[1];
  inst.position[2] = position[2];
  inst.direction[0] = direction[0];
  inst.direction[1] = direction[1];
  inst.direction[2] = direction[2];
  UpdateLightInstance(static_cast<size_t>(moveInstance), inst);
  SelectLight(moveInstance);
  s_lightPlacementMoveInstance = -1;
  s_lightPlacementMode = LightPlacementMode::None;
  return true;
}

// AddScatterTargetFromPick / HandleScatterPick moved to scatter.cpp.

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
  ClearTransformHistory();
  PrepareForDestructiveMeshMutation();
  s_nodes.clear();
  VolumetricRenderer::ClearActiveVolume();
  g_loadedMeshes.clear();
  g_loadedMaterials.clear();
  g_loadedTextures.clear();
  s_materialStableIds.clear();
  s_materialIndicesByStableId.clear();
  s_materialIndicesByName.clear();
  s_materialMetadataDirty = false;
  s_sharedImportedMeshesBySourcePath.clear();
  s_textureIndicesBySourceUri.clear();
  SetScatterModels({}); // wipe scatter state via its public API
  s_lightPrototypes.clear();
  s_lightInstances.clear();
  s_flattenedLights.clear();
  s_lightFlattenMapping.clear();
  s_lightPlacementMoveInstance = -1;
  s_lightPlacementCreateIesProfile = -1;
  s_lightPlacementMode = LightPlacementMode::None;
  s_iesProfiles.clear();
  s_selectedLightIdx = -1;
  AnimationSequence::Clear();
  SavedViews::Clear();
  g_textureDescriptorCount = 0;
  DxrRenderer::MarkTextureDescriptorTableDirty();
  // Note: In a full implementation, we should also release GPU
  // resources/descriptors and reset the IBL manager, but for now this clears
  // the CPU state which is then rebuilt by LoadScene.

  // Reset DXR state if it's active
  DxrRenderer::UpdateIESAtlas(nullptr, 0);
  UpdateLights();
  DxrRenderer::ResetAccumulation();
  // Ensure camera/exposure defaults are restored when starting a fresh scene
  DxrRenderer::SetAutoExposure(false);
  DxrRenderer::SetPhysicalCameraExposure(true);
  DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 125.0f, 16.0f);
  NotifySceneChanged();
}

std::string CleanOrphanedData() {
  if (g_loadedMaterials.empty() && g_loadedTextures.empty()) {
    return "No material or texture data to clean.";
  }

  // Must flush GPU before we mutate textures whose resources are in-flight.
  WaitGPUIdle();

  // --- Pass 1: collect all material indices used by any node or mesh ---
  std::vector<bool> materialUsed(g_loadedMaterials.size(), false);

  for (const auto &node : s_nodes) {
    for (size_t meshIdx : node.meshIndices) {
      if (meshIdx < g_loadedMeshes.size()) {
        int matIdx = g_loadedMeshes[meshIdx].materialIndex;
        if (matIdx >= 0 && static_cast<size_t>(matIdx) < materialUsed.size()) {
          materialUsed[static_cast<size_t>(matIdx)] = true;
        }
      }
    }
    for (int linkedMatIdx : node.linkedMaterialIndices) {
      if (linkedMatIdx >= 0 && static_cast<size_t>(linkedMatIdx) < materialUsed.size()) {
        materialUsed[static_cast<size_t>(linkedMatIdx)] = true;
      }
    }
  }

  // Also keep materials referenced by scatter objects' source meshes
  for (const auto &model : GetScatterModels()) {
    for (const auto &obj : model.objects) {
      for (size_t meshIdx : obj.meshIndices) {
        if (meshIdx < g_loadedMeshes.size()) {
          int matIdx = g_loadedMeshes[meshIdx].materialIndex;
          if (matIdx >= 0 && static_cast<size_t>(matIdx) < materialUsed.size()) {
            materialUsed[static_cast<size_t>(matIdx)] = true;
          }
        }
      }
    }
  }

  // --- Pass 2: compact materials (keep only used ones, build remap) ---
  size_t removedMaterials = 0;
  std::vector<int> materialRemap(g_loadedMaterials.size(), -1);
  {
    std::vector<Asset::Material> compactedMaterials;
    std::vector<std::string> compactedStableIds;
    compactedMaterials.reserve(g_loadedMaterials.size());
    if (s_materialStableIds.size() < g_loadedMaterials.size()) {
      s_materialStableIds.resize(g_loadedMaterials.size());
    }
    compactedStableIds.reserve(s_materialStableIds.size());

    for (size_t oldIdx = 0; oldIdx < g_loadedMaterials.size(); ++oldIdx) {
      if (!materialUsed[oldIdx]) {
        ++removedMaterials;
        continue;
      }
      materialRemap[oldIdx] = static_cast<int>(compactedMaterials.size());
      compactedMaterials.push_back(std::move(g_loadedMaterials[oldIdx]));
      compactedStableIds.push_back(
          oldIdx < s_materialStableIds.size()
              ? std::move(s_materialStableIds[oldIdx])
              : std::string());
    }
    g_loadedMaterials = std::move(compactedMaterials);
    s_materialStableIds = std::move(compactedStableIds);
  }

  // Rebuild material lookup maps
  s_materialIndicesByStableId.clear();
  s_materialIndicesByName.clear();
  for (size_t i = 0; i < g_loadedMaterials.size(); ++i) {
    const std::string &name = g_loadedMaterials[i].name;
    if (!name.empty()) {
      s_materialIndicesByName[name] = static_cast<int>(i);
    }
    if (i < s_materialStableIds.size() && !s_materialStableIds[i].empty()) {
      s_materialIndicesByStableId[s_materialStableIds[i]] = static_cast<int>(i);
    }
  }

  // Remap material indices in meshes, nodes, scatter targets, shared entries
  for (Asset::GpuMesh &mesh : g_loadedMeshes) {
    if (mesh.materialIndex >= 0 &&
        mesh.materialIndex < static_cast<int>(materialRemap.size())) {
      mesh.materialIndex = materialRemap[static_cast<size_t>(mesh.materialIndex)];
    } else if (mesh.materialIndex >= static_cast<int>(materialRemap.size())) {
      mesh.materialIndex = -1;
    }
  }
  for (Node &node : s_nodes) {
    for (int &linkedIdx : node.linkedMaterialIndices) {
      if (linkedIdx >= 0 && linkedIdx < static_cast<int>(materialRemap.size())) {
        linkedIdx = materialRemap[static_cast<size_t>(linkedIdx)];
      } else if (linkedIdx >= static_cast<int>(materialRemap.size())) {
        linkedIdx = -1;
      }
    }
  }
  for (auto &[_, entry] : s_sharedImportedMeshesBySourcePath) {
    for (int &linkedIdx : entry.linkedMaterialIndices) {
      if (linkedIdx >= 0 && linkedIdx < static_cast<int>(materialRemap.size())) {
        linkedIdx = materialRemap[static_cast<size_t>(linkedIdx)];
      } else if (linkedIdx >= static_cast<int>(materialRemap.size())) {
        linkedIdx = -1;
      }
    }
  }
  // Scatter target material hints follow the compaction remap (scatter.cpp
  // owns the state and handles the rewrite).
  RemapScatterTargetMaterialIndices(materialRemap);

  // --- Pass 3: collect all texture indices used by remaining materials ---
  std::vector<bool> textureUsed(g_loadedTextures.size(), false);

  auto markTexture = [&](int texIdx) {
    if (texIdx >= 0 && static_cast<size_t>(texIdx) < textureUsed.size()) {
      textureUsed[static_cast<size_t>(texIdx)] = true;
    }
  };

  for (const auto &mat : g_loadedMaterials) {
    markTexture(mat.diffuseTexture);
    markTexture(mat.normalTexture);
    markTexture(mat.opacityTexture);
    markTexture(mat.emissiveTexture);
    markTexture(mat.occlusionTexture);
    markTexture(mat.metalRoughTexture);
    markTexture(mat.runtimeMetalRoughTexture);
    markTexture(mat.metalnessTexture);
    markTexture(mat.roughnessGlossTexture);
    markTexture(mat.specularColorTexture);
    markTexture(mat.thicknessTexture);
    markTexture(mat.coatNormalTexture);
    markTexture(mat.parallaxTexture);
  }

  // --- Pass 4: compact textures (keep only used, rewrite descriptors) ---
  size_t removedTextures = 0;
  std::vector<int> textureRemap(g_loadedTextures.size(), -1);
  {
    std::vector<Asset::Texture> compactedTextures;
    compactedTextures.reserve(g_loadedTextures.size());

    for (size_t oldIdx = 0; oldIdx < g_loadedTextures.size(); ++oldIdx) {
      if (!textureUsed[oldIdx]) {
        ++removedTextures;
        continue;
      }
      textureRemap[oldIdx] = static_cast<int>(compactedTextures.size());
      compactedTextures.push_back(std::move(g_loadedTextures[oldIdx]));
    }
    g_loadedTextures = std::move(compactedTextures);
  }

  // Rewrite SRV descriptors for all remaining textures at their new positions,
  // then clamp the active descriptor count.
  RegisterTextures(g_loadedTextures);
  DxrRenderer::MarkTextureDescriptorTableDirty();

  // Fix up s_textureIndicesBySourceUri after compaction
  for (auto it = s_textureIndicesBySourceUri.begin();
       it != s_textureIndicesBySourceUri.end();) {
    int oldIdx = it->second;
    if (oldIdx >= 0 && oldIdx < static_cast<int>(textureRemap.size()) &&
        textureRemap[static_cast<size_t>(oldIdx)] >= 0) {
      it->second = textureRemap[static_cast<size_t>(oldIdx)];
      ++it;
    } else {
      it = s_textureIndicesBySourceUri.erase(it);
    }
  }

  // Fix up material texture indices
  auto remapTexIdx = [&](int &texIdx) {
    if (texIdx >= 0 && static_cast<size_t>(texIdx) < textureRemap.size()) {
      texIdx = textureRemap[static_cast<size_t>(texIdx)];
    } else if (texIdx >= static_cast<int>(textureRemap.size())) {
      texIdx = -1;
    }
  };

  for (auto &mat : g_loadedMaterials) {
    remapTexIdx(mat.diffuseTexture);
    remapTexIdx(mat.normalTexture);
    remapTexIdx(mat.opacityTexture);
    remapTexIdx(mat.emissiveTexture);
    remapTexIdx(mat.occlusionTexture);
    remapTexIdx(mat.metalRoughTexture);
    remapTexIdx(mat.runtimeMetalRoughTexture);
    remapTexIdx(mat.metalnessTexture);
    remapTexIdx(mat.roughnessGlossTexture);
    remapTexIdx(mat.specularColorTexture);
    remapTexIdx(mat.thicknessTexture);
    remapTexIdx(mat.coatNormalTexture);
    remapTexIdx(mat.parallaxTexture);
  }

  // Signal the renderer that material data changed
  DxrRenderer::RequestAccelerationStructureRebuild();
  DxrRenderer::ResetAccumulation();

  char buf[256];
  snprintf(buf, sizeof(buf),
           "Cleaned %zu orphaned material(s) and %zu orphaned texture(s).",
           removedMaterials, removedTextures);
  return std::string(buf);
}

} // namespace Scene
