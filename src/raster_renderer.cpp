#define NOMINMAX
#include "raster_renderer.h"
#include "clouds.h"
#include "camera.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "dxc_wrapper.h"
#include "ibl_manager.h"
#include "scene.h"
#include <fstream>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

// Access to a few global symbols from main.cpp
extern ComPtr<ID3D12RootSignature> g_rootSignature;
extern DescriptorHeapAllocator g_cbvSrvAllocator;

using namespace DX12Context;
extern bool g_rasterDebugUV;
extern bool g_rasterWireframe;
extern bool g_rasterDebugDepth;

// Define raster-specific resources here
ComPtr<ID3D12Resource> RasterRenderer::g_gridVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW RasterRenderer::g_gridVBView = {};
UINT RasterRenderer::g_gridVertexCount = 0;
ComPtr<ID3D12PipelineState> RasterRenderer::g_gridPipelineState;
ComPtr<ID3D12PipelineState> RasterRenderer::g_meshPipelineState;
ComPtr<ID3D12PipelineState> RasterRenderer::g_skyboxPipelineState;
ComPtr<ID3D12PipelineState> RasterRenderer::g_depthOnlyPipelineState;
ComPtr<ID3D12PipelineState> RasterRenderer::g_shadowPipelineState;

static ComPtr<ID3D12Resource> s_shadowMap;
static ComPtr<ID3D12DescriptorHeap> s_shadowDsvHeap;
static D3D12_GPU_DESCRIPTOR_HANDLE s_shadowSrvGpu{};
static D3D12_CPU_DESCRIPTOR_HANDLE s_shadowSrvCpu{};
static bool s_shadowSrvAllocated = false;
static UINT s_shadowMapSize = 2048;

static DxcHelper s_dxcHelper;

static void TransitionResource(ID3D12GraphicsCommandList *cmdList,
                               ID3D12Resource *resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
  if (!resource || before == after)
    return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
}

static ComPtr<ID3D12Resource> s_hdrColor;
static ComPtr<ID3D12Resource> s_hdrNormal;
static ComPtr<ID3D12DescriptorHeap> s_hdrRtvHeap;
static UINT s_hdrWidth = 0;
static UINT s_hdrHeight = 0;
static D3D12_RESOURCE_STATES s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
static D3D12_RESOURCE_STATES s_normalState = D3D12_RESOURCE_STATE_RENDER_TARGET;

static ComPtr<ID3D12Resource> s_tonemapOutput;
static D3D12_RESOURCE_STATES s_tonemapOutputState =
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
static ComPtr<ID3D12RootSignature> s_tonemapRootSig;
static ComPtr<ID3D12PipelineState> s_tonemapPSO;
static ComPtr<ID3D12Resource> s_tonemapCB;
static ComPtr<ID3D12DescriptorHeap> s_tonemapHeap;
 
static ComPtr<ID3D12RootSignature> s_ssrRootSig;
static ComPtr<ID3D12PipelineState> s_ssrPSO;
static ComPtr<ID3D12Resource> s_hdrColorCopy;
static ComPtr<ID3D12DescriptorHeap> s_ssrHeap;
 
static ComPtr<ID3D12RootSignature> s_ssaoRootSig;
static ComPtr<ID3D12PipelineState> s_ssaoPSO;
static ComPtr<ID3D12Resource> s_ssaoMap;
static ComPtr<ID3D12DescriptorHeap> s_ssaoHeap;

static ComPtr<ID3D12RootSignature> s_bloomRootSig;
static ComPtr<ID3D12PipelineState> s_bloomExtractPSO;
static ComPtr<ID3D12PipelineState> s_blurPSO;
static ComPtr<ID3D12Resource> s_bloomBuffers[2]; // Two for ping-pong blur
static ComPtr<ID3D12DescriptorHeap> s_bloomHeaps[4]; // Extract, BlurH, BlurV, Tonemap

static ComPtr<ID3D12RootSignature> s_avgLumRootSig;
static ComPtr<ID3D12PipelineState> s_avgLumPSO;
static ComPtr<ID3D12Resource> s_avgLumCB;
static ComPtr<ID3D12Resource> s_avgLumBuffer;
static ComPtr<ID3D12Resource> s_avgLumReadbackBuffer;
static ComPtr<ID3D12DescriptorHeap> s_avgLumHeap;
static UINT s_avgLumCapacity = 0;

static float s_avgLuminanceCdM2 = 0.0f;
static float s_lastEV100 = -10.0f;
static float s_smoothedExposure = 0.02f;

namespace RasterRenderer {
static bool EnsureTonemapPipeline(ID3D12Device *device);
static bool EnsureAvgLumPipeline(ID3D12Device *device);
static bool EnsureSSRPipeline(ID3D12Device *device);
static bool EnsureSSAOPipeline(ID3D12Device *device);
static bool EnsureBloomPipeline(ID3D12Device *device);
static void RunBloom(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputHdr);


std::wstring FindShaderFileLocal(const wchar_t *relativePath) {
  std::vector<std::wstring> searchPaths;
  searchPaths.push_back(relativePath);
  searchPaths.push_back(std::wstring(L"..\\..\\") + relativePath);
  searchPaths.push_back(std::wstring(L"..\\") + relativePath);
  for (auto &p : searchPaths) {
     std::ifstream f(p);
     if (f.good()) return p;
  }
  return relativePath;
}

void CreateGridResources(ID3D12Device *device, float gridThickness) {
  // Create grid PSO
  std::wstring vsPath = FindShaderFileLocal(L"shaders\\simple.hlsl");
  ComPtr<IDxcBlob> vsBlob =
      s_dxcHelper.Compile(vsPath, L"VSMain", L"vs_6_0", {});
  ComPtr<IDxcBlob> psBlob =
      s_dxcHelper.Compile(vsPath, L"PSMain", L"ps_6_0", {});
  D3D12_INPUT_ELEMENT_DESC simpleLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

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
    blendDesc.RenderTarget[i].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
  }

  D3D12_DEPTH_STENCIL_DESC depthDesc = {};
  depthDesc.DepthEnable = TRUE;
  depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  psoDesc.InputLayout = {simpleLayout, _countof(simpleLayout)};
  psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  psoDesc.RasterizerState = rasterDesc;
  psoDesc.BlendState = blendDesc;
  psoDesc.DepthStencilState = depthDesc;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
  psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  psoDesc.SampleDesc.Count = 1;

