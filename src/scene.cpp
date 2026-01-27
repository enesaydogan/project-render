#define NOMINMAX
#include "scene.h"
#include "assets/asset_loader.h"
#include "file_import.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "dxr_renderer.h"
#include "d3d12_helpers.h"
#include "camera.h"
#include <wrl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <algorithm>

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
static ImGuizmo::OPERATION g_currentGizmoOp = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE g_currentGizmoMode = ImGuizmo::WORLD;

// Helper: Simple matrix math for ImGuizmo
void BuildViewMatrix(float* mat) {
    float pos[3] = {g_cameraData.pos[0], g_cameraData.pos[1], g_cameraData.pos[2]};
    float fwd[3] = {g_cameraData.forward[0], g_cameraData.forward[1], g_cameraData.forward[2]};
    float up_in[3] = {0, 1, 0}; // Use world up as reference

    // R = F x U (as in shader)
    float R[3] = {
        fwd[1]*up_in[2] - fwd[2]*up_in[1],
        fwd[2]*up_in[0] - fwd[0]*up_in[2],
        fwd[0]*up_in[1] - fwd[1]*up_in[0]
    };
    float rlen = sqrtf(R[0]*R[0] + R[1]*R[1] + R[2]*R[2]);
    if (rlen > 0) { R[0]/=rlen; R[1]/=rlen; R[2]/=rlen; }

    // U = R x F
    float U[3] = {
        R[1]*fwd[2] - R[2]*fwd[1],
        R[2]*fwd[0] - R[0]*fwd[2],
        R[0]*fwd[1] - R[1]*fwd[0]
    };
    float ulen = sqrtf(U[0]*U[0] + U[1]*U[1] + U[2]*U[2]);
    if (ulen > 0) { U[0]/=ulen; U[1]/=ulen; U[2]/=ulen; }

    memset(mat, 0, 16*sizeof(float));
    // Column 0
    mat[0] = R[0]; mat[1] = U[0]; mat[2] = fwd[0];
    // Column 1
    mat[4] = R[1]; mat[5] = U[1]; mat[6] = fwd[1];
    // Column 2
    mat[8] = R[2]; mat[9] = U[2]; mat[10] = fwd[2];
    // Column 3 (Trans)
    mat[12] = -(R[0]*pos[0] + R[1]*pos[1] + R[2]*pos[2]);
    mat[13] = -(U[0]*pos[0] + U[1]*pos[1] + U[2]*pos[2]);
    mat[14] = -(fwd[0]*pos[0] + fwd[1]*pos[1] + fwd[2]*pos[2]);
    mat[15] = 1.0f;
}

void BuildProjectionMatrix(float* mat) {
    float fovRad = g_cameraData.fov * 3.14159265359f / 180.0f;
    float aspect = g_cameraData.aspect;
    float n = g_cameraData.nearZ;
    float f = g_cameraData.farZ;
    float focalScale = 1.0f / tanf(fovRad * 0.5f);
    
    memset(mat, 0, 16*sizeof(float));
    mat[0] = focalScale / aspect;
    mat[5] = focalScale;
    mat[10] = f / (f - n);
    mat[11] = 1.0f;
    mat[14] = -(f * n) / (f - n);
}

static std::string s_lastStatus;

Node::Node() {
    name = "New Node";
    // Identity matrix
    for (int i = 0; i < 16; ++i) transform[i] = 0.0f;
    transform[0] = transform[5] = transform[10] = transform[15] = 1.0f;
    selected = false;
    visible = true;
}

const std::string& LastStatus() { return s_lastStatus; }

