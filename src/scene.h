#pragma once

#include "assets/asset_loader.h"
#include "light.h"
#include <DirectXMath.h>
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
  float transform[16];             // 4x4 column-major matrix
  std::string sourcePath;          // Path to the asset file for re-loading
  bool selected = false;
  bool visible = true;

  Node();
};

// Instance mapping for rendering
struct Instance {
  std::string name;
  DirectX::XMMATRIX transform;
  int id;
  const Asset::GpuMesh *mesh;
};

// Import a model (glTF, OBJ, STL) file into the scene. Returns true on success.
bool ImportModel(const std::string &utf8path,
                 const float *rootTranslation = nullptr);
// Open file dialog and import selected model
bool ImportModelWithDialog(HWND hwnd);
// Open file dialog and import selected HDR/EXR
bool ImportHDRWithDialog(HWND hwnd);

// Load a texture from file and append it to the global texture array.
// Returns the global texture index, or -1 on failure.
int AddTextureFromFile(const std::string &utf8path, bool isHDR = false);

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
void SelectNode(size_t index);
std::vector<Instance> GetInstances();

// Light manipulation
std::vector<Light> &GetLights();
void AddLight(LightType type);
void RemoveLight(size_t index);
void UpdateLights();

// Ray-cast selection from mouse. Returns the global material index of the hit
// submesh, or -1.
int UpdateSelection(float screenWidth, float screenHeight);
void DeleteNode(size_t index);

// Rebuild acceleration structures using active meshes
void RebuildAccelerationStructures();

// Return vector of pointers to active meshes for rendering / DXR dispatch
std::vector<const Asset::GpuMesh *> GetActiveMeshes();

// Status message to display in UI
const std::string &LastStatus();

// Register textures with the descriptor heap (create SRVs)
void RegisterTextures(const std::vector<Asset::Texture> &textures);

} // namespace Scene
