#include "asset_loader.h"
#include <stdio.h>
#include <windows.h>
#include <vector>
#include <iostream>
#include <string>
#include <sstream>

// If tinygltf is enabled via CMake, use it. Otherwise keep the stub.
#ifdef USE_TINYGLTF
#include <windows.h>
#include <stdio.h>
#include <tiny_gltf.h>
#include <filesystem>
#include <wrl.h>
#include <d3d12.h>
#include <vector>
#include <sstream>
#include <algorithm>

using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "HRESULT 0x%08x\n", static_cast<unsigned>(hr));
        OutputDebugStringA(buf);
        ExitProcess(static_cast<UINT>(hr));
    }
}

namespace Asset {

static ComPtr<ID3D12Device> s_device;
static ComPtr<ID3D12CommandQueue> s_queue;

void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    s_device = device;
    s_queue = queue;
}

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

static void CreateDefaultBuffer(const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& defaultBuffer, ComPtr<ID3D12Resource>& uploadBuffer)
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

    // Transition default buffer to GENERIC_READ
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = defaultBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    cmdList->ResourceBarrier(1, &barrier);

    ExecuteCommandListAndWait(cmdList.Get());
}

bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures)
{
    std::ostringstream oss;
    oss << "Asset::LoadGltf (tinygltf) requested: " << path << "\n";
    OutputDebugStringA(oss.str().c_str());

    if (!std::filesystem::exists(path)) {
        OutputDebugStringA("File not found: glTF path does not exist\n");
        return false;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = false;
    // Choose ASCII or Binary loader based on file extension
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot + 1);
        for (char &c : ext) c = (char)tolower(c);
    }
    if (ext == "glb") {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!warn.empty()) {
        OutputDebugStringA(warn.c_str());
        FILE *log = nullptr; if (fopen_s(&log, "startup.log", "a")==0 && log) { fprintf(log, "tinygltf warn: %s\n", warn.c_str()); fclose(log); }
    }
    if (!err.empty()) {
        OutputDebugStringA(err.c_str());
        FILE *log = nullptr; if (fopen_s(&log, "startup.log", "a")==0 && log) { fprintf(log, "tinygltf err: %s\n", err.c_str()); fclose(log); }
    }
    if (!ret) {
        OutputDebugStringA("tinygltf: Failed to load glTF file\n");
        FILE *log = nullptr; if (fopen_s(&log, "startup.log", "a")==0 && log) { fprintf(log, "tinygltf: Failed to load glTF file %s\n", path.c_str()); fclose(log); }
        return false;
    }

    std::ostringstream info;
    info << "Loaded glTF: " << path << " | scenes=" << model.scenes.size()
         << " nodes=" << model.nodes.size() << " meshes=" << model.meshes.size()
         << " images=" << model.images.size() << "\n";
    OutputDebugStringA(info.str().c_str());

    // Optionally prepare textures and materials containers
    std::vector<Texture> tmpTextures;
    std::vector<Material> tmpMaterials;
    bool wantTextures = (outTextures != nullptr);
    bool wantMaterials = (outMaterials != nullptr);

    // Helper: create a default RGBA8 texture from image bytes and upload to GPU
    auto CreateTextureFromImage = [&](const unsigned char* src, int width, int height, int components, Texture& outTex) -> bool {
        if (!s_device) return false;

        DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = (UINT)width;
        texDesc.Height = (UINT)height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = fmt;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES defaultHeapProps = {};
        defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        ComPtr<ID3D12Resource> texture;
        ThrowIfFailed(s_device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)));

        // Get required size for upload buffer
        UINT64 requiredSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        s_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = totalBytes;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> uploadBuffer;
        ThrowIfFailed(s_device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadBuffer)));

        // Prepare a temporary RGBA buffer if source has 3 components
        std::vector<unsigned char> rgba;
        const unsigned char* srcPtr = src;
        if (components == 3) {
            rgba.resize(width * height * 4);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int si = (y * width + x) * 3;
                    int di = (y * width + x) * 4;
                    rgba[di+0] = src[si+0];
                    rgba[di+1] = src[si+1];
                    rgba[di+2] = src[si+2];
                    rgba[di+3] = 255;
                }
            }
            srcPtr = rgba.data();
        }

        // Map upload buffer and copy rows respecting RowPitch
        UINT8* mapped = nullptr;
        D3D12_RANGE readRange = {0,0};
        ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
        for (UINT y = 0; y < numRows; ++y) {
            BYTE* dest = mapped + footprint.Offset + (SIZE_T)footprint.Footprint.RowPitch * y;
            const BYTE* srcRow = srcPtr + (size_t)y * width * 4;
            memcpy(dest, srcRow, (size_t)width * 4);
        }
        uploadBuffer->Unmap(0, nullptr);

        // Create a temporary command allocator and list
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> cmdList;
        ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)));

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = texture.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;

        D3D12_BOX srcBox = {};
        srcBox.left = 0; srcBox.top = 0; srcBox.front = 0;
        srcBox.right = width; srcBox.bottom = height; srcBox.back = 1;

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, &srcBox);

        // Transition to PIXEL_SHADER_RESOURCE
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &barrier);

        ExecuteCommandListAndWait(cmdList.Get());

        outTex.resource = texture;
        outTex.width = (UINT)width;
        outTex.height = (UINT)height;
        outTex.format = fmt;
        outTex.mipLevels = 1;
        return true;
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
                    OutputDebugStringA("Failed to create texture from image\n");
                }
                tmpTextures[ii] = std::move(t);
            } else {
                OutputDebugStringA("Image missing pixel data; skipping texture\n");
            }
        }
    }

    if (wantMaterials && model.materials.size() > 0) {
        tmpMaterials.resize(model.materials.size());
        for (size_t mi = 0; mi < model.materials.size(); ++mi) {
            const tinygltf::Material& m = model.materials[mi];
            Material mat;
            // pbrMetallicRoughness baseColorFactor
            if (m.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
                for (int i = 0; i < 4; ++i) mat.baseColorFactor[i] = (float)m.pbrMetallicRoughness.baseColorFactor[i];
            }
            mat.metallicFactor = (float)m.pbrMetallicRoughness.metallicFactor;
            mat.roughnessFactor = (float)m.pbrMetallicRoughness.roughnessFactor;
            if (m.pbrMetallicRoughness.baseColorTexture.index >= 0) mat.baseColorTexture = m.pbrMetallicRoughness.baseColorTexture.index;
            if (m.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) mat.metallicRoughnessTexture = m.pbrMetallicRoughness.metallicRoughnessTexture.index;
            if (m.normalTexture.index >= 0) mat.normalTexture = m.normalTexture.index;
            if (m.occlusionTexture.index >= 0) mat.occlusionTexture = m.occlusionTexture.index;
            if (m.emissiveTexture.index >= 0) mat.emissiveTexture = m.emissiveTexture.index;
            mat.doubleSided = m.doubleSided;
            if (!m.alphaMode.empty()) mat.alphaMode = m.alphaMode;
            // Check for specular-glossiness (additionalValues often contains these when extension used)
            auto specIt = m.additionalValues.find("specularFactor");
            auto glossIt = m.additionalValues.find("glossinessFactor");
            if (specIt != m.additionalValues.end() || glossIt != m.additionalValues.end()) {
                // Mark specular-glossiness workflow
                mat.workflow = 1;
                if (specIt != m.additionalValues.end()) {
                    const tinygltf::Parameter& p = specIt->second;
                    if (p.number_array.size() >= 3) {
                        mat.specularFactor[0] = (float)p.number_array[0];
                        mat.specularFactor[1] = (float)p.number_array[1];
                        mat.specularFactor[2] = (float)p.number_array[2];
                    }
                }
                if (glossIt != m.additionalValues.end()) {
                    const tinygltf::Parameter& p = glossIt->second;
                    if (!p.number_array.empty()) mat.glossinessFactor = (float)p.number_array[0];
                }
            }
            tmpMaterials[mi] = std::move(mat);
        }
    }
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& mesh = model.meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& prim = mesh.primitives[pi];

            // Only handle TRIANGLES or TRIANGLE_STRIP/STRIP conversions are not implemented
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP && prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
                OutputDebugStringA("Skipping non-triangle primitive\n");
                continue;
            }

            // Find POSITION accessor
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) {
                OutputDebugStringA("Primitive missing POSITION attribute; skipping\n");
                continue;
            }

            const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
            if (posAccessor.bufferView < 0) {
                OutputDebugStringA("POSITION accessor has no bufferView; skipping\n");
                continue;
            }

            const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
            if (posView.buffer < 0 || posView.buffer >= (int)model.buffers.size()) {
                OutputDebugStringA("POSITION bufferView references invalid buffer; skipping\n");
                continue;
            }

            const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
            if (posBuffer.data.empty()) {
                OutputDebugStringA("POSITION buffer is empty; skipping\n");
                continue;
            }

            const unsigned char* posData = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
            size_t posByteStride = posAccessor.ByteStride(posView);
            if (posByteStride == 0) posByteStride = sizeof(float) * 3;

            // Optional NORMAL accessor
            const unsigned char* normData = nullptr;
            size_t normByteStride = 0;
            bool hasNormal = false;
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                const tinygltf::Accessor& normAccessor = model.accessors[normIt->second];
                if (normAccessor.bufferView >= 0) {
                    const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
                    if (normView.buffer >= 0 && normView.buffer < (int)model.buffers.size()) {
                        const tinygltf::Buffer& normBuffer = model.buffers[normView.buffer];
                        if (!normBuffer.data.empty()) {
                            normData = normBuffer.data.data() + normView.byteOffset + normAccessor.byteOffset;
                            normByteStride = normAccessor.ByteStride(normView);
                            if (normByteStride == 0) normByteStride = sizeof(float) * 3;
                            hasNormal = true;
                        }
                    }
                }
            }

            // Optional TANGENT accessor (vec4)
            const unsigned char* tanData = nullptr;
            size_t tanByteStride = 0;
            bool hasTangent = false;
            auto tanIt = prim.attributes.find("TANGENT");
            if (tanIt != prim.attributes.end()) {
                const tinygltf::Accessor& tanAccessor = model.accessors[tanIt->second];
                if (tanAccessor.bufferView >= 0) {
                    const tinygltf::BufferView& tanView = model.bufferViews[tanAccessor.bufferView];
                    if (tanView.buffer >= 0 && tanView.buffer < (int)model.buffers.size()) {
                        const tinygltf::Buffer& tanBuffer = model.buffers[tanView.buffer];
                        if (!tanBuffer.data.empty()) {
                            tanData = tanBuffer.data.data() + tanView.byteOffset + tanAccessor.byteOffset;
                            tanByteStride = tanAccessor.ByteStride(tanView);
                            if (tanByteStride == 0) tanByteStride = sizeof(float) * 4;
                            hasTangent = true;
                        }
                    }
                }
            }

            // Optional TEXCOORD_0 accessor
            const unsigned char* uvData = nullptr;
            size_t uvByteStride = 0;
            bool hasUV = false;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                const tinygltf::Accessor& uvAccessor = model.accessors[uvIt->second];
                if (uvAccessor.bufferView >= 0) {
                    const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
                    if (uvView.buffer >= 0 && uvView.buffer < (int)model.buffers.size()) {
                        const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
                        if (!uvBuffer.data.empty()) {
                            uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
                            uvByteStride = uvAccessor.ByteStride(uvView);
                            if (uvByteStride == 0) uvByteStride = sizeof(float) * 2;
                            hasUV = true;
                        }
                    }
                }
            }

            // Gather interleaved vertices (pos, normal, uv)
            std::vector<Vertex> vertices;
            vertices.reserve(posAccessor.count);
            for (size_t i = 0; i < posAccessor.count; ++i) {
                const float* p = reinterpret_cast<const float*>(posData + i * posByteStride);
                float nx = 0.0f, ny = 0.0f, nz = 0.0f;
                if (hasNormal) {
                    const float* n = reinterpret_cast<const float*>(normData + i * normByteStride);
                    nx = n[0]; ny = n[1]; nz = n[2];
                }

                float u = 0.0f, v = 0.0f;
                if (hasUV) {
                    const float* uv = reinterpret_cast<const float*>(uvData + i * uvByteStride);
                    u = uv[0]; v = uv[1];
                }

                float tx = 0.0f, ty = 0.0f, tz = 0.0f, tw = 0.0f;
                if (hasTangent) {
                    const float* t = reinterpret_cast<const float*>(tanData + i * tanByteStride);
                    tx = t[0]; ty = t[1]; tz = t[2]; tw = t[3];
                }

                Vertex vv;
                vv.pos[0] = p[0]; vv.pos[1] = p[1]; vv.pos[2] = p[2];
                vv.normal[0] = nx; vv.normal[1] = ny; vv.normal[2] = nz;
                vv.tangent[0] = tx; vv.tangent[1] = ty; vv.tangent[2] = tz; vv.tangent[3] = tw;
                vv.uv[0] = u; vv.uv[1] = v;
                vertices.push_back(vv);
            }

            // Indices
            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                const tinygltf::Accessor& idxAccessor = model.accessors[prim.indices];
                if (idxAccessor.bufferView < 0) {
                    OutputDebugStringA("Index accessor has no bufferView; skipping primitive\n");
                    continue;
                }

                const tinygltf::BufferView& idxView = model.bufferViews[idxAccessor.bufferView];
                if (idxView.buffer < 0 || idxView.buffer >= (int)model.buffers.size()) {
                    OutputDebugStringA("Index bufferView references invalid buffer; skipping primitive\n");
                    continue;
                }

                const tinygltf::Buffer& idxBuffer = model.buffers[idxView.buffer];
                if (idxBuffer.data.empty()) {
                    OutputDebugStringA("Index buffer is empty; skipping primitive\n");
                    continue;
                }

                const unsigned char* idxData = idxBuffer.data.data() + idxView.byteOffset + idxAccessor.byteOffset;

                indices.resize(idxAccessor.count);
                bool unsupportedIndexType = false;
                for (size_t i = 0; i < idxAccessor.count; ++i) {
                    if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        const uint16_t* s = reinterpret_cast<const uint16_t*>(idxData + i * 2);
                        indices[i] = static_cast<uint32_t>(*s);
                    } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        const uint32_t* s = reinterpret_cast<const uint32_t*>(idxData + i * 4);
                        indices[i] = *s;
                    } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        const uint8_t* s = reinterpret_cast<const uint8_t*>(idxData + i);
                        indices[i] = static_cast<uint32_t>(*s);
                    } else {
                        OutputDebugStringA("Unsupported index component type; skipping primitive\n");
                        unsupportedIndexType = true;
                        break;
                    }
                }

                if (unsupportedIndexType) continue;
            } else {
                // No indices; generate a simple 0..N-1 index list
                indices.resize(posAccessor.count);
                for (uint32_t i = 0; i < (uint32_t)posAccessor.count; ++i) indices[i] = i;
            }

            if (vertices.empty() || indices.empty()) {
                OutputDebugStringA("Generated empty vertex or index list; skipping primitive\n");
                continue;
            }

            // Create GPU buffers
            GpuMesh gm;
            ComPtr<ID3D12Resource> vbUpload;
            ComPtr<ID3D12Resource> ibUpload;

            CreateDefaultBuffer(vertices.data(), sizeof(Vertex) * vertices.size(), gm.vertexBuffer, vbUpload);
            CreateDefaultBuffer(indices.data(), sizeof(uint32_t) * indices.size(), gm.indexBuffer, ibUpload);

            gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
            gm.vbView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * vertices.size());
            gm.vbView.StrideInBytes = sizeof(Vertex);

            gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
            gm.ibView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indices.size());
            gm.ibView.Format = DXGI_FORMAT_R32_UINT;

            gm.vertexCount = static_cast<UINT>(vertices.size());
            gm.indexCount = static_cast<UINT>(indices.size());
            gm.materialIndex = (prim.material >= 0) ? prim.material : -1;

            outMeshes.push_back(std::move(gm));

            std::ostringstream log;
            log << "Imported mesh[" << mi << "] prim[" << pi << "] verts=" << posAccessor.count << " idx=" << indices.size() << "\n";
            OutputDebugStringA(log.str().c_str());
        }
    }

    // If caller requested materials/textures, move temporaries out
    if (outTextures) *outTextures = std::move(tmpTextures);
    if (outMaterials) *outMaterials = std::move(tmpMaterials);

    return true;
}

} // namespace Asset

#else

#include <filesystem>

namespace Asset {

bool LoadGltf(const std::string& path, std::vector<GpuMesh>& outMeshes, std::vector<Material>* outMaterials, std::vector<Texture>* outTextures)
{
    std::ostringstream oss;
    oss << "Asset::LoadGltf requested: " << path << "\n";
    OutputDebugStringA(oss.str().c_str());

    if (!std::filesystem::exists(path)) {
        OutputDebugStringA("File not found: glTF path does not exist\n");
        return false;
    }

    OutputDebugStringA("Found file but loader is stubbed. Enable USE_TINYGLTF in CMake to use tinygltf.\n");
    return true;
}

} // namespace Asset

#endif
