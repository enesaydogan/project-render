#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "asset_loader.h"
#include <stdio.h>
#include <windows.h>
#include <vector>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cfloat>
#include <filesystem>
#include <wrl.h>
#include <d3d12.h>
#include <algorithm>
#include <stb_image.h>
#include <tinyexr.h>
#include <cmath>
#include <functional>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#ifdef USE_TINYGLTF
#include <tiny_gltf.h>
#endif

using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "HRESULT 0x%08x\n", static_cast<unsigned>(hr));
        fprintf(stderr, "%s", buf);
        ExitProcess(static_cast<UINT>(hr));
    }
}

namespace Asset {

static ComPtr<ID3D12Device> s_device;
static ComPtr<ID3D12CommandQueue> s_queue;
// Optional progress callback. Signature: progress [0..1], status message
static std::function<void(float, const std::string&)> s_progressCb;

void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    s_device = device;
    s_queue = queue;
}

void SetProgressCallback(ProgressCallback cb)
{
    s_progressCb = cb;
}

void ClearProgressCallback()
{
    s_progressCb = ProgressCallback();
}

// ... rest of the file ... (excluding LoadGltf until later)


static void WaitForQueueIdle(ID3D12CommandQueue* queue)
{
    // Create a temporary fence and wait until GPU has finished executing.
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    const UINT64 fenceValue = 1;
    ThrowIfFailed(queue->Signal(fence.Get(), fenceValue));
    if (fence->GetCompletedValue() < fenceValue) {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, event));
        WaitForSingleObject(event, INFINITE);
    }
    CloseHandle(event);
}

static void ExecuteCommandListAndWait(ID3D12GraphicsCommandList* cmdList)
{
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    s_queue->ExecuteCommandLists(1, lists);
    WaitForQueueIdle(s_queue.Get());
}

inline uint32_t ComputeMipLevels(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return 1;
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

static bool CreateGpuTexture(const void* src, int width, int height, int components, DXGI_FORMAT format, Texture& outTex)
{
    if (!s_device || !src) return false;

    uint32_t mipLevels = ComputeMipLevels(width, height);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = (UINT)width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = (UINT16)mipLevels;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeapProps = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    ThrowIfFailed(s_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outTex.resource)));

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
    std::vector<UINT> numRows(mipLevels);
    std::vector<UINT64> rowPitches(mipLevels);
    UINT64 totalBytes = 0;
    s_device->GetCopyableFootprints(&texDesc, 0, (UINT)mipLevels, 0, footprints.data(), numRows.data(), rowPitches.data(), &totalBytes);

    D3D12_HEAP_PROPERTIES uploadHeapProps = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalBytes;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    ThrowIfFailed(s_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

    void* mapped = nullptr;
    ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mapped));

    int bpp = (format == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
    
    // Copy base level and generate mips in system memory
    std::vector<uint8_t> mipMemory;
    const uint8_t* currentSrc = (const uint8_t*)src;
    int curW = width;
    int curH = height;

    for (uint32_t i = 0; i < mipLevels; ++i) {
        // Copy current level to upload buffer
        for (int y = 0; y < curH; ++y) {
            memcpy((uint8_t*)mapped + footprints[i].Offset + (size_t)y * footprints[i].Footprint.RowPitch, 
                   currentSrc + (size_t)y * curW * bpp, (size_t)curW * bpp);
        }

        if (i + 1 < mipLevels) {
            int nextW = std::max(1, curW >> 1);
            int nextH = std::max(1, curH >> 1);
            std::vector<uint8_t> nextMip(nextW * nextH * bpp);

            if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                const float* s = (const float*)currentSrc;
                float* d = (float*)nextMip.data();
                for (int y = 0; y < nextH; ++y) {
                    for (int x = 0; x < nextW; ++x) {
                        float r=0, g=0, b=0, a=0;
                        for (int sy=0; sy<2; ++sy) {
                            for (int sx=0; sx<2; ++sx) {
                                int sX = std::min(x * 2 + sx, curW - 1);
                                int sY = std::min(y * 2 + sy, curH - 1);
                                const float* p = s + (sY * curW + sX) * 4;
                                r += p[0]; g += p[1]; b += p[2]; a += p[3];
                            }
                        }
                        float* pDst = d + (y * nextW + x) * 4;
                        pDst[0] = r * 0.25f; pDst[1] = g * 0.25f; pDst[2] = b * 0.25f; pDst[3] = a * 0.25f;
                    }
                }
            } else {
                for (int y = 0; y < nextH; ++y) {
                    for (int x = 0; x < nextW; ++x) {
                        int r=0, g=0, b=0, a=0;
                        for (int sy=0; sy<2; ++sy) {
                            for (int sx=0; sx<2; ++sx) {
                                int sX = std::min(x * 2 + sx, curW - 1);
                                int sY = std::min(y * 2 + sy, curH - 1);
                                const uint8_t* p = currentSrc + (sY * curW + sX) * 4;
                                r += p[0]; g += p[1]; b += p[2]; a += p[3];
                            }
                        }
                        uint8_t* pDst = nextMip.data() + (y * nextW + x) * 4;
                        pDst[0] = (uint8_t)(r / 4); pDst[1] = (uint8_t)(g / 4); pDst[2] = (uint8_t)(b / 4); pDst[3] = (uint8_t)(a / 4);
                    }
                }
            }
            mipMemory = std::move(nextMip);
            currentSrc = mipMemory.data();
            curW = nextW;
            curH = nextH;
        }
    }

    uploadBuffer->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)));

    for (uint32_t i = 0; i < mipLevels; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst = { outTex.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, i };
        D3D12_TEXTURE_COPY_LOCATION srcLoc = { uploadBuffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, footprints[i] };
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outTex.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);

    ExecuteCommandListAndWait(cmdList.Get());

    outTex.width = width;
    outTex.height = height;
    outTex.format = format;
    outTex.mipLevels = mipLevels;
    return true;
}

