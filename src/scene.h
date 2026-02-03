#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include "assets/asset_loader.h"

namespace Scene {

struct Node {
  std::string name;
  std::vector<size_t> meshIndices; // indices into global g_loadedMeshes
  float transform[16];             // 4x4 column-major matrix
  bool selected = false;
  bool visible = true;

  Node();
};

// Instance mapping for rendering
struct Instance {
  Asset::GpuMesh mesh;
  const float* transform; // pointer to node.transform
  size_t nodeIndex;
};

// Import a model (glTF, OBJ, STL) file into the scene. Returns true on success.
bool ImportModel(const std::string &utf8path, const float* rootTranslation = nullptr);
// Open file dialog and import selected model
bool ImportModelWithDialog(HWND hwnd);
// Open file dialog and import selected HDR/EXR
bool ImportHDRWithDialog(HWND hwnd);

// Add a default ground plane
void AddDefaultPlane(float offset_y = 0.0f);

// Draw the Scene (Assets) panel UI. Should be called by main when assets window is visible.
void DrawScenePanel(HWND hwnd, bool &visible);

// Draw the ImGuizmo gizmo for selection
void DrawGizmo();

// Node manipulation
const std::vector<Node>& GetNodes();
void SelectNode(size_t index);
std::vector<Instance> GetInstances();

// Ray-cast selection from mouse. Returns the global material index of the hit submesh, or -1.
int UpdateSelection(float screenWidth, float screenHeight);
void DeleteNode(size_t index);

// Rebuild acceleration structures using active meshes
void RebuildAccelerationStructures();

// Return vector of active meshes (copy) for rendering / DXR dispatch
std::vector<Asset::GpuMesh> GetActiveMeshes();

// Status message to display in UI
const std::string& LastStatus();

} // namespace Scene
