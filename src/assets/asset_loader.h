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

enum class TextureCompressionMode : uint32_t {
  Off = 0,
  Balanced = 1,
  HighQuality = 2,
};

enum class TextureUsageSemantic : uint32_t {
  Unknown = 0,
  Color = 1,
  ColorAlpha = 2,
  Normal = 3,
  PackedSurface = 4,
  Scalar = 5,
  Emissive = 6,
  Hdr = 7,
};

struct Texture {
  ComPtr<ID3D12Resource> resource;
  UINT width = 0;
  UINT height = 0;
  UINT mipLevels = 1;
  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  DXGI_FORMAT cpuFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  UINT cpuMipLevels = 1;
  TextureUsageSemantic usageSemantic = TextureUsageSemantic::Unknown;
  TextureCompressionMode compressionMode = TextureCompressionMode::Off;
  bool gpuCompressed = false;
  std::vector<uint8_t> cpuData; // Added for serialization
  bool hiddenInEditor = false;
};

struct Material {
  static constexpr uint32_t kSchemaVersionOpenPbrSubset = 9;
  static constexpr uint32_t kSchemaVersionCoronaArchviz = 14;
  static constexpr uint32_t kWorkflowMetalRoughness = 0;
  static constexpr uint32_t kWorkflowReflectionGlossiness = 1;
  static constexpr uint32_t kMaterialClassGeneric = 0;
  static constexpr uint32_t kMaterialClassMetal = 1;
  static constexpr uint32_t kMaterialClassGlass = 2;
  static constexpr uint32_t kMaterialClassFabric = 3;
  static constexpr uint32_t kMaterialClassLeaf = 4;
  static constexpr uint32_t kMaterialClassEmissive = 5;
  static constexpr uint32_t kTriPlanarVariationOff = 0;
  static constexpr uint32_t kTriPlanarVariationPerMesh = 1;
  static constexpr uint32_t kTriPlanarVariationPerSurface = 2;
  static constexpr uint32_t kParallaxModeOff = 0;
  static constexpr uint32_t kParallaxModeHeightMap = 1;
  static constexpr uint32_t kParallaxModeWindowBox = 2;

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
  float uvRotationDegrees = 0.0f;

  // Tri-planar mapping (world-space projection)
  float triPlanarEnabled = 0.0f;   // 0/1
  float triPlanarScale = 1.0f;     // world tiling frequency
  float triPlanarSharpness = 4.0f; // blending exponent (higher = sharper)
  float triPlanarNormalStrength =
      1.0f; // normal intensity for tri-planar normal maps
  float triPlanarRotationDegrees[3] = {0.0f, 0.0f, 0.0f};
  uint32_t triPlanarVariationMode = kTriPlanarVariationOff;
  float triPlanarVariationOffset =
      0.0f; // shared jitter for tri-planar variation and UV stochastic tiling
  float stochasticTilingRotationDegrees = 0.0f;
  float stochasticTilingColorVariation = 0.0f;
  bool stochasticTilingMirror = false;

  int diffuseTexture = -1; // Was baseColor
  int normalTexture = -1;
  int opacityTexture = -1;
  int emissiveTexture = -1;
  int occlusionTexture = -1;
  int metalRoughTexture = -1; // User-authored packed metal/rough map.
  int runtimeMetalRoughTexture = -1; // Internal derived packed runtime map.
  int metalnessTexture = -1;
  int roughnessGlossTexture = -1;
  int specularColorTexture = -1;
  int thicknessTexture = -1;
  int coatNormalTexture = -1;
  int parallaxTexture = -1;
  float diffuseTextureAmount = 1.0f;
  float opacityTextureAmount = 1.0f;
  float metalRoughTextureAmount = 1.0f;
  float metalnessTextureAmount = 1.0f;
  float roughnessGlossTextureAmount = 1.0f;
  float normalTextureAmount = 1.0f;
  bool normalMapFlipY = false;
  float occlusionTextureAmount = 1.0f;
  float emissiveTextureAmount = 1.0f;
  float specularColorTextureAmount = 1.0f;
  float thicknessTextureAmount = 1.0f;
  float coatNormalTextureAmount = 1.0f;
  uint32_t parallaxMode = kParallaxModeOff;
  float parallaxDepthScale = 0.0f;
  float parallaxRoomDepth = 1.0f;
  float parallaxWindowAspect = 1.0f;
  float parallaxUvScale[2] = {1.0f, 1.0f};
  float parallaxUvOffset[2] = {0.0f, 0.0f};
  bool parallaxBackFace = false;

  bool doubleSided = false;
  std::string alphaMode = "OPAQUE";
  bool invertRoughnessTexture = false;
  float alphaCutoff = 0.35f;

  // Grass controls (UI driven).
  bool isGrass = false;
  float grassColor[3] = {0.34f, 0.52f, 0.21f};
  float grassBladeSize = 0.6f;
  float grassBladeCount = 48.0f; // density in grass patches per square meter
  float grassBladeVariation = 0.45f; // 0=no randomness, 1=full random scale/yaw

  // Canonical OpenPBR runtime subset fields.
  uint32_t schemaVersion = kSchemaVersionCoronaArchviz;
  uint32_t materialClass = kMaterialClassGeneric;
  uint32_t workflow = kWorkflowMetalRoughness;
  float roughness = 0.2f;
  float specularWeight = 1.0f;
  float specularColor[3] = {1.0f, 1.0f, 1.0f};
  float transmissionWeight = 0.0f;
  float transmissionColor[3] = {1.0f, 1.0f, 1.0f};
  float thickness = 0.0f;
  float attenuationDistance = 0.0f; // 0 = disabled / infinite
  float coatWeight = 0.0f;
  float coatRoughness = 0.1f;
  float coatIor = 1.5f;
  float anisotropy = 0.0f;
  float anisotropyRotation = 0.0f;
  float sheenWeight = 0.0f;
  float sheenColor[3] = {1.0f, 1.0f, 1.0f};
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
  m.materialClass = Material::kMaterialClassLeaf;
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

void SetTextureCompressionMode(TextureCompressionMode mode);
TextureCompressionMode GetTextureCompressionMode();
const char *TextureCompressionModeName(TextureCompressionMode mode);
const char *TextureUsageSemanticName(TextureUsageSemantic semantic);
bool ApplyTextureCompressionForUsage(Texture &texture,
                                     TextureUsageSemantic semantic);

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
Texture LoadTextureFromFile(const std::string &path, bool isHDR,
                            TextureUsageSemantic semantic);

// Load a single texture from encoded image bytes (png/jpg/hdr/exr).
Texture LoadTextureFromEncodedMemory(const void *src, size_t size,
                                     bool isHDRHint = false);
Texture LoadTextureFromEncodedMemory(const void *src, size_t size,
                                     bool isHDRHint,
                                     TextureUsageSemantic semantic);

// Load a single texture from memory.
Texture LoadTextureFromMemory(const void *src, int width, int height,
                              DXGI_FORMAT format);
Texture LoadTextureFromMemoryMipChain(const void *src, size_t srcSize,
                                      int width, int height,
                                      DXGI_FORMAT format,
                                      uint32_t mipLevels);

// Load a single mesh from memory.
GpuMesh LoadMeshFromMemory(const std::vector<Vertex> &vertices,
                           const std::vector<uint32_t> &indices);
// Upload any meshes that still only have CPU geometry to the GPU in one batch.
void UploadMeshes(std::vector<GpuMesh> &meshes);
} // namespace Asset
