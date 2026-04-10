#include "grass_manager.h"
#include "dx12_context.h"
#include "dxr_helpers.h"
#include "dxc_wrapper.h"
#include <array>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr size_t kBandCount = 2;
constexpr float kGrassNearDistance = 14.0f;
constexpr float kGrassMidDistance = 40.0f;

std::array<ComPtr<ID3D12Resource>, kBandCount> s_resetBuffers;

UINT NextGrassCapacity(UINT requested) {
    UINT capacity = 256;
    while (capacity < requested && capacity < (1u << 30)) {
        capacity <<= 1;
    }
    return capacity;
}

ComPtr<IDxcBlob> CompileCS(const wchar_t *path) {
    static DxcHelper s_dxcHelper;
    try {
        std::vector<std::wstring> defines;
        return s_dxcHelper.Compile(path, L"CSMain", L"cs_6_5", defines);
    } catch (const std::exception &e) {
        fprintf(stderr, "GrassManager: failed to compile %ls: %s\n", path,
                e.what());
        return nullptr;
    }
}

} // namespace

ComPtr<ID3D12Resource> GrassManager::s_patchBuffer;
ComPtr<ID3D12Resource> GrassManager::s_patchUploadBuffer;
ComPtr<ID3D12Resource> GrassManager::s_rtPatchBuffer;
ComPtr<ID3D12Resource> GrassManager::s_rtPatchUploadBuffer;
ComPtr<ID3D12Resource> GrassManager::s_visibleBuffers[2];
ComPtr<ID3D12Resource> GrassManager::s_indirectArgsBuffers[2];
ComPtr<ID3D12RootSignature> GrassManager::s_cullRootSig;
ComPtr<ID3D12PipelineState> GrassManager::s_cullPSO;
ComPtr<ID3D12RootSignature> GrassManager::s_tlasRootSig;
ComPtr<ID3D12PipelineState> GrassManager::s_tlasPSO;
ComPtr<ID3D12CommandSignature> GrassManager::s_drawCommandSignature;
const Asset::GpuMesh *GrassManager::s_patchMeshes[2] = {nullptr, nullptr};
UINT GrassManager::s_maxPatches = 0;
UINT GrassManager::s_maxRtPatches = 0;
UINT GrassManager::s_currentPatchCount = 0;
UINT GrassManager::s_rtPatchCount = 0;
std::vector<FGrassPatch> GrassManager::s_cpuPatches;
D3D12_RESOURCE_STATES GrassManager::s_patchBufferState =
    D3D12_RESOURCE_STATE_COMMON;
D3D12_RESOURCE_STATES GrassManager::s_rtPatchBufferState =
    D3D12_RESOURCE_STATE_COMMON;
D3D12_RESOURCE_STATES GrassManager::s_visibleBufferStates[2] = {
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_COMMON};
D3D12_RESOURCE_STATES GrassManager::s_indirectArgsStates[2] = {
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_COMMON};
bool GrassManager::s_patchUploadPending = false;

size_t GrassManager::BandIndex(LodBand band) {
    return static_cast<size_t>(band);
}

float GrassManager::GetNearDistance() { return kGrassNearDistance; }

float GrassManager::GetMidDistance() { return kGrassMidDistance; }

const Asset::GpuMesh *GrassManager::GetPatchMesh() {
    return s_patchMeshes[BandIndex(LodBand::Near)];
}

const Asset::GpuMesh *GrassManager::GetMidPatchMesh() {
    return s_patchMeshes[BandIndex(LodBand::Mid)];
}

