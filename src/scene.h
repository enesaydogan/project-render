#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include "assets/asset_loader.h"

namespace Scene {

struct Node {
  std::string name;
  std::vector<size_t> meshIndices; // indices into global g_loadedMeshes
  bool selected = false;
  bool visible = true;
};

// Import a glTF/glb file into the scene. Returns true on success.
bool ImportGltf(const std::string &utf8path);
// Open file dialog and import selected glTF
bool ImportGltfWithDialog(HWND hwnd);

// Add a default ground plane
void AddDefaultPlane();

// Draw the Scene (Assets) panel UI. Should be called by main when assets window is visible.
void DrawScenePanel(HWND hwnd, bool &visible);

// Node manipulation
const std::vector<Node>& GetNodes();
void SelectNode(size_t index);
void DeleteNode(size_t index);

// Rebuild acceleration structures using active meshes
void RebuildAccelerationStructures();

// Return vector of active meshes (copy) for rendering / DXR dispatch
std::vector<Asset::GpuMesh> GetActiveMeshes();

// Status message to display in UI
const std::string& LastStatus();

} // namespace Scene