Texture LoadTextureFromFile(const std::string& path, bool isHDR)
{
    if (!s_device) return {};

    int width, height, comp;
    if (isHDR) {
        float* data = nullptr;
        bool loaded = false;

        if (path.find(".exr") != std::string::npos) {
            const char* err = nullptr;
            int ret = LoadEXR(&data, &width, &height, path.c_str(), &err);
            if (ret != TINYEXR_SUCCESS) {
                if (err) {
                    fprintf(stderr, "tinyexr error: %s\n", err);
                    FreeEXRErrorMessage(err);
                }
                return {};
            }
            loaded = true;
        } else {
            data = stbi_loadf(path.c_str(), &width, &height, &comp, 4);
            if (data) loaded = true;
        }

        if (!loaded || !data) return {};

        Texture tex;
        CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, tex);
        
        if (path.find(".exr") != std::string::npos) {
            free(data);
        } else {
            stbi_image_free(data);
        }
        return tex;
    } else {
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &comp, 4);
        if (!data) return {};
        Texture tex;
        CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, tex);
        stbi_image_free(data);
        return tex;
    }
}

static void CreateDefaultBuffer(const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& defaultBuffer, ComPtr<ID3D12Resource>& uploadBuffer, D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_GENERIC_READ)
{
    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = byteSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(s_device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer)));

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    ThrowIfFailed(s_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // Copy init data into upload buffer
    UINT8* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    memcpy(mapped, initData, static_cast<size_t>(byteSize));
    uploadBuffer->Unmap(0, nullptr);

    // Create a temporary command allocator and list
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)));

    // Copy from upload to default
    cmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0, byteSize);

    // Transition default buffer to the requested final state (defaults to GENERIC_READ)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = defaultBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    cmdList->ResourceBarrier(1, &barrier);

    ExecuteCommandListAndWait(cmdList.Get());
}

#ifdef USE_TINYGLTF

bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    std::ostringstream oss;
    oss << "Asset::LoadGltf (tinygltf) requested: " << path << "\n";
    fprintf(stderr, "%s", oss.str().c_str());

    if (!std::filesystem::exists(path)) {
        fprintf(stderr, "File not found: glTF path does not exist\n");
        return false;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool isBinary = (path.size() >= 4 && path.substr(path.size() - 4) == ".glb");
    bool ret;

    // Diagnostics: print file size and header bytes before calling loader
    std::error_code ec;
    uint64_t fsize = 0;
    try {
      fsize = std::filesystem::file_size(path, ec);
    } catch (...) {
      ec.assign(1, std::generic_category());
    }
    if (ec) {
      fprintf(stderr, "LoadGltf: failed to stat file '%s': %s\n", path.c_str(), ec.message().c_str());
    } else {
      fprintf(stderr, "LoadGltf: file size = %llu bytes\n", (unsigned long long)fsize);
    }

    if (isBinary) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            unsigned char header[4] = {0,0,0,0};
            in.read(reinterpret_cast<char*>(header), sizeof(header));
            fprintf(stderr, "LoadGltf: header bytes: %02x %02x %02x %02x ('%c%c%c%c')\n",
                    header[0], header[1], header[2], header[3],
                    isprint(header[0]) ? header[0] : '?', isprint(header[1]) ? header[1] : '?', isprint(header[2]) ? header[2] : '?', isprint(header[3]) ? header[3] : '?');
        } else {
            fprintf(stderr, "LoadGltf: failed to open file for header inspection: %s\n", path.c_str());
        }
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }
    if (!warn.empty()) fprintf(stderr, "%s", warn.c_str());
    if (!err.empty()) fprintf(stderr, "%s", err.c_str());
    if (!ret) {
        fprintf(stderr, "tinygltf: Failed to load glTF file\n");
        return false;
    }

    std::ostringstream info;
    info << "Loaded glTF: " << path << " | scenes=" << model.scenes.size()
         << " nodes=" << model.nodes.size() << " meshes=" << model.meshes.size()
         << " images=" << model.images.size() << "\n";
    fprintf(stderr, "%s", info.str().c_str());

    // Optionally prepare textures and materials containers
    std::vector<Texture> tmpTextures;
    std::vector<Material> tmpMaterials;
    bool wantTextures = (outTextures != nullptr);
    bool wantMaterials = (outMaterials != nullptr);

    // Helper: create a default RGBA8 texture from image bytes and upload to GPU
    auto CreateTextureFromImage = [&](const unsigned char* src, int width, int height, int components, Texture& outTex) -> bool {
        if (!s_device) return false;

        // Support any component count (1, 2, 3, 4) by converting to RGBA8
        std::vector<unsigned char> rgba;
        const unsigned char* srcPtr = src;
        if (components != 4) {
            rgba.resize(width * height * 4);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int si = (y * width + x) * components;
                    int di = (y * width + x) * 4;
                    if (components == 1) { // Grayscale
                        rgba[di+0] = rgba[di+1] = rgba[di+2] = src[si+0];
                        rgba[di+3] = 255;
                    } else if (components == 2) { // Grayscale + Alpha
                        rgba[di+0] = rgba[di+1] = rgba[di+2] = src[si+0];
                        rgba[di+3] = src[si+1];
                    } else if (components == 3) { // RGB
                        rgba[di+0] = src[si+0];
                        rgba[di+1] = src[si+1];
                        rgba[di+2] = src[si+2];
                        rgba[di+3] = 255;
                    }
                }
            }
            srcPtr = rgba.data();
        }

        return CreateGpuTexture(srcPtr, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, outTex);
    };

    // If the caller requested materials/textures, load them first
    if (wantTextures && model.images.size() > 0) {
        tmpTextures.resize(model.images.size());
        for (size_t ii = 0; ii < model.images.size(); ++ii) {
            const tinygltf::Image& img = model.images[ii];
            if (img.width > 0 && img.height > 0 && !img.image.empty()) {
                int comp = img.component; // 3 or 4 usually
                const unsigned char* src = img.image.data();
                // tinygltf may store PNG/JPEG raw bytes if not requested to load; but when loaded via Loader it usually decodes.
                // Assume decoded image in img.image; try create texture
                Texture t;
                if (!CreateTextureFromImage(src, img.width, img.height, comp, t)) {
                    fprintf(stderr, "Failed to create texture from image\n");
                }
                tmpTextures[ii] = std::move(t);
            } else {
                fprintf(stderr, "Image missing pixel data; skipping texture\n");
            }
        }
    }

    if (wantMaterials && model.materials.size() > 0) {
        tmpMaterials.resize(model.materials.size());
        for (size_t mi = 0; mi < model.materials.size(); ++mi) {
            const tinygltf::Material& m = model.materials[mi];
            Material mat;
            // Use Name if available
            if (!m.name.empty()) strncpy_s(mat.name, m.name.c_str(), _TRUNCATE);

            // Standard PBR Metallic-Roughness logic
            float baseColorFactor[4] = {1,1,1,1};
            if (m.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
                for (int i = 0; i < 4; ++i) baseColorFactor[i] = (float)m.pbrMetallicRoughness.baseColorFactor[i];
            }
            float metallicFactor = (float)m.pbrMetallicRoughness.metallicFactor;
            float roughnessFactor = (float)m.pbrMetallicRoughness.roughnessFactor;
            
            // Store raw factors. Shaders will handle the Metallic vs Specular split.
            // We use diffuseColor for Base Color factor in GLTF mode.
            for(int c=0; c<3; ++c) mat.diffuseColor[c] = baseColorFactor[c];
            mat.diffuseColor[3] = baseColorFactor[3];
            
            // We store metallic in mat.metalness and roughness in reflectionColor.w (Glossiness = 1-R)
            mat.reflectionColor[0] = 1.0f; // Specular tint default
            mat.reflectionColor[1] = 1.0f;
            mat.reflectionColor[2] = 1.0f; 
            mat.metalness = metallicFactor;
            mat.reflectionGlossiness = 1.0f - roughnessFactor;
            
            // Proper mapping from texture index to image source index
            auto GetImgIdx = [&](int texIdx) {
                if (texIdx >= 0 && texIdx < (int)model.textures.size()) {
                    return model.textures[texIdx].source;
                }
                return -1;
            };

            // Emissive
            if (m.emissiveFactor.size() >= 3) {
                for(int i=0; i<3; ++i) mat.emissiveColor[i] = (float)m.emissiveFactor[i];
            } else if (GetImgIdx(m.emissiveTexture.index) >= 0) {
                // Default to white if an emissive texture is present but no factor is defined
                mat.emissiveColor[0] = mat.emissiveColor[1] = mat.emissiveColor[2] = 1.0f;
            }
            mat.emissiveColor[3] = 1.0f; // ior default if not packed elsewhere
            mat.emissiveIntensity = 1.0f;

            int baseColorTexIdx = GetImgIdx(m.pbrMetallicRoughness.baseColorTexture.index);
            mat.diffuseTexture = baseColorTexIdx;
            mat.normalTexture = GetImgIdx(m.normalTexture.index);
            mat.emissiveTexture = GetImgIdx(m.emissiveTexture.index);
            mat.occlusionTexture = GetImgIdx(m.occlusionTexture.index);
            mat.metalRoughTexture = GetImgIdx(m.pbrMetallicRoughness.metallicRoughnessTexture.index);
            
            // Fallback for non-PBR shaders: set reflectionTexture to BaseColor if metallic
            if (metallicFactor > 0.5f) {
                mat.reflectionTexture = baseColorTexIdx;
            }

            mat.doubleSided = m.doubleSided;
            if (!m.alphaMode.empty()) mat.alphaMode = m.alphaMode;
            
            // Handle KHR_materials_pbrSpecularGlossiness extension - Reference for proper Glossiness workflow
            auto khrSpec = m.extensions.find("KHR_materials_pbrSpecularGlossiness");
            if (khrSpec != m.extensions.end()) {
                const auto& ext = khrSpec->second;
                if (ext.Has("diffuseFactor")) {
                    auto p = ext.Get("diffuseFactor");
                    for (int i = 0; i < 4; ++i) mat.diffuseColor[i] = (float)p.Get(i).GetNumberAsDouble();
                }
                if (ext.Has("specularFactor")) {
                    auto p = ext.Get("specularFactor");
                    for (int i = 0; i < 3; ++i) mat.reflectionColor[i] = (float)p.Get(i).GetNumberAsDouble();
                }
                if (ext.Has("glossinessFactor")) {
                    mat.reflectionGlossiness = (float)ext.Get("glossinessFactor").GetNumberAsDouble();
                }
                if (ext.Has("diffuseTexture")) {
                    mat.diffuseTexture = GetImgIdx(ext.Get("diffuseTexture").Get("index").GetNumberAsInt());
                }
                if (ext.Has("specularGlossinessTexture")) {
                    mat.reflectionTexture = GetImgIdx(ext.Get("specularGlossinessTexture").Get("index").GetNumberAsInt());
                }
            }
            tmpMaterials[mi] = std::move(mat);
        }
    }

    auto GetCompSize = [](int compType) -> size_t {
        if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT) return 4;
        if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT || compType == TINYGLTF_COMPONENT_TYPE_SHORT) return 2;
        if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE || compType == TINYGLTF_COMPONENT_TYPE_BYTE) return 1;
        return 4;
    };

    auto GetNumComps = [](int type) -> size_t {
        if (type == TINYGLTF_TYPE_VEC4) return 4;
        if (type == TINYGLTF_TYPE_VEC3) return 3;
        if (type == TINYGLTF_TYPE_VEC2) return 2;
        if (type == TINYGLTF_TYPE_SCALAR) return 1;
        return 0;
    };

    // Helper to extract data from tinygltf accessors robustly
    auto GetAccessorData = [&](int accessorIdx, const unsigned char*& dataOut, size_t& strideOut, size_t& countOut, int& typeOut, int& compTypeOut) -> bool {
        if (accessorIdx < 0) return false;
        const auto& acc = model.accessors[accessorIdx];
        if (acc.bufferView < 0) return false;
        const auto& view = model.bufferViews[acc.bufferView];
        dataOut = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
        strideOut = acc.ByteStride(view);
        if (strideOut == 0) {
            strideOut = GetCompSize(acc.componentType) * GetNumComps(acc.type);
        }
        countOut = acc.count;
        typeOut = acc.type;
        compTypeOut = acc.componentType;
        return true;
    };

    auto ReadVec2 = [&](const unsigned char* data, int compType, float* out) {
        for (int i=0; i<2; ++i) {
            if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT) out[i] = reinterpret_cast<const float*>(data)[i];
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) out[i] = (float)reinterpret_cast<const uint16_t*>(data)[i] / 65535.0f;
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) out[i] = (float)data[i] / 255.0f;
        }
    };

    auto ReadVec3 = [&](const unsigned char* data, int compType, float* out) {
        for (int i=0; i<3; ++i) {
            if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT) out[i] = reinterpret_cast<const float*>(data)[i];
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) out[i] = (float)reinterpret_cast<const uint16_t*>(data)[i] / 65535.0f;
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) out[i] = (float)data[i] / 255.0f;
        }
    };

    auto ReadVec4 = [&](const unsigned char* data, int compType, float* out) {
        for (int i=0; i<4; ++i) {
            if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT) out[i] = reinterpret_cast<const float*>(data)[i];
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) out[i] = (float)reinterpret_cast<const uint16_t*>(data)[i] / 65535.0f;
            else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) out[i] = (float)data[i] / 255.0f;
        }
    };

    // --- Node traversal to bake transforms into vertices ---
    struct NodeTransform {
        int meshIdx;
        tinygltf::Value matrix;
        std::vector<double> translation, rotation, scale;
    };

    // Helper to get global transform of a node (Column-Major)
    auto GetNodeTransform = [](const tinygltf::Node& node) -> std::vector<float> {
        std::vector<float> mat(16, 0.0f);
        if (node.matrix.size() == 16) {
            for (int i = 0; i < 16; ++i) mat[i] = (float)node.matrix[i];
        } else {
            // Start with Identity
            for (int i = 0; i < 4; ++i) mat[i * 4 + i] = 1.0f;

            float sx = 1.0f, sy = 1.0f, sz = 1.0f;
            if (node.scale.size() == 3) {
                sx = (float)node.scale[0]; sy = (float)node.scale[1]; sz = (float)node.scale[2];
            }

            float r[9] = {1,0,0, 0,1,0, 0,0,1};
            if (node.rotation.size() == 4) {
                float qx = (float)node.rotation[0], qy = (float)node.rotation[1], qz = (float)node.rotation[2], qw = (float)node.rotation[3];
                r[0] = 1 - 2*qy*qy - 2*qz*qz; r[1] = 2*qx*qy - 2*qz*qw; r[2] = 2*qx*qz + 2*qy*qw;
                r[3] = 2*qx*qy + 2*qz*qw;     r[4] = 1 - 2*qx*qx - 2*qz*qz; r[5] = 2*qy*qz - 2*qx*qw;
                r[6] = 2*qx*qz - 2*qy*qw;     r[7] = 2*qy*qz + 2*qx*qw;     r[8] = 1 - 2*qx*qx - 2*qy*qy;
            }

            // Fill Column-Major Matrix from TRS: M = T * R * S
            // Col 0
            mat[0] = r[0] * sx; mat[1] = r[3] * sx; mat[2] = r[6] * sx;
            // Col 1
            mat[4] = r[1] * sy; mat[5] = r[4] * sy; mat[6] = r[7] * sy;
            // Col 2
            mat[8] = r[2] * sz; mat[9] = r[5] * sz; mat[10] = r[8] * sz;
            
            if (node.translation.size() == 3) {
                mat[12] = (float)node.translation[0]; mat[13] = (float)node.translation[1]; mat[14] = (float)node.translation[2];
            }
        }
        return mat;
    };

    // Matrix Multiply (Column-Major)
    auto Multiply = [](const std::vector<float>& A, const std::vector<float>& B) {
        std::vector<float> C(16, 0.0f);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += A[k * 4 + row] * B[col * 4 + k];
                }
                C[col * 4 + row] = sum;
            }
        }
        return C;
    };

    std::vector<std::vector<float>> globalTransforms(model.nodes.size());
    std::vector<bool> visited(model.nodes.size(), false);

    auto Traverse = [&](auto self, int nodeIdx, const std::vector<float>& parentTransform) -> void {
        if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size()) return;
        const auto& node = model.nodes[nodeIdx];
        std::vector<float> local = GetNodeTransform(node);
        globalTransforms[nodeIdx] = Multiply(parentTransform, local);
        visited[nodeIdx] = true;
        for (int child : node.children) self(self, child, globalTransforms[nodeIdx]);
    };

    std::vector<float> identity(16, 0.0f);
    for (int i = 0; i < 4; ++i) identity[i * 4 + i] = 1.0f;
    
    // Apply initial root translation if provided
    if (rootTranslation) {
        identity[12] = rootTranslation[0];
        identity[13] = rootTranslation[1];
        identity[14] = rootTranslation[2];
    }

    const auto& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (int nodeIdx : scene.nodes) Traverse(Traverse, nodeIdx, identity);

    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        if (!visited[ni] || model.nodes[ni].mesh < 0) continue;
        const auto& node = model.nodes[ni];
        const auto& mesh = model.meshes[node.mesh];
        const auto& worldMat = globalTransforms[ni];

        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& prim = mesh.primitives[pi];

            // Only handle TRIANGLES or TRIANGLE_STRIP/STRIP conversions are not implemented
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP && prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
                fprintf(stderr, "Skipping non-triangle primitive\n");
                continue;
            }

            const unsigned char *posData = nullptr, *normData = nullptr, *uvData = nullptr, *tanData = nullptr;
            size_t posStride = 0, normStride = 0, uvStride = 0, tanStride = 0;
            size_t vertexCount = 0;
            int posType, posComp, normType, normComp, uvType, uvComp, tanType, tanComp;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end() || !GetAccessorData(posIt->second, posData, posStride, vertexCount, posType, posComp)) {
                fprintf(stderr, "Primitive missing POSITION; skipping\n");
                continue;
            }
            if (posStride == 0) posStride = sizeof(float) * 3;

            bool hasNormal = false;
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end() && GetAccessorData(normIt->second, normData, normStride, vertexCount, normType, normComp)) {
                if (normStride == 0) normStride = sizeof(float) * 3;
                hasNormal = true;
            }

            bool hasUV = false;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end() && GetAccessorData(uvIt->second, uvData, uvStride, vertexCount, uvType, uvComp)) {
                if (uvStride == 0) uvStride = sizeof(float) * 2;
                hasUV = true;
            }

            bool hasTangent = false;
            auto tanIt = prim.attributes.find("TANGENT");
            if (tanIt != prim.attributes.end() && GetAccessorData(tanIt->second, tanData, tanStride, vertexCount, tanType, tanComp)) {
                if (tanStride == 0) tanStride = sizeof(float) * 4;
                hasTangent = true;
            }

            // Gather interleaved vertices
            std::vector<Vertex> vertices;
            vertices.reserve(vertexCount);
            float minBound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
            float maxBound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

            for (size_t i = 0; i < vertexCount; ++i) {
                float p[3] = {0,0,0};
                ReadVec3(posData + i * posStride, posComp, p);

                float nx = 0.0f, ny = 1.0f, nz = 0.0f; 
                if (hasNormal) {
                    float n[3]; ReadVec3(normData + i * normStride, normComp, n);
                    nx = n[0]; ny = n[1]; nz = n[2];
                }

                float u = 0.0f, v = 0.0f;
                if (hasUV) {
                    float uv[2]; ReadVec2(uvData + i * uvStride, uvComp, uv);
                    u = uv[0]; v = uv[1];
                }

                float tx = 1.0f, ty = 0.0f, tz = 0.0f, tw = 1.0f;
                if (hasTangent) {
                    float t[4]; ReadVec4(tanData + i * tanStride, tanComp, t);
                    tx = t[0]; ty = t[1]; tz = t[2]; tw = t[3];
                }

                Vertex vv;
                // Position: P' = M * P
                vv.pos[0] = p[0] * worldMat[0] + p[1] * worldMat[4] + p[2] * worldMat[8] + worldMat[12];
                vv.pos[1] = p[0] * worldMat[1] + p[1] * worldMat[5] + p[2] * worldMat[9] + worldMat[13];
                vv.pos[2] = p[0] * worldMat[2] + p[1] * worldMat[6] + p[2] * worldMat[10] + worldMat[14];
                
                // Update bounds
                for (int c = 0; c < 3; ++c) {
                    if (vv.pos[c] < minBound[c]) minBound[c] = vv.pos[c];
                    if (vv.pos[c] > maxBound[c]) maxBound[c] = vv.pos[c];
                }

                // Normal: N' = M_3x3 * N
                vv.normal[0] = nx * worldMat[0] + ny * worldMat[4] + nz * worldMat[8];
                vv.normal[1] = nx * worldMat[1] + ny * worldMat[5] + nz * worldMat[9];
                vv.normal[2] = nx * worldMat[2] + ny * worldMat[6] + nz * worldMat[10];
                
                // Tangent: T' = M_3x3 * T
                vv.tangent[0] = tx * worldMat[0] + ty * worldMat[4] + tz * worldMat[8];
                vv.tangent[1] = tx * worldMat[1] + ty * worldMat[5] + tz * worldMat[9];
                vv.tangent[2] = tx * worldMat[2] + ty * worldMat[6] + tz * worldMat[10];
                vv.tangent[3] = tw;

                vv.uv[0] = u; vv.uv[1] = v;
                vertices.push_back(vv);
            }

            // Indices
            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                const unsigned char* idxData = nullptr;
                size_t idxStride = 0, idxCount = 0;
                int idxType, idxComp;
                if (GetAccessorData(prim.indices, idxData, idxStride, idxCount, idxType, idxComp)) {
                    indices.resize(idxCount);
                    for (size_t i = 0; i < idxCount; ++i) {
                        if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            indices[i] = *reinterpret_cast<const uint16_t*>(idxData + i * 2);
                        } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                            indices[i] = *reinterpret_cast<const uint32_t*>(idxData + i * 4);
                        } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                            indices[i] = idxData[i];
                        }
                    }
                }
            } else {
                indices.resize(vertexCount);
                for (uint32_t i = 0; i < (uint32_t)vertexCount; ++i) indices[i] = i;
            }

            if (vertices.empty() || indices.empty()) {
                fprintf(stderr, "Generated empty vertex or index list; skipping primitive\n");
                continue;
            }

            // Create GPU buffers
            GpuMesh gm;
            ComPtr<ID3D12Resource> vbUpload;
            ComPtr<ID3D12Resource> ibUpload;

            fprintf(stderr, "CreateDefaultBuffer: vb bytes=%zu ib bytes=%zu (mesh verts=%zu idx=%zu)\n", sizeof(Vertex) * vertices.size(), sizeof(uint32_t) * indices.size(), vertices.size(), indices.size());
            fflush(stderr);
            CreateDefaultBuffer(vertices.data(), sizeof(Vertex) * vertices.size(), gm.vertexBuffer, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            fprintf(stderr, "CreateDefaultBuffer: vb created\n"); fflush(stderr);
            CreateDefaultBuffer(indices.data(), sizeof(uint32_t) * indices.size(), gm.indexBuffer, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
            fprintf(stderr, "CreateDefaultBuffer: ib created\n"); fflush(stderr);

            gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
            gm.vbView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * vertices.size());
            gm.vbView.StrideInBytes = sizeof(Vertex);

            gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
            gm.ibView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indices.size());
            gm.ibView.Format = DXGI_FORMAT_R32_UINT;

            gm.vertexCount = static_cast<UINT>(vertices.size());
            gm.indexCount = static_cast<UINT>(indices.size());
            gm.materialIndex = (prim.material >= 0) ? prim.material : -1;

            for (int c = 0; c < 3; ++c) {
                gm.minBound[c] = minBound[c];
                gm.maxBound[c] = maxBound[c];
            }

            outMeshes.push_back(std::move(gm));

            std::ostringstream log;
            log << "Imported node[" << ni << "] mesh[" << node.mesh << "] prim[" << pi << "] verts=" << vertexCount << " idx=" << indices.size() << "\n";
            fprintf(stderr, "%s", log.str().c_str());
        }
    }

    // If caller requested materials/textures, move temporaries out
    if (outTextures) *outTextures = std::move(tmpTextures);
    if (outMaterials) *outMaterials = std::move(tmpMaterials);

    return true;
}