  // Ensure PSO uses the application's root signature
  if (g_rootSignature)
    psoDesc.pRootSignature = g_rootSignature.Get();

  ThrowIfFailed(device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_gridPipelineState)));

  // Create vertex buffer for grid
  struct GridVertex {
    float pos[3];
    float col[3];
  };
  const int half = 20;     // Larger grid
  const float step = 1.0f; // 1.0 unit steps
  std::vector<GridVertex> verts;
  verts.reserve((half * 2 + 1) * 6 * 4); // Reserve enough for sublines too
  float halfThickness = gridThickness * 0.5f;

  for (int i = -half; i <= half; ++i) {
    float coord = i * step;

    // Determine line color
    float color[3] = {0.2f, 0.2f, 0.22f}; // Default darker gray
    float thickness = halfThickness;

    if (i == 0) {
      // Axis line
      thickness *= 2.0f;
    } else if (i % 5 == 0) {
      // Major line every 5 units
      color[0] = 0.35f;
      color[1] = 0.35f;
      color[2] = 0.38f;
      thickness *= 1.5f;
    }

    // Line along X (varying Z)
    {
      float sx = (float)-half * step, sz = coord;
      float ex = (float)half * step, ez = coord;
      float oz = thickness;

      float finalCol[3] = {color[0], color[1], color[2]};
      if (i == 0) {
        finalCol[0] = 0.6f;
        finalCol[1] = 0.1f;
        finalCol[2] = 0.1f;
      } // X axis is Red-ish

      verts.push_back(
          {{sx, 0.0f, sz - oz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex, 0.0f, ez - oz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex, 0.0f, ez + oz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{sx, 0.0f, sz - oz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex, 0.0f, ez + oz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{sx, 0.0f, sz + oz}, {finalCol[0], finalCol[1], finalCol[2]}});
    }
    // Line along Z (varying X)
    {
      float sx = coord, sz = (float)-half * step;
      float ex = coord, ez = (float)half * step;
      float ox = thickness;

      float finalCol[3] = {color[0], color[1], color[2]};
      if (i == 0) {
        finalCol[0] = 0.1f;
        finalCol[1] = 0.1f;
        finalCol[2] = 0.6f;
      } // Z axis is Blue-ish

      verts.push_back(
          {{sx - ox, 0.0f, sz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex - ox, 0.0f, ez}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex + ox, 0.0f, ez}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{sx - ox, 0.0f, sz}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{ex + ox, 0.0f, ez}, {finalCol[0], finalCol[1], finalCol[2]}});
      verts.push_back(
          {{sx + ox, 0.0f, sz}, {finalCol[0], finalCol[1], finalCol[2]}});
    }
  }

  g_gridVertexCount = (UINT)verts.size();
  UINT vbSize = (UINT)(verts.size() * sizeof(GridVertex));

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC vbDesc = {};
  vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  vbDesc.Width = vbSize;
  vbDesc.Height = 1;
  vbDesc.DepthOrArraySize = 1;
  vbDesc.MipLevels = 1;
  vbDesc.SampleDesc.Count = 1;
  vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_gridVertexBuffer)));

  UINT8 *pData = nullptr;
  D3D12_RANGE readRange = {0, 0};
  ThrowIfFailed(g_gridVertexBuffer->Map(0, &readRange,
                                        reinterpret_cast<void **>(&pData)));
  memcpy(pData, verts.data(), vbSize);
  g_gridVertexBuffer->Unmap(0, nullptr);

  g_gridVBView.BufferLocation = g_gridVertexBuffer->GetGPUVirtualAddress();
  g_gridVBView.StrideInBytes = sizeof(GridVertex);
  g_gridVBView.SizeInBytes = vbSize;
}

void RecreateMeshPipeline(ID3D12Device *device, ID3D12RootSignature *rootSig) {
  std::wstring pbrShaderPath = FindShaderFileLocal(L"shaders\\pbr_mesh.hlsl");

  try {
    std::vector<std::wstring> compileDefines;
    if (::g_rasterDebugUV) {
      compileDefines.push_back(L"RASTER_DEBUG_UV=1");
      fprintf(stderr, "RecreateMeshPipeline: adding RASTER_DEBUG_UV define\n");
    }
    if (::g_rasterDebugDepth) {
      compileDefines.push_back(L"RASTER_DEBUG_DEPTH=1");
      fprintf(stderr,
              "RecreateMeshPipeline: adding RASTER_DEBUG_DEPTH define\n");
    }

    ComPtr<IDxcBlob> vsMeshBlob;
    ComPtr<IDxcBlob> psMeshBlob;

    vsMeshBlob = s_dxcHelper.Compile(pbrShaderPath, L"VSMainMesh", L"vs_6_0",
                                     compileDefines);
    psMeshBlob = s_dxcHelper.Compile(pbrShaderPath, L"PSMainMesh", L"ps_6_0",
                                     compileDefines);

    D3D12_INPUT_ELEMENT_DESC meshInputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC meshPsoDesc = {};
    meshPsoDesc.InputLayout = {meshInputLayout, _countof(meshInputLayout)};
    meshPsoDesc.VS = {vsMeshBlob->GetBufferPointer(),
                      vsMeshBlob->GetBufferSize()};
    meshPsoDesc.PS = {psMeshBlob->GetBufferPointer(),
                      psMeshBlob->GetBufferSize()};

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode =
        g_rasterWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode =
        g_rasterWireframe ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    rasterDesc.FrontCounterClockwise = TRUE;
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
      blendDesc.RenderTarget[i].RenderTargetWriteMask =
          D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    meshPsoDesc.RasterizerState = rasterDesc;
    meshPsoDesc.BlendState = blendDesc;
    meshPsoDesc.DepthStencilState = depthDesc;
    meshPsoDesc.SampleMask = UINT_MAX;
    meshPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    meshPsoDesc.NumRenderTargets = 2;
    meshPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    meshPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Normals
    meshPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    meshPsoDesc.SampleDesc.Count = 1;

    if (rootSig)
      meshPsoDesc.pRootSignature = rootSig;

    ComPtr<ID3D12PipelineState> newMeshPSO;
    HRESULT hrMesh = device->CreateGraphicsPipelineState(
        &meshPsoDesc, IID_PPV_ARGS(&newMeshPSO));
    if (FAILED(hrMesh)) {
      fprintf(stderr, "RasterRenderer: CreateGraphicsPipelineState (mesh) failed: 0x%08x\n", (unsigned)hrMesh);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(stderr, "D3D12 INFO (PSO mesh): %s\n", pMsg->pDescription);
        }
      }
#endif
      ThrowIfFailed(hrMesh);
    }
    g_meshPipelineState = newMeshPSO;

    // Depth-only PSO (same as mesh but no color writes)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = meshPsoDesc;
    depthPsoDesc.PS = {nullptr, 0}; // Required when NumRenderTargets == 0
    depthPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        0;                             // Disable color output
    depthPsoDesc.NumRenderTargets = 0; // No render targets bound for depth-only
    depthPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    depthPsoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    depthPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    ComPtr<ID3D12PipelineState> newDepthPSO;
    HRESULT hrDepth = device->CreateGraphicsPipelineState(
        &depthPsoDesc, IID_PPV_ARGS(&newDepthPSO));
    if (FAILED(hrDepth)) {
      fprintf(stderr, "RasterRenderer: CreateGraphicsPipelineState (depth) failed: 0x%08x\n", (unsigned)hrDepth);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(stderr, "D3D12 INFO (PSO depth): %s\n", pMsg->pDescription);
        }
      }