void GrassManager::Initialize(ID3D12Device *device) {
    if (!device) {
        return;
    }

    if (!s_cullPSO) {
        D3D12_ROOT_PARAMETER params[7] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].Descriptor.ShaderRegister = 1;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[4].Descriptor.ShaderRegister = 2;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[5].Descriptor.ShaderRegister = 3;
        params[5].Descriptor.RegisterSpace = 0;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[6].Descriptor.ShaderRegister = 1;
        params[6].Descriptor.RegisterSpace = 0;
        params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = _countof(params);
        rsDesc.pParameters = params;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(
                &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
            if (err) {
                fprintf(stderr, "GrassManager: cull RS error: %s\n",
                        (char *)err->GetBufferPointer());
            }
        } else {
            device->CreateRootSignature(0, sig->GetBufferPointer(),
                                        sig->GetBufferSize(),
                                        IID_PPV_ARGS(&s_cullRootSig));
        }

        ComPtr<IDxcBlob> cs = CompileCS(L"shaders/grass_cull_cs.hlsl");
        if (cs && s_cullRootSig) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = s_cullRootSig.Get();
            psoDesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            device->CreateComputePipelineState(&psoDesc,
                                               IID_PPV_ARGS(&s_cullPSO));
        }
    }

    if (!s_tlasPSO) {
        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 5;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = _countof(params);
        rsDesc.pParameters = params;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(
                &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
            if (err) {
                fprintf(stderr, "GrassManager: tlas RS error: %s\n",
                        (char *)err->GetBufferPointer());
            }
        } else {
            device->CreateRootSignature(0, sig->GetBufferPointer(),
                                        sig->GetBufferSize(),
                                        IID_PPV_ARGS(&s_tlasRootSig));
        }

        ComPtr<IDxcBlob> cs = CompileCS(L"shaders/grass_tlas_cs.hlsl");
        if (cs && s_tlasRootSig) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = s_tlasRootSig.Get();
            psoDesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            device->CreateComputePipelineState(&psoDesc,
                                               IID_PPV_ARGS(&s_tlasPSO));
        }
    }

    for (size_t i = 0; i < kBandCount; ++i) {
        if (!s_resetBuffers[i]) {
            UINT resetData[9] = {};
            AllocateUploadBuffer(device, resetData, sizeof(resetData),
                                 &s_resetBuffers[i],
                                 i == 0 ? L"Grass Near Reset Buffer"
                                        : L"Grass Mid Reset Buffer");
        }
    }
}

void GrassManager::Shutdown() {
    s_patchBuffer.Reset();
    s_patchUploadBuffer.Reset();
    s_rtPatchBuffer.Reset();
    s_rtPatchUploadBuffer.Reset();
    for (size_t i = 0; i < kBandCount; ++i) {
        s_visibleBuffers[i].Reset();
        s_indirectArgsBuffers[i].Reset();
    }
    s_cullRootSig.Reset();
    s_cullPSO.Reset();
    s_tlasRootSig.Reset();
    s_tlasPSO.Reset();
    s_drawCommandSignature.Reset();
    for (auto &resetBuffer : s_resetBuffers) {
        resetBuffer.Reset();
    }
    s_patchMeshes[0] = nullptr;
    s_patchMeshes[1] = nullptr;
    s_maxPatches = 0;
    s_maxRtPatches = 0;
    s_currentPatchCount = 0;
    s_rtPatchCount = 0;
    s_cpuPatches.clear();
    s_patchBufferState = D3D12_RESOURCE_STATE_COMMON;
    s_rtPatchBufferState = D3D12_RESOURCE_STATE_COMMON;
    s_visibleBufferStates[0] = D3D12_RESOURCE_STATE_COMMON;
    s_visibleBufferStates[1] = D3D12_RESOURCE_STATE_COMMON;
    s_indirectArgsStates[0] = D3D12_RESOURCE_STATE_COMMON;
    s_indirectArgsStates[1] = D3D12_RESOURCE_STATE_COMMON;
    s_patchUploadPending = false;
}

void GrassManager::EnsureCapacity(UINT requested) {
    if (requested <= s_maxPatches) {
        return;
    }

    s_maxPatches = NextGrassCapacity(requested);
    const UINT64 byteSize = (UINT64)s_maxPatches * sizeof(FGrassPatch);
    AllocateUAVBuffer(DX12Context::g_device.Get(), byteSize, &s_patchBuffer,
                      D3D12_RESOURCE_STATE_COMMON, L"Grass Patch Buffer");
    AllocateUploadBuffer(DX12Context::g_device.Get(), nullptr, byteSize,
                         &s_patchUploadBuffer, L"Grass Patch Upload");
    s_patchBufferState = D3D12_RESOURCE_STATE_COMMON;

    for (size_t i = 0; i < kBandCount; ++i) {
        AllocateUAVBuffer(DX12Context::g_device.Get(),
                          (UINT64)(s_maxPatches + 1) * sizeof(DirectX::XMFLOAT4),
                          &s_visibleBuffers[i], D3D12_RESOURCE_STATE_COMMON,
                          i == 0 ? L"Grass Near Visible" : L"Grass Mid Visible");
        AllocateUAVBuffer(DX12Context::g_device.Get(),
                          sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                          &s_indirectArgsBuffers[i], D3D12_RESOURCE_STATE_COMMON,
                          i == 0 ? L"Grass Near Indirect Args"
                                 : L"Grass Mid Indirect Args");
        s_visibleBufferStates[i] = D3D12_RESOURCE_STATE_COMMON;
        s_indirectArgsStates[i] = D3D12_RESOURCE_STATE_COMMON;
    }
}

