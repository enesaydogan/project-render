#pragma once
#include "asset_library/asset_id.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

// Renders a cooked Volume asset (from a .vdb) as a ray-marched density field
// composited into the raster HDR target. The active volume is resolved into a
// dense R32F 3D density texture and uploaded lazily on the render command list.
// This is the consumer the Phase-5 volume pipeline was missing.
namespace VolumetricRenderer {

struct Params {
  float densityScale = 1.0f;   // multiplies sampled density
  float absorption = 1.0f;     // extinction multiplier
  int marchSteps = 96;         // primary view-march steps
  float scatter = 0.6f;        // HG forward-scattering g
  float ambient = 0.35f;       // ambient fill (sky) contribution
  float stepJitter = 1.0f;     // 0..1 dither to hide banding
};

// Resolve + stage a Volume asset for rendering (main thread). Pass an invalid
// id to clear. Returns false if the asset can't be resolved.
bool SetActiveVolume(const assetlib::AssetId &id);
void ClearActiveVolume();
bool HasActiveVolume();
const assetlib::AssetId &ActiveVolumeId();

Params &GetParams();

// --- Used by the raster renderer ---------------------------------------
// Performs any pending GPU upload onto cmdList. Returns false if nothing is
// active. After this, GetDensityTexture()/bounds are valid for binding.
bool EnsureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList);
ID3D12Resource *GetDensityTexture();
DirectX::XMFLOAT3 BoundsMin();
DirectX::XMFLOAT3 BoundsMax();

} // namespace VolumetricRenderer