#endif
      ThrowIfFailed(hrDepth);
    }
    g_depthOnlyPipelineState = newDepthPSO;

    // Shadow PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = depthPsoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 1000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.25f;
    
    ComPtr<ID3D12PipelineState> newShadowPSO;
    HRESULT hrShadow = device->CreateGraphicsPipelineState(
        &shadowPsoDesc, IID_PPV_ARGS(&newShadowPSO));
    if (FAILED(hrShadow)) {
      fprintf(stderr, "RasterRenderer: CreateGraphicsPipelineState (shadow) failed: 0x%08x\n", (unsigned)hrShadow);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(stderr, "D3D12 INFO (PSO shadow): %s\n", pMsg->pDescription);
        }
      }
#endif
      ThrowIfFailed(hrShadow);
    }
    g_shadowPipelineState = newShadowPSO;

    // --- Skybox PSO ---
    try {
      std::wstring skyboxPath = FindShaderFileLocal(L"shaders\\skybox.hlsl");
      ComPtr<IDxcBlob> vsSkyBlob =
          s_dxcHelper.Compile(skyboxPath, L"VSMain", L"vs_6_0", {});
      ComPtr<IDxcBlob> psSkyBlob =
          s_dxcHelper.Compile(skyboxPath, L"PSMain", L"ps_6_0", {});

      D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = meshPsoDesc;
      skyPsoDesc.VS = {vsSkyBlob->GetBufferPointer(),
                       vsSkyBlob->GetBufferSize()};
      skyPsoDesc.PS = {psSkyBlob->GetBufferPointer(),
                       psSkyBlob->GetBufferSize()};
      skyPsoDesc.InputLayout = {nullptr,
                                0}; // No input layout (generated in VS)

      // Skybox should render behind everything - but use ALWAYS to ensure it
      // draws if depth is 1.0
      skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

      ThrowIfFailed(device->CreateGraphicsPipelineState(
          &skyPsoDesc, IID_PPV_ARGS(&g_skyboxPipelineState)));
      fprintf(stderr, "RecreateMeshPipeline: Skybox PSO created\n");
    } catch (const std::exception &eSky) {
      fprintf(stderr, "RecreateMeshPipeline: Skybox PSO failed: %s\n",
              eSky.what());
    }

    fprintf(stderr, "RecreateMeshPipeline: Mesh PSOs recreated\n");

  } catch (const std::exception &e) {
    fprintf(stderr, "RecreateMeshPipeline failed: %s\n", e.what());
  }
}

void DrawGrid(ID3D12GraphicsCommandList *cmdList, ID3D12Resource *cameraCB) {
  if (!g_gridPipelineState || g_gridVertexCount == 0)
    return;
  cmdList->SetPipelineState(g_gridPipelineState.Get());
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cmdList->IASetVertexBuffers(0, 1, &g_gridVBView);
  if (cameraCB)
    cmdList->SetGraphicsRootConstantBufferView(
        0, cameraCB->GetGPUVirtualAddress());
  cmdList->DrawInstanced(g_gridVertexCount, 1, 0, 0);
}

void DrawSkybox(ID3D12GraphicsCommandList *cmdList, ID3D12Resource *cameraCB) {
  if (!g_skyboxPipelineState)
    return;
  cmdList->SetPipelineState(g_skyboxPipelineState.Get());
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cmdList->SetGraphicsRootConstantBufferView(0,
                                             cameraCB->GetGPUVirtualAddress());
  if (IBLManager::Get().IsLoaded()) {
    cmdList->SetGraphicsRootDescriptorTable(4,
                                            IBLManager::Get().GetGPUHandle());
  }
  // Bind cloud descriptor table (CBV + BaseSRV + DetailSRV) at root param 5 if
  // available
  if (g_cloudManager.GetGPUHandle().ptr != 0) {
    cmdList->SetGraphicsRootDescriptorTable(5, g_cloudManager.GetGPUHandle());
  }
  cmdList->DrawInstanced(3, 1, 0, 0); // Full screen triangle
}
void DrawSceneDepthOnly(ID3D12GraphicsCommandList *cmdList,
                        ID3D12Resource *cameraCB,
                        const std::vector<Scene::Instance> &instances) {
  if (!g_depthOnlyPipelineState || instances.empty())
    return;

  cmdList->SetPipelineState(g_depthOnlyPipelineState.Get());
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (cameraCB)
    cmdList->SetGraphicsRootConstantBufferView(
        0, cameraCB->GetGPUVirtualAddress());

  for (const auto &inst : instances) {
    const auto &gm = *inst.mesh;
    if (!gm.vertexBuffer || !gm.indexBuffer)
      continue;

    // Set World Matrix (Parameter index 3, register b2)
    cmdList->SetGraphicsRoot32BitConstants(3, 16, &inst.transform, 0);

    cmdList->IASetVertexBuffers(0, 1, &gm.vbView);
    cmdList->IASetIndexBuffer(&gm.ibView);
    cmdList->DrawIndexedInstanced(gm.indexCount, 1, 0, 0, 0);
  }
}

