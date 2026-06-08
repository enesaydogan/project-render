#pragma once
// scatter.h
//
// Scatter authoring and runtime. Owned by scatter.cpp. Lives in the Scene
// namespace so existing call sites (Scene::ScatterModel, Scene::AddScatterModel,
// etc.) continue to compile unchanged after the extraction out of scene.cpp.

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
  float edgeAvoidance = 0.0f;
  float collisionAvoidanceRadius = 0.0f;
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
};

struct ScatterRuntimeStats {
  uint32_t generatedInstances = 0;
  // Renamed conceptually: this is now "model preview budget overflow".
  // Per-object cap overflow is tracked separately so the stats label can
  // distinguish "raise the model budget" from "raise the per-object cap."
  uint32_t skippedByBudget = 0;
  uint32_t skippedByObjectCap = 0;
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

} // namespace Scene