bool ImportGltf(const std::string &utf8path, const float* rootTranslation) {
    try {
        fprintf(stderr, "Scene::ImportGltf: importing %s\n", utf8path.c_str());
        std::vector<Asset::GpuMesh> meshes;
        std::vector<Asset::Material> materials;
        std::vector<Asset::Texture> textures;
        bool ok = Asset::LoadGltf(utf8path, meshes, &materials, &textures, rootTranslation);
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

void AddDefaultPlane(float offset_y) {
    try {
        // plane 10x10 centered at origin on XZ plane (Y up)
        const float half = 5.0f;
        Asset::Vertex verts[4] = {
            {{-half, offset_y, -half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {0.0f, 0.0f}},
            {{ half, offset_y, -half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {1.0f, 0.0f}},
            {{ half, offset_y,  half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {1.0f, 1.0f}},
            {{-half, offset_y,  half}, {0.0f,1.0f,0.0f}, {1,0,0,1}, {0.0f, 1.0f}}
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

        gm.minBound[0] = -half; gm.minBound[1] = offset_y; gm.minBound[2] = -half;
        gm.maxBound[0] = half; gm.maxBound[1] = offset_y; gm.maxBound[2] = half;

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
    DxrRenderer::BuildAccelerationStructures(GetActiveMeshes(), GetInstances());
}

std::vector<Asset::GpuMesh> GetActiveMeshes() {
    std::vector<Asset::GpuMesh> active;
    for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
        const auto &m = g_loadedMeshes[i];
        if (m.vertexBuffer && m.indexBuffer && m.vertexCount > 0 && m.indexCount > 0) active.push_back(m);
    }
    return active;
}

std::vector<Instance> GetInstances() {
    std::vector<Instance> instances;
    for (size_t ni = 0; ni < s_nodes.size(); ++ni) {
        const auto &node = s_nodes[ni];
        if (!node.visible) continue;
        for (size_t mi : node.meshIndices) {
            if (mi < g_loadedMeshes.size()) {
                Instance inst;
                inst.mesh = g_loadedMeshes[mi];
                inst.transform = node.transform;
                inst.nodeIndex = ni;
                instances.push_back(inst);
            }
        }
    }
    return instances;
}

void MatMul(const float* a, const float* b, float* out) {
    float tmp[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            tmp[col * 4 + row] = sum;
        }
    }
    memcpy(out, tmp, 16 * sizeof(float));
}

void DrawGizmo() {
    size_t selectedIdx = (size_t)-1;
    for (size_t i = 0; i < s_nodes.size(); ++i) {
        if (s_nodes[i].selected) {
            selectedIdx = i;
            break;
        }
    }

    ImGuizmo::BeginFrame();
    if (selectedIdx == (size_t)-1) return;
    auto& node = s_nodes[selectedIdx];

    float view[16], proj[16];
    BuildViewMatrix(view);
    BuildProjectionMatrix(proj);

    if (ImGui::IsKeyPressed(ImGuiKey_M)) g_currentGizmoOp = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_R)) g_currentGizmoOp = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_T)) g_currentGizmoOp = ImGuizmo::SCALE;
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        g_currentGizmoMode = (g_currentGizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }

    // Scaling is almost always performed in Local space
    ImGuizmo::MODE actualMode = (g_currentGizmoOp == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : g_currentGizmoMode;
    // Default to WORLD for Rotation if implied by user request, but respect the toggle
    // If the user feels it "looks weird", ensuring AxisFlip is off can help stability
    ImGuizmo::AllowAxisFlip(false);

    // Make gizmo lines thicker for easier clicking
    ImGuizmo::GetStyle().TranslationLineThickness = 6.0f;
    ImGuizmo::GetStyle().RotationLineThickness = 6.0f;

    ImGuizmo::SetID((int)selectedIdx);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    
    float windowWidth = (float)ImGui::GetIO().DisplaySize.x;
    float windowHeight = (float)ImGui::GetIO().DisplaySize.y;
    ImGuizmo::SetRect(0, 0, windowWidth, windowHeight);

    // Compute mesh local center to position gizmo at center of the object
    float localCenter[3] = {0,0,0};
    int count = 0;
    for (size_t mi : node.meshIndices) {
        if (mi < g_loadedMeshes.size()) {
            const auto& m = g_loadedMeshes[mi];
            for (int a=0; a<3; ++a) localCenter[a] += (m.minBound[a] + m.maxBound[a]) * 0.5f;
            count++;
        }
    }
    if (count > 0) { localCenter[0] /= count; localCenter[1] /= count; localCenter[2] /= count; }

    float pivotMatrix[16];
    memcpy(pivotMatrix, node.transform, 16 * sizeof(float));
    float translationMat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, localCenter[0], localCenter[1], localCenter[2], 1 };
    MatMul(pivotMatrix, translationMat, pivotMatrix);

    // Adapt matrices for ImGuizmo (GL-style: Look=-Z) to fix sorting/interaction flipped feel
    float viewImGuizmo[16];
    memcpy(viewImGuizmo, view, 16 * sizeof(float));
    // Flip Z-axis of View Matrix (Row 2 in math, which is indices 2,6,10,14 in Column-Major)
    // view[2,6,10] is the Z basis vector. view[14] is Z translation.
    viewImGuizmo[2] *= -1.0f;
    viewImGuizmo[6] *= -1.0f;
    viewImGuizmo[10] *= -1.0f;
    viewImGuizmo[14] *= -1.0f;

    // Build standard GL Perspective Projection (W = -Z)
    float projImGuizmo[16];
    memset(projImGuizmo, 0, 16*sizeof(float));
    float fovRad = g_cameraData.fov * 3.14159265359f / 180.0f;
    float aspect = g_cameraData.aspect;
    float n = g_cameraData.nearZ;
    float f = g_cameraData.farZ;
    float t = tanf(fovRad * 0.5f);
    
    // 1/tan(fov/2) / aspect, 0, 0, 0
    // 0, 1/tan(fov/2), 0, 0
    // 0, 0, -(f+n)/(f-n), -1
    // 0, 0, -2fn/(f-n), 0
    
    projImGuizmo[0] = 1.0f / (aspect * t);
    projImGuizmo[5] = 1.0f / t;
    projImGuizmo[10] = -(f + n) / (f - n); // GL Z remapping
    projImGuizmo[11] = -1.0f;              // W = -Z
    projImGuizmo[14] = -(2.0f * f * n) / (f - n);

    if (ImGuizmo::Manipulate(viewImGuizmo, projImGuizmo, g_currentGizmoOp, actualMode, pivotMatrix)) {
        // NodeTransform = pivotMatrix * Translation(-localCenter)
        float invTranslationMat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, -localCenter[0], -localCenter[1], -localCenter[2], 1 };
        MatMul(pivotMatrix, invTranslationMat, node.transform);
    }
}