void CreateShadowResources(ID3D12Device *device) {
  if (s_shadowMap) return;

  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = s_shadowMapSize;
  desc.Height = s_shadowMapSize;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_D32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_HEAP_PROPERTIES prop{};
  prop.Type = D3D12_HEAP_TYPE_DEFAULT;
  
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 1.0f;

  ThrowIfFailed(device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, 
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&s_shadowMap)));

  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
  dsvHeapDesc.NumDescriptors = 1;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&s_shadowDsvHeap)));
  
  device->CreateDepthStencilView(s_shadowMap.Get(), nullptr, s_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

  if (!s_shadowSrvAllocated) {
    auto alloc = g_cbvSrvAllocator.AllocatePersistent(1);
    s_shadowSrvGpu = alloc.gpu;
    s_shadowSrvCpu = alloc.cpu;
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(s_shadowMap.Get(), &srvDesc, alloc.cpu);
    s_shadowSrvAllocated = true;
  }
}

void DrawShadowMap(ID3D12GraphicsCommandList *cmdList, ID3D12Resource *cameraCB, const std::vector<Scene::Instance> &instances) {
  if (!s_shadowMap || !g_shadowPipelineState) return;

  TransitionResource(cmdList, s_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = s_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
  cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  
  D3D12_VIEWPORT vp{0, 0, (float)s_shadowMapSize, (float)s_shadowMapSize, 0, 1};
  D3D12_RECT sc{0, 0, (LONG)s_shadowMapSize, (LONG)s_shadowMapSize};
  cmdList->RSSetViewports(1, &vp);
  cmdList->RSSetScissorRects(1, &sc);
  
  cmdList->SetPipelineState(g_shadowPipelineState.Get());
  cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (cameraCB) cmdList->SetGraphicsRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  for (const auto &inst : instances) {
    if (!inst.mesh || !inst.mesh->vertexBuffer || !inst.mesh->indexBuffer) continue;
    cmdList->SetGraphicsRoot32BitConstants(3, 16, &inst.transform, 0);
    cmdList->IASetVertexBuffers(0, 1, &inst.mesh->vbView);
    cmdList->IASetIndexBuffer(&inst.mesh->ibView);
    cmdList->DrawIndexedInstanced(inst.mesh->indexCount, 1, 0, 0, 0);
  }

  TransitionResource(cmdList, s_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

D3D12_GPU_DESCRIPTOR_HANDLE GetShadowMapSrv() { return s_shadowSrvGpu; }
D3D12_CPU_DESCRIPTOR_HANDLE GetShadowMapSrvCpu() { return s_shadowSrvCpu; }

struct TonemapConstants {
  uint32_t outWidth;
  uint32_t outHeight;
  float exposure;
  float vignette;
  float saturation;
  float contrast;
  float ssaoEnabled;
  float _pad[1];
};

static bool EnsureTonemapPipeline(ID3D12Device *device) {
  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapCB && s_tonemapHeap)
    return true;

  D3D12_DESCRIPTOR_RANGE srvRange{};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 3; // HDR, SSAO, Bloom
  srvRange.BaseShaderRegister = 0;

  D3D12_DESCRIPTOR_RANGE uavRange{};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;

  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  ThrowIfFailed(
      D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                  &err));
  ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(),
                                            sig->GetBufferSize(),
                                            IID_PPV_ARGS(&s_tonemapRootSig)));

  std::wstring csPath = FindShaderFileLocal(L"shaders\\tonemap_cs.hlsl");
  ComPtr<IDxcBlob> csBlob = s_dxcHelper.Compile(csPath, L"CSMain", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = s_tonemapRootSig.Get();
  psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
  ThrowIfFailed(device->CreateComputePipelineState(&psoDesc,
                                                   IID_PPV_ARGS(&s_tonemapPSO)));

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cbDesc{};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = 256;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  cbDesc.SampleDesc.Count = 1;
  ThrowIfFailed(device->CreateCommittedResource(
      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_tonemapCB)));

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 5; // CBV, 3xSRV, UAV
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(
      device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_tonemapHeap)));
  return true;
}

static bool EnsureAvgLumPipeline(ID3D12Device *device) {
  if (s_avgLumPSO && s_avgLumRootSig && s_avgLumCB && s_avgLumBuffer &&
      s_avgLumReadbackBuffer && s_avgLumHeap)
    return true;

  D3D12_DESCRIPTOR_RANGE srvRange{};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;

  D3D12_DESCRIPTOR_RANGE uavRange{};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;

  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  ThrowIfFailed(
      D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                  &err));
  ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(),
                                            sig->GetBufferSize(),
                                            IID_PPV_ARGS(&s_avgLumRootSig)));

  std::wstring csPath = FindShaderFileLocal(L"shaders\\avg_luminance_cs.hlsl");
  ComPtr<IDxcBlob> csBlob = s_dxcHelper.Compile(csPath, L"CSMain", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = s_avgLumRootSig.Get();
  psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
  ThrowIfFailed(device->CreateComputePipelineState(&psoDesc,
                                                   IID_PPV_ARGS(&s_avgLumPSO)));

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cbDesc{};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = 256;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  cbDesc.SampleDesc.Count = 1;
  ThrowIfFailed(device->CreateCommittedResource(
      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_avgLumCB)));

  s_avgLumCapacity = 256;
  D3D12_RESOURCE_DESC bufDesc{};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = s_avgLumCapacity * sizeof(float) * 2;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES defHeap{};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ThrowIfFailed(device->CreateCommittedResource(
      &defHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_avgLumBuffer)));

  bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  D3D12_HEAP_PROPERTIES rdHeap{};
  rdHeap.Type = D3D12_HEAP_TYPE_READBACK;
  ThrowIfFailed(device->CreateCommittedResource(
      &rdHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&s_avgLumReadbackBuffer)));

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 2;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(
      device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_avgLumHeap)));
  return true;
}

