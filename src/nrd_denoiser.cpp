#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "nrd_denoiser.h"
#include "camera.h"

// Math includes
#include <DirectXMath.h>
using namespace DirectX;

// Define to include D3D12 wrapper for NRI
#define NRI_WRAPPER_D3D12_H 1

// We need these headers from NRI and NRD Integration
#include "NRD.h"
#include "NRI.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "NRDIntegration.hpp"

static NrdDenoiser* g_NrdDenoiser = nullptr;

NrdDenoiser& NrdDenoiser::Get() {
    if (!g_NrdDenoiser) g_NrdDenoiser = new NrdDenoiser();
    return *g_NrdDenoiser;
}

NrdDenoiser::NrdDenoiser() {
    m_nrdIntegration = new nrd::Integration();
}

NrdDenoiser::~NrdDenoiser() {
    Shutdown();
    if (m_nrdIntegration) {
        delete m_nrdIntegration;
        m_nrdIntegration = nullptr;
    }
}

bool NrdDenoiser::Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    if (m_initialized) return true;

    m_d3dDevice = device;
    m_d3dQueue = cmdQueue;

    m_initialized = true;
    return true;
}

void NrdDenoiser::Shutdown() {
    if (!m_initialized) return;

    if (m_nrdIntegration) {
        m_nrdIntegration->Destroy();
    }
    m_initialized = false;
}

void NrdDenoiser::Recreate(uint32_t width, uint32_t height) {
    if (!m_initialized) return;

    m_width = width;
    m_height = height;

    // Setup integration creation desc
    nrd::IntegrationCreationDesc integrationDesc = {};
    integrationDesc.resourceWidth = width;
    integrationDesc.resourceHeight = height;
    integrationDesc.queuedFrameNum = 2; // match our render pipeline
    integrationDesc.enableWholeLifetimeDescriptorCaching = false;
    integrationDesc.demoteFloat32to16 = false;

    // Setup instance creation desc
    nrd::DenoiserDesc denoisers[1] = {};
    denoisers[0].identifier = 0;
    denoisers[0].denoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;

    nrd::InstanceCreationDesc instanceDesc = {};
    instanceDesc.denoisers = denoisers;
    instanceDesc.denoisersNum = 1;

    ID3D12CommandQueue* queues[] = { m_d3dQueue };

    nri::QueueFamilyD3D12Desc queueDesc = {};
    queueDesc.d3d12Queues = queues;
    queueDesc.queueNum = 1;
    queueDesc.queueType = nri::QueueType::GRAPHICS;

    nri::DeviceCreationD3D12Desc deviceDesc = {};
    deviceDesc.d3d12Device = m_d3dDevice;
    deviceDesc.queueFamilies = &queueDesc;
    deviceDesc.queueFamilyNum = 1;

    m_nrdIntegration->RecreateD3D12(integrationDesc, instanceDesc, deviceDesc);
}

