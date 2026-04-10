#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "assets/asset_loader.h" // for Asset::GpuMesh

// One grass patch record written by CPU and consumed by compute shaders.
// A patch is the runtime unit for raster and DXR; the near/mid meshes provide
// the visual blade complexity.
struct FGrassPatch {
    DirectX::XMFLOAT3 position;
    float scale;
    DirectX::XMFLOAT3 normal;
    float yawRadians;
    DirectX::XMFLOAT2 emitterUv;
    uint32_t colorVariation;
    uint32_t packedData = 0;
};
static_assert(sizeof(FGrassPatch) == 48, "FGrassPatch layout must match HLSL.");

class GrassManager {
public:
    enum class LodBand : uint32_t {
        Near = 0,
        Mid = 1,
        Count = 2,
    };

    // initialize the manager after the D3D12 device has been created
    static void Initialize(ID3D12Device *device);
    static void Shutdown();

    // upload CPU-side patch list; data is copied into the GPU structured
    // buffer and the runtime count is updated.
    static void SetPatches(const std::vector<FGrassPatch> &patches);
    static void PrepareGpuBuffers(ID3D12GraphicsCommandList *cmdList);
    static void UploadRayTracingPatches(
        ID3D12GraphicsCommandList *cmdList,
        const std::vector<FGrassPatch> &patches);

    // legacy alias kept to minimize call-site churn.
    static void SetBlades(const std::vector<FGrassPatch> &patches);
    // legacy alias kept to minimize call-site churn.
    static void SetInstances(const std::vector<FGrassPatch> &patches);

    // called from the rasterization path once per frame right before
    // drawing the scene.  "cameraCB" should contain whatever frustum
    // parameters the culling shader expects (view/proj or 6 planes).
    static void CullingAndPrepareIndirect(ID3D12GraphicsCommandList *cmdList,
                                          ID3D12Resource *cameraCB);

    // execute the indirect draw generated in the culling pass.  This
    // assumes the vertex/index buffers for the grass patch have already
    // been bound.
    static void DrawVisible(ID3D12GraphicsCommandList *cmdList, LodBand band);

    // during TLAS build: append GPU-generated instance descriptors into
    // the provided buffer.  "sceneCount" is the number of descriptors
    // already written by the caller (scene geometry).  The compute shader
    // will write its data starting at that offset.
    // blasAddress is the GPU virtual address of the grass patch BLAS built
    // elsewhere (e.g. by DxrRenderer).  The compute shader uses it for each
    // instance record.
    static void AppendTlasInstances(ID3D12GraphicsCommandList *cmdList,
                                    ID3D12Resource *tlasDescBuffer,
                                    UINT sceneCount,
                                    UINT64 blasAddress,
                                    UINT patchMeshIndex);

    // helpers for the DXR build code
    static UINT GetPatchCount();
    static const std::vector<FGrassPatch> &GetPatches();
    static UINT GetInstanceCount();
    static const std::vector<FGrassPatch> &GetBlades();
    static D3D12_GPU_VIRTUAL_ADDRESS GetInstanceBufferGpuAddress();
    static D3D12_GPU_VIRTUAL_ADDRESS GetRayTracingInstanceBufferGpuAddress();
    static D3D12_GPU_VIRTUAL_ADDRESS GetVisibleBufferGpuAddress(LodBand band);
    static float GetNearDistance();
    static float GetMidDistance();

    // inform the manager which BLAS/mesh will be used as the grass patch.
    // the mesh's vertex/index buffers are bound once when drawing during
    // the raster path and the indirect command signature is created from
    // the mesh's index count.
    static void SetPatchMesh(const Asset::GpuMesh *mesh);
    static void SetMidPatchMesh(const Asset::GpuMesh *mesh);
    // query the mesh pointer that was registered (may be nullptr)
    static const Asset::GpuMesh *GetPatchMesh();
    static const Asset::GpuMesh *GetMidPatchMesh();

private:
    static void EnsureCapacity(UINT requested);
    static void UploadPatchBuffer(ID3D12GraphicsCommandList *cmdList);
    static void TransitionPatchBuffer(ID3D12GraphicsCommandList *cmdList,
                                      D3D12_RESOURCE_STATES newState);
    static size_t BandIndex(LodBand band);

    static Microsoft::WRL::ComPtr<ID3D12Resource> s_patchBuffer;
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_patchUploadBuffer;
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_rtPatchBuffer;
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_rtPatchUploadBuffer;
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_visibleBuffers[2];
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_indirectArgsBuffers[2];

    // compute pipeline objects for culling and TLAS generation
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> s_cullRootSig;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> s_cullPSO;
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> s_tlasRootSig;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> s_tlasPSO;

    // command signature used by ExecuteIndirect when drawing patches
    static Microsoft::WRL::ComPtr<ID3D12CommandSignature> s_drawCommandSignature;
    // meshes used for the grass patch per raster band.
    static const Asset::GpuMesh *s_patchMeshes[2];

    // capability information (allocated size)
    static UINT s_maxPatches;
    static UINT s_maxRtPatches;
    static UINT s_currentPatchCount;
    static UINT s_rtPatchCount;
    static std::vector<FGrassPatch> s_cpuPatches;
    static D3D12_RESOURCE_STATES s_patchBufferState;
    static D3D12_RESOURCE_STATES s_rtPatchBufferState;
    static D3D12_RESOURCE_STATES s_visibleBufferStates[2];
    static D3D12_RESOURCE_STATES s_indirectArgsStates[2];
    static bool s_patchUploadPending;
};
