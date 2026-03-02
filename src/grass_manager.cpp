#include "grass_manager.h"
#include "dx12_context.h" // for helper functions like AllocateUAVBuffer?
#include "dxr_helpers.h"
#include "dxc_wrapper.h" // for DxcHelper used in CompileCS
#include <assert.h>
#include <vector>

using Microsoft::WRL::ComPtr;

// static member definitions
ComPtr<ID3D12Resource> GrassManager::s_instanceBuffer;
ComPtr<ID3D12Resource> GrassManager::s_visibleBuffer;
ComPtr<ID3D12Resource> GrassManager::s_indirectArgsBuffer;
ComPtr<ID3D12RootSignature> GrassManager::s_cullRootSig;
ComPtr<ID3D12PipelineState> GrassManager::s_cullPSO;
ComPtr<ID3D12RootSignature> GrassManager::s_tlasRootSig;
ComPtr<ID3D12PipelineState> GrassManager::s_tlasPSO;
UINT GrassManager::s_maxInstances = 0;
UINT GrassManager::s_currentInstanceCount = 0;
std::vector<FGrassBlade> GrassManager::s_cpuBlades;
ComPtr<ID3D12CommandSignature> GrassManager::s_drawCommandSignature;
const Asset::GpuMesh *GrassManager::s_patchMesh = nullptr;

// file-scope helpers for buffer reset and state tracking
static ComPtr<ID3D12Resource> s_resetBuffer;
static D3D12_RESOURCE_STATES s_indirectArgsState = D3D12_RESOURCE_STATE_COMMON;
static D3D12_RESOURCE_STATES s_visibleBufferState = D3D12_RESOURCE_STATE_COMMON;

const Asset::GpuMesh *GrassManager::GetPatchMesh() {
    return s_patchMesh;
}

namespace {
// helpers to compile compute shaders (duplicate code from dxr_renderer)
static ComPtr<IDxcBlob> CompileCS(const wchar_t *path) {
    // each TU keeps its own DXC helper instance
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

void GrassManager::Initialize(ID3D12Device *device) {
    if (!device)
        return;
    // create pipelines for culling and TLAS generation if not already done
    if (!s_cullPSO) {
        // root signature: b0 instance count (32-bit constant), t0 instance buffer, u0 visible, u1 indirect args
        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 1;
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

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = _countof(params);
        rsDesc.pParameters = params;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                              D3D_ROOT_SIGNATURE_VERSION_1,
                                              &sig, &err))) {
            if (err)
                fprintf(stderr, "GrassManager: cull RS error: %s\n",
                        (char *)err->GetBufferPointer());
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
            device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_cullPSO));
        }
    }

    if (!s_tlasPSO) {
        // root signature for tlas: b0 parameters (5 x 32-bit constants), t0 instance buffer, u0 output descs
        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 5; // startIndex, instanceCount, blasAddress (2x uint32), patchMeshIndex
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
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                              D3D_ROOT_SIGNATURE_VERSION_1,
                                              &sig, &err))) {
            if (err)
                fprintf(stderr, "GrassManager: tlas RS error: %s\n",
                        (char *)err->GetBufferPointer());
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
            device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_tlasPSO));
        }
    }

    // Small upload buffer used to reset UAV counters each frame.
    // Layout: [0..15]  = 16 bytes of zeros  (visible buffer counter)
    //         [16..35] = D3D12_DRAW_INDEXED_ARGUMENTS (indirect args reset)
    if (!s_resetBuffer) {
        UINT resetData[9] = {}; // 36 bytes, all zeros initially
        AllocateUploadBuffer(device, resetData, sizeof(resetData),
                             &s_resetBuffer, L"Grass Reset Buffer");
    }
}

void GrassManager::Shutdown() {
    s_instanceBuffer.Reset();
    s_visibleBuffer.Reset();
    s_indirectArgsBuffer.Reset();
    s_cullRootSig.Reset();
    s_cullPSO.Reset();
    s_tlasRootSig.Reset();
    s_tlasPSO.Reset();
    s_drawCommandSignature.Reset();
    s_resetBuffer.Reset();
    s_patchMesh = nullptr;
    s_maxInstances = 0;
    s_currentInstanceCount = 0;
    s_cpuBlades.clear();
    s_indirectArgsState = D3D12_RESOURCE_STATE_COMMON;
    s_visibleBufferState = D3D12_RESOURCE_STATE_COMMON;
}

