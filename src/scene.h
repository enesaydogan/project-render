#pragma once

#include "assets/asset_loader.h"
#include "light.h"
#include <DirectXMath.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

enum class RenderMode {
  Raster, // Fast rasterization for scene traversal
  DXR     // Unified Ray Tracing / Path Tracing path
};

namespace Scene {

struct Node {
  std::string name;
  std::vector<size_t> meshIndices; // indices into global g_loadedMeshes
  size_t parentIndex;              // invalid when this is a root node
  float transform[16];             // 4x4 column-major matrix
  std::string sourcePath;          // Path to the asset file for re-loading
  std::string importGroupKey;      // Shared key for imported multi-node groups
  std::vector<int> linkedMaterialIndices; // global material indices per import slot
  std::vector<std::string> linkedMaterialSourceNames; // original imported names per slot
  bool selected = false;
  bool visible = true;
  bool liveLinkManaged = false;
  bool importGroupRoot = false;
  bool selectionLocked = false;

  Node();
};

// Instance mapping for rendering
struct Instance {
  std::string name;
  DirectX::XMMATRIX transform;
  int id;
  const Asset::GpuMesh *mesh;
};

struct ImportedNodePayload {
  std::string sourcePath;
  std::string displayName;
  std::string importGroupKey;
  std::vector<Asset::GpuMesh> meshes;
  std::vector<Asset::ImportedSceneNode> sceneNodes;
  std::vector<Asset::Material> materials;
  std::vector<std::string> materialStableIds;
  std::vector<int> preferredLinkedMaterialIndices;
  std::vector<std::string> preferredLinkedMaterialSourceNames;
  std::vector<Asset::Texture> textures;
  std::vector<std::string> textureSourceUris;
  bool materialsContainFullDefinitions = true;
};

struct GpuUploadStats {
  uint64_t batchCount = 0;
  uint64_t totalUploadMs = 0;
  uint64_t lastUploadMs = 0;
  size_t lastMeshCount = 0;
};

// Import a model (glTF, OBJ, STL) file into the scene. Returns true on success.
bool ImportModel(const std::string &utf8path,
                 const float *rootTranslation = nullptr);
bool ImportModelAsync(const std::string &utf8path);
bool ReimportNode(size_t index);
bool CanReimportNode(size_t index);
// Open file dialog and import selected model
bool ImportModelWithDialog(HWND hwnd);
// Open file dialog and import selected HDR/EXR
bool ImportHDRWithDialog(HWND hwnd);

// Load a texture from file and append it to the global texture array.
// Returns the global texture index, or -1 on failure.
int AddTextureFromFile(const std::string &utf8path, bool isHDR = false);
int AddTexture(Asset::Texture texture);

// Add a default ground plane
void AddDefaultPlane(float offset_y = 0.0f);

// Draw the Scene (Assets) panel UI. Should be called by main when assets window
// is visible.
void DrawScenePanel(HWND hwnd, bool &visible);

// Draw the Lights panel UI.
void DrawLightsPanel(bool &visible);

// Clear all scene data (nodes, meshes, materials, textures)
void ResetScene();

// Draw the ImGuizmo gizmo for selection
void DrawGizmo();

// Draw ImGuizmo gizmo for the selected light
void DrawLightGizmo();

// Node manipulation
const std::vector<Node> &GetNodes();
size_t AddNode(Node node);
bool AddImportedNode(ImportedNodePayload payload,
                     size_t *outNodeIndex = nullptr);
bool ReplaceNodeImportedContent(size_t index, ImportedNodePayload payload);
bool RenameNode(size_t index, const std::string &name);
bool UpdateNodeTransform(size_t index, const float *columnMajor4x4);
bool SetNodeVisibility(size_t index, bool visible);
bool SetNodeSelectionLocked(size_t index, bool locked);
bool SetNodeLiveLinkManaged(size_t index, bool liveLinkManaged);
bool SetNodeParent(size_t index, size_t parentIndex);
bool RemoveNode(size_t index);
void SelectNode(size_t index);
void SelectNodes(const std::vector<size_t> &indices);
bool HasPendingCloneOptions();
void ResolvePendingCloneAsCopy();
void ResolvePendingCloneAsInstance();
size_t RegisterChangeListener(std::function<void()> callback);
void UnregisterChangeListener(size_t listenerId);
std::vector<Instance> GetInstances();

// Light manipulation
std::vector<Light> &GetLights();
size_t AddLight(LightType type);
bool UpdateLight(size_t index, const Light &light);
void RemoveLight(size_t index);
void UpdateLights();
int GetSelectedLightIndex();
void SelectLight(int index);

// Material manipulation
size_t GetMaterialCount();
size_t GetTextureCount();
int FindMaterialByName(const std::string &name);
int FindMaterialByStableId(const std::string &stableId);
bool GetMaterial(size_t index, Asset::Material *outMaterial);
int FindOrCreateMaterial(const Asset::Material &material,
                         const std::string &stableId = {});
bool SetMaterialStableId(size_t index, const std::string &stableId);
bool RebindNodeMaterialSlot(size_t nodeIndex, size_t materialSlot,
                            int materialIndex);
bool UpdateNodeMaterialSourceName(size_t nodeIndex, size_t materialSlot,
                                  const std::string &materialName);
bool UpdateMaterial(size_t index, const Asset::Material &material);
void RefreshAllMaterialRuntimeTextures();
GpuUploadStats GetGpuUploadStats();
void BeginBatchedUpdates();
void EndBatchedUpdates();

// Ray-cast selection from mouse. Returns the global material index of the hit
// submesh, or -1.
int UpdateSelection(float screenWidth, float screenHeight);
int PickMaterialAt(float screenX, float screenY, float screenWidth,
                   float screenHeight);
int PickMaterialAtCursor(float screenWidth, float screenHeight);
void DeleteNode(size_t index);

// Rebuild acceleration structures using active meshes
void RebuildAccelerationStructures();
// Request a deferred full renderer scene rebuild on the next DXR frame.
void RequestRendererFullRebuild();
// Request a deferred TLAS refresh on the next DXR frame.
void RequestRendererTlasRefresh();

// Return vector of pointers to active meshes for rendering / DXR dispatch
std::vector<const Asset::GpuMesh *> GetActiveMeshes();

// Status message to display in UI
const std::string &LastStatus();

// Import progress for async model import
bool IsImportInProgress();
float GetImportProgress();
std::string GetImportStatus();
void ProcessPendingImport();

// Register textures with the descriptor heap (create SRVs)
void RegisterTextures(const std::vector<Asset::Texture> &textures);

} // namespace Scene
