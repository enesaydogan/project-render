#pragma once

#include "asset_library/asset_id.h"
#include "assets/asset_loader.h"
#include "ies_profile.h"
#include "light.h"
#include "scatter.h"
#include <DirectXMath.h>
#include <array>
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

struct VolumeMaterial {
  float densityScale = 1.0f;
  float absorption = 1.0f;
  float scattering = 0.6f;
  float ambient = 0.35f;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float emissionColor[3] = {1.0f, 0.35f, 0.05f};
  // ~100 with the shader's internal x1000 scale gives a full flame; headroom to
  // 1000. Only volumes with a temperature channel emit (smoke-only unaffected).
  float emissionStrength = 100.0f;
  float stepJitter = 1.0f;
  int marchSteps = 96;
  int lightSteps = 8;
};

struct Node {
  std::string name;
  std::vector<size_t> meshIndices; // indices into global g_loadedMeshes
  size_t parentIndex;              // invalid when this is a root node
  float transform[16];             // 4x4 column-major matrix
  std::string sourcePath;          // Path to the asset file for re-loading
  std::string importGroupKey;      // Shared key for imported multi-node groups
  std::string volumeAssetId;       // non-empty => this node is a volumetric asset
  VolumeMaterial volumeMaterial;
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

// Scatter types (ScatterTarget, ScatterObject, ScatterModel, ScatterRuntimeStats)
// and the scatter API are declared in scatter.h, which is included above.

struct ImportedNodePayload {
  std::string sourcePath;
  std::string displayName;
  std::string importGroupKey;
  std::array<float, 3> rootTranslation = {};
  std::vector<Asset::GpuMesh> meshes;
  std::vector<Asset::ImportedSceneNode> sceneNodes;
  std::vector<Asset::Material> materials;
  std::vector<std::string> materialStableIds;
  std::vector<int> preferredLinkedMaterialIndices;
  std::vector<std::string> preferredLinkedMaterialSourceNames;
  std::vector<Asset::Texture> textures;
  std::vector<std::string> textureSourceUris;
  bool materialsContainFullDefinitions = true;
  bool hasRootTranslation = false;
  // Set when the payload was rebuilt from a library asset (InstantiateAssetModel)
  // so AddImportedNode does not re-register/recook it into the asset library.
  bool skipLibraryRegister = false;
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
bool ImportModelAsync(const std::string &utf8path,
                      const float *rootTranslation = nullptr);
bool ResolveViewportImportPlacement(float screenX, float screenY,
                                    float screenWidth, float screenHeight,
                                    float outTranslation[3]);
bool ReimportNode(size_t index);
bool CanReimportNode(size_t index);
// Instantiate a library Model asset into the scene by resolving its cooked
// payload (the Assets panel drag-and-drop target). Returns true on success.
bool InstantiateAssetModel(const assetlib::AssetId &id,
                           const float *rootTranslation = nullptr,
                           size_t *outNodeIndex = nullptr);
// Assign a library Material asset to every currently selected node (the
// Assets-panel material drag target). Returns true if at least one node was
// updated.
bool AssignMaterialAssetToSelection(const assetlib::AssetId &id);

// Instantiate a library Volume asset as a selectable, transformable scene node
// (the Assets-panel volume drag target). The node carries no meshes; the
// volumetric renderer places/scales the volume by this node's transform. The
// default transform fits the volume to a sensible size. Returns the node index.
size_t AddVolumeNode(const assetlib::AssetId &id);
// Copies the (root) volume node's column-major transform for `id` into out[16].
// Returns false if no such node exists. Used by the volumetric renderer.
bool FindVolumeNodeTransform(const assetlib::AssetId &id, float out[16]);
bool FindVolumeNodeMaterial(const assetlib::AssetId &id,
                            VolumeMaterial &out);
bool SetVolumeNodeMaterial(size_t nodeIndex, const VolumeMaterial &material);

// Extract a self-contained Model asset directly from a set of global scene
// meshes (with the meshes' materials and textures), baking the supplied
// per-mesh local transforms into the geometry so orientation/arrangement is
// preserved. Used to auto-register scatter prototype source models that are not
// yet in the library. Returns the new Model AssetId (invalid on failure).
assetlib::AssetId ExtractModelAssetFromMeshes(
    const std::vector<size_t> &meshIndices,
    const std::vector<std::array<float, 16>> &localTransforms,
    const std::string &displayName, const std::string &folder,
    const std::string &sourcePath);
// Open file dialog and import selected model
bool ImportModelWithDialog(HWND hwnd);
// Open file dialog and import selected HDR/EXR
bool ImportHDRWithDialog(HWND hwnd);

// Load a texture from file and append it to the global texture array.
// Returns the global texture index, or -1 on failure.
int AddTextureFromFile(const std::string &utf8path, bool isHDR = false);
int AddTextureFromFile(const std::string &utf8path, bool isHDR,
                       Asset::TextureUsageSemantic semantic);
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
bool IsTransformGizmoActiveOrHovered();
bool IsTransformGizmoHitAt(float screenX, float screenY, float screenWidth,
                           float screenHeight);

// Draw ImGuizmo gizmo for the selected light
void DrawLightGizmo();
void SetLightGizmosVisible(bool visible);
bool AreLightGizmosVisible();
int PickLightGizmoAt(float screenX, float screenY, float screenWidth,
                     float screenHeight);

enum class GizmoOperation {
  Translate,
  Rotate,
  Scale,
};

enum class GizmoSpace {
  Local,
  World,
};

enum class SelectionToolMode {
  Pointer,
  Box,
};

enum class SelectionFilter {
  Meshes,
  Lights,
  MeshesAndLights,
};

enum class MirrorAxis {
  X,
  Y,
  Z,
};

enum class MirrorPivot {
  SelectionCenter,
  WorldOrigin,
  ActiveNode,
};

enum class MirrorSpace {
  World,
  Local,
};

enum class LightPlacementMode {
  None,
  Create,
  MoveSelected,
};

void SetGizmoOperation(GizmoOperation operation);
GizmoOperation GetGizmoOperation();
void SetGizmoSpace(GizmoSpace space);
GizmoSpace GetGizmoSpace();
void SetSelectionToolMode(SelectionToolMode mode);
SelectionToolMode GetSelectionToolMode();
void SetSelectionFilter(SelectionFilter filter);
SelectionFilter GetSelectionFilter();
bool MirrorSelectedNodes(MirrorAxis axis, MirrorPivot pivot, MirrorSpace space);
bool CanUndoTransform();
bool CanRedoTransform();
bool UndoTransform();
bool RedoTransform();
void ClearTransformHistory();

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
// Converts a multi-mesh node into a transform group with one independently
// selectable standalone node per mesh. Meshes in descendants are included and
// their world transforms are preserved. The exploded result is detached from
// import/reimport linkage so later source updates cannot overwrite authored
// deletions. Returns the number of mesh nodes created.
bool CanExplodeNodeMeshes(size_t index);
size_t ExplodeNodeMeshes(size_t index);
bool RemoveNode(size_t index);
void SelectNode(size_t index);
void SelectNodes(const std::vector<size_t> &indices);
std::vector<size_t> GetSelectedNodeIndices();
bool HasPendingCloneOptions();
void ResolvePendingCloneAsCopy();
void ResolvePendingCloneAsInstance();
size_t RegisterChangeListener(std::function<void()> callback);
void UnregisterChangeListener(size_t listenerId);
std::vector<Instance> GetInstances();

// Scatter API lives in scatter.h (included above): GetScatterModels,
// AddScatterModel, AddSelectedNodesAsScatter*, AddScatterTargetFromPick,
// SetScatterPickTarget, etc. Kept in the Scene namespace for compatibility.

// Light manipulation
const std::vector<LightPrototype> &GetLightPrototypes();
const std::vector<LightInstance> &GetLightInstances();
const std::vector<Light> &GetLights();
std::vector<size_t> GetSelectedLightIndices();

// Prototype operations
size_t AddLightPrototype(LightType type);
size_t AddLightPrototypeRaw(const LightPrototype &proto); // no auto-instance, for scene loading
bool UpdateLightPrototype(size_t index, const LightPrototype &proto);
void RemoveLightPrototype(size_t index);
size_t DuplicateLightInstanceAsInstance(size_t instanceIndex);
size_t DuplicateLightInstanceAsCopy(size_t instanceIndex);
int MergeCompatibleLightPrototypes(size_t targetPrototypeIndex);
int FindLightPrototypeByStableId(const std::string &stableId);
bool SetLightPrototypeStableId(size_t index, const std::string &stableId);

// Instance operations
size_t AddLightInstance(size_t prototypeIndex);
size_t AddLightInstanceRaw(const LightInstance &inst); // no flatten call, for scene loading
bool UpdateLightInstance(size_t index, const LightInstance &inst);
void RemoveLightInstance(size_t index);
void RemoveLightInstances(const std::vector<size_t> &indices);

// Selection (instance-based)
int GetSelectedLightIndex();
void SelectLight(int instanceIndex);
void SelectLights(const std::vector<size_t> &instanceIndices);
void ToggleLightSelection(size_t instanceIndex);

// Viewport light placement.
void BeginCreateLightAtClick(LightType type, int iesProfileIndex = -1);
void BeginMoveLightToSurface(int instanceIndex);
void BeginMoveSelectedLightToSurface();
bool IsLightPlacementActive();
LightPlacementMode GetLightPlacementMode();
void CancelLightPlacement();
bool HandleLightPlacement(float screenX, float screenY, float screenWidth,
                          float screenHeight);

// Rebuild flattened array and upload to GPU
void UpdateLights();

// IES profile management
const std::vector<IESProfile> &GetIESProfiles();
int LoadIESProfile(const std::string &path);
int AddIESProfileFromSource(const std::string &sourceText,
                            const std::string &originalPath,
                            const std::string &displayName);
int AddIESProfile(IESProfile profile);
void ClearIESProfile(int profileIndex);
void ClearIESProfiles();
void RebuildIESAtlas();
bool EnsureIESAtlasReady();

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
void RefreshTextureCompression(bool resetAccumulation = true);
GpuUploadStats GetGpuUploadStats();
void BeginBatchedUpdates();
void EndBatchedUpdates();

// Ray-cast selection from mouse. Returns the global material index of the hit
// submesh, or -1.
int UpdateSelection(float screenWidth, float screenHeight);
bool BoxSelect(float startScreenX, float startScreenY, float endScreenX,
               float endScreenY, float screenWidth, float screenHeight,
               bool additive);
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

// Clean up orphaned data — removes materials unreferenced by any node and
// textures unreferenced by any remaining material. Returns summary message.
std::string CleanOrphanedData();

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
