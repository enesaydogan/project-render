#include "dxr_renderer.h"
#include "dxc_wrapper.h"
#include "dxr_helpers.h"
#include "d3d12_helpers.h"
#include <wrl.h>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Module-local state
static ID3D12Device* s_device = nullptr;
static ID3D12CommandQueue* s_commandQueue = nullptr;

// Some debug toggles live in main.cpp; declare them here so we can react to UI changes
extern bool g_dxrDebugUV;
extern bool g_dxrHitDebug;
extern bool g_dxrDumpD3D12Messages;
static ID3D12Fence* s_fence = nullptr;
static UINT64* s_fenceValues = nullptr;
static UINT* s_frameIndexPtr = nullptr;
static HANDLE s_fenceEvent = nullptr;

bool g_rayTracingSupported = false; // defined here

// DXR-specific state kept internal to this module
static ComPtr<ID3D12Device5> s_dxrDevice;

inline void TransitionResource(ID3D12GraphicsCommandList *cmdList,
                               ID3D12Resource *resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
  if (before == after) return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
}
static DxcHelper s_dxcHelper;
static ComPtr<ID3D12StateObject> s_rtStateObject;
static ComPtr<ID3D12Resource> s_sbtStorage;
static ComPtr<ID3D12RootSignature> s_rtGlobalRootSignature;
static ComPtr<ID3D12Resource> s_outputUAV;
static UINT s_outputUAVDescriptorSize = 0;
static D3D12_GPU_DESCRIPTOR_HANDLE s_outputUAVGpuHandle = {0};
static ComPtr<ID3D12DescriptorHeap> s_uavHeap;

struct ShaderTableEntry { void* id; };
static UINT s_shaderTableEntrySize = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_rayGenShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_missShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_hitGroupShaderTable = 0;

struct MeshBLAS { AccelerationStructureBuffers buffers; UINT64 meshId; };
static std::vector<MeshBLAS> s_allBLAS;
static AccelerationStructureBuffers s_tlas;