// Simple Ray-AABB intersection for node selection
static bool RayAABBIntersection(const float* rayOrigin, const float* rayDir, const float* minP, const float* maxP, float& t) {
    float tmin = -FLT_MAX, tmax = FLT_MAX;
    for (int i = 0; i < 3; ++i) {
        if (abs(rayDir[i]) < 1e-6f) {
            if (rayOrigin[i] < minP[i] || rayOrigin[i] > maxP[i]) return false;
        } else {
            float invD = 1.0f / rayDir[i];
            float t1 = (minP[i] - rayOrigin[i]) * invD;
            float t2 = (maxP[i] - rayOrigin[i]) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
        }
    }
    t = tmin;
    return tmax >= std::max(0.0f, tmin);
}

void MatMul(const float* a, const float* b, float* out);

// Helper: Inverse 4x4 matrix (simplified, assumes affine/orthonormal part)
bool Inverse4x4(const float* m, float* out) {
    float inv[16];
    float det;
    int i;

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) return false;

    det = 1.0f / det;
    for (i = 0; i < 16; i++) out[i] = inv[i] * det;
    return true;
}

void UpdateSelection(float screenWidth, float screenHeight) {
    if (ImGuizmo::IsOver() || ImGuizmo::IsUsing() || ImGui::IsAnyItemHovered()) return;

    float view[16], proj[16];
    BuildViewMatrix(view);
    BuildProjectionMatrix(proj);

    // Use ImGui mouse position for better synchronization with UI and Gizmo
    ImVec2 mpos = ImGui::GetIO().MousePos;
    // NDC [-1, 1]
    float mox = (mpos.x / screenWidth) * 2.0f - 1.0f;
    float moy = 1.0f - (mpos.y / screenHeight) * 2.0f;

    // Ray construction
    float vx = mox / proj[0];
    float vy = moy / proj[5];
    float vz = 1.0f;

    // View to World Ray Direction
    float dir[3] = {
        vx * view[0] + vy * view[1] + vz * view[2],
        vx * view[4] + vy * view[5] + vz * view[6],
        vx * view[8] + vy * view[9] + vz * view[10]
    };
    float orig[3] = { g_cameraData.pos[0], g_cameraData.pos[1], g_cameraData.pos[2] };

    float minT = 1e30f;
    int hitNode = -1;

    for (size_t i = 0; i < s_nodes.size(); ++i) {
        auto& node = s_nodes[i];
        if (!node.visible) continue;

        float invNode[16];
        if (!Inverse4x4(node.transform, invNode)) continue;

        // Transform ray to local space
        float localOrig[3] = {
            orig[0] * invNode[0] + orig[1] * invNode[4] + orig[2] * invNode[8] + invNode[12],
            orig[0] * invNode[1] + orig[1] * invNode[5] + orig[2] * invNode[9] + invNode[13],
            orig[0] * invNode[2] + orig[1] * invNode[6] + orig[2] * invNode[10] + invNode[14]
        };
        float localDir[3] = {
            dir[0] * invNode[0] + dir[1] * invNode[4] + dir[2] * invNode[8],
            dir[0] * invNode[1] + dir[1] * invNode[5] + dir[2] * invNode[9],
            dir[0] * invNode[2] + dir[1] * invNode[6] + dir[2] * invNode[10]
        };

        for (size_t mIdx : node.meshIndices) {
            if (mIdx >= g_loadedMeshes.size()) continue;
            const auto& mesh = g_loadedMeshes[mIdx];
            
            float tmin = 0.001f, tmax = 1e30f;
            for (int a = 0; a < 3; ++a) {
                float invD = 1.0f / (localDir[a] != 0.0f ? localDir[a] : 1e-9f);
                float t0 = (mesh.minBound[a] - localOrig[a]) * invD;
                float t1 = (mesh.maxBound[a] - localOrig[a]) * invD;
                if (invD < 0.0f) std::swap(t0, t1);
                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
            }

            if (tmax >= tmin && tmin < minT) {
                minT = tmin;
                hitNode = (int)i;
            }
        }
    }

    if (hitNode != -1) {
        for (auto& n : s_nodes) n.selected = false;
        s_nodes[hitNode].selected = true;
    }
}

