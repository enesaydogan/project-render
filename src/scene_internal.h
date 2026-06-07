#pragma once
// scene_internal.h
//
// Helpers exposed by scene.cpp to other in-process scene modules (currently
// scatter.cpp). These are not part of the public Scene API — they expose
// internals such as world-transform construction and viewport picking that
// only sibling scene-side TUs should reach for.

#include "scene.h"

#include <array>
#include <cstddef>
#include <vector>

namespace Scene {

// ---- Renderer invalidation ------------------------------------------------
// Defined in scene.cpp. Mirrored here so sibling TUs can ask the scene to
// re-evaluate accumulation / TLAS / full AS rebuilds without duplicating
// the enum.
enum class RendererInvalidationPlan {
  None,
  AccumulationOnly,
  TlasRefresh,
  FullAccelerationStructureRebuild,
};
void ApplyRendererInvalidation(RendererInvalidationPlan plan);

// Notify registered change listeners that scene/scatter state mutated.
// Also invalidates scatter caches.
void NotifySceneChanged();

// Invalidate just scatter's instance cache without notifying full scene
// listeners. Called from scatter.cpp internal code when only render state
// (not authoring state) changes.
void InvalidateScatterRuntimeCache();

// ---- Node / transform helpers --------------------------------------------
// Build column-major world-transforms for every Scene::Node, respecting
// import-group parenting. Indices match Scene::GetNodes().
std::vector<std::array<float, 16>> BuildNodeWorldTransforms();

// True if nodeIndex sits at or below ancestorIndex in the parent chain.
bool IsNodeDescendantOf(size_t nodeIndex, size_t ancestorIndex);

// out = a * b (column-major 4x4). out may alias a or b.
void MulColumnMajor4x4(const float *a, const float *b, float *out);

// Inverse of a column-major 4x4. Returns false if singular.
bool Inverse4x4(const float *m, float *out);

// Mutable access to scene nodes (for scatter "hide source" toggling).
std::vector<Node> &GetMutableNodes();

// ---- Viewport picking ----------------------------------------------------
struct SceneMeshPickHit {
  size_t nodeIndex = static_cast<size_t>(-1);
  size_t meshIndex = static_cast<size_t>(-1);
  float worldPosition[3] = {};
  float worldNormal[3] = {0.0f, 1.0f, 0.0f};
};
bool PickSceneMeshAt(float screenX, float screenY, float screenWidth,
                     float screenHeight, SceneMeshPickHit &outHit);

} // namespace Scene