#else

bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    std::ostringstream oss;
    oss << "Asset::LoadGltf requested: " << path << "\n";
    fprintf(stderr, "%s", oss.str().c_str());

    if (!std::filesystem::exists(path)) {
        fprintf(stderr, "File not found: glTF path does not exist\n");
        return false;
    }

    fprintf(stderr, "Found file but loader is stubbed. Enable USE_TINYGLTF in CMake to use tinygltf.\n");
    return true;
}

#endif

bool LoadWithAssimp(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    Assimp::Importer importer;
    if (s_progressCb) s_progressCb(0.01f, std::string("Starting import: ") + path);
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | 
        aiProcess_CalcTangentSpace | 
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_ConvertToLeftHanded |
        aiProcess_GlobalScale);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        fprintf(stderr, "Assimp Error: %s\n", importer.GetErrorString());
        if (s_progressCb) s_progressCb(0.0f, std::string("Assimp Error: ") + importer.GetErrorString());
        return false;
    }

    if (outMaterials && scene->HasMaterials()) {
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
            aiMaterial* aiMat = scene->mMaterials[i];
            Material mat;
            aiString name;
            if (aiMat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) strncpy_s(mat.name, name.C_Str(), _TRUNCATE);

            aiColor4D color;
            if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
                mat.diffuseColor[0] = color.r; mat.diffuseColor[1] = color.g; mat.diffuseColor[2] = color.b; mat.diffuseColor[3] = color.a;
            }
            if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
                mat.emissiveColor[0] = color.r; mat.emissiveColor[1] = color.g; mat.emissiveColor[2] = color.b;
            }
            
            float strength = 1.0f;
            if (aiMat->Get(AI_MATKEY_EMISSIVE_INTENSITY, strength) == AI_SUCCESS) mat.emissiveIntensity = strength;

            float roughness = 0.5f, metalness = 0.0f;
            aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
            aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metalness);
            mat.reflectionGlossiness = 1.0f - roughness;
            mat.metalness = metalness;

            auto GetTexturePath = [&](aiTextureType type) -> std::string {
                aiString texPath;
                if (aiMat->GetTextureCount(type) > 0) {
                    aiMat->GetTexture(type, 0, &texPath);
                    std::filesystem::path p = path;
                    return (p.parent_path() / texPath.C_Str()).string();
                }
                return "";
            };

            if (outTextures) {
                std::string dp = GetTexturePath(aiTextureType_DIFFUSE);
                if (!dp.empty()) { mat.diffuseTexture = (int)outTextures->size(); outTextures->push_back(LoadTextureFromFile(dp)); }
                
                std::string np = GetTexturePath(aiTextureType_NORMALS);
                if (np.empty()) np = GetTexturePath(aiTextureType_HEIGHT); // Fallback
                if (!np.empty()) { mat.normalTexture = (int)outTextures->size(); outTextures->push_back(LoadTextureFromFile(np)); }

                std::string ep = GetTexturePath(aiTextureType_EMISSIVE);
                if (!ep.empty()) { mat.emissiveTexture = (int)outTextures->size(); outTextures->push_back(LoadTextureFromFile(ep)); }
            }
            outMaterials->push_back(mat);
        }
        if (s_progressCb) s_progressCb(0.12f, std::string("Materials parsed: ") + std::to_string(scene->mNumMaterials));
    }

    // Progress accounting for mesh processing
    int totalMeshes = (int)scene->mNumMeshes;
    int processedMeshes = 0;

    if (s_progressCb) s_progressCb(0.15f, std::string("Processing meshes: 0/") + std::to_string(totalMeshes));

    std::function<void(aiNode*, aiMatrix4x4)> processNode = [&](aiNode* node, aiMatrix4x4 parentTransform) {
        aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;
        
        float worldMat[16] = {
            currentTransform.a1, currentTransform.b1, currentTransform.c1, currentTransform.d1,
            currentTransform.a2, currentTransform.b2, currentTransform.c2, currentTransform.d2,
            currentTransform.a3, currentTransform.b3, currentTransform.c3, currentTransform.d3,
            currentTransform.a4, currentTransform.b4, currentTransform.c4, currentTransform.d4
        };

        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            float minB[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, maxB[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

            for (unsigned int vIdx = 0; vIdx < mesh->mNumVertices; ++vIdx) {
                Vertex v = {};
                aiVector3D p = mesh->mVertices[vIdx];
                v.pos[0] = p.x * worldMat[0] + p.y * worldMat[4] + p.z * worldMat[8] + worldMat[12];
                v.pos[1] = p.x * worldMat[1] + p.y * worldMat[5] + p.z * worldMat[9] + worldMat[13];
                v.pos[2] = p.x * worldMat[2] + p.y * worldMat[6] + p.z * worldMat[10] + worldMat[14];
                
                if (rootTranslation) {
                    v.pos[0] += rootTranslation[0]; v.pos[1] += rootTranslation[1]; v.pos[2] += rootTranslation[2];
                }

                if (mesh->HasNormals()) {
                    aiVector3D n = mesh->mNormals[vIdx];
                    v.normal[0] = n.x * worldMat[0] + n.y * worldMat[4] + n.z * worldMat[8];
                    v.normal[1] = n.x * worldMat[1] + n.y * worldMat[5] + n.z * worldMat[9];
                    v.normal[2] = n.x * worldMat[2] + n.y * worldMat[6] + n.z * worldMat[10];
                }
                if (mesh->HasTextureCoords(0)) {
                    v.uv[0] = mesh->mTextureCoords[0][vIdx].x;
                    v.uv[1] = mesh->mTextureCoords[0][vIdx].y;
                }
                if (mesh->HasTangentsAndBitangents()) {
                    aiVector3D t = mesh->mTangents[vIdx];
                    v.tangent[0] = t.x * worldMat[0] + t.y * worldMat[4] + t.z * worldMat[8];
                    v.tangent[1] = t.x * worldMat[1] + t.y * worldMat[5] + t.z * worldMat[9];
                    v.tangent[2] = t.x * worldMat[2] + t.y * worldMat[6] + t.z * worldMat[10];
                    v.tangent[3] = 1.0f;
                }
                for (int c = 0; c < 3; ++c) {
                    minB[c] = std::min(minB[c], v.pos[c]);
                    maxB[c] = std::max(maxB[c], v.pos[c]);
                }
                vertices.push_back(v);
            }

            for (unsigned int fIdx = 0; fIdx < mesh->mNumFaces; ++fIdx) {
                aiFace face = mesh->mFaces[fIdx];
                for (unsigned int idx = 0; idx < face.mNumIndices; ++idx)
                    indices.push_back(face.mIndices[idx]);
            }

            GpuMesh gm;
            ComPtr<ID3D12Resource> vbUpload, ibUpload;
            fprintf(stderr, "CreateDefaultBuffer (GLTF path): vb bytes=%zu ib bytes=%zu (mesh verts=%zu idx=%zu)\n", sizeof(Vertex) * vertices.size(), sizeof(uint32_t) * indices.size(), vertices.size(), indices.size()); fflush(stderr);
            CreateDefaultBuffer(vertices.data(), sizeof(Vertex) * vertices.size(), gm.vertexBuffer, vbUpload, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            fprintf(stderr, "CreateDefaultBuffer (GLTF path): vb created\n"); fflush(stderr);
            CreateDefaultBuffer(indices.data(), sizeof(uint32_t) * indices.size(), gm.indexBuffer, ibUpload, D3D12_RESOURCE_STATE_INDEX_BUFFER);
            fprintf(stderr, "CreateDefaultBuffer (GLTF path): ib created\n"); fflush(stderr);

            gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
            gm.vbView.SizeInBytes = (UINT)(sizeof(Vertex) * vertices.size());
            gm.vbView.StrideInBytes = sizeof(Vertex);
            gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
            gm.ibView.SizeInBytes = (UINT)(sizeof(uint32_t) * indices.size());
            gm.ibView.Format = DXGI_FORMAT_R32_UINT;
            gm.vertexCount = (UINT)vertices.size();
            gm.indexCount = (UINT)indices.size();
            for(int c=0; c<3; ++c) { gm.minBound[c] = minB[c]; gm.maxBound[c] = maxB[c]; }
            gm.materialIndex = mesh->mMaterialIndex;

            outMeshes.push_back(std::move(gm));
            // Update progress after each mesh
            processedMeshes++;
            if (s_progressCb && totalMeshes > 0) {
                float p = 0.15f + 0.8f * (processedMeshes / (float)totalMeshes);
                if (p > 0.95f) p = 0.95f;
                char buf[256]; sprintf_s(buf, "Importing meshes: %d/%d", processedMeshes, totalMeshes);
                s_progressCb(p, std::string(buf));
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], currentTransform);
    };

    processNode(scene->mRootNode, aiMatrix4x4());
    if (s_progressCb) s_progressCb(1.0f, std::string("Import complete: ") + path);
    return true;
}

bool LoadOBJ(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    return LoadWithAssimp(path, outMeshes, outMaterials, outTextures, rootTranslation);
}

bool LoadSTL(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    return LoadWithAssimp(path, outMeshes, outMaterials, outTextures, rootTranslation);
}

bool LoadModel(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures, const float* rootTranslation)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".gltf" || ext == ".glb") {
        return LoadGltf(path, outMeshes, outMaterials, outTextures, rootTranslation);
    } else {
        return LoadWithAssimp(path, outMeshes, outMaterials, outTextures, rootTranslation);
    }
}

} // namespace Asset
