#pragma once
#include "asset_library/asset_id.h"
#include "light.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include <vector>

// Renders cooked Volume assets as ray-marched density fields. Runtime data is
// cached per asset while transforms/materials remain owned by scene nodes.
namespace VolumetricRenderer {

struct Params {
  float densityScale = 1.0f;   // multiplies sampled density
  float absorption = 1.0f;     // extinction multiplier
  int marchSteps = 96;         // primary view-march steps
  int lightSteps = 8;          // secondary march toward the directional light
  float scatter = 0.6f;        // HG forward-scattering g
  float ambient = 0.35f;       // ambient fill (sky) contribution
  float stepJitter = 1.0f;     // 0..1 dither to hide banding
};

// Resolve + cache a Volume asset. Rendering walks all visible scene volume nodes.
bool SetActiveVolume(const assetlib::AssetId &id);
void ClearActiveVolume();
bool HasActiveVolume();
const assetlib::AssetId &ActiveVolumeId();

Params &GetParams();

// Adds a bounded set of point-light representatives sampled from visible
// emissive volume nodes. These participate in DXR/ReSTIR and raster lighting.
void AppendEmissionLights(std::vector<Light> &lights);

// --- Used by the raster renderer ---------------------------------------
// Performs any pending GPU upload onto cmdList. Returns false if nothing is
// active. After this, GetDensityTexture()/bounds are valid for binding.
bool EnsureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList);
ID3D12Resource *GetDensityTexture();
DirectX::XMFLOAT3 BoundsMin();
DirectX::XMFLOAT3 BoundsMax();

enum class DepthEncoding {
  HardwareDepth,
  LinearViewDepth,
};

// Composites every visible scene volume into colorTarget. colorTarget must be in UAV
// state and depthTexture must be in NON_PIXEL_SHADER_RESOURCE state. The caller
// owns resource transitions because raster and DXR have different surrounding
// stages.
bool Composite(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
               ID3D12Resource *cameraCB, ID3D12Resource *colorTarget,
               ID3D12Resource *depthTexture, UINT width, UINT height,
               DepthEncoding depthEncoding);

} // namespace VolumetricRenderer