void GrassManager::SetBlades(const std::vector<FGrassBlade> &blades) {
    s_cpuBlades = blades;
    s_currentInstanceCount = (UINT)blades.size();
    if (s_currentInstanceCount > s_maxInstances) {
        // reallocate buffers to accommodate
        s_maxInstances = s_currentInstanceCount;
        UINT64 byteSize = (UINT64)s_maxInstances * sizeof(FGrassBlade);
        AllocateUploadBuffer(DX12Context::g_device.Get(),
                             nullptr, byteSize, &s_instanceBuffer,
                             L"Grass Instance Buffer");
        // allocate visible list and indirect args large enough
        // visible vector stores a counter in element 0 plus one uint4 per blade
        AllocateUAVBuffer(DX12Context::g_device.Get(),
                          (UINT64)(s_maxInstances + 1) * sizeof(DirectX::XMFLOAT4),
                          &s_visibleBuffer);
        // indirect args: one D3D12_DRAW_INDEXED_ARGUMENTS struct (UAV for compute writes)
        AllocateUAVBuffer(DX12Context::g_device.Get(),
                          sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                          &s_indirectArgsBuffer,
                          D3D12_RESOURCE_STATE_COMMON,
                          L"Grass Indirect Args");
        s_indirectArgsState = D3D12_RESOURCE_STATE_COMMON;
        s_visibleBufferState = D3D12_RESOURCE_STATE_COMMON;
    }
    // copy data immediately
    if (s_instanceBuffer) {
        void *mapPtr = nullptr;
        D3D12_RANGE r = {0, 0};
        if (SUCCEEDED(s_instanceBuffer->Map(0, &r, &mapPtr))) {
            memcpy(mapPtr, blades.data(), s_currentInstanceCount * sizeof(FGrassBlade));
            s_instanceBuffer->Unmap(0, nullptr);
        }
    }
}

void GrassManager::SetInstances(const std::vector<FGrassBlade> &instances) {
    SetBlades(instances);
}

void GrassManager::CullingAndPrepareIndirect(ID3D12GraphicsCommandList *cmdList) {
    if (!cmdList || !s_cullPSO || s_currentInstanceCount == 0)
        return;
    if (!s_resetBuffer || !s_visibleBuffer || !s_indirectArgsBuffer)
        return;

    // --- Reset counters via copy from the staging reset buffer ---
    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = s_visibleBuffer.Get();
        barriers[0].Transition.StateBefore = s_visibleBufferState;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = s_indirectArgsBuffer.Get();
        barriers[1].Transition.StateBefore = s_indirectArgsState;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(2, barriers);
    }
    // visible buffer: zero out counter at offset 0 (first uint4 = 16 bytes)
    cmdList->CopyBufferRegion(s_visibleBuffer.Get(), 0,
                              s_resetBuffer.Get(), 0, 16);
    // indirect args: copy full D3D12_DRAW_INDEXED_ARGUMENTS from offset 16
    cmdList->CopyBufferRegion(s_indirectArgsBuffer.Get(), 0,
                              s_resetBuffer.Get(), 16,
                              sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = s_visibleBuffer.Get();
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = s_indirectArgsBuffer.Get();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(2, barriers);
    }
    s_visibleBufferState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    s_indirectArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // --- Dispatch culling compute shader ---
    cmdList->SetPipelineState(s_cullPSO.Get());
    cmdList->SetComputeRootSignature(s_cullRootSig.Get());
    UINT count = s_currentInstanceCount;
    cmdList->SetComputeRoot32BitConstants(0, 1, &count, 0);
    cmdList->SetComputeRootShaderResourceView(1, s_instanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2, s_visibleBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, s_indirectArgsBuffer->GetGPUVirtualAddress());

    UINT groups = (s_currentInstanceCount + 63) / 64;
    cmdList->Dispatch(groups, 1, 1);

    // UAV barrier so the draw call sees the compute output
    D3D12_RESOURCE_BARRIER uavBarriers[2] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = s_visibleBuffer.Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = s_indirectArgsBuffer.Get();
    cmdList->ResourceBarrier(2, uavBarriers);
}

