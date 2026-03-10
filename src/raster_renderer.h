#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "assets/asset_loader.h"

namespace Scene { struct Instance; }

using Microsoft::WRL::ComPtr;

namespace RasterRenderer {
  // Raster module API
  void CreateGridResources(ID3D12Device* device, float gridThickness);
  void RecreateMeshPipeline(ID3D12Device* device, ID3D12RootSignature* rootSig);
  void DrawGrid(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB);
  void DrawSkybox(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB);
  void DrawSceneDepthOnly(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, const std::vector<Scene::Instance>& instances);
  
  void CreateShadowResources(ID3D12Device* device);
  void DrawShadowMap(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, const std::vector<Scene::Instance>& instances);
  D3D12_GPU_DESCRIPTOR_HANDLE GetShadowMapSrv();
  D3D12_CPU_DESCRIPTOR_HANDLE GetShadowMapSrvCpu();
  bool PrepareHdrRenderTarget(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                              UINT width, UINT height,
                              D3D12_CPU_DESCRIPTOR_HANDLE dsv);
  void BindHdrRenderTarget(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
                           D3D12_CPU_DESCRIPTOR_HANDLE dsv);
  bool TonemapHdrToBackbuffer(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
                              ID3D12Resource *backbuffer, UINT width,
                              UINT height, ID3D12Resource *cameraCB,
                              ID3D12Resource *depthBuffer);
  float GetCurrentAvgLuminance();
  float GetCurrentEV100();
  
  void RunSSR(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, ID3D12Resource* depthBuffer);
  void RunSSAO(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cameraCB, ID3D12Resource* depthBuffer);

  // Expose some resources so main can inspect them (if necessary)
  extern ComPtr<ID3D12Resource> g_gridVertexBuffer;
  extern D3D12_VERTEX_BUFFER_VIEW g_gridVBView;
  extern UINT g_gridVertexCount;
  extern ComPtr<ID3D12PipelineState> g_gridPipelineState;
  extern ComPtr<ID3D12PipelineState> g_meshPipelineState;
  extern ComPtr<ID3D12PipelineState> g_skyboxPipelineState;
  extern ComPtr<ID3D12PipelineState> g_depthOnlyPipelineState;
  extern ComPtr<ID3D12PipelineState> g_shadowPipelineState;
}
