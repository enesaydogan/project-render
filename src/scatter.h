#pragma once
// scatter.h
//
// Scatter authoring and runtime. Owned by scatter.cpp. Lives in the Scene
// namespace so existing call sites (Scene::ScatterModel, Scene::AddScatterModel,
// etc.) continue to compile unchanged after the extraction out of scene.cpp.

#include "asset_library/asset_id.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Scene {

// Forward decl from scene.h. We only need the type for the
// std::vector<Instance>& signature on AppendScatterInstances; full definition
// is supplied by including scene.h at the call site.
struct Instance;

struct ScatterTarget {
  size_t nodeIndex = static_cast<size_t>(-1);
  size_t meshIndex = static_cast<size_t>(-1);
  std::string nodeName;
  std::string sourcePath;
  std::string importGroupKey;
  int materialIndex = -1;
  int materialSlot = -1;
  float weight = 1.0f;
  bool enabled = true;
};

struct ScatterObject {
  std::string name = "Scatter Object";
  std::string sourceNodeName;
  std::string sourcePath;
  std::vector<size_t> meshIndices;
  std::vector<std::array<float, 16>> meshLocalTransforms;
  float densityPerSquareMeter = 12.0f;
  float weight = 1.0f;
  uint32_t maxInstances = 25000;
  uint32_t previewMaxInstances = 25000;
  float minScale = 0.85f;
  float maxScale = 1.15f;
  float randomYawDegrees = 360.0f;
  float randomPitchDegrees = 0.0f;
  float randomRollDegrees = 0.0f;
  float normalAlign = 1.0f;
  float slopeMinDegrees = 0.0f;
  float slopeMaxDegrees = 55.0f;
  float heightMin = -100000.0f;
  float heightMax = 100000.0f;
  float jitterMeters = 0.0f;
  float minDistance = 0.0f;
  float maxDistance = 0.0f; // 0 = unlimited
  // D7: smooth fade width *inside* maxDistance. e.g. maxDistance=100,
  // distanceFadeMeters=20 -> full density to 80m, linear thin to 0 at 100m.
  // 0 = hard cutoff (legacy behavior).
  float distanceFadeMeters = 0.0f;
  float clumpScale = 0.0f;
  float clumpStrength = 0.0f;
  // World-space inset from open target-mesh boundaries. Internal triangle
  // edges are ignored.
  float edgeTrimMeters = 0.0f;
  // Minimum center-to-center spacing shared by every prototype in the model.
  // This is deliberate density thinning, not scene-geometry collision.
  float instanceSpacingMeters = 0.0f;
  // Reject placements this close to visible, non-target scene geometry.
  float meshClearanceMeters = 0.0f;
  // D5: skip placements within this radius of any enabled scene light.
  // 0 = disabled. Cheap O(instances * lights) check; lights are usually few.
  float avoidLightRadius = 0.0f;
  bool enabled = true;
  bool librarySourceHidden = false;
};

struct ScatterModel {
  std::string name = "Scatter";
  uint32_t seed = 1;
  bool enabled = true;
  float previewDensityScale = 1.0f;
  uint32_t previewInstanceBudget = 200000;
  std::vector<ScatterTarget> targets;
  std::vector<ScatterObject> objects;
};

// Per-object slice of the last cache rebuild. Exposed so the panel can show
// a per-object instance breakdown rather than just a single total.
struct ScatterObjectStats {
  size_t modelIndex = 0;
  size_t objectIndex = 0;
  std::string modelName;
  std::string objectName;
  uint32_t instancesGenerated = 0;
  uint32_t skippedByObjectCap = 0;
  uint32_t skippedByEdgeTrim = 0;
  uint32_t skippedByMeshClearance = 0;
  uint32_t skippedBySpacing = 0;
};

struct ScatterRuntimeStats {
  uint32_t generatedInstances = 0;
  // Renamed conceptually: this is now "model preview budget overflow".
  // Per-object cap overflow is tracked separately so the stats label can
  // distinguish "raise the model budget" from "raise the per-object cap."
  uint32_t skippedByBudget = 0;
  uint32_t skippedByObjectCap = 0;
  uint32_t skippedByEdgeTrim = 0;
  uint32_t skippedByMeshClearance = 0;
  uint32_t skippedBySpacing = 0;
  uint32_t activeTargets = 0;
  uint32_t activeObjects = 0;
  uint64_t revision = 0;
  std::vector<ScatterObjectStats> perObject; // unsorted; UI sorts as it sees fit
};

// ---- Model CRUD ----------------------------------------------------------
const std::vector<ScatterModel> &GetScatterModels();
void SetScatterModels(std::vector<ScatterModel> models);
size_t AddScatterModel(const std::string &name = "Scatter");
bool RemoveScatterModel(size_t index);

// Full-replace update. Conservatively requests a full BLAS+TLAS rebuild
// because mesh set membership may have changed. Prefer the granular
// updates below for slider-drag edits — they pick TlasRefresh when the
// mesh set is identical, avoiding the GPU stutter that a full AS rebuild
// causes on every spinbox tick.
bool UpdateScatterModel(size_t index, const ScatterModel &model);
// Header = name, seed, enabled, previewDensityScale, previewInstanceBudget.
// Never changes mesh set, so always TlasRefresh.
bool UpdateScatterModelHeader(size_t index, const ScatterModel &header);
// Per-target update. Uses TlasRefresh if nodeIndex/meshIndex are unchanged,
// full rebuild otherwise.
bool UpdateScatterTarget(size_t modelIndex, size_t targetIndex,
                         const ScatterTarget &target);