void NrdDenoiser::Denoise(ID3D12GraphicsCommandList* cmdList,
                 ID3D12Resource* inDiffuseRadianceHitDist,
                 ID3D12Resource* inSpecRadianceHitDist,
                 ID3D12Resource* inViewZ,
                 ID3D12Resource* inNormalRoughness,
                 ID3D12Resource* inMv,
                 ID3D12Resource* outDiffuse,
                 ID3D12Resource* outSpecular,
                 const struct CameraCB& cam,
                 float jitterX,
                 float jitterY,
                 bool resetHistory) {

    if (!m_initialized || !m_nrdIntegration) return;

    m_nrdIntegration->NewFrame();

    nrd::CommonSettings commonSettings = {};
    
    // Calculate matrices using DirectXMath
    XMVECTOR pos = XMLoadFloat3((const XMFLOAT3*)cam.pos);
    XMVECTOR fwd = XMLoadFloat3((const XMFLOAT3*)cam.forward);
    XMVECTOR up = XMLoadFloat3((const XMFLOAT3*)cam.up);

    XMMATRIX worldToView = XMMatrixLookToRH(pos, fwd, up);
    XMMATRIX viewToWorld = XMMatrixInverse(nullptr, worldToView);
    XMMATRIX viewToClip = XMMatrixPerspectiveFovRH(XMConvertToRadians(cam.fov), cam.aspect, cam.nearZ, cam.farZ);
    XMMATRIX clipToView = XMMatrixInverse(nullptr, viewToClip);

    // Prev matrices
    XMVECTOR prevPos = XMLoadFloat3((const XMFLOAT3*)cam.prevPos);
    XMVECTOR prevFwd = XMLoadFloat3((const XMFLOAT3*)cam.prevForward);
    XMVECTOR prevUp = XMLoadFloat3((const XMFLOAT3*)cam.prevUp);

    XMMATRIX worldToViewPrev = XMMatrixLookToRH(prevPos, prevFwd, prevUp);
    XMMATRIX viewToClipPrev = XMMatrixPerspectiveFovRH(XMConvertToRadians(cam.prevFov), cam.prevAspect, cam.prevNearZ, cam.prevFarZ);

    XMStoreFloat4x4((XMFLOAT4X4*)commonSettings.viewToClipMatrix, XMMatrixTranspose(viewToClip));
    XMStoreFloat4x4((XMFLOAT4X4*)commonSettings.viewToClipMatrixPrev, XMMatrixTranspose(viewToClipPrev));
    XMStoreFloat4x4((XMFLOAT4X4*)commonSettings.worldToViewMatrix, XMMatrixTranspose(worldToView));
    XMStoreFloat4x4((XMFLOAT4X4*)commonSettings.worldToViewMatrixPrev, XMMatrixTranspose(worldToViewPrev));

    for (int i = 0; i < 16; i++) {
        commonSettings.cameraJitter[0] = jitterX;
        commonSettings.cameraJitter[1] = jitterY;
        commonSettings.cameraJitterPrev[0] = jitterX; // Approximate for previous frame
        commonSettings.cameraJitterPrev[1] = jitterY;
    }
    
    // Basic Common Settings
    commonSettings.resourceSize[0] = static_cast<uint16_t>(m_width);
    commonSettings.resourceSize[1] = static_cast<uint16_t>(m_height);
    commonSettings.rectSize[0] = static_cast<uint16_t>(m_width);
    commonSettings.rectSize[1] = static_cast<uint16_t>(m_height);
    
    commonSettings.cameraJitter[0] = jitterX;
    commonSettings.cameraJitter[1] = jitterY;
    commonSettings.frameIndex = (uint32_t)cam.frameCount;
    commonSettings.accumulationMode = resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    
    m_nrdIntegration->SetCommonSettings(commonSettings);

    commonSettings.motionVectorScale[0] = 1.0f;
    commonSettings.motionVectorScale[1] = 1.0f;
    commonSettings.motionVectorScale[2] = 0.0f;

    static uint32_t s_frameIndex = 0;
    if (resetHistory) s_frameIndex = 0;
    commonSettings.frameIndex = s_frameIndex++;

    m_nrdIntegration->SetCommonSettings(commonSettings);

    // Setup relax settings
    nrd::RelaxSettings relaxSettings = {};
    m_nrdIntegration->SetDenoiserSettings(nrd::Identifier(0), &relaxSettings);

    nrd::ResourceSnapshot resourceSnapshot = {};
    resourceSnapshot.restoreInitialState = true;

    auto SetResource = [&](nrd::ResourceType type, ID3D12Resource* res, int32_t format) {
        nrd::Resource r = {};
        r.d3d12.resource = res;
        r.d3d12.format = format;
        r.state.access = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        r.state.layout = nri::Layout::SHADER_RESOURCE_STORAGE;
        r.state.stages = nri::StageBits::COMPUTE_SHADER;
        resourceSnapshot.SetResource(type, r);
    };

    SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, inDiffuseRadianceHitDist, 0);
    SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, inSpecRadianceHitDist, 0);
    SetResource(nrd::ResourceType::IN_VIEWZ, inViewZ, 0);
    SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, inNormalRoughness, 0);
    SetResource(nrd::ResourceType::IN_MV, inMv, 0);
    SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, outDiffuse, 0);
    SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, outSpecular, 0);

    nrd::Identifier denoisers[] = { 0 };

    nri::CommandBufferD3D12Desc cmdDesc = {};
    cmdDesc.d3d12CommandList = cmdList;

    m_nrdIntegration->DenoiseD3D12(denoisers, 1, cmdDesc, resourceSnapshot);
}