void GrassManager::TransitionPatchBuffer(ID3D12GraphicsCommandList *cmdList,
                                         D3D12_RESOURCE_STATES newState) {
    if (!cmdList || !s_patchBuffer || s_patchBufferState == newState) {
        return;
    }
    D3D12_RESOURCE_BARRIER bar = {};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = s_patchBuffer.Get();
    bar.Transition.StateBefore = s_patchBufferState;
    bar.Transition.StateAfter = newState;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &bar);
    s_patchBufferState = newState;
}

void GrassManager::UploadPatchBuffer(ID3D12GraphicsCommandList *cmdList) {
    if (!cmdList || !s_patchUploadPending || !s_patchBuffer ||
        !s_patchUploadBuffer || s_currentPatchCount == 0) {
        if (s_currentPatchCount == 0) {
            s_patchUploadPending = false;
        }
        return;
    }

    TransitionPatchBuffer(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->CopyBufferRegion(s_patchBuffer.Get(), 0, s_patchUploadBuffer.Get(), 0,
                              (UINT64)s_currentPatchCount * sizeof(FGrassPatch));
    TransitionPatchBuffer(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s_patchUploadPending = false;
}

void GrassManager::SetPatches(const std::vector<FGrassPatch> &patches) {
    s_cpuPatches = patches;
    s_currentPatchCount = (UINT)patches.size();
    if (s_currentPatchCount == 0) {
        s_patchUploadPending = false;
        return;
    }

    EnsureCapacity(s_currentPatchCount);
    if (s_patchUploadBuffer) {
        void *mapPtr = nullptr;
        D3D12_RANGE r = {0, 0};
        if (SUCCEEDED(s_patchUploadBuffer->Map(0, &r, &mapPtr))) {
            memcpy(mapPtr, patches.data(),
                   s_currentPatchCount * sizeof(FGrassPatch));
            s_patchUploadBuffer->Unmap(0, nullptr);
            s_patchUploadPending = true;
        }
    }
}

void GrassManager::PrepareGpuBuffers(ID3D12GraphicsCommandList *cmdList) {
    UploadPatchBuffer(cmdList);
}

void GrassManager::UploadRayTracingPatches(
    ID3D12GraphicsCommandList *cmdList, const std::vector<FGrassPatch> &patches) {
    s_rtPatchCount = (UINT)patches.size();
    if (!cmdList || s_rtPatchCount == 0) {
        return;
    }

    if (s_rtPatchCount > s_maxRtPatches) {
        s_maxRtPatches = NextGrassCapacity(s_rtPatchCount);
        const UINT64 byteSize = (UINT64)s_maxRtPatches * sizeof(FGrassPatch);
        AllocateUAVBuffer(DX12Context::g_device.Get(), byteSize, &s_rtPatchBuffer,
                          D3D12_RESOURCE_STATE_COMMON,
                          L"Grass RT Patch Buffer");
        AllocateUploadBuffer(DX12Context::g_device.Get(), nullptr, byteSize,
                             &s_rtPatchUploadBuffer,
                             L"Grass RT Patch Upload");
        s_rtPatchBufferState = D3D12_RESOURCE_STATE_COMMON;
    }

    if (!s_rtPatchBuffer || !s_rtPatchUploadBuffer) {
        return;
    }

    void *mapped = nullptr;
    D3D12_RANGE range = {0, 0};
    if (FAILED(s_rtPatchUploadBuffer->Map(0, &range, &mapped))) {
        return;
    }
    memcpy(mapped, patches.data(), (size_t)s_rtPatchCount * sizeof(FGrassPatch));
    s_rtPatchUploadBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER toCopy = {};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = s_rtPatchBuffer.Get();
    toCopy.Transition.StateBefore = s_rtPatchBufferState;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toCopy);
    s_rtPatchBufferState = D3D12_RESOURCE_STATE_COPY_DEST;

    cmdList->CopyBufferRegion(s_rtPatchBuffer.Get(), 0, s_rtPatchUploadBuffer.Get(),
                              0, (UINT64)s_rtPatchCount * sizeof(FGrassPatch));

    D3D12_RESOURCE_BARRIER toSrv = {};
    toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrv.Transition.pResource = s_rtPatchBuffer.Get();
    toSrv.Transition.StateBefore = s_rtPatchBufferState;
    toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toSrv);
    s_rtPatchBufferState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void GrassManager::SetBlades(const std::vector<FGrassPatch> &patches) {
    SetPatches(patches);
}

void GrassManager::SetInstances(const std::vector<FGrassPatch> &patches) {
    SetPatches(patches);
}

void GrassManager::CullingAndPrepareIndirect(ID3D12GraphicsCommandList *cmdList,
                                             ID3D12Resource *cameraCB) {
    if (!cmdList || !s_cullPSO || s_currentPatchCount == 0 || !cameraCB ||
        !s_patchBuffer) {
        return;
    }

    UploadPatchBuffer(cmdList);
    TransitionPatchBuffer(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    for (size_t i = 0; i < kBandCount; ++i) {
        if (!s_visibleBuffers[i] || !s_indirectArgsBuffers[i] ||
            !s_resetBuffers[i]) {
            return;
        }
    }

    for (size_t i = 0; i < kBandCount; ++i) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = s_visibleBuffers[i].Get();
        barriers[0].Transition.StateBefore = s_visibleBufferStates[i];
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = s_indirectArgsBuffers[i].Get();
        barriers[1].Transition.StateBefore = s_indirectArgsStates[i];
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(2, barriers);

        cmdList->CopyBufferRegion(s_visibleBuffers[i].Get(), 0,
                                  s_resetBuffers[i].Get(), 0, 16);
        cmdList->CopyBufferRegion(s_indirectArgsBuffers[i].Get(), 0,
                                  s_resetBuffers[i].Get(), 16,
                                  sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmdList->ResourceBarrier(2, barriers);

        s_visibleBufferStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        s_indirectArgsStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    struct {
        UINT instanceCount;
        float nearDistanceSq;
        float midDistanceSq;
        float _pad0;
    } params = {s_currentPatchCount, kGrassNearDistance * kGrassNearDistance,
                kGrassMidDistance * kGrassMidDistance, 0.0f};

    cmdList->SetPipelineState(s_cullPSO.Get());
    cmdList->SetComputeRootSignature(s_cullRootSig.Get());
    cmdList->SetComputeRoot32BitConstants(0, 4, &params, 0);
    cmdList->SetComputeRootShaderResourceView(1,
                                              s_patchBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(
        2, s_visibleBuffers[BandIndex(LodBand::Near)]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(
        3, s_indirectArgsBuffers[BandIndex(LodBand::Near)]
               ->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(
        4, s_visibleBuffers[BandIndex(LodBand::Mid)]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(
        5, s_indirectArgsBuffers[BandIndex(LodBand::Mid)]
               ->GetGPUVirtualAddress());
    cmdList->SetComputeRootConstantBufferView(6, cameraCB->GetGPUVirtualAddress());

    const UINT groups = (s_currentPatchCount + 63) / 64;
    cmdList->Dispatch(groups, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarriers[4] = {};
    for (size_t i = 0; i < kBandCount; ++i) {
        uavBarriers[i * 2 + 0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[i * 2 + 0].UAV.pResource = s_visibleBuffers[i].Get();
        uavBarriers[i * 2 + 1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[i * 2 + 1].UAV.pResource = s_indirectArgsBuffers[i].Get();
    }
    cmdList->ResourceBarrier(4, uavBarriers);
}

void GrassManager::DrawVisible(ID3D12GraphicsCommandList *cmdList, LodBand band) {
    const size_t bandIdx = BandIndex(band);
    if (!cmdList || s_currentPatchCount == 0 || bandIdx >= kBandCount) {
        return;
    }

    const Asset::GpuMesh *mesh = s_patchMeshes[bandIdx];
    if (mesh && mesh->vertexBuffer.Get() && mesh->indexBuffer.Get()) {
        cmdList->IASetVertexBuffers(0, 1, &mesh->vbView);
        cmdList->IASetIndexBuffer(&mesh->ibView);
    }

    if (!s_drawCommandSignature || !s_indirectArgsBuffers[bandIdx] ||
        !s_visibleBuffers[bandIdx]) {
        return;
    }

    if (s_visibleBufferStates[bandIdx] !=
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER bar = {};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource = s_visibleBuffers[bandIdx].Get();
        bar.Transition.StateBefore = s_visibleBufferStates[bandIdx];
        bar.Transition.StateAfter =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &bar);
        s_visibleBufferStates[bandIdx] =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (s_indirectArgsStates[bandIdx] != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        D3D12_RESOURCE_BARRIER bar = {};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource = s_indirectArgsBuffers[bandIdx].Get();
        bar.Transition.StateBefore = s_indirectArgsStates[bandIdx];
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &bar);
        s_indirectArgsStates[bandIdx] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    cmdList->ExecuteIndirect(s_drawCommandSignature.Get(), 1,
                             s_indirectArgsBuffers[bandIdx].Get(), 0, nullptr, 0);
}

void GrassManager::AppendTlasInstances(ID3D12GraphicsCommandList *cmdList,
                                       ID3D12Resource *tlasDescBuffer,
                                       UINT sceneCount, UINT64 blasAddress,
                                       UINT patchMeshIndex) {
    if (!cmdList || !s_tlasPSO || s_currentPatchCount == 0 || !tlasDescBuffer ||
        !blasAddress || !s_patchBuffer) {
        return;
    }

    UploadPatchBuffer(cmdList);
    TransitionPatchBuffer(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    struct {
        UINT startIndex;
        UINT instanceCount;
        UINT blasAddrLo;
        UINT blasAddrHi;
        UINT patchMeshIndex;
    } params = {sceneCount, s_currentPatchCount,
                (UINT)(blasAddress & 0xFFFFFFFFu), (UINT)(blasAddress >> 32),
                patchMeshIndex};

    cmdList->SetPipelineState(s_tlasPSO.Get());
    cmdList->SetComputeRootSignature(s_tlasRootSig.Get());
    cmdList->SetComputeRoot32BitConstants(0, 5, &params, 0);
    cmdList->SetComputeRootShaderResourceView(1,
                                              s_patchBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2,
                                               tlasDescBuffer->GetGPUVirtualAddress());

    const UINT groups = (s_currentPatchCount + 63) / 64;
    cmdList->Dispatch(groups, 1, 1);

    D3D12_RESOURCE_BARRIER bar = {};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bar.UAV.pResource = tlasDescBuffer;
    cmdList->ResourceBarrier(1, &bar);
}

UINT GrassManager::GetPatchCount() { return s_currentPatchCount; }

const std::vector<FGrassPatch> &GrassManager::GetPatches() { return s_cpuPatches; }

UINT GrassManager::GetInstanceCount() { return GetPatchCount(); }

const std::vector<FGrassPatch> &GrassManager::GetBlades() { return s_cpuPatches; }

D3D12_GPU_VIRTUAL_ADDRESS GrassManager::GetInstanceBufferGpuAddress() {
    return s_patchBuffer ? s_patchBuffer->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS GrassManager::GetRayTracingInstanceBufferGpuAddress() {
    return s_rtPatchBuffer ? s_rtPatchBuffer->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS
GrassManager::GetVisibleBufferGpuAddress(LodBand band) {
    const size_t bandIdx = BandIndex(band);
    return s_visibleBuffers[bandIdx]
               ? s_visibleBuffers[bandIdx]->GetGPUVirtualAddress()
               : 0;
}

void GrassManager::SetPatchMesh(const Asset::GpuMesh *mesh) {
    if (!mesh || !mesh->indexBuffer) {
        return;
    }
    s_patchMeshes[BandIndex(LodBand::Near)] = mesh;
    if (!s_drawCommandSignature) {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        DX12Context::g_device->CreateCommandSignature(
            &sigDesc, nullptr, IID_PPV_ARGS(&s_drawCommandSignature));
    }
    if (s_resetBuffers[BandIndex(LodBand::Near)]) {
        void *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        if (SUCCEEDED(s_resetBuffers[BandIndex(LodBand::Near)]->Map(
                0, &readRange, &mapped))) {
            UINT indexStride =
                (mesh->ibView.Format == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
            D3D12_DRAW_INDEXED_ARGUMENTS args = {};
            args.IndexCountPerInstance = mesh->ibView.SizeInBytes / indexStride;
            memcpy(static_cast<BYTE *>(mapped) + 16, &args, sizeof(args));
            s_resetBuffers[BandIndex(LodBand::Near)]->Unmap(0, nullptr);
        }
    }
}

void GrassManager::SetMidPatchMesh(const Asset::GpuMesh *mesh) {
    if (!mesh || !mesh->indexBuffer) {
        return;
    }
    s_patchMeshes[BandIndex(LodBand::Mid)] = mesh;
    if (s_resetBuffers[BandIndex(LodBand::Mid)]) {
        void *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        if (SUCCEEDED(s_resetBuffers[BandIndex(LodBand::Mid)]->Map(
                0, &readRange, &mapped))) {
            UINT indexStride =
                (mesh->ibView.Format == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
            D3D12_DRAW_INDEXED_ARGUMENTS args = {};
            args.IndexCountPerInstance = mesh->ibView.SizeInBytes / indexStride;
            memcpy(static_cast<BYTE *>(mapped) + 16, &args, sizeof(args));
            s_resetBuffers[BandIndex(LodBand::Mid)]->Unmap(0, nullptr);
        }
    }
}