namespace DxrRenderer {

void Initialize(ID3D12Device* device) {
    s_device = device;
    if (!s_device) { g_rayTracingSupported = false; return; }
    ComPtr<ID3D12Device5> dev5;
    if (SUCCEEDED(s_device->QueryInterface(IID_PPV_ARGS(&dev5)))) {
        g_rayTracingSupported = true;
        s_dxrDevice = dev5;
        fprintf(stderr, "DxrRenderer: DXR supported on device\n");
    } else {
        g_rayTracingSupported = false;
        s_dxrDevice.Reset();
    }
}

void SetCommandQueue(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence, UINT64* fenceValues, UINT* frameIndexPtr, HANDLE fenceEvent) {
    s_commandQueue = commandQueue;
    s_fence = fence;
    s_fenceValues = fenceValues;
    s_frameIndexPtr = frameIndexPtr;
    s_fenceEvent = fenceEvent;
}

void CreateRayTracingPipeline() {
    if (!g_rayTracingSupported || !s_dxrDevice) return;

    fprintf(stderr, "DxrRenderer: Creating Ray Tracing Pipeline...\n");

    // Compile shader
    ComPtr<IDxcBlob> shaderBlob;
    try {
        std::vector<std::wstring> compileDefines;
        if (::g_dxrDebugUV) compileDefines.push_back(L"RAYGEN_DEBUG=1");
        if (::g_dxrHitDebug) compileDefines.push_back(L"HIT_DEBUG=1");
        shaderBlob = s_dxcHelper.Compile(L"shaders/raytracing.hlsl", L"", L"lib_6_3", compileDefines);
    } catch (const std::exception &e) {
        fprintf(stderr, "DxrRenderer: Shader Compilation Failed: %s\n", e.what());
        return;
    }
    if (!shaderBlob) { fprintf(stderr, "DxrRenderer: shader blob null\n"); return; }

    // Create global root signature
    D3D12_ROOT_PARAMETER params[7] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; params[0].Descriptor.ShaderRegister = 0; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE uavRange = {}; uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uavRange.NumDescriptors = 1; uavRange.BaseShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &uavRange; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    static D3D12_DESCRIPTOR_RANGE srvRange = {}; srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srvRange.NumDescriptors = 8; srvRange.BaseShaderRegister = 1; srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[2].DescriptorTable.NumDescriptorRanges = 1; params[2].DescriptorTable.pDescriptorRanges = &srvRange; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[3].Descriptor.ShaderRegister = 0; params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[4].Descriptor.ShaderRegister = 1; params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; params[5].Descriptor.ShaderRegister = 9; params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; params[6].Descriptor.ShaderRegister = 10; params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 7; rootDesc.pParameters = params; rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    static D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; staticSampler.ShaderRegister = 0; staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootDesc.NumStaticSamplers = 1; rootDesc.pStaticSamplers = &staticSampler;

    ComPtr<ID3DBlob> signature; ComPtr<ID3DBlob> error;
    HRESULT hrSerialize = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hrSerialize)) { if (error) fprintf(stderr, "DxrRenderer: Root signature error: %s\n", (char*)error->GetBufferPointer()); return; }
    HRESULT hrCreate = s_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&s_rtGlobalRootSignature));
    if (FAILED(hrCreate)) { fprintf(stderr, "DxrRenderer: CreateRootSignature failed: 0x%08x\n", (unsigned)hrCreate); return; }

    // Create state object (DXIL lib etc.)
    static D3D12_DXIL_LIBRARY_DESC libDesc = {};
    static D3D12_EXPORT_DESC exports[3] = {};
    static D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    static D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    static D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    static D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};

    libDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer(); libDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
    exports[0].Name = L"RayGen"; exports[1].Name = L"Miss"; exports[2].Name = L"ClosestHit"; libDesc.NumExports = 3; libDesc.pExports = exports;
    D3D12_STATE_SUBOBJECT libSub = {}; libSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY; libSub.pDesc = &libDesc;

    hitGroupDesc.HitGroupExport = L"HitGroup"; hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES; hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
    D3D12_STATE_SUBOBJECT hitSub = {}; hitSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP; hitSub.pDesc = &hitGroupDesc;

    shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float); shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);
    D3D12_STATE_SUBOBJECT shaderConfigSub = {}; shaderConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG; shaderConfigSub.pDesc = &shaderConfig;

    pipelineConfig.MaxTraceRecursionDepth = 1; D3D12_STATE_SUBOBJECT pipeConfigSub = {}; pipeConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG; pipeConfigSub.pDesc = &pipelineConfig;

    globalRootSigDesc.pGlobalRootSignature = s_rtGlobalRootSignature.Get(); D3D12_STATE_SUBOBJECT rootSigSub = {}; rootSigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE; rootSigSub.pDesc = &globalRootSigDesc;

    std::vector<D3D12_STATE_SUBOBJECT> subobjects; subobjects.push_back(libSub); subobjects.push_back(hitSub); subobjects.push_back(shaderConfigSub); subobjects.push_back(pipeConfigSub); subobjects.push_back(rootSigSub);

    D3D12_STATE_OBJECT_DESC stateObjDesc = {}; stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE; stateObjDesc.NumSubobjects = (UINT)subobjects.size(); stateObjDesc.pSubobjects = subobjects.data();

    HRESULT hrState = s_dxrDevice->CreateStateObject(&stateObjDesc, IID_PPV_ARGS(&s_rtStateObject));
    if (FAILED(hrState)) { fprintf(stderr, "DxrRenderer: CreateStateObject failed: 0x%08x\n", (unsigned)hrState); return; }

    // Create Shader Table
    ComPtr<ID3D12StateObjectProperties> properties;
    ThrowIfFailed(s_rtStateObject.As(&properties));
    void* rayGenId = properties->GetShaderIdentifier(L"RayGen");
    void* missId = properties->GetShaderIdentifier(L"Miss");
    void* hitGroupId = properties->GetShaderIdentifier(L"HitGroup");
    if (!rayGenId || !missId || !hitGroupId) { fprintf(stderr, "DxrRenderer: Shader IDs null\n"); return; }
    UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    s_shaderTableEntrySize = Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    UINT shaderTableSize = s_shaderTableEntrySize * 3;
    AllocateUploadBuffer(s_device, nullptr, shaderTableSize, &s_sbtStorage, L"Shader Table");
    UINT8* pData = nullptr; s_sbtStorage->Map(0, nullptr, (void**)&pData);
    memcpy(pData, rayGenId, shaderIdentifierSize); memcpy(pData + s_shaderTableEntrySize, missId, shaderIdentifierSize); memcpy(pData + s_shaderTableEntrySize*2, hitGroupId, shaderIdentifierSize);
    s_sbtStorage->Unmap(0, nullptr);
    s_rayGenShaderTable = s_sbtStorage->GetGPUVirtualAddress(); s_missShaderTable = s_rayGenShaderTable + s_shaderTableEntrySize; s_hitGroupShaderTable = s_missShaderTable + s_shaderTableEntrySize;

    // Create output UAV texture and heap
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {}; uavHeapDesc.NumDescriptors = 1; uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(s_device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&s_uavHeap)));

    // Create a default heap 2D texture to hold raytracing output (same format/dim as swapchain)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = 1280;
    texDesc.Height = 720;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(s_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s_outputUAV)));
    if (s_outputUAV) s_outputUAV->SetName(L"RT Output Texture");

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    if (s_uavHeap) { s_device->CreateUnorderedAccessView(s_outputUAV.Get(), nullptr, &uavDesc, s_uavHeap->GetCPUDescriptorHandleForHeapStart()); s_outputUAVGpuHandle = s_uavHeap->GetGPUDescriptorHandleForHeapStart(); }

    fprintf(stderr, "DxrRenderer: Ray Tracing Pipeline ready\n");
}

