#pragma once
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include <vector>

// Simple asset loader API
namespace Asset {
    using Microsoft::WRL::ComPtr;

    struct GpuMesh {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vbView{};
        D3D12_INDEX_BUFFER_VIEW ibView{};
        // Counts for convenience
        UINT vertexCount = 0;
        UINT indexCount = 0;
        int materialIndex = -1; // index into materials array if provided

        // Bounding box
        float minBound[3]={0,0,0};
        float maxBound[3]={0,0,0};
    };

    // Interleaved vertex layout used by the loader
    struct Vertex {
        float pos[3];
        float normal[3];
        float tangent[4];
        float uv[2];
    };
    
    struct Texture {
        ComPtr<ID3D12Resource> resource;
        UINT width = 0;
        UINT height = 0;
        UINT mipLevels = 1;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    };
    
    struct Material {
        char name[64] = "Material";
        float diffuseColor[4] = {1,1,1,1};
        float reflectionColor[4] = {0,0,0,1};
        float reflectionGlossiness = 0.8f;
        float refractionColor[4] = {0,0,0,1};
        float refractionGlossiness = 1.0f;
        float ior = 1.6f;
        float emissiveColor[4] = {0,0,0,1};
        
        // V-Ray / Glossiness workflow texture mapping
        int diffuseTexture = -1; // Was baseColor
        int reflectionTexture = -1; // Was metallicRoughness (re-used often) or specular
        int refractionTexture = -1; 
        int normalTexture = -1;
        int emissiveTexture = -1;
        int occlusionTexture = -1;

        bool doubleSided = false;
        std::string alphaMode = "OPAQUE";
    };

    // Initialize the loader with a device and command queue for GPU uploads.
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

    // Load a glTF file. Returns true on success. Fills out created meshes in `outMeshes`.
    bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials = nullptr, std::vector<Texture>* outTextures = nullptr, const float* rootTranslation = nullptr);
}