static bool EnsureHdrResources(ID3D12Device *device, UINT width, UINT height) {
  if (s_hdrColor && s_tonemapOutput && s_hdrWidth == width &&
      s_hdrHeight == height)
    return true;

  s_hdrColor.Reset();
  s_hdrNormal.Reset();
  s_tonemapOutput.Reset();
  s_hdrRtvHeap.Reset();
  s_hdrWidth = width;
  s_hdrHeight = height;

  D3D12_RESOURCE_DESC hdrDesc{};
  hdrDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  hdrDesc.Width = width;
  hdrDesc.Height = height;
  hdrDesc.DepthOrArraySize = 1;
  hdrDesc.MipLevels = 1;
  hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  hdrDesc.SampleDesc.Count = 1;
  hdrDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES defHeap{};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  // Can't use optimized clear value with UAV flag
  ThrowIfFailed(device->CreateCommittedResource(
      &defHeap, D3D12_HEAP_FLAG_NONE, &hdrDesc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
      IID_PPV_ARGS(&s_hdrColor)));
  s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  // Normal buffer (also needs UAV for SSAO write-back)
  D3D12_CLEAR_VALUE clearValue{};
  clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  clearValue.Color[0] = 0.0f;
  clearValue.Color[1] = 0.0f;
  clearValue.Color[2] = 0.0f;
  clearValue.Color[3] = 1.0f;
  ThrowIfFailed(device->CreateCommittedResource(
      &defHeap, D3D12_HEAP_FLAG_NONE, &hdrDesc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
      IID_PPV_ARGS(&s_hdrNormal)));
  s_normalState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.NumDescriptors = 2;
  ThrowIfFailed(
      device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&s_hdrRtvHeap)));
  
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = s_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
  UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  
  device->CreateRenderTargetView(s_hdrColor.Get(), nullptr, rtvHandle);
  rtvHandle.ptr += rtvSize;
  device->CreateRenderTargetView(s_hdrNormal.Get(), nullptr, rtvHandle);

  D3D12_RESOURCE_DESC outDesc{};
  outDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  outDesc.Width = width;
  outDesc.Height = height;
  outDesc.DepthOrArraySize = 1;
  outDesc.MipLevels = 1;
  outDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  outDesc.SampleDesc.Count = 1;
  outDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  outDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ThrowIfFailed(device->CreateCommittedResource(
      &defHeap, D3D12_HEAP_FLAG_NONE, &outDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_tonemapOutput)));
  s_tonemapOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

  return EnsureTonemapPipeline(device) && EnsureAvgLumPipeline(device) && EnsureSSRPipeline(device) && EnsureSSAOPipeline(device);
}

bool PrepareHdrRenderTarget(ID3D12Device *device,
                            ID3D12GraphicsCommandList *cmdList, UINT width,
                            UINT height,
                            D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
  if (!EnsureHdrResources(device, width, height))
    return false;
  TransitionResource(cmdList, s_hdrColor.Get(), s_hdrState,
                     D3D12_RESOURCE_STATE_RENDER_TARGET);
  TransitionResource(cmdList, s_hdrNormal.Get(), s_normalState,
                     D3D12_RESOURCE_STATE_RENDER_TARGET);
  s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  s_normalState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
  rtvs[0] = s_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
  rtvs[1] = rtvs[0];
  rtvs[1].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  
  cmdList->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
  
  D3D12_VIEWPORT viewport = {0, 0, (float)width, (float)height, 0, 1};
  D3D12_RECT scissor = {0, 0, (LONG)width, (LONG)height};
  cmdList->RSSetViewports(1, &viewport);
  cmdList->RSSetScissorRects(1, &scissor);
  
  FLOAT clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  cmdList->ClearRenderTargetView(rtvs[0], clearColor, 0, nullptr);
  cmdList->ClearRenderTargetView(rtvs[1], clearColor, 0, nullptr);
  cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  
  return true;
}

float GetCurrentAvgLuminance() { return s_avgLuminanceCdM2; }
float GetCurrentEV100() { return s_lastEV100; }

void BindHdrRenderTarget(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
  rtvs[0] = s_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
  rtvs[1] = rtvs[0];
  rtvs[1].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  
  cmdList->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
  
  // Also set viewport/scissor as they are likely reset after shadow map
  D3D12_VIEWPORT viewport = {0, 0, (float)s_hdrWidth, (float)s_hdrHeight, 0, 1};
  D3D12_RECT scissor = {0, 0, (LONG)s_hdrWidth, (LONG)s_hdrHeight};
  cmdList->RSSetViewports(1, &viewport);
  cmdList->RSSetScissorRects(1, &scissor);
}

