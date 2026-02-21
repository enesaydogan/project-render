#define NOMINMAX
#include "raster_renderer.h"
#include "clouds.h"
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxc_wrapper.h"
#include "ibl_manager.h"
#include "scene.h"
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
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
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
    meshPsoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
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
    const auto &gm = inst.mesh;
    if (!gm.vertexBuffer || !gm.indexBuffer)
      continue;

    // Set World Matrix (Parameter index 3, register b2)
    cmdList->SetGraphicsRoot32BitConstants(3, 16, inst.transform, 0);

    cmdList->IASetVertexBuffers(0, 1, &gm.vbView);
    cmdList->IASetIndexBuffer(&gm.ibView);
    cmdList->DrawIndexedInstanced(gm.indexCount, 1, 0, 0, 0);
  }
}

} // namespace RasterRenderer
