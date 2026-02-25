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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

// Access to a few global symbols from main.cpp
extern ComPtr<ID3D12RootSignature> g_rootSignature;

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
static ComPtr<ID3D12DescriptorHeap> s_hdrRtvHeap;
static UINT s_hdrWidth = 0;
static UINT s_hdrHeight = 0;
static D3D12_RESOURCE_STATES s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

static ComPtr<ID3D12Resource> s_tonemapOutput;
static D3D12_RESOURCE_STATES s_tonemapOutputState =
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
static ComPtr<ID3D12RootSignature> s_tonemapRootSig;
static ComPtr<ID3D12PipelineState> s_tonemapPSO;
static ComPtr<ID3D12Resource> s_tonemapCB;
static ComPtr<ID3D12DescriptorHeap> s_tonemapHeap;

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

static std::wstring FindShaderFileLocal(const wchar_t *relativePath) {
  std::vector<std::wstring> searchPaths;
  searchPaths.push_back(relativePath);
  searchPaths.push_back(std::wstring(L"..\\..\\") + relativePath);
  searchPaths.push_back(std::wstring(L"..\\") + relativePath);
  for (auto &p : searchPaths)
    if (std::filesystem::exists(p))
      return p;
  return relativePath;
}

namespace RasterRenderer {

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
    meshPsoDesc.NumRenderTargets = 1;
    meshPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    meshPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    meshPsoDesc.SampleDesc.Count = 1;

    if (rootSig)
      meshPsoDesc.pRootSignature = rootSig;

    ComPtr<ID3D12PipelineState> newMeshPSO;
    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &meshPsoDesc, IID_PPV_ARGS(&newMeshPSO)));
    g_meshPipelineState = newMeshPSO;

    // Depth-only PSO (same as mesh but no color writes)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = meshPsoDesc;
    depthPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        0;                             // Disable color output
    depthPsoDesc.NumRenderTargets = 0; // No render targets bound for depth-only
    depthPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    depthPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    ComPtr<ID3D12PipelineState> newDepthPSO;
    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &depthPsoDesc, IID_PPV_ARGS(&newDepthPSO)));
    g_depthOnlyPipelineState = newDepthPSO;

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

struct TonemapConstants {
  uint32_t outWidth;
  uint32_t outHeight;
  float exposure;
  float _pad;
};

static bool EnsureTonemapPipeline(ID3D12Device *device) {
  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapCB && s_tonemapHeap)
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
  heapDesc.NumDescriptors = 2;
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
  hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_HEAP_PROPERTIES defHeap{};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_CLEAR_VALUE clearValue{};
  clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  clearValue.Color[0] = 0.0f;
  clearValue.Color[1] = 0.0f;
  clearValue.Color[2] = 0.0f;
  clearValue.Color[3] = 1.0f;
  ThrowIfFailed(device->CreateCommittedResource(
      &defHeap, D3D12_HEAP_FLAG_NONE, &hdrDesc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
      IID_PPV_ARGS(&s_hdrColor)));
  s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.NumDescriptors = 1;
  ThrowIfFailed(
      device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&s_hdrRtvHeap)));
  device->CreateRenderTargetView(s_hdrColor.Get(), nullptr,
                                 s_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart());

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

  return EnsureTonemapPipeline(device) && EnsureAvgLumPipeline(device);
}

bool PrepareHdrRenderTarget(ID3D12Device *device,
                            ID3D12GraphicsCommandList *cmdList, UINT width,
                            UINT height,
                            D3D12_CPU_DESCRIPTOR_HANDLE *outRtv) {
  if (!EnsureHdrResources(device, width, height))
    return false;
  TransitionResource(cmdList, s_hdrColor.Get(), s_hdrState,
                     D3D12_RESOURCE_STATE_RENDER_TARGET);
  s_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  if (outRtv)
    *outRtv = s_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
  return true;
}

float GetCurrentAvgLuminance() { return s_avgLuminanceCdM2; }
float GetCurrentEV100() { return s_lastEV100; }

bool TonemapHdrToBackbuffer(ID3D12Device *device,
                            ID3D12GraphicsCommandList *cmdList,
                            ID3D12Resource *backbuffer, UINT width,
                            UINT height) {
  if (!EnsureHdrResources(device, width, height) || !backbuffer)
    return false;

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
    targetExposure = (std::clamp)(targetExposure, 1e-20f, 1e10f);
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
  if (SUCCEEDED(s_tonemapCB->Map(0, nullptr, &p))) {
    memcpy(p, &tc, sizeof(tc));
    s_tonemapCB->Unmap(0, nullptr);
  }

  D3D12_CPU_DESCRIPTOR_HANDLE tmCpu =
      s_tonemapHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC tmSrv{};
  tmSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  tmSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  tmSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  tmSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(s_hdrColor.Get(), &tmSrv, tmCpu);

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
  tmGpu.ptr += descSize;
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

} // namespace RasterRenderer
