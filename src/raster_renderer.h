#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace RasterRenderer {
  // Raster module API
  void CreateGridResources(ID3D12Device* device, float gridThickness);
  void CreateDebugTriangleResources(ID3D12Device* device);
  void RecreateMeshPipeline(ID3D12Device* device, ID3D12RootSignature* rootSig);
  void DrawGrid(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB);
  void DrawDebugTriangle(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB);

  // Expose some resources so main can inspect them (if necessary)
  extern ComPtr<ID3D12Resource> g_gridVertexBuffer;
  extern D3D12_VERTEX_BUFFER_VIEW g_gridVBView;
  extern UINT g_gridVertexCount;
  extern ComPtr<ID3D12Resource> g_debugVertexBuffer;
  extern D3D12_VERTEX_BUFFER_VIEW g_debugVBView;
  extern UINT g_debugVertexCount;
  extern ComPtr<ID3D12PipelineState> g_gridPipelineState;
  extern ComPtr<ID3D12PipelineState> g_meshPipelineState;
}
