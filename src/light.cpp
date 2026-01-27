#include "light.h"
#include "assets/asset_loader.h"
#include "d3d12_helpers.h"
#include <wrl.h>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Externals from main.cpp
extern ComPtr<ID3D12Device> g_device;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;

DirectionalLight g_defaultLight = {{0.5f, 0.5f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};

void AddDefaultPlane() {
    try {
        // plane 10x10 centered at origin on XZ plane (Y up)
        const float half = 5.0f;
        Asset::Vertex verts[4] = {
            {{-half, 0.0f, -half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {0.0f, 0.0f}},
            {{ half, 0.0f, -half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {1.0f, 0.0f}},
            {{ half, 0.0f,  half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {1.0f, 1.0f}},
            {{-half, 0.0f,  half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {0.0f, 1.0f}}
        };
        UINT indices[6] = {0,1,2, 0,2,3};

        // Create upload vertex buffer
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = sizeof(verts);
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Asset::GpuMesh gm;
        ThrowIfFailed(g_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gm.vertexBuffer)));
        UINT8 *pData = nullptr; D3D12_RANGE readRange = {0,0};
        ThrowIfFailed(gm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, verts, sizeof(verts));
        gm.vertexBuffer->Unmap(0, nullptr);

        gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
        gm.vbView.StrideInBytes = sizeof(Asset::Vertex);
        gm.vbView.SizeInBytes = sizeof(verts);

        // Index buffer
        D3D12_RESOURCE_DESC ibDesc = vbDesc; ibDesc.Width = sizeof(indices);
        ThrowIfFailed(g_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gm.indexBuffer)));
        pData = nullptr;
        ThrowIfFailed(gm.indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
        memcpy(pData, indices, sizeof(indices));
        gm.indexBuffer->Unmap(0, nullptr);

        gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
        gm.ibView.Format = DXGI_FORMAT_R32_UINT;
        gm.ibView.SizeInBytes = sizeof(indices);

        gm.vertexCount = 4;
        gm.indexCount = 6;

        // Default material
        Asset::Material mat;
        mat.baseColorFactor[0] = 0.8f; mat.baseColorFactor[1] = 0.8f; mat.baseColorFactor[2] = 0.8f; mat.baseColorFactor[3] = 1.0f;
        mat.metallicFactor = 0.0f; mat.roughnessFactor = 1.0f; mat.workflow = 0;
        mat.baseColorTexture = -1; mat.metallicRoughnessTexture = -1; mat.normalTexture = -1; mat.occlusionTexture = -1; mat.emissiveTexture = -1;

        int matIndex = (int)g_loadedMaterials.size();
        g_loadedMaterials.push_back(mat);
        gm.materialIndex = matIndex;

        g_loadedMeshes.push_back(gm);
        fprintf(stderr, "AddDefaultPlane: added plane (10x10) with material %d\n", matIndex);
    } catch (const std::exception &e) {
        fprintf(stderr, "AddDefaultPlane: exception: %s\n", e.what());
    }
}
