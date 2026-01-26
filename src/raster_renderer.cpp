#include "raster_renderer.h"
#include "dxc_wrapper.h"
#include "d3d12_helpers.h"
#include <wrl.h>
#include <vector>
#include <cstdio>
#include <filesystem>

using Microsoft::WRL::ComPtr;

// Access to a few global symbols from main.cpp
extern ComPtr<ID3D12Device> g_device;
extern ComPtr<ID3D12RootSignature> g_rootSignature;
extern bool g_rasterDebugUV;

// Define raster-specific resources here
ComPtr<ID3D12Resource> RasterRenderer::g_gridVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW RasterRenderer::g_gridVBView = {};
UINT RasterRenderer::g_gridVertexCount = 0;
ComPtr<ID3D12PipelineState> RasterRenderer::g_gridPipelineState;
ComPtr<ID3D12PipelineState> RasterRenderer::g_meshPipelineState;

static DxcHelper s_dxcHelper;

static std::wstring FindShaderFileLocal(const wchar_t* relativePath) {
    std::vector<std::wstring> searchPaths;
    searchPaths.push_back(relativePath);
    searchPaths.push_back(std::wstring(L"..\\..\\") + relativePath);
    searchPaths.push_back(std::wstring(L"..\\") + relativePath);
    for (auto &p : searchPaths) if (std::filesystem::exists(p)) return p;
    return relativePath;
}

namespace RasterRenderer {

void CreateGridResources(ID3D12Device* device, float gridThickness) {
    // Create grid PSO
    std::wstring vsPath = FindShaderFileLocal(L"shaders\\simple.hlsl");
    ComPtr<IDxcBlob> vsBlob = s_dxcHelper.Compile(vsPath, L"VSMain", L"vs_6_0", {});
    ComPtr<IDxcBlob> psBlob = s_dxcHelper.Compile(vsPath, L"PSMain", L"ps_6_0", {});
    D3D12_INPUT_ELEMENT_DESC simpleLayout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    // Some defaults to mimic main.cpp original
    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;

    psoDesc.InputLayout = {simpleLayout, _countof(simpleLayout)};
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.RasterizerState = rasterDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    // Ensure PSO uses the application's root signature
    if (g_rootSignature) psoDesc.pRootSignature = g_rootSignature.Get();

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_gridPipelineState)));

    // Create vertex buffer for grid
    struct GridVertex { float pos[3]; float col[3]; };
    const int half = 10;
    const float step = 1.0f;
    std::vector<GridVertex> verts;
    verts.reserve((half*2+1)*6*2);
    float halfThickness = gridThickness * 0.5f;

    for (int i = -half; i <= half; ++i) {
      float coord = i * step;
      // Line along X
      {
        float sx = (float)-half * step, sz = coord;
        float ex = (float)half * step, ez = coord;
        float oz = halfThickness;
        verts.push_back({{sx,0.0f,sz - oz},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex,0.0f,ez - oz},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex,0.0f,ez + oz},{0.3f,0.3f,0.3f}});
        verts.push_back({{sx,0.0f,sz - oz},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex,0.0f,ez + oz},{0.3f,0.3f,0.3f}});
        verts.push_back({{sx,0.0f,sz + oz},{0.3f,0.3f,0.3f}});
      }
      // Line along Z
      {
        float sx = coord, sz = (float)-half * step;
        float ex = coord, ez = (float)half * step;
        float ox = halfThickness;
        verts.push_back({{sx - ox,0.0f,sz},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex - ox,0.0f,ez},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex + ox,0.0f,ez},{0.3f,0.3f,0.3f}});
        verts.push_back({{sx - ox,0.0f,sz},{0.3f,0.3f,0.3f}});
        verts.push_back({{ex + ox,0.0f,ez},{0.3f,0.3f,0.3f}});
        verts.push_back({{sx + ox,0.0f,sz},{0.3f,0.3f,0.3f}});
      }
    }

    g_gridVertexCount = (UINT)verts.size();
    UINT vbSize = (UINT)(verts.size() * sizeof(GridVertex));

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC vbDesc = {}; vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; vbDesc.Width = vbSize; vbDesc.Height = 1; vbDesc.DepthOrArraySize = 1; vbDesc.MipLevels = 1; vbDesc.SampleDesc.Count = 1; vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_gridVertexBuffer)));

    UINT8* pData = nullptr; D3D12_RANGE readRange = {0,0}; ThrowIfFailed(g_gridVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData))); memcpy(pData, verts.data(), vbSize); g_gridVertexBuffer->Unmap(0, nullptr);

    g_gridVBView.BufferLocation = g_gridVertexBuffer->GetGPUVirtualAddress();
    g_gridVBView.StrideInBytes = sizeof(GridVertex);
    g_gridVBView.SizeInBytes = vbSize;
}

void RecreateMeshPipeline(ID3D12Device* device, ID3D12RootSignature* rootSig) {
  std::wstring pbrShaderPath = FindShaderFileLocal(L"shaders\\pbr_mesh.hlsl");

  try {
    std::vector<std::wstring> compileDefines;
    if (::g_rasterDebugUV) {
      compileDefines.push_back(L"RASTER_DEBUG_UV=1");
      fprintf(stderr, "RecreateMeshPipeline: adding RASTER_DEBUG_UV define\n");
    }

    ComPtr<IDxcBlob> vsMeshBlob;
    ComPtr<IDxcBlob> psMeshBlob;

    vsMeshBlob = s_dxcHelper.Compile(pbrShaderPath, L"VSMainMesh", L"vs_6_0", compileDefines);
    psMeshBlob = s_dxcHelper.Compile(pbrShaderPath, L"PSMainMesh", L"ps_6_0", compileDefines);

    D3D12_INPUT_ELEMENT_DESC meshInputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC meshPsoDesc = {};
    meshPsoDesc.InputLayout = {meshInputLayout, _countof(meshInputLayout)};
    meshPsoDesc.VS = {vsMeshBlob->GetBufferPointer(), vsMeshBlob->GetBufferSize()};
    meshPsoDesc.PS = {psMeshBlob->GetBufferPointer(), psMeshBlob->GetBufferSize()};

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      blendDesc.RenderTarget[i].BlendEnable = FALSE;
      blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
      blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
      blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
      blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
      blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
      blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
      blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
      blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
      blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    meshPsoDesc.RasterizerState = rasterDesc;
    meshPsoDesc.BlendState = blendDesc;
    meshPsoDesc.DepthStencilState = depthDesc;
    meshPsoDesc.SampleMask = UINT_MAX;
    meshPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    meshPsoDesc.NumRenderTargets = 1;
    meshPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    meshPsoDesc.SampleDesc.Count = 1;

    if (rootSig) meshPsoDesc.pRootSignature = rootSig;

    ComPtr<ID3D12PipelineState> newMeshPSO;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&meshPsoDesc, IID_PPV_ARGS(&newMeshPSO)));

    g_meshPipelineState = newMeshPSO;
    fprintf(stderr, "RecreateMeshPipeline: Mesh PSO recreated (RASTER_DEBUG_UV=%d)\n", (int)g_rasterDebugUV);

  } catch (const std::exception &e) {
    fprintf(stderr, "RecreateMeshPipeline failed: %s\n", e.what());
  }
}

void DrawGrid(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB) {
    if (!g_gridPipelineState || g_gridVertexCount == 0) return;
    cmdList->SetPipelineState(g_gridPipelineState.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &g_gridVBView);
    if (cameraCB) cmdList->SetGraphicsRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());
    cmdList->DrawInstanced(g_gridVertexCount, 1, 0, 0);
}

} // namespace RasterRenderer