void BuildAccelerationStructures(const std::vector<Asset::GpuMesh>& meshes) {
    if (!g_rayTracingSupported || !s_dxrDevice) return;
    if (meshes.empty()) {
        fprintf(stderr, "DxrRenderer: BuildAccelerationStructures called with empty mesh list\n");
        return;
    }

    // Basic validation of command queue/fence setup
    if (!s_commandQueue || !s_fence || !s_fenceValues || !s_frameIndexPtr || !s_fenceEvent) {
        fprintf(stderr, "DxrRenderer: Cannot build AS - command queue / fence not set\n");
        return;
    }

    // Ensure meshes are valid
    for (size_t i=0;i<meshes.size();++i) {
        const auto &m = meshes[i];
        if (!m.vertexBuffer || !m.indexBuffer) {
            fprintf(stderr, "DxrRenderer: Mesh %zu missing vertex or index buffer - aborting AS build\n", i);
            return;
        }
        if (m.vertexCount == 0 || m.indexCount == 0) {
            fprintf(stderr, "DxrRenderer: Mesh %zu has zero vertices or indices - aborting AS build\n", i);
            return;
        }
    }

    // Wait for GPU (simple sync)
    const UINT64 fence = s_fenceValues[*s_frameIndexPtr];
    HRESULT hr = s_commandQueue->Signal(s_fence, fence);
    if (FAILED(hr)) { fprintf(stderr, "DxrRenderer: Signal before AS build failed: 0x%08x\n", (unsigned)hr); }
    s_fenceValues[*s_frameIndexPtr]++;
    if (s_fence->GetCompletedValue() < fence) { s_fence->SetEventOnCompletion(fence, s_fenceEvent); WaitForSingleObject(s_fenceEvent, INFINITE); }

    // Create command list
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList4> cmdList;
    HRESULT hrAlloc = s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    if (FAILED(hrAlloc)) { fprintf(stderr, "DxrRenderer: CreateCommandAllocator failed: 0x%08x\n", (unsigned)hrAlloc); return; }
    HRESULT hrList = s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    if (FAILED(hrList)) { fprintf(stderr, "DxrRenderer: CreateCommandList failed: 0x%08x\n", (unsigned)hrList); return; }

    // BLAS
    s_allBLAS.clear();
    try {
        for (size_t i=0;i<meshes.size();++i) {
            const auto &mesh = meshes[i];
            // Protect against invalid GPU virtual addresses
            if (!mesh.vertexBuffer || !mesh.indexBuffer) {
                fprintf(stderr, "DxrRenderer: Skipping mesh %zu because buffers are null\n", i);
                continue;
            }
            auto vbAddr = mesh.vertexBuffer->GetGPUVirtualAddress();
            auto ibAddr = mesh.indexBuffer->GetGPUVirtualAddress();
            if (vbAddr == 0 || ibAddr == 0) {
                fprintf(stderr, "DxrRenderer: Mesh %zu has invalid GPU addresses (vb=0x%016llx ib=0x%016llx) - aborting\n", i, (unsigned long long)vbAddr, (unsigned long long)ibAddr);
                return;
            }
            auto bl = BuildBLAS(s_dxrDevice.Get(), cmdList.Get(), vbAddr, mesh.vertexCount, sizeof(Asset::Vertex), ibAddr, mesh.indexCount);
            // Basic validation
            if (!bl.result || !bl.scratch) {
                fprintf(stderr, "DxrRenderer: BuildBLAS produced invalid buffers for mesh %zu\n", i);
                return;
            }
            s_allBLAS.push_back({bl, (UINT64)i});
        }

        if (s_allBLAS.empty()) {
            fprintf(stderr, "DxrRenderer: No BLAS built - aborting TLAS build\n");
            return;
        }

        // TLAS
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        for (size_t i=0;i<s_allBLAS.size();++i) {
            if (!s_allBLAS[i].buffers.result) {
                fprintf(stderr, "DxrRenderer: BLAS %zu missing result buffer - aborting\n", i);
                return;
            }
            D3D12_RAYTRACING_INSTANCE_DESC inst = {};
            inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 0.1f; // small scale to be non-zero
            inst.InstanceID = (UINT)i;
            inst.InstanceMask = 0xFF;
            inst.InstanceContributionToHitGroupIndex = 0;
            inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            inst.AccelerationStructure = s_allBLAS[i].buffers.result->GetGPUVirtualAddress();
            instanceDescs.push_back(inst);
        }

        ComPtr<ID3D12Resource> instanceDescBuffer;
        AllocateUploadBuffer(s_device, instanceDescs.data(), instanceDescs.size()*sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instanceDescBuffer, L"TLAS Instance Buffer");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = (UINT)instanceDescs.size();
        inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        s_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        s_tlas.scratchSizeInBytes = Align(info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
        s_tlas.resultSizeInBytes = Align(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
        AllocateUAVBuffer(s_device, s_tlas.scratchSizeInBytes, &s_tlas.scratch, D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
        AllocateUAVBuffer(s_device, s_tlas.resultSizeInBytes, &s_tlas.result, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"TLAS Result");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = inputs;
        buildDesc.DestAccelerationStructureData = s_tlas.result->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = s_tlas.scratch->GetGPUVirtualAddress();
        cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = s_tlas.result.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        ThrowIfFailed(cmdList->Close());
        ID3D12CommandList* lists[] = {cmdList.Get()};
        s_commandQueue->ExecuteCommandLists(1, lists);

        // Wait for finish
        const UINT64 fence2 = s_fenceValues[*s_frameIndexPtr];
        s_commandQueue->Signal(s_fence, fence2);
        s_fenceValues[*s_frameIndexPtr]++;
        if (s_fence->GetCompletedValue() < fence2) { s_fence->SetEventOnCompletion(fence2, s_fenceEvent); WaitForSingleObject(s_fenceEvent, INFINITE); }
        fprintf(stderr, "DxrRenderer: Acceleration structures built\n");

    } catch (const std::exception &e) {
        fprintf(stderr, "DxrRenderer: Exception during AS build: %s\n", e.what());
#ifdef _DEBUG
        // Dump recent D3D12 info queue messages if available
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(s_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < num; ++i) {
                SIZE_T messageLength = 0;
                infoQueue->GetMessage(i, nullptr, &messageLength);
                std::vector<char> message(messageLength);
                D3D12_MESSAGE* pMsg = reinterpret_cast<D3D12_MESSAGE*>(message.data());
                infoQueue->GetMessage(i, pMsg, &messageLength);
                fprintf(stderr, "D3D12 INFO (AS build): Category=%d Severity=%d ID=%d: %s\n",
                        (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
                        pMsg->pDescription);
            }
        }
#endif
    }
}

bool IsReady() {
    return g_rayTracingSupported && s_rtStateObject != nullptr && s_tlas.result != nullptr;
}

bool RenderFrame(ID3D12GraphicsCommandList* commandListBase, UINT frameIndex, ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, ID3D12Resource* cameraCB, ID3D12Resource* materialCB, D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart, UINT textureDescriptorCount, const std::vector<Asset::GpuMesh>& meshes) {
    if (!IsReady()) return false;
    ComPtr<ID3D12GraphicsCommandList4> dxrList;
    if (FAILED(commandListBase->QueryInterface(IID_PPV_ARGS(&dxrList)))) return false;

    dxrList->SetPipelineState1(s_rtStateObject.Get());
    dxrList->SetComputeRootSignature(s_rtGlobalRootSignature.Get());
    dxrList->SetComputeRootShaderResourceView(0, s_tlas.result->GetGPUVirtualAddress());
    ID3D12DescriptorHeap* heaps[] = { s_uavHeap.Get() };
    dxrList->SetDescriptorHeaps(1, heaps);
    dxrList->SetComputeRootDescriptorTable(1, s_outputUAVGpuHandle);

    if (textureDescriptorCount > 0) dxrList->SetComputeRootDescriptorTable(2, texturesGpuStart);
    if (cameraCB) dxrList->SetComputeRootConstantBufferView(3, cameraCB->GetGPUVirtualAddress());
    if (materialCB) dxrList->SetComputeRootConstantBufferView(4, materialCB->GetGPUVirtualAddress());
    if (!meshes.empty() && meshes[0].vertexBuffer) dxrList->SetComputeRootShaderResourceView(5, meshes[0].vertexBuffer->GetGPUVirtualAddress());
    if (!meshes.empty() && meshes[0].indexBuffer) dxrList->SetComputeRootShaderResourceView(6, meshes[0].indexBuffer->GetGPUVirtualAddress());

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = s_rayGenShaderTable;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.MissShaderTable.StartAddress = s_missShaderTable;
    dispatchDesc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StartAddress = s_hitGroupShaderTable;
    dispatchDesc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.Width = 1280; dispatchDesc.Height = 720; dispatchDesc.Depth = 1;

    dxrList->DispatchRays(&dispatchDesc);

    TransitionResource(dxrList.Get(), s_outputUAV.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    dxrList->CopyResource(renderTarget, s_outputUAV.Get());

    // Transition back
    TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(dxrList.Get(), s_outputUAV.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Bind RTV for subsequent ImGui draws
    commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    return true;
}

} // namespace DxrRenderer
