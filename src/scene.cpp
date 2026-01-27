#include "scene.h"
#include "assets/asset_loader.h"
#include "file_import.h"
#include "imgui.h"
#include "dxr_renderer.h"
#include "d3d12_helpers.h" // for DescriptorAllocation
#include <wrl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Externals from main.cpp (global symbols)
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;
extern UINT g_textureDescriptorCount;
extern D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart;
extern DescriptorHeapAllocator g_cbvSrvAllocator;
extern ComPtr<ID3D12Device> g_device;

namespace Scene {

static std::vector<Node> s_nodes;
static std::string s_lastStatus;

const std::string& LastStatus() { return s_lastStatus; }

bool ImportGltf(const std::string &utf8path) {
    try {
        fprintf(stderr, "Scene::ImportGltf: importing %s\n", utf8path.c_str());
        std::vector<Asset::GpuMesh> meshes;
        std::vector<Asset::Material> materials;
        std::vector<Asset::Texture> textures;
        bool ok = Asset::LoadGltf(utf8path, meshes, &materials, &textures);
        if (!ok) {
            s_lastStatus = std::string("Load failed: ") + utf8path;
            fprintf(stderr, "%s\n", s_lastStatus.c_str());
            return false;
        }

        size_t meshBase = g_loadedMeshes.size();
        size_t materialBase = g_loadedMaterials.size();
        size_t textureBase = g_loadedTextures.size();

        g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(), meshes.end());
        g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(), materials.end());
        g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(), textures.end());

        // Adjust newly-inserted meshes' material indices to global material base
        for (size_t i = 0; i < meshes.size(); ++i) {
            int &mi = g_loadedMeshes[meshBase + i].materialIndex;
            if (mi >= 0) mi = mi + (int)materialBase;
        }

        // Adjust newly-inserted materials to reference global texture indices
        for (size_t i = 0; i < materials.size(); ++i) {
            Asset::Material &m = g_loadedMaterials[materialBase + i];
            if (m.baseColorTexture >= 0) m.baseColorTexture += (int)textureBase;
            if (m.metallicRoughnessTexture >= 0) m.metallicRoughnessTexture += (int)textureBase;
            if (m.normalTexture >= 0) m.normalTexture += (int)textureBase;
            if (m.occlusionTexture >= 0) m.occlusionTexture += (int)textureBase;
            if (m.emissiveTexture >= 0) m.emissiveTexture += (int)textureBase;
        }

        // Allocate SRV descriptors for new textures - using persistent allocation
        if (!textures.empty()) {
            DescriptorAllocation alloc = g_cbvSrvAllocator.AllocatePersistent((UINT)textures.size());
            if (g_textureDescriptorCount == 0) g_texturesGpuStart = alloc.gpu;
            for (size_t i = 0; i < textures.size(); ++i) {
                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = alloc.cpu;
                cpuHandle.ptr += i * g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                const Asset::Texture &tex = textures[i];
                if (tex.resource) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Format = tex.format;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = tex.mipLevels;
                    g_device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpuHandle);
                }
            }
            g_textureDescriptorCount += (UINT)textures.size();
        }

        // Create a scene node for this import
        Node node;
        node.name = std::filesystem::path(utf8path).filename().string();
        for (size_t i = 0; i < meshes.size(); ++i) node.meshIndices.push_back(meshBase + i);
        s_nodes.push_back(node);

        s_lastStatus = std::string("Loaded: ") + utf8path;
        fprintf(stderr, "%s\n", s_lastStatus.c_str());

        // Rebuild AS for current active meshes
        RebuildAccelerationStructures();
        // Recreate DXR pipeline so it can merge texture descriptors (if any)
        DxrRenderer::CreateRayTracingPipeline(0, 0);
        return true;
    } catch (const std::exception &e) {
        s_lastStatus = std::string("Import exception: ") + e.what();
        fprintf(stderr, "%s\n", s_lastStatus.c_str());
        return false;
    }
}

