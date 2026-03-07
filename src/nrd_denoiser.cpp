#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "nrd_denoiser.h"
#include "camera.h"

#include <algorithm>
#include <cmath>

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
    m_frameIndex = 0;
    m_prevJitterX = 0.0f;
    m_prevJitterY = 0.0f;
    m_hasPrevJitter = false;

    // Setup integration creation desc
    nrd::IntegrationCreationDesc integrationDesc = {};
    integrationDesc.resourceWidth = static_cast<uint16_t>((std::min)(width, 65535u));
    integrationDesc.resourceHeight = static_cast<uint16_t>((std::min)(height, 65535u));
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
    if (resetHistory) {
        m_frameIndex = 0;
        m_hasPrevJitter = false;
    }

    auto Normalize3 = [](const float v[3], float out[3]) {
        const float lenSq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        const float invLen = (lenSq > 1e-20f) ? (1.0f / std::sqrt(lenSq)) : 1.0f;
        out[0] = v[0] * invLen;
        out[1] = v[1] * invLen;
        out[2] = v[2] * invLen;
    };

    auto Cross3 = [](const float a[3], const float b[3], float out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    };

    auto Dot3 = [](const float a[3], const float b[3]) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    auto BuildWorldToViewRowMajor = [&](const float pos[3], const float fwdIn[3], const float upIn[3], float out[16]) {
        float fwd[3];
        float up[3];
        float right[3];
        Normalize3(fwdIn, fwd);
        Normalize3(upIn, up);
        Cross3(fwd, up, right);
        Normalize3(right, right);
        Cross3(right, fwd, up);
        Normalize3(up, up);

        out[0] = right[0]; out[1] = up[0]; out[2] = fwd[0]; out[3] = 0.0f;
        out[4] = right[1]; out[5] = up[1]; out[6] = fwd[1]; out[7] = 0.0f;
        out[8] = right[2]; out[9] = up[2]; out[10] = fwd[2]; out[11] = 0.0f;
        out[12] = -Dot3(pos, right); out[13] = -Dot3(pos, up); out[14] = -Dot3(pos, fwd); out[15] = 1.0f;
    };

    auto BuildViewToClipRowMajor = [](float fovDeg, float aspect, float nearZ, float farZ, float out[16]) {
        const float safeNear = (nearZ > 0.0f) ? nearZ : 0.001f;
        const float safeFar = (farZ > safeNear + 1e-6f) ? farZ : (safeNear + 1.0f);
        const float safeAspect = (aspect > 1e-6f) ? aspect : 1.0f;
        const float f = 1.0f / std::tan((fovDeg * 3.14159265359f / 180.0f) * 0.5f);
        const float A = safeFar / (safeFar - safeNear);
        const float B = (-safeNear * safeFar) / (safeFar - safeNear);

        out[0] = f / safeAspect; out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f;
        out[4] = 0.0f; out[5] = f; out[6] = 0.0f; out[7] = 0.0f;
        out[8] = 0.0f; out[9] = 0.0f; out[10] = A; out[11] = 1.0f;
        out[12] = 0.0f; out[13] = 0.0f; out[14] = B; out[15] = 0.0f;
    };

    float worldToView[16];
    float worldToViewPrev[16];
    float viewToClip[16];
    float viewToClipPrev[16];
    BuildWorldToViewRowMajor(cam.pos, cam.forward, cam.up, worldToView);
    BuildWorldToViewRowMajor(cam.prevPos, cam.prevForward, cam.prevUp, worldToViewPrev);
    BuildViewToClipRowMajor(cam.fov, cam.aspect, cam.nearZ, cam.farZ, viewToClip);
    BuildViewToClipRowMajor(cam.prevFov, cam.prevAspect, cam.prevNearZ, cam.prevFarZ, viewToClipPrev);
    memcpy(commonSettings.viewToClipMatrix, viewToClip, sizeof(viewToClip));
    memcpy(commonSettings.viewToClipMatrixPrev, viewToClipPrev, sizeof(viewToClipPrev));
    memcpy(commonSettings.worldToViewMatrix, worldToView, sizeof(worldToView));
    memcpy(commonSettings.worldToViewMatrixPrev, worldToViewPrev, sizeof(worldToViewPrev));

    commonSettings.resourceSize[0] = static_cast<uint16_t>(m_width);
    commonSettings.resourceSize[1] = static_cast<uint16_t>(m_height);
    commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
    commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
    commonSettings.rectSize[0] = static_cast<uint16_t>(m_width);
    commonSettings.rectSize[1] = static_cast<uint16_t>(m_height);
    commonSettings.rectSizePrev[0] = commonSettings.rectSize[0];
    commonSettings.rectSizePrev[1] = commonSettings.rectSize[1];
    commonSettings.cameraJitter[0] = jitterX;
    commonSettings.cameraJitter[1] = jitterY;
    commonSettings.cameraJitterPrev[0] = m_hasPrevJitter ? m_prevJitterX : jitterX;
    commonSettings.cameraJitterPrev[1] = m_hasPrevJitter ? m_prevJitterY : jitterY;
    // Screen-space motion vectors in pixel units; NRD normalizes them with the
    // scale below.  World-space mode is NOT used because our MV texture is
    // RG16F (2-channel) and NRD would read garbage for the third component.
    commonSettings.isMotionVectorInWorldSpace = false;
    commonSettings.motionVectorScale[0] = (m_width  > 0) ? (1.0f / static_cast<float>(m_width))  : 1.0f;
    commonSettings.motionVectorScale[1] = (m_height > 0) ? (1.0f / static_cast<float>(m_height)) : 1.0f;
    commonSettings.motionVectorScale[2] = 0.0f;
    commonSettings.frameIndex = m_frameIndex++;
    commonSettings.accumulationMode = resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    commonSettings.denoisingRange = (cam.farZ > 1.0f) ? cam.farZ : 500000.0f;

    m_nrdIntegration->SetCommonSettings(commonSettings);
    m_prevJitterX = jitterX;
    m_prevJitterY = jitterY;
    m_hasPrevJitter = true;

    // RELAX settings tuned for 1-spp realtime path tracing WITH albedo demodulation.
    // Albedo is now stripped from the diffuse input, so NRD operates on irradiance
    // which is much smoother scene-to-scene.  Moderate aggressiveness gives clean
    // output without over-blurring fine detail.
    nrd::RelaxSettings relaxSettings = {};
    relaxSettings.diffuseMaxAccumulatedFrameNum  = 32;
    relaxSettings.specularMaxAccumulatedFrameNum = 32;
    relaxSettings.diffuseMaxFastAccumulatedFrameNum  = 6;
    relaxSettings.specularMaxFastAccumulatedFrameNum = 6;
    relaxSettings.historyFixFrameNum = 4;
    // Prepass Gaussian helps gather energy from sparse 1-spp traces.
    // With demodulated irradiance these values preserve texture edges well.
    relaxSettings.diffusePrepassBlurRadius  = 30.0f;
    relaxSettings.specularPrepassBlurRadius = 50.0f;
    relaxSettings.minHitDistanceWeight = 0.05f;
    // 5 A-Trous passes = ~32px effective spatial radius; appropriate for 1-spp.
    relaxSettings.atrousIterationNum = 5;
    relaxSettings.enableAntiFirefly = true;
    relaxSettings.luminanceEdgeStoppingRelaxation = 0.5f;
    relaxSettings.normalEdgeStoppingRelaxation    = 0.3f;
    m_nrdIntegration->SetDenoiserSettings(nrd::Identifier(0), &relaxSettings);

    nrd::ResourceSnapshot resourceSnapshot = {};
    resourceSnapshot.restoreInitialState = true;

    auto SetResource = [&](nrd::ResourceType type, ID3D12Resource* res) {
        if (!res) return;
        nrd::Resource r = {};
        r.d3d12.resource = res;
        r.d3d12.format = static_cast<int32_t>(res->GetDesc().Format);
        r.state.access = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        r.state.layout = nri::Layout::SHADER_RESOURCE_STORAGE;
        r.state.stages = nri::StageBits::COMPUTE_SHADER;
        resourceSnapshot.SetResource(type, r);
    };

    SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, inDiffuseRadianceHitDist);
    SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, inSpecRadianceHitDist);
    SetResource(nrd::ResourceType::IN_VIEWZ, inViewZ);
    SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, inNormalRoughness);
    SetResource(nrd::ResourceType::IN_MV, inMv);
    SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, outDiffuse);
    SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, outSpecular);

    nrd::Identifier denoisers[] = { 0 };

    nri::CommandBufferD3D12Desc cmdDesc = {};
    cmdDesc.d3d12CommandList = cmdList;

    m_nrdIntegration->DenoiseD3D12(denoisers, 1, cmdDesc, resourceSnapshot);
}