bool TonemapHdrToBackbuffer(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
                            ID3D12Resource *backbuffer, UINT width,
                            UINT height, ID3D12Resource *cameraCB,
                            ID3D12Resource *depthBuffer) {
  if (!EnsureHdrResources(device, width, height) || !backbuffer || !cameraCB || !depthBuffer)
    return false;

  // Run SSR and SSAO before Tonemapping
  // These modify s_hdrColor (SSR) or generate s_ssaoMap (SSAO)
  RunSSR(device, cmdList, cameraCB, depthBuffer);
  RunSSAO(device, cmdList, cameraCB, depthBuffer);

  float *data = nullptr;
  if (SUCCEEDED(s_avgLumReadbackBuffer->Map(0, nullptr, (void **)&data))) {
    const UINT stride = 8;
    const UINT gridW = (width + stride - 1) / stride;
    const UINT gridH = (height + stride - 1) / stride;
    const UINT total = gridW * gridH;
    double sumLogLum = 0.0;
    double sumLum = 0.0;
    UINT count = 0;
    const UINT maxFloats =
        (UINT)(s_avgLumReadbackBuffer->GetDesc().Width / sizeof(float));
    const UINT limit = (std::min)(total, maxFloats / 2);
    for (UINT i = 0; i < limit; ++i) {
      float logVal = data[i * 2 + 0];
      float lumVal = data[i * 2 + 1];
      if (std::isfinite(logVal) && std::isfinite(lumVal)) {
        sumLogLum += logVal;
        sumLum += lumVal;
        ++count;
      }
    }
    float avgLog = (count > 0) ? expf((float)(sumLogLum / count)) : 0.0f;
    float avgLin = (count > 0) ? (float)(sumLum / count) : 0.0f;
    float targetLum = avgLog;
    if (avgLin > avgLog * 10.0f) {
      targetLum = avgLog * 0.2f + avgLin * 0.8f;
    }
    s_avgLuminanceCdM2 = (std::max)(targetLum, 1e-4f);
    s_lastEV100 = log2f(s_avgLuminanceCdM2 / 0.125f);
    s_avgLumReadbackBuffer->Unmap(0, nullptr);
  }

  const UINT stride = 8;
  const UINT gridW = (width + stride - 1) / stride;
  const UINT gridH = (height + stride - 1) / stride;
  const UINT total = gridW * gridH;
  if (total > s_avgLumCapacity) {
    s_avgLumCapacity = total;
    D3D12_RESOURCE_DESC desc = s_avgLumBuffer->GetDesc();
    desc.Width = total * sizeof(float) * 2;
    D3D12_HEAP_PROPERTIES defHeap = {};
    defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(device->CreateCommittedResource(
        &defHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&s_avgLumBuffer)));

    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES rdHeap = {};
    rdHeap.Type = D3D12_HEAP_TYPE_READBACK;
    ThrowIfFailed(device->CreateCommittedResource(
        &rdHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&s_avgLumReadbackBuffer)));
  }

  struct {
    uint32_t w, h;
    float padding[2];
  } avgCb = {width, height, {0.0f, 0.0f}};
  void *p = nullptr;
  if (SUCCEEDED(s_avgLumCB->Map(0, nullptr, &p))) {
    memcpy(p, &avgCb, sizeof(avgCb));
    s_avgLumCB->Unmap(0, nullptr);
  }

  const UINT descSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE avgCpu =
      s_avgLumHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC avgSrv{};
  avgSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  avgSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  avgSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  avgSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(s_hdrColor.Get(), &avgSrv, avgCpu);

  D3D12_CPU_DESCRIPTOR_HANDLE avgUavCpu = avgCpu;
  avgUavCpu.ptr += descSize;
  D3D12_UNORDERED_ACCESS_VIEW_DESC avgUav{};
  avgUav.Format = DXGI_FORMAT_UNKNOWN;
  avgUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  avgUav.Buffer.NumElements = total;
  avgUav.Buffer.StructureByteStride = sizeof(float) * 2;
  device->CreateUnorderedAccessView(s_avgLumBuffer.Get(), nullptr, &avgUav,
                                    avgUavCpu);

  TransitionResource(cmdList, s_hdrColor.Get(), s_hdrState,
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  s_hdrState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  ID3D12DescriptorHeap *avgHeaps[] = {s_avgLumHeap.Get()};
  cmdList->SetDescriptorHeaps(1, avgHeaps);
  cmdList->SetPipelineState(s_avgLumPSO.Get());
  cmdList->SetComputeRootSignature(s_avgLumRootSig.Get());
  cmdList->SetComputeRootConstantBufferView(0, s_avgLumCB->GetGPUVirtualAddress());
  D3D12_GPU_DESCRIPTOR_HANDLE avgGpu =
      s_avgLumHeap->GetGPUDescriptorHandleForHeapStart();
  cmdList->SetComputeRootDescriptorTable(1, avgGpu);
  avgGpu.ptr += descSize;
  cmdList->SetComputeRootDescriptorTable(2, avgGpu);
  cmdList->Dispatch((gridW + 15) / 16, (gridH + 15) / 16, 1);

  TransitionResource(cmdList, s_avgLumBuffer.Get(),
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
  cmdList->CopyResource(s_avgLumReadbackBuffer.Get(), s_avgLumBuffer.Get());
  TransitionResource(cmdList, s_avgLumBuffer.Get(),
                     D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  float exposure = g_cameraData.intensity;
  if (DxrRenderer::GetAutoExposure()) {
    float targetExposure = 1.0f;
    if (s_avgLuminanceCdM2 > 1e-5f) {
      targetExposure = (0.18f / s_avgLuminanceCdM2) *
                       DxrRenderer::GetExposureCompensation();
    }
    targetExposure = (std::min)((std::max)(targetExposure, 1e-20f), 1e10f);
    s_smoothedExposure += (targetExposure - s_smoothedExposure) * 0.05f;
    exposure = s_smoothedExposure;
    g_cameraData.intensity = exposure;
  } else if (DxrRenderer::GetPhysicalCameraExposure()) {
    const float ev100 = DxrRenderer::GetPhysicalCameraEV100();
    exposure = (1.0f / (1.2f * powf(2.0f, ev100))) *
               DxrRenderer::GetExposureCompensation();
    exposure = (std::max)(exposure, 1e-20f);
    g_cameraData.intensity = exposure;
    s_smoothedExposure = exposure;
  } else {
    s_smoothedExposure = g_cameraData.intensity;
    exposure = g_cameraData.intensity;
  }

  TonemapConstants tc{};
  tc.outWidth = width;
  tc.outHeight = height;
  tc.exposure = exposure;
  tc.vignette = 0.15f;
  tc.saturation = 1.05f;
  tc.contrast = 1.05f;
  tc._pad[0] = s_ssaoMap ? 1.0f : 0.0f;
  if (SUCCEEDED(s_tonemapCB->Map(0, nullptr, &p))) {
    memcpy(p, &tc, sizeof(tc));
    s_tonemapCB->Unmap(0, nullptr);
  }

  RunBloom(device, cmdList, s_hdrColor.Get());

  D3D12_CPU_DESCRIPTOR_HANDLE tmCpu =
      s_tonemapHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC tmSrv{};
  tmSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  tmSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  tmSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  tmSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(s_hdrColor.Get(), &tmSrv, tmCpu);

  tmCpu.ptr += descSize;
  // SSAO Map SRV
  if (s_ssaoMap) {
     device->CreateShaderResourceView(s_ssaoMap.Get(), nullptr, tmCpu);
  } else {
     // Create a dummy 1x1 white texture or just a null SRV with white?
     // For now, I'll just skip or handle in shader.
     // Better create a dummy SRV.
     device->CreateShaderResourceView(nullptr, nullptr, tmCpu); // Actually this might fail without a desc.
  }

  tmCpu.ptr += descSize;
  // Bloom Map SRV
  if (s_bloomBuffers[0]) {
      device->CreateShaderResourceView(s_bloomBuffers[0].Get(), nullptr, tmCpu);
  } else {
      device->CreateShaderResourceView(nullptr, nullptr, tmCpu);
  }

  D3D12_CPU_DESCRIPTOR_HANDLE tmUavCpu = tmCpu;
  tmUavCpu.ptr += descSize;
  D3D12_UNORDERED_ACCESS_VIEW_DESC tmUav{};
  tmUav.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  tmUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(s_tonemapOutput.Get(), nullptr, &tmUav,
                                    tmUavCpu);

  ID3D12DescriptorHeap *tmHeaps[] = {s_tonemapHeap.Get()};
  cmdList->SetDescriptorHeaps(1, tmHeaps);
  cmdList->SetPipelineState(s_tonemapPSO.Get());
  cmdList->SetComputeRootSignature(s_tonemapRootSig.Get());
  cmdList->SetComputeRootConstantBufferView(0, s_tonemapCB->GetGPUVirtualAddress());
  D3D12_GPU_DESCRIPTOR_HANDLE tmGpu =
      s_tonemapHeap->GetGPUDescriptorHandleForHeapStart();
  cmdList->SetComputeRootDescriptorTable(1, tmGpu);
  tmGpu.ptr += 3 * descSize; // Skip 3 SRVs (HDR, SSAO, Bloom)
  cmdList->SetComputeRootDescriptorTable(2, tmGpu);
  cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  TransitionResource(cmdList, s_tonemapOutput.Get(), s_tonemapOutputState,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
  s_tonemapOutputState = D3D12_RESOURCE_STATE_COPY_SOURCE;
  TransitionResource(cmdList, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
                     D3D12_RESOURCE_STATE_COPY_DEST);
  cmdList->CopyResource(backbuffer, s_tonemapOutput.Get());
  TransitionResource(cmdList, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST,
                     D3D12_RESOURCE_STATE_RENDER_TARGET);
  TransitionResource(cmdList, s_tonemapOutput.Get(), s_tonemapOutputState,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  s_tonemapOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

  return true;
}

static bool EnsureSSRPipeline(ID3D12Device *device) {
  if (s_ssrPSO) return true;

  try {
    std::wstring ssrPath = FindShaderFileLocal(L"shaders\\ssr_cs.hlsl");
    ComPtr<IDxcBlob> csBlob = s_dxcHelper.Compile(ssrPath, L"CSMain", L"cs_6_0", {});

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3; // ColorTex(t0), NormalTex(t1), DepthTex(t2)
    ranges[0].BaseShaderRegister = 0;
    
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> rsBlob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &err);
    ThrowIfFailed(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&s_ssrRootSig)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = s_ssrRootSig.Get();
    psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_ssrPSO)));

    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "SSR pipeline creation failed: %s\n", e.what());
    return false;
  }
}

