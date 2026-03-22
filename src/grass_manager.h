#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "assets/asset_loader.h" // for Asset::GpuMesh

// One instanced blade record written by CPU and consumed by compute shaders.
struct FGrassBlade {
    DirectX::XMFLOAT3 position;
    float scale;
    DirectX::XMFLOAT3 normal;
    float yawRadians;
    DirectX::XMFLOAT2 emitterUv;
    int32_t emitterDiffuseTexture = -1;
    uint32_t _padEmitter = 0;
    uint32_t colorVariation;
    uint32_t sourceMeshId;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};
static_assert(sizeof(FGrassBlade) == 64, "FGrassBlade layout must match HLSL.");

class GrassManager {
public:
    // initialize the manager after the D3D12 device has been created
    static void Initialize(ID3D12Device *device);
    static void Shutdown();

    // upload CPU-side blade list; data is copied into the GPU structured
    // buffer and the runtime count is updated.
    static void SetBlades(const std::vector<FGrassBlade> &blades);
    // legacy alias kept to minimize call-site churn.
    static void SetInstances(const std::vector<FGrassBlade> &instances);

    // called from the rasterization path once per frame right before
    // drawing the scene.  "cameraCB" should contain whatever frustum
    // parameters the culling shader expects (view/proj or 6 planes).
    static void CullingAndPrepareIndirect(ID3D12GraphicsCommandList *cmdList,
                                          ID3D12Resource *cameraCB);

    // execute the indirect draw generated in the culling pass.  This
    // assumes the vertex/index buffers for the grass patch have already
    // been bound.
    static void DrawVisible(ID3D12GraphicsCommandList *cmdList);

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
    static UINT GetInstanceCount();
    static const std::vector<FGrassBlade> &GetBlades();
    static D3D12_GPU_VIRTUAL_ADDRESS GetInstanceBufferGpuAddress();
    static D3D12_GPU_VIRTUAL_ADDRESS GetVisibleBufferGpuAddress();

    // inform the manager which BLAS/mesh will be used as the grass patch.
    // the mesh's vertex/index buffers are bound once when drawing during
    // the raster path and the indirect command signature is created from
    // the mesh's index count.
    static void SetPatchMesh(const Asset::GpuMesh *mesh);
    // query the mesh pointer that was registered (may be nullptr)
    static const Asset::GpuMesh *GetPatchMesh();

private:
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_instanceBuffer;    // StructuredBuffer<FGrassBlade>
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_visibleBuffer;     // AppendStructuredBuffer<uint4> (transform + index)
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_indirectArgsBuffer; // Buffer storing draw args

    // compute pipeline objects for culling and TLAS generation
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> s_cullRootSig;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> s_cullPSO;
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> s_tlasRootSig;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> s_tlasPSO;

    // command signature used by ExecuteIndirect when drawing patches
    static Microsoft::WRL::ComPtr<ID3D12CommandSignature> s_drawCommandSignature;
    // mesh used for the grass patch (single BLAS instance)
    static const Asset::GpuMesh *s_patchMesh;

    // capability information (allocated size)
    static UINT s_maxInstances;
    static UINT s_currentInstanceCount;
    static std::vector<FGrassBlade> s_cpuBlades;
};