void GrassManager::DrawVisible(ID3D12GraphicsCommandList *cmdList) {
    if (!cmdList || s_currentInstanceCount == 0)
        return;
    // bind the patch mesh vertex/index buffers if they were provided
    if (s_patchMesh && s_patchMesh->vertexBuffer.Get() &&
        s_patchMesh->indexBuffer.Get()) {
        cmdList->IASetVertexBuffers(0, 1, &s_patchMesh->vbView);
        cmdList->IASetIndexBuffer(&s_patchMesh->ibView);
    }

    // draw using indirect args buffer; the structure was incremented by the
    // culling compute shader.  do nothing if the command signature hasn't been
    // created yet (patch mesh hasn't been set).
    if (s_drawCommandSignature && s_indirectArgsBuffer) {
        // Transition indirect args to INDIRECT_ARGUMENT state for ExecuteIndirect
        if (s_indirectArgsState != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
            D3D12_RESOURCE_BARRIER bar = {};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Transition.pResource = s_indirectArgsBuffer.Get();
            bar.Transition.StateBefore = s_indirectArgsState;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bar);
            s_indirectArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }
        cmdList->ExecuteIndirect(s_drawCommandSignature.Get(), 1,
                                 s_indirectArgsBuffer.Get(), 0, nullptr, 0);
    }
}

void GrassManager::AppendTlasInstances(ID3D12GraphicsCommandList *cmdList,
                                       ID3D12Resource *tlasDescBuffer,
                                       UINT sceneCount,
                                       UINT64 blasAddress,
                                       UINT patchMeshIndex) {
    if (!cmdList || !s_tlasPSO || s_currentInstanceCount == 0 ||
        !tlasDescBuffer || !blasAddress)
        return;
    // set root signature, parameters
    cmdList->SetPipelineState(s_tlasPSO.Get());
    cmdList->SetComputeRootSignature(s_tlasRootSig.Get());
    // b0 parameters as root constants: startIndex, instanceCount,
    // blasAddress (lo + hi), patchMeshIndex
    struct {
        UINT startIndex;
        UINT instanceCount;
        UINT blasAddrLo;
        UINT blasAddrHi;
        UINT patchMeshIndex;
    } p;
    p.startIndex = sceneCount;
    p.instanceCount = s_currentInstanceCount;
    p.blasAddrLo = (UINT)(blasAddress & 0xFFFFFFFFu);
    p.blasAddrHi = (UINT)(blasAddress >> 32);
    p.patchMeshIndex = patchMeshIndex;
    cmdList->SetComputeRoot32BitConstants(0, 5, &p, 0);
    // t0 instance buffer
    cmdList->SetComputeRootShaderResourceView(1, s_instanceBuffer->GetGPUVirtualAddress());
    // u0 output descriptor buffer
    cmdList->SetComputeRootUnorderedAccessView(2, tlasDescBuffer->GetGPUVirtualAddress());

    UINT groups = (s_currentInstanceCount + 63) / 64;
    cmdList->Dispatch(groups, 1, 1);
    // UAV barrier to ensure the build code sees updated descriptors
    D3D12_RESOURCE_BARRIER bar = {};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bar.UAV.pResource = tlasDescBuffer;
    cmdList->ResourceBarrier(1, &bar);
}

UINT GrassManager::GetInstanceCount() { return s_currentInstanceCount; }

const std::vector<FGrassBlade> &GrassManager::GetBlades() { return s_cpuBlades; }

void GrassManager::SetPatchMesh(const Asset::GpuMesh *mesh) {
    if (!mesh || !mesh->indexBuffer)
        return;
    s_patchMesh = mesh;
    // create a simple draw indexed indirect command signature if not already
    if (!s_drawCommandSignature) {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        DX12Context::g_device->CreateCommandSignature(
            &sigDesc, nullptr,
            IID_PPV_ARGS(&s_drawCommandSignature));
    }
    // Update the reset buffer with the correct IndexCountPerInstance for this mesh
    if (s_resetBuffer) {
        void *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        if (SUCCEEDED(s_resetBuffer->Map(0, &readRange, &mapped))) {
            UINT indexStride = (mesh->ibView.Format == DXGI_FORMAT_R16_UINT) ? 2 : 4;
            D3D12_DRAW_INDEXED_ARGUMENTS args = {};
            args.IndexCountPerInstance = mesh->ibView.SizeInBytes / indexStride;
            args.InstanceCount = 0; // incremented by cull shader
            memcpy(static_cast<BYTE *>(mapped) + 16, &args, sizeof(args));
            s_resetBuffer->Unmap(0, nullptr);
        }
    }
}