void RunSSR(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, ID3D12Resource* depthBuffer) {
  if (!s_ssrPSO || !s_hdrColor || !s_hdrNormal) return;

  // 1. Create Color Copy for sampling
  if (!s_hdrColorCopy || s_hdrColorCopy->GetDesc().Width != s_hdrWidth) {
    D3D12_RESOURCE_DESC desc = s_hdrColor->GetDesc();
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES prop = {D3D12_HEAP_TYPE_DEFAULT};
    ThrowIfFailed(device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s_hdrColorCopy)));
    
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_ssrHeap)));
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = s_ssrHeap->GetCPUDescriptorHandleForHeapStart();
    UINT size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    device->CreateShaderResourceView(s_hdrColorCopy.Get(), nullptr, cpu); cpu.ptr += size;
    device->CreateShaderResourceView(s_hdrNormal.Get(), nullptr, cpu); cpu.ptr += size;
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
      depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
      depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      depthSrvDesc.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(depthBuffer, &depthSrvDesc, cpu); cpu.ptr += size;
    }
    device->CreateUnorderedAccessView(s_hdrColor.Get(), nullptr, nullptr, cpu);
  }

  TransitionResource(cmdList, s_hdrColor.Get(), s_hdrState, D3D12_RESOURCE_STATE_COPY_SOURCE);
  TransitionResource(cmdList, s_hdrColorCopy.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  cmdList->CopyResource(s_hdrColorCopy.Get(), s_hdrColor.Get());
  TransitionResource(cmdList, s_hdrColor.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  TransitionResource(cmdList, s_hdrColorCopy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  TransitionResource(cmdList, s_hdrNormal.Get(), s_normalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  TransitionResource(cmdList, depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdList->SetPipelineState(s_ssrPSO.Get());
  cmdList->SetComputeRootSignature(s_ssrRootSig.Get());
  cmdList->SetDescriptorHeaps(1, s_ssrHeap.GetAddressOf());
  cmdList->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());
  cmdList->SetComputeRootDescriptorTable(1, s_ssrHeap->GetGPUDescriptorHandleForHeapStart());
  
  D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = s_ssrHeap->GetGPUDescriptorHandleForHeapStart();
  uavGpu.ptr += 3 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetComputeRootDescriptorTable(2, uavGpu);

  cmdList->Dispatch((s_hdrWidth + 7) / 8, (s_hdrHeight + 7) / 8, 1);

  TransitionResource(cmdList, s_hdrColor.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
  TransitionResource(cmdList, s_hdrNormal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  TransitionResource(cmdList, depthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  s_normalState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

static bool EnsureSSAOPipeline(ID3D12Device *device) {
  if (s_ssaoPSO) return true;
  try {
    std::wstring ssaoPath = FindShaderFileLocal(L"shaders\\ssao_cs.hlsl");
    ComPtr<IDxcBlob> csBlob = s_dxcHelper.Compile(ssaoPath, L"CSMain", L"cs_6_0", {});

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2; // Normal, Depth
    ranges[0].BaseShaderRegister = 0;
    
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rsDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> rsBlob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &err);
    ThrowIfFailed(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&s_ssaoRootSig)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = s_ssaoRootSig.Get();
    psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_ssaoPSO)));
    return true;
  } catch(...) { return false; }
}

void RunSSAO(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, ID3D12Resource* depthBuffer) {
  if (!s_ssaoPSO || !s_hdrNormal) return;

  if (!s_ssaoMap || s_ssaoMap->GetDesc().Width != s_hdrWidth) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = s_hdrWidth;
    desc.Height = s_hdrHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES prop = {D3D12_HEAP_TYPE_DEFAULT};
    ThrowIfFailed(device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s_ssaoMap)));
    
    D3D12_DESCRIPTOR_HEAP_DESC ssaodehpDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ThrowIfFailed(device->CreateDescriptorHeap(&ssaodehpDesc, IID_PPV_ARGS(&s_ssaoHeap)));
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = s_ssaoHeap->GetCPUDescriptorHandleForHeapStart();
    UINT size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CreateShaderResourceView(s_hdrNormal.Get(), nullptr, cpu); cpu.ptr += size;
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
      depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
      depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      depthSrvDesc.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(depthBuffer, &depthSrvDesc, cpu); cpu.ptr += size;
    }
    device->CreateUnorderedAccessView(s_ssaoMap.Get(), nullptr, nullptr, cpu);
  }

  TransitionResource(cmdList, s_hdrNormal.Get(), s_normalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  TransitionResource(cmdList, depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  cmdList->SetPipelineState(s_ssaoPSO.Get());
  cmdList->SetComputeRootSignature(s_ssaoRootSig.Get());
  cmdList->SetDescriptorHeaps(1, s_ssaoHeap.GetAddressOf());
  cmdList->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());
  cmdList->SetComputeRootDescriptorTable(1, s_ssaoHeap->GetGPUDescriptorHandleForHeapStart());
  
  D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = s_ssaoHeap->GetGPUDescriptorHandleForHeapStart();
  uavGpu.ptr += 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetComputeRootDescriptorTable(2, uavGpu);

  cmdList->Dispatch((s_hdrWidth + 7) / 8, (s_hdrHeight + 7) / 8, 1);

  TransitionResource(cmdList, s_hdrNormal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, s_normalState);
  TransitionResource(cmdList, depthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  
  // Now multiply color by SSAO map
  // To keep it simple, I'll update the tonemapper to sample the SSAO map if it exists.
}

static bool EnsureBloomPipeline(ID3D12Device *device) {
  if (s_bloomExtractPSO && s_blurPSO) return true;
  try {
    std::wstring extractPath = FindShaderFileLocal(L"shaders\\bloom_extract_cs.hlsl");
    ComPtr<IDxcBlob> extractBlob = s_dxcHelper.Compile(extractPath, L"CSMain", L"cs_6_0", {});
    
    std::wstring blurPath = FindShaderFileLocal(L"shaders\\blur_cs.hlsl");
    ComPtr<IDxcBlob> blurBlob = s_dxcHelper.Compile(blurPath, L"CSMain", L"cs_6_0", {});

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 4;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &err);
    ThrowIfFailed(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&s_bloomRootSig)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = s_bloomRootSig.Get();
    psoDesc.CS = {extractBlob->GetBufferPointer(), extractBlob->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_bloomExtractPSO)));

    psoDesc.CS = {blurBlob->GetBufferPointer(), blurBlob->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_blurPSO)));

    return true;
  } catch(...) { return false; }
}

