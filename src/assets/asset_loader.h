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
        float baseColorFactor[4] = {1,1,1,1};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        // Specular-Glossiness workflow
        float specularFactor[3] = {1.0f,1.0f,1.0f};
        float glossinessFactor = 1.0f;
        // 0 = metallic-roughness, 1 = specular-glossiness
        int workflow = 0;
        int baseColorTexture = -1;
        int metallicRoughnessTexture = -1;
        int normalTexture = -1;
        int occlusionTexture = -1;
        int emissiveTexture = -1;
        bool doubleSided = false;
        std::string alphaMode = "OPAQUE";
    };

    // Initialize the loader with a device and command queue for GPU uploads.
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

    // Load a glTF file. Returns true on success. Fills out created meshes in `outMeshes`.
    bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials = nullptr, std::vector<Texture>* outTextures = nullptr, const float* rootTranslation = nullptr);
}