void DrawScenePanel(HWND hwnd, bool &visible) {
    if (!visible) return;
    if (ImGui::Begin("Scene", &visible)) {
        // Action area
        if (ImGui::Button("Import GLB...", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
            ImportGltfWithDialog(hwnd);
        }
        ImGui::SameLine();
        const char* spaceNames[] = { "Local", "World" };
        int currentSpace = (g_currentGizmoMode == ImGuizmo::WORLD) ? 1 : 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##Space", &currentSpace, spaceNames, 2)) {
            g_currentGizmoMode = (currentSpace == 1) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }

        ImGui::Separator();
        ImGui::Text("Hierarchy");

        // Use a child for the list area to allow scrolling independently of the header/footer
        if (ImGui::BeginChild("HierarchyRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), true)) {
            if (ImGui::BeginTable("HierarchyTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                // ImGui::TableHeadersRow();

                for (size_t i = 0; i < s_nodes.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    
                    ImGui::PushID((int)i);
                    bool selected = s_nodes[i].selected;
                    if (ImGui::Selectable(s_nodes[i].name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                        SelectNode(i);
                    }

                    ImGui::TableSetColumnIndex(1);
                    // Minimalist red button for deletion
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                    if (ImGui::Button("Delete", ImVec2(-FLT_MIN, 0))) {
                        DeleteNode(i);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        ImGui::EndTable();
                        ImGui::EndChild();
                        ImGui::End();
                        return; // Refresh state next frame
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        if (!s_lastStatus.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Status:");
            ImGui::TextWrapped("%s", s_lastStatus.c_str());
        }
    }
    ImGui::End();
}

} // namespace Scene
