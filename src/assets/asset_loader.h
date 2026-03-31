#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <functional>
#include <string>
#include <vector>
#include <wrl.h>

// Simple asset loader API
namespace Asset {
using Microsoft::WRL::ComPtr;

struct GpuMesh {
  ComPtr<ID3D12Resource> vertexBuffer;
  ComPtr<ID3D12Resource> indexBuffer;
  D3D12_VERTEX_BUFFER_VIEW vbView{};
  D3D12_INDEX_BUFFER_VIEW ibView{};
  // Counts for convenience
  UINT vertexCount = 0;
  UINT indexCount = 0;
  int materialIndex = -1; // index into materials array if provided
  int materialSlot = -1; // Source material slot for the mesh

  // Bounding box
  float minBound[3] = {0, 0, 0};
  float maxBound[3] = {0, 0, 0};

  // CPU copies for raycasting/physics
  std::vector<struct Vertex> cpuVertices;
  std::vector<uint32_t> cpuIndices;
};

// Interleaved vertex layout used by the loader
struct Vertex {
  float pos[3];
  float normal[3];
  float tangent[4];
  float uv[2];
};

struct Texture {
  ComPtr<ID3D12Resource> resource;
  UINT width = 0;
  UINT height = 0;
  UINT mipLevels = 1;
  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  std::vector<uint8_t> cpuData; // Added for serialization
};

struct Material {
  static constexpr uint32_t kSchemaVersionOpenPbrSubset = 3;

  char name[64] = "Material";
  float diffuseColor[4] = {1, 1, 1, 1};
  float metalness = 0.0f; // Added for PBR support
  float ior = 1.6f;
  float emissiveColor[4] = {0, 0, 0, 1};
  float emissiveIntensity = 1.0f; // Multiplier for emissive

  // Thin-walled transmission model (window glass, leaves)
  float thinWalled = 0.0f; // 0/1
  // Diffuse-like translucency (leaves/fabric approximation)
  float translucency = 0.0f; // [0..1]
  // Simple UV transform for real-world scaling
  float uvScale[2] = {1.0f, 1.0f};
  float uvOffset[2] = {0.0f, 0.0f};

  // Tri-planar mapping (world-space projection)
  float triPlanarEnabled = 0.0f;   // 0/1
  float triPlanarScale = 1.0f;     // world tiling frequency
  float triPlanarSharpness = 4.0f; // blending exponent (higher = sharper)
  float triPlanarNormalStrength =
      1.0f; // normal intensity for tri-planar normal maps

  int diffuseTexture = -1; // Was baseColor
  int normalTexture = -1;
  int emissiveTexture = -1;
  int occlusionTexture = -1;
  int metalRoughTexture = -1; // Added for GLTF PBR support

  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
  bool invertRoughnessTexture = false;

  // Grass controls (UI driven).
  bool isGrass = false;
  float grassColor[3] = {0.34f, 0.52f, 0.21f};
  float grassBladeSize = 0.6f;
  float grassBladeCount = 48.0f; // density in blades per square meter
  float grassBladeVariation = 0.45f; // 0=no randomness, 1=full random scale/yaw

  // Canonical OpenPBR runtime subset fields.
  uint32_t schemaVersion = kSchemaVersionOpenPbrSubset;
  float roughness = 0.2f;
  float specularWeight = 1.0f;
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
};

struct ImportedSceneNode {
  std::string name;
  std::vector<size_t> meshIndices;
  size_t parentIndex = static_cast<size_t>(-1);
  float transform[16] = {0.0f};