// Per-object update. Uses TlasRefresh if meshIndices vector is unchanged,
// full rebuild otherwise.
bool UpdateScatterObject(size_t modelIndex, size_t objectIndex,
                         const ScatterObject &object);

// ---- Target / object population from current selection -------------------
bool AddSelectedNodesAsScatterTargets(size_t scatterIndex);
bool AddSelectedNodesAsScatterObjects(size_t scatterIndex);

// ---- Asset library integration (Phase 3) ---------------------------------
// Drag a library Model asset into a scatter model: instantiates the model as a
// hidden source node and adds it as a scatter object with correct orientation
// and materials. Returns true on success.
bool AddLibraryModelAsScatterObject(size_t scatterIndex,
                                    const assetlib::AssetId &modelId);
// Drag a library ScatterObject asset into a scatter model: instantiates its
// source model and applies the asset's saved placement params.
bool AddScatterObjectAssetToModel(size_t scatterIndex,
                                  const assetlib::AssetId &scatterObjectId);
// Apply a library ScatterPreset asset. objectIndex < 0 applies the preset to
// every object in the model (plus model header); otherwise to that object only.
bool ApplyScatterPresetAsset(size_t scatterIndex, int objectIndex,
                             const assetlib::AssetId &presetId);
// Save a configured scatter object as a reusable ScatterObject library asset
// (records its source Model AssetId dependency via sourcePath). Returns the new
// asset id (invalid on failure, e.g. no resolvable source model).
assetlib::AssetId SaveScatterObjectAsAsset(size_t scatterIndex,
                                           size_t objectIndex,
                                           const std::string &assetName);
// Save ALL prototypes of a scatter model as a single multi-object ScatterObject
// library asset (e.g. a grass+flowers+rocks mix). Each prototype records its
// source Model AssetId. Objects whose source model is not in the library are
// skipped. Returns invalid if none could be saved.
assetlib::AssetId SaveScatterModelAsAsset(size_t scatterIndex,
                                          const std::string &assetName);
// Save placement params as a reusable ScatterPreset library asset. Captures the
// given object's params (objectIndex >= 0) plus the model header settings.
assetlib::AssetId SaveScatterPresetAsset(size_t scatterIndex, int objectIndex,
                                         const std::string &assetName);

// ---- Viewport pick targeting ---------------------------------------------
bool AddScatterTargetFromPick(size_t scatterIndex, float screenX, float screenY,
                              float screenWidth, float screenHeight);
void SetScatterPickTarget(size_t scatterIndex);
bool IsScatterPickingTarget();
bool HandleScatterPick(float screenX, float screenY, float screenWidth,
                       float screenHeight);
void CancelScatterPick();

// ---- Library housekeeping ------------------------------------------------
bool SetScatterObjectSourcesHidden(size_t scatterIndex, size_t objectIndex,
                                   bool hidden);
// Prunes scatter objects whose meshes are gone AND scatter targets whose
// node references became invalid (nodeIndex == size_t(-1)) after node
// removal. Single sweep so the panel only needs one "Clean" button.
bool RemoveUnusedScatterObjects(size_t scatterIndex);

// ---- Runtime stats -------------------------------------------------------
ScatterRuntimeStats GetScatterRuntimeStats();
uint64_t GetScatterRuntimeRevision();

// D2: flatten a scatter model's current instances into real Scene::Nodes.
// Triggers a cache rebuild so the result reflects current authoring state.
// Returns the number of nodes created. On success the source model can be
// optionally disabled by the caller (UI does this so baked + procedural
// don't double-render).
size_t BakeScatterModelToNodes(size_t modelIndex);

// ---- Hooks called from scene.cpp -----------------------------------------
// Append scatter-generated instances to the render list. Called by
// Scene::GetInstances() during BLAS / TLAS gathering.
void AppendScatterInstances(std::vector<Instance> &outInstances);

// Called when a scene node is removed so target.nodeIndex references can be
// fixed up (decremented or disabled). Bumps the scatter runtime revision.
void ReindexScatterNodeReferencesAfterRemoval(size_t removedNodeIndex);

// Apply a material-index remap (after material compaction) to all scatter
// target material hints. Returns the number of indices rewritten.
size_t RemapScatterTargetMaterialIndices(const std::vector<int> &remap);

// Called by scene's NotifySceneChanged path to bump the scatter cache
// revision when scene-side state changes. Decoupled from
// InvalidateScatterRuntimeCache (which is for scatter's own use).
void OnSceneStateChanged();

// Per-frame tick the renderer calls *before* deciding whether the TLAS
// needs rebuild. When any scatter object uses min/max distance culling
// and the camera has moved since the last scatter cache build, this
// queues a full-AS rebuild for the current frame so distance-faded
// scatter actually moves with the camera (without this, the scatter
// cache rebuilds correctly but the renderer never asks for the new
// instance set, leaving the TLAS frozen until the next authoring edit).
void TickScatterCameraInvalidation();

} // namespace Scene
