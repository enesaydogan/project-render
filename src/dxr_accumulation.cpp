#include "dxr_accumulation.h"
#include "d3d12_helpers.h"
#include <iostream>

DxrAccumulation::DxrAccumulation() : m_frameCount(0), m_width(0), m_height(0), m_isAccumulating(true), m_enabled(true) {
}

DxrAccumulation::~DxrAccumulation() {
}

void DxrAccumulation::Initialize(ID3D12Device* device, UINT width, UINT height) {
    m_device = device;
    CreateResources(width, height);
}

void DxrAccumulation::Resize(UINT width, UINT height) {
    if (m_width == width && m_height == height) return;
    CreateResources(width, height);
    Reset();
}

void DxrAccumulation::Reset() {
    m_frameCount = 0;
}

void DxrAccumulation::IncrementFrame() {
    if (m_enabled) {
        m_frameCount++;
    }
}

void DxrAccumulation::CreateResources(UINT width, UINT height) {
    m_width = width;
    m_height = height;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // Use high precision for accumulation
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_accumulationBuffer));

    if (FAILED(hr)) {
        std::cerr << "Failed to create accumulation buffer" << std::endl;
        return;
    }
    m_accumulationBuffer->SetName(L"Accumulation Buffer");

    // Create a descriptor heap for the UAV
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_uavHeap));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;

    m_device->CreateUnorderedAccessView(m_accumulationBuffer.Get(), nullptr, &uavDesc, m_uavHeap->GetCPUDescriptorHandleForHeapStart());
    m_accumulationUAVGpu = m_uavHeap->GetGPUDescriptorHandleForHeapStart();
}