void RunBloom(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputHdr) {
  if (!EnsureBloomPipeline(device)) return;

  UINT width = s_hdrWidth / 2; // Bloom at half res
  UINT height = s_hdrHeight / 2;
  if (width < 1) width = 1;
  if (height < 1) height = 1;

  if (!s_bloomBuffers[0] || s_bloomBuffers[0]->GetDesc().Width != width) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES prop = {D3D12_HEAP_TYPE_DEFAULT};

    for (int i = 0; i < 2; i++) {
        ThrowIfFailed(device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s_bloomBuffers[i])));
    }

    // Heaps
    D3D12_DESCRIPTOR_HEAP_DESC hDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    for (int i = 0; i < 4; i++) {
        ThrowIfFailed(device->CreateDescriptorHeap(&hDesc, IID_PPV_ARGS(&s_bloomHeaps[i])));
    }

    UINT dSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    // Heap 0: Extract (InputHDR -> Bloom0)
    auto cpu0 = s_bloomHeaps[0]->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(inputHdr, nullptr, cpu0);
    cpu0.ptr += dSize;
    device->CreateUnorderedAccessView(s_bloomBuffers[0].Get(), nullptr, nullptr, cpu0);

    // Heap 1: BlurH (Bloom0 -> Bloom1)
    auto cpu1 = s_bloomHeaps[1]->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(s_bloomBuffers[0].Get(), nullptr, cpu1);
    cpu1.ptr += dSize;
    device->CreateUnorderedAccessView(s_bloomBuffers[1].Get(), nullptr, nullptr, cpu1);

    // Heap 2: BlurV (Bloom1 -> Bloom0)
    auto cpu2 = s_bloomHeaps[2]->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(s_bloomBuffers[1].Get(), nullptr, cpu2);
    cpu2.ptr += dSize;
    device->CreateUnorderedAccessView(s_bloomBuffers[0].Get(), nullptr, nullptr, cpu2);
  }

  cmdList->SetComputeRootSignature(s_bloomRootSig.Get());

  // 1. Extract
  TransitionResource(cmdList, inputHdr, s_hdrState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  cmdList->SetPipelineState(s_bloomExtractPSO.Get());
  cmdList->SetDescriptorHeaps(1, s_bloomHeaps[0].GetAddressOf());
  struct { float t; float i; float p[2]; } params = { 1.0f, 0.5f, {0,0} };
  cmdList->SetComputeRoot32BitConstants(0, 4, &params, 0);
  cmdList->SetComputeRootDescriptorTable(1, s_bloomHeaps[0]->GetGPUDescriptorHandleForHeapStart());
  D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = s_bloomHeaps[0]->GetGPUDescriptorHandleForHeapStart();
  uavGpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetComputeRootDescriptorTable(2, uavGpu);
  cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  TransitionResource(cmdList, s_bloomBuffers[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  // 2. Blur H
  cmdList->SetPipelineState(s_blurPSO.Get());
  cmdList->SetDescriptorHeaps(1, s_bloomHeaps[1].GetAddressOf());
  struct { uint32_t hor; uint32_t w; uint32_t h; float p; } bparams = { 1, width, height, 0 };
  cmdList->SetComputeRoot32BitConstants(0, 4, &bparams, 0);
  cmdList->SetComputeRootDescriptorTable(1, s_bloomHeaps[1]->GetGPUDescriptorHandleForHeapStart());
  uavGpu = s_bloomHeaps[1]->GetGPUDescriptorHandleForHeapStart();
  uavGpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetComputeRootDescriptorTable(2, uavGpu);
  cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  TransitionResource(cmdList, s_bloomBuffers[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  TransitionResource(cmdList, s_bloomBuffers[1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  // 3. Blur V
  cmdList->SetDescriptorHeaps(1, s_bloomHeaps[2].GetAddressOf());
  bparams.hor = 0;
  cmdList->SetComputeRoot32BitConstants(0, 4, &bparams, 0);
  cmdList->SetComputeRootDescriptorTable(1, s_bloomHeaps[2]->GetGPUDescriptorHandleForHeapStart());
  uavGpu = s_bloomHeaps[2]->GetGPUDescriptorHandleForHeapStart();
  uavGpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetComputeRootDescriptorTable(2, uavGpu);
  cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  TransitionResource(cmdList, s_bloomBuffers[1].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  TransitionResource(cmdList, inputHdr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, s_hdrState);
}

} // namespace RasterRenderer