  ImportedSceneNode() {
    transform[0] = 1.0f;
    transform[5] = 1.0f;
    transform[10] = 1.0f;
    transform[15] = 1.0f;
  }
};

inline void ApplyDefaultGrassLook(Material &m) {
  const bool hasDiffuseTexture = m.diffuseTexture >= 0;
  const float defaultTint[3] = {0.30f, 0.47f, 0.18f};
  const float texturedTint[3] = {0.94f, 0.98f, 0.92f};
  const float *tint = hasDiffuseTexture ? texturedTint : defaultTint;

  m.isGrass = true;
  m.grassColor[0] = tint[0];
  m.grassColor[1] = tint[1];
  m.grassColor[2] = tint[2];
  m.diffuseColor[0] = tint[0];
  m.diffuseColor[1] = tint[1];
  m.diffuseColor[2] = tint[2];
  m.diffuseColor[3] = 1.0f;
  m.metalness = 0.0f;
  m.roughness = 0.88f;
  m.specularWeight = 0.22f;
  m.thinWalled = 1.0f;
  m.translucency = 0.58f;
  m.doubleSided = true;
  m.alphaMode = hasDiffuseTexture ? "MASK" : "OPAQUE";
  m.grassBladeSize = 0.55f;
  m.grassBladeCount = 56.0f;
  m.grassBladeVariation = 0.62f;
}

// Initialize the loader with a device and command queue for GPU uploads.
void Initialize(ID3D12Device *device, ID3D12CommandQueue *queue);

// Progress callback: progress [0..1], status message. May be called from loader
// thread.
using ProgressCallback = std::function<void(float, const std::string &)>;
void SetProgressCallback(ProgressCallback cb);
void ClearProgressCallback();
// When enabled for the current thread, mesh loading will keep CPU geometry and
// defer GPU buffer creation. Useful for background import workers.
void SetDeferGpuUpload(bool enable);
bool GetDeferGpuUpload();

// Expose current progress callback to importers that run in separate translation
// units (used by skp_loader.cpp). Kept intentionally minimal.
extern std::function<void(float, const std::string &)> s_progressCb;

// Load a model based on extension (GLTF, OBJ, STL). Fills out created meshes in
// `outMeshes`.
bool LoadModel(const std::string &path, std::vector<GpuMesh> &outMeshes,
               std::vector<Material> *outMaterials = nullptr,
               std::vector<Texture> *outTextures = nullptr,
               const float *rootTranslation = nullptr,
               std::vector<ImportedSceneNode> *outSceneNodes = nullptr);

// Load a glTF file. Returns true on success.
bool LoadGltf(const std::string &path, std::vector<GpuMesh> &outMeshes,
              std::vector<Material> *outMaterials = nullptr,
              std::vector<Texture> *outTextures = nullptr,
              const float *rootTranslation = nullptr,
              std::vector<ImportedSceneNode> *outSceneNodes = nullptr);

// Load an OBJ file. Returns true on success.
bool LoadOBJ(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials = nullptr,
             std::vector<Texture> *outTextures = nullptr,
             const float *rootTranslation = nullptr,
             std::vector<ImportedSceneNode> *outSceneNodes = nullptr);

// Load an STL file. Returns true on success.
bool LoadSTL(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials = nullptr,
             std::vector<Texture> *outTextures = nullptr,
             const float *rootTranslation = nullptr,
             std::vector<ImportedSceneNode> *outSceneNodes = nullptr);

// Load a SketchUp (.skp) file. Requires BUILD option USE_SKETCHUP_SDK to be ON.
bool LoadSkp(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials = nullptr,
             std::vector<Texture> *outTextures = nullptr,
             const float *rootTranslation = nullptr,
             std::vector<ImportedSceneNode> *outSceneNodes = nullptr);

// Load a single texture from file.
Texture LoadTextureFromFile(const std::string &path, bool isHDR = false);

// Load a single texture from encoded image bytes (png/jpg/hdr/exr).
Texture LoadTextureFromEncodedMemory(const void *src, size_t size,
                                     bool isHDRHint = false);

// Load a single texture from memory.
Texture LoadTextureFromMemory(const void *src, int width, int height,
                              DXGI_FORMAT format);

// Load a single mesh from memory.
GpuMesh LoadMeshFromMemory(const std::vector<Vertex> &vertices,
                           const std::vector<uint32_t> &indices);
// Upload any meshes that still only have CPU geometry to the GPU in one batch.
void UploadMeshes(std::vector<GpuMesh> &meshes);
} // namespace Asset