bool ImportGltfWithDialog(HWND hwnd) {
    std::wstring chosen;
    if (OpenGltfFileDialog(hwnd, chosen)) {
        if (chosen.empty()) { s_lastStatus = "No file chosen"; return false; }
        // convert wstring -> utf8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, chosen.c_str(), (int)chosen.size(), NULL, 0, NULL, NULL);
        std::string utf8path(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, chosen.c_str(), (int)chosen.size(), &utf8path[0], size_needed, NULL, NULL);
        return ImportGltf(utf8path);
    }
    s_lastStatus = "Open cancelled";
    return false;
}

const std::vector<Node>& GetNodes() { return s_nodes; }

void SelectNode(size_t index) {
    if (index >= s_nodes.size()) return;
    for (size_t i = 0; i < s_nodes.size(); ++i) s_nodes[i].selected = false;
    s_nodes[index].selected = true;
}

void DeleteNode(size_t index) {
    if (index >= s_nodes.size()) return;
    // Mark meshes as empty by clearing their vertex/index resources
    for (size_t mi : s_nodes[index].meshIndices) {
        if (mi < g_loadedMeshes.size()) {
            g_loadedMeshes[mi].vertexBuffer.Reset();
            g_loadedMeshes[mi].indexBuffer.Reset();
            // mark counts zero
            g_loadedMeshes[mi].vertexCount = 0;
            g_loadedMeshes[mi].indexCount = 0;
        }
    }
    s_nodes.erase(s_nodes.begin() + index);

    // Rebuild AS after deletion
    RebuildAccelerationStructures();
}

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
        // Use CCW winding that points UP (0-3-2 and 0-2-1)
        UINT indices[6] = {0, 3, 2, 0, 2, 1};

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

        size_t meshIndex = g_loadedMeshes.size();
        g_loadedMeshes.push_back(gm);

        // Create a node for the plane
        Node node;
        node.name = "Ground Plane";
        node.meshIndices.push_back(meshIndex);
        s_nodes.push_back(node);

        fprintf(stderr, "AddDefaultPlane: added plane (10x10) with material %d\n", matIndex);
        
        // Rebuild AS
        RebuildAccelerationStructures();
    } catch (const std::exception &e) {
        fprintf(stderr, "AddDefaultPlane: exception: %s\n", e.what());
    }
}

void RebuildAccelerationStructures() {
    DxrRenderer::BuildAccelerationStructures(GetActiveMeshes());
}

std::vector<Asset::GpuMesh> GetActiveMeshes() {
    std::vector<Asset::GpuMesh> active;
    for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
        const auto &m = g_loadedMeshes[i];
        if (m.vertexBuffer && m.indexBuffer && m.vertexCount > 0 && m.indexCount > 0) active.push_back(m);
    }
    return active;
}

void DrawScenePanel(HWND hwnd, bool &visible) {
    if (!visible) return;
    if (ImGui::Begin("Scene", &visible)) {
        ImGui::Columns(2, "scene_cols", false);
        if (ImGui::Button("Import GLB...")) {
            ImportGltfWithDialog(hwnd);
        }
        ImGui::NextColumn();

        // Scene outline
        ImGui::Text("Hierarchy");
        ImGui::Separator();
        for (size_t i = 0; i < s_nodes.size(); ++i) {
            ImGui::PushID((int)i);
            bool selected = s_nodes[i].selected;
            if (ImGui::Selectable(s_nodes[i].name.c_str(), selected)) {
                SelectNode(i);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                DeleteNode(i);
                ImGui::PopID();
                break; // indices changed
            }
            ImGui::PopID();
        }
        ImGui::Columns(1);

        if (!s_lastStatus.empty()) ImGui::TextWrapped("Status: %s", s_lastStatus.c_str());

    }
    ImGui::End();
}

} // namespace Scene
