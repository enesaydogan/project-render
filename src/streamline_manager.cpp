#include "streamline_manager.h"

#include "camera.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sl_security.h>

using Microsoft::WRL::ComPtr;

namespace {
static void SLLogCallback(sl::LogType type, const char* msg) {
  // Keep it simple: mirror to stderr.
  const char* prefix = "SL";
  switch (type) {
  case sl::LogType::eInfo:
    prefix = "SL-INFO";
    break;
  case sl::LogType::eWarn:
    prefix = "SL-WARN";
    break;
  case sl::LogType::eError:
    prefix = "SL-ERR";
    break;
  default:
    break;
  }
  fprintf(stderr, "%s: %s\n", prefix, msg ? msg : "(null)");
}

static std::wstring GetExecutableDir() {
  wchar_t path[MAX_PATH] = {};
  DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return L".";
  }
  std::filesystem::path p(path);
  return p.parent_path().wstring();
}

static sl::float4x4 MakePerspectiveViewToClip(float fovDegrees, float aspect,
                                             float nearZ, float farZ) {
  // Row-major matrix, matching the projection math used in your shaders.
  //
  // In simple.hlsl you effectively do:
  //   A = far/(far-near)
  //   B = -near*far/(far-near)
  //   clip = [ x*f/aspect, y*f, z*A + B, z ]
  // where f = 1/tan(fov/2)
  const float f = 1.0f / tanf((fovDegrees * 3.14159265359f / 180.0f) * 0.5f);
  const float A = farZ / (farZ - nearZ);
  const float B = (-nearZ * farZ) / (farZ - nearZ);

  sl::float4x4 m{};
  m[0] = sl::float4(f / aspect, 0, 0, 0);
  m[1] = sl::float4(0, f, 0, 0);
  m[2] = sl::float4(0, 0, A, 1);
  m[3] = sl::float4(0, 0, B, 0);
  return m;
}

static sl::float3 ToSl3(const float v[3]) { return sl::float3(v[0], v[1], v[2]); }

static sl::float4x4 MakeCameraViewToWorld(const CameraCB& cam) {
  // Build an orthonormal basis like in the shaders.
  sl::float3 fwd = ToSl3(cam.forward);
  sl::float3 up = ToSl3(cam.up);
  sl::float3 right;
  sl::vectorCrossProduct(right, fwd, up);
  sl::vectorNormalize(right);
  sl::vectorCrossProduct(up, fwd, right);
  sl::vectorNormalize(up);
  sl::vectorNormalize(fwd);

  sl::float4x4 m{};
  m[0] = sl::float4(right.x, right.y, right.z, 0.0f);
  m[1] = sl::float4(up.x, up.y, up.z, 0.0f);
  m[2] = sl::float4(fwd.x, fwd.y, fwd.z, 0.0f);
  m[3] = sl::float4(cam.pos[0], cam.pos[1], cam.pos[2], 1.0f);
  return m;
}

static sl::float4x4 MakeWorldToCameraView(const CameraCB& cam) {
  sl::float4x4 camViewToWorld = MakeCameraViewToWorld(cam);
  sl::float4x4 worldToCamera{};
  sl::matrixOrthoNormalInvert(worldToCamera, camViewToWorld);
  return worldToCamera;
}

static void FillExtent(sl::Extent& e, uint32_t w, uint32_t h) {
  e.left = 0;
  e.top = 0;
  e.width = w;
  e.height = h;
}

static float Halton(uint32_t index, uint32_t base) {
  float f = 1.0f;
  float r = 0.0f;
  while (index > 0) {
    f /= (float)base;
    r += f * (float)(index % base);
    index /= base;
  }
  return r;
}

static uint32_t ParseUint32(const std::string& s) {
  // Accept decimal or 0x-prefixed hex.
  char* end = nullptr;
  unsigned long v = std::strtoul(s.c_str(), &end, 0);
  if (!end || end == s.c_str())
    return 0;
  if (v > 0xFFFFFFFFul)
    return 0;
  return (uint32_t)v;
}

static uint32_t ReadApplicationIdFromFile(const std::wstring& exeDir) {
  // Simple convention: drop a text file next to the exe with the NGX app id.
  // Example content: 1234567
  const std::filesystem::path p = std::filesystem::path(exeDir) / L"sl_appid.txt";
  std::ifstream f(p, std::ios::in);
  if (!f.is_open())
    return 0;
  std::string line;
  if (!std::getline(f, line))
    return 0;
  return ParseUint32(line);
}

static uint32_t ReadApplicationIdFromEnv() {
  // Prefer the generic Streamline name, but allow a project-specific one too.
  wchar_t buf[128] = {};
  DWORD n = GetEnvironmentVariableW(L"SL_APPLICATION_ID", buf, (DWORD)_countof(buf));
  if (n > 0 && n < _countof(buf)) {
    std::wstring ws(buf);
    std::string s(ws.begin(), ws.end());
    return ParseUint32(s);
  }
  n = GetEnvironmentVariableW(L"PROJECT_RENDER_SL_APPLICATION_ID", buf,
                              (DWORD)_countof(buf));
  if (n > 0 && n < _countof(buf)) {
    std::wstring ws(buf);
    std::string s(ws.begin(), ws.end());
    return ParseUint32(s);
  }
  return 0;
}

static bool ReadBoolEnv(const wchar_t* name, bool defaultValue) {
  wchar_t buf[16] = {};
  DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)_countof(buf));
  if (n == 0 || n >= _countof(buf))
    return defaultValue;
  // Accept 0/1, true/false, yes/no
  if (_wcsicmp(buf, L"1") == 0 || _wcsicmp(buf, L"true") == 0 ||
      _wcsicmp(buf, L"yes") == 0 || _wcsicmp(buf, L"on") == 0)
    return true;
  if (_wcsicmp(buf, L"0") == 0 || _wcsicmp(buf, L"false") == 0 ||
      _wcsicmp(buf, L"no") == 0 || _wcsicmp(buf, L"off") == 0)
    return false;
  return defaultValue;
}

// TODO: replace with your NVIDIA-provided NGX applicationId.
static constexpr uint32_t kDefaultNgxApplicationId = 24;
} // namespace

StreamlineManager::StreamlineManager() = default;

StreamlineManager::~StreamlineManager() { Shutdown(); }

void StreamlineManager::SetApplicationId(uint32_t applicationId) {
  if (m_initialized)
    return; // too late
  m_applicationId = applicationId;
}

void StreamlineManager::SetLogToFile(bool enabled) {
  if (m_initialized)
    return; // too late
  m_logToFile = enabled;
}

void StreamlineManager::SetMirrorLogsToStderr(bool enabled) {
  if (m_initialized)
    return; // too late
  m_mirrorLogsToStderr = enabled;
}

bool StreamlineManager::InitializeEarly() {
  if (!m_enabled)
    return false;
  if (m_initialized)
    return true;

  m_pluginDir = GetExecutableDir();

  // Allow lightweight overrides without code changes.
  // - PROJECT_RENDER_SL_LOG_TO_FILE=0 disables file logging.
  // - PROJECT_RENDER_SL_MIRROR_LOGS=1 mirrors SL log messages to stderr.
  m_logToFile = ReadBoolEnv(L"PROJECT_RENDER_SL_LOG_TO_FILE", m_logToFile);
  m_mirrorLogsToStderr =
      ReadBoolEnv(L"PROJECT_RENDER_SL_MIRROR_LOGS", m_mirrorLogsToStderr);

  if (m_applicationId == 0) {
    // Allow config without code changes.
    m_applicationId = ReadApplicationIdFromEnv();
    if (m_applicationId == 0) {
      m_applicationId = ReadApplicationIdFromFile(m_pluginDir);
    }
  }
  if (m_applicationId == 0) {
    m_applicationId = kDefaultNgxApplicationId;
  }

  if (!LoadInterposer()) {
    fprintf(stderr,
            "StreamlineManager: sl.interposer.dll not available; DLSS disabled\n");
    return false;
  }

  if (!LoadCoreFunctions()) {
    fprintf(stderr,
            "StreamlineManager: Failed to load core Streamline exports\n");
    return false;
  }

  // Request features at init.
  sl::Feature featuresToLoad[] = {sl::kFeatureDLSS, sl::kFeatureDLSS_RR};

  m_logsDir = m_pluginDir + L"\\sl_logs";
  if (m_logToFile) {
    std::filesystem::create_directories(m_logsDir);
  }

  const wchar_t* pluginPaths[] = {m_pluginDir.c_str()};

  sl::Preferences pref{};
  pref.showConsole = false;
  pref.logLevel = sl::LogLevel::eDefault;
  pref.pathsToPlugins = pluginPaths;
  pref.numPathsToPlugins = 1;
  pref.pathToLogsAndData = m_logToFile ? m_logsDir.c_str() : nullptr;
  // IMPORTANT: setting a callback here mirrors SL logs into our stderr.
  // Leave it off by default so app logs stay clean.
  pref.logMessageCallback = m_mirrorLogsToStderr ? &SLLogCallback : nullptr;
  pref.renderAPI = sl::RenderAPI::eD3D12;
  // NGX-backed features (DLSS/DLSS-RR) require an application id.
  pref.applicationId = 24;
  pref.engine = sl::EngineType::eCustom;
  pref.engineVersion = "1.0.0";
  pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";
  pref.flags = sl::PreferenceFlags::eDisableCLStateTracking |
               sl::PreferenceFlags::eAllowOTA |
               sl::PreferenceFlags::eLoadDownloadedPlugins |
               sl::PreferenceFlags::eUseFrameBasedResourceTagging;
  pref.featuresToLoad = featuresToLoad;
  pref.numFeaturesToLoad = (uint32_t)_countof(featuresToLoad);

  sl::Result res = m_slInit(pref, sl::kSDKVersion);
  if (res != sl::Result::eOk) {
    fprintf(stderr, "StreamlineManager: slInit failed (%d)\n", (int)res);
    return false;
  }

  if (m_applicationId == 0) {
    fprintf(stderr,
            "StreamlineManager: NOTE: No NGX applicationId configured. "
            "Using placeholder id=1. For production NGX DLLs you must set "
            "SL_APPLICATION_ID or sl_appid.txt.\n");
  }

  m_initialized = true;
  return true;
}

bool StreamlineManager::OnD3D12DeviceCreated(ID3D12Device* device) {
  if (!m_initialized)
    return false;
  if (!device)
    return false;

  sl::Result res = m_slSetD3DDevice(device);
  if (res != sl::Result::eOk) {
    fprintf(stderr, "StreamlineManager: slSetD3DDevice failed (%d)\n",
            (int)res);
    return false;
  }

  m_deviceSet = true;

  // Cache adapter LUID for slIsFeatureSupported checks.
  // (ID3D12Device::GetAdapterLuid is available on D3D12 devices.)
  m_adapterLuid = device->GetAdapterLuid();
  m_hasAdapterLuid = true;

  // Resolve feature function pointers.
  m_featureFunctionsReady = LoadFeatureFunctions();
  if (!m_featureFunctionsReady) {
    fprintf(stderr,
            "StreamlineManager: Feature functions not available (DLSS may be "
            "disabled - check NGX applicationId and Streamline logs)\n");
  }

  return true;
}

void StreamlineManager::Shutdown() {
  if (m_slShutdown && m_initialized) {
    m_slShutdown();
  }
  m_initialized = false;
  m_deviceSet = false;
  m_featureFunctionsReady = false;

  if (m_interposerModule) {
    FreeLibrary(m_interposerModule);
    m_interposerModule = nullptr;
  }

  m_cachedOptimalValid = false;
}

void StreamlineManager::SetEnabled(bool enabled) {
  m_enabled = enabled;
  if (!enabled) {
    SetMode(Mode::Off);
  }
}

void StreamlineManager::SetMode(Mode mode) {
  if (m_mode == mode)
    return;
  m_mode = mode;
  m_cachedOptimalValid = false;
}

void StreamlineManager::SetQuality(Quality quality) {
  if (m_quality == quality)
    return;
  m_quality = quality;
  m_cachedOptimalValid = false;
}

sl::DLSSMode StreamlineManager::ToSlDlssMode(Quality q) const {
  switch (q) {
  case Quality::MaxPerformance:
    return sl::DLSSMode::eMaxPerformance;
  case Quality::Balanced:
    return sl::DLSSMode::eBalanced;
  case Quality::MaxQuality:
    return sl::DLSSMode::eMaxQuality;
  case Quality::UltraPerformance:
    return sl::DLSSMode::eUltraPerformance;
  case Quality::UltraQuality:
    return sl::DLSSMode::eUltraQuality;
  case Quality::DLAA:
    return sl::DLSSMode::eDLAA;
  default:
    return sl::DLSSMode::eBalanced;
  }
}

StreamlineManager::RecommendedRenderSize
StreamlineManager::GetRecommendedRenderSize(uint32_t outputWidth,
                                            uint32_t outputHeight) const {
  RecommendedRenderSize out{outputWidth, outputHeight};
  if (!m_initialized || !m_deviceSet || !m_cachedOptimalValid ||
      m_cachedOptimalOutW != outputWidth || m_cachedOptimalOutH != outputHeight ||
      m_cachedOptimalMode != m_mode ||
      m_cachedOptimalQuality != m_quality) {
    // recompute
    if (!m_initialized || !m_deviceSet) {
      return out;
    }

    if (m_mode == Mode::DLSS_SuperResolution && m_slDLSSGetOptimalSettings) {
      sl::DLSSOptions options{};
      options.mode = ToSlDlssMode(m_quality);
      options.outputWidth = outputWidth;
      options.outputHeight = outputHeight;
      options.colorBuffersHDR = sl::Boolean::eFalse;

      sl::DLSSOptimalSettings settings{};
      sl::Result res = m_slDLSSGetOptimalSettings(options, settings);
      if (res == sl::Result::eOk && settings.optimalRenderWidth > 0 &&
          settings.optimalRenderHeight > 0) {
        m_cachedDlssOptimal = settings;
        m_cachedOptimalValid = true;
        m_cachedOptimalOutW = outputWidth;
        m_cachedOptimalOutH = outputHeight;
        m_cachedOptimalMode = m_mode;
        m_cachedOptimalQuality = m_quality;
        out.renderWidth = settings.optimalRenderWidth;
        out.renderHeight = settings.optimalRenderHeight;
        return out;
      }

      return out;
    }

    if (m_mode == Mode::DLSS_RayReconstruction && m_slDLSSDGetOptimalSettings) {
      sl::DLSSDOptions options{};
      options.mode = ToSlDlssMode(m_quality);
      options.outputWidth = outputWidth;
      options.outputHeight = outputHeight;
      // DLSS-RR only supports HDR pipelines.
      options.colorBuffersHDR = sl::Boolean::eTrue;
      // matrices filled during Evaluate

      sl::DLSSDOptimalSettings settings{};
      sl::Result res = m_slDLSSDGetOptimalSettings(options, settings);
      if (res == sl::Result::eOk && settings.optimalRenderWidth > 0 &&
          settings.optimalRenderHeight > 0) {
        m_cachedDlssdOptimal = settings;
        m_cachedOptimalValid = true;
        m_cachedOptimalOutW = outputWidth;
        m_cachedOptimalOutH = outputHeight;
        m_cachedOptimalMode = m_mode;
        m_cachedOptimalQuality = m_quality;
        out.renderWidth = settings.optimalRenderWidth;
        out.renderHeight = settings.optimalRenderHeight;
        return out;
      }
      return out;
    }

    // Off
    return out;
  }

  // cached
  if (m_mode == Mode::DLSS_SuperResolution) {
    out.renderWidth = m_cachedDlssOptimal.optimalRenderWidth;
    out.renderHeight = m_cachedDlssOptimal.optimalRenderHeight;
  } else if (m_mode == Mode::DLSS_RayReconstruction) {
    out.renderWidth = m_cachedDlssdOptimal.optimalRenderWidth;
    out.renderHeight = m_cachedDlssdOptimal.optimalRenderHeight;
  }

  return out;
}

bool StreamlineManager::Evaluate(
    ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colorIn,
    D3D12_RESOURCE_STATES colorInState, ID3D12Resource* colorOut,
    D3D12_RESOURCE_STATES colorOutState, uint32_t renderWidth,
    uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
    ID3D12Resource* depth, D3D12_RESOURCE_STATES depthState,
    ID3D12Resource* mvec, D3D12_RESOURCE_STATES mvecState,
    ID3D12Resource* normalRoughness,
    D3D12_RESOURCE_STATES normalRoughnessState, ID3D12Resource* albedo,
    D3D12_RESOURCE_STATES albedoState,
    ID3D12Resource* specularAlbedo,
    D3D12_RESOURCE_STATES specularAlbedoState,
    ID3D12Resource* specularHitDistance,
    D3D12_RESOURCE_STATES specularHitDistanceState,
    ID3D12Resource* specularMotionVectors,
    D3D12_RESOURCE_STATES specularMotionVectorsState,
    bool resetHistory, float jitterX, float jitterY) {
  if (!m_enabled || m_mode == Mode::Off)
    return false;
  if (!m_initialized || !m_deviceSet)
    return false;
  if (!m_featureFunctionsReady)
    return false;
  if (!cmdList || !colorIn || !colorOut || !depth || !mvec)
    return false;

  // Frame token
  sl::FrameToken* frameToken = nullptr;
  uint32_t frameIndex = m_frameCounter++;
  sl::Result res = m_slGetNewFrameToken(frameToken, &frameIndex);
  if (res != sl::Result::eOk || !frameToken) {
    fprintf(stderr, "StreamlineManager: slGetNewFrameToken failed (%d)\n",
            (int)res);
    return false;
  }

  // Common constants
  sl::Constants c{};

  // Camera matrices must NOT include jitter; provide jitter separately.
  const CameraCB& cam = g_cameraData;

  CameraCB prevCam = cam;
  if (cam.prevValid > 0.5f) {
    prevCam.pos[0] = cam.prevPos[0];
    prevCam.pos[1] = cam.prevPos[1];
    prevCam.pos[2] = cam.prevPos[2];
    prevCam.forward[0] = cam.prevForward[0];
    prevCam.forward[1] = cam.prevForward[1];
    prevCam.forward[2] = cam.prevForward[2];
    prevCam.up[0] = cam.prevUp[0];
    prevCam.up[1] = cam.prevUp[1];
    prevCam.up[2] = cam.prevUp[2];
    prevCam.fov = cam.prevFov;
    prevCam.aspect = cam.prevAspect;
    prevCam.nearZ = cam.prevNearZ;
    prevCam.farZ = cam.prevFarZ;
  }

  c.cameraViewToClip = MakePerspectiveViewToClip(cam.fov, cam.aspect, cam.nearZ,
                                                 cam.farZ);
  sl::matrixFullInvert(c.clipToCameraView, c.cameraViewToClip);

  // Camera vectors
  c.cameraPos = sl::float3(cam.pos[0], cam.pos[1], cam.pos[2]);
  c.cameraFwd = sl::float3(cam.forward[0], cam.forward[1], cam.forward[2]);
  c.cameraUp = sl::float3(cam.up[0], cam.up[1], cam.up[2]);
  sl::vectorCrossProduct(c.cameraRight, c.cameraFwd, c.cameraUp);
  sl::vectorNormalize(c.cameraRight);
  sl::vectorCrossProduct(c.cameraUp, c.cameraFwd, c.cameraRight);
  sl::vectorNormalize(c.cameraUp);
  sl::vectorNormalize(c.cameraFwd);

  // Prev transforms
  sl::float4x4 cameraViewToWorld = MakeCameraViewToWorld(cam);
  sl::float4x4 cameraViewToWorldPrev = MakeCameraViewToWorld(prevCam);

  sl::float4x4 cameraViewToPrevCameraView;
  sl::calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld,
                             cameraViewToWorldPrev);

  sl::float4x4 prevViewToClip = MakePerspectiveViewToClip(
      prevCam.fov, prevCam.aspect, prevCam.nearZ, prevCam.farZ);

  sl::float4x4 clipToPrevCameraView;
  sl::matrixMul(clipToPrevCameraView, c.clipToCameraView,
                cameraViewToPrevCameraView);
  sl::matrixMul(c.clipToPrevClip, clipToPrevCameraView, prevViewToClip);
  sl::matrixFullInvert(c.prevClipToClip, c.clipToPrevClip);

  // Match raygen jitter (see shaders/path_tracer_core.hlsl): Halton jitter in
  // pixel units.
  // Using passed jitter values to ensure synchronization with shader.
  c.jitterOffset = sl::float2(jitterX, jitterY);

  // Required by Streamline common validation; 0 offset for a pinhole camera.
  c.cameraPinholeOffset = sl::float2(0.0f, 0.0f);

  // Motion vectors are in pixel space (see ProgrammingGuideDLSS_RR.md).
  c.mvecScale =
      sl::float2(1.0f / (float)renderWidth, 1.0f / (float)renderHeight);

  c.cameraNear = cam.nearZ;
  c.cameraFar = cam.farZ;
  c.cameraFOV = cam.fov * 3.14159265359f / 180.0f;
  c.cameraAspectRatio = cam.aspect;

  c.depthInverted = sl::Boolean::eFalse;
  c.cameraMotionIncluded = sl::Boolean::eTrue;
  c.motionVectors3D = sl::Boolean::eFalse;
  c.motionVectorsDilated = sl::Boolean::eFalse;
  // Shader calculates motion vectors without the jitter offset applied.
  c.motionVectorsJittered = sl::Boolean::eFalse;
  c.reset = resetHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  // Streamline 2.10 Constants has no 'renderingGameFrames' field.

  // Provide a reasonable invalid value (we always write motion)
  c.motionVectorsInvalidValue = 0.0f;

  res = m_slSetConstants(c, *frameToken, m_viewport);
  if (res != sl::Result::eOk) {
    fprintf(stderr, "StreamlineManager: slSetConstants failed (%d)\n", (int)res);
    return false;
  }

  // Feature options
  if (m_mode == Mode::DLSS_SuperResolution) {
    if (!m_slDLSSSetOptions)
      return false;
    sl::DLSSOptions opt{};
    opt.mode = ToSlDlssMode(m_quality);
    opt.outputWidth = outputWidth;
    opt.outputHeight = outputHeight;
    opt.colorBuffersHDR = sl::Boolean::eFalse;
    res = m_slDLSSSetOptions(m_viewport, opt);
    if (res != sl::Result::eOk) {
      fprintf(stderr, "StreamlineManager: slDLSSSetOptions failed (%d)\n",
              (int)res);
      return false;
    }
  } else if (m_mode == Mode::DLSS_RayReconstruction) {
    // DLSS-RR is an extension of DLSS and requires both DLSS + DLSSD options
    // (see ProgrammingGuideDLSS_RR.md section 5.0).
    if (!m_slDLSSSetOptions || !m_slDLSSDSetOptions)
      return false;

    sl::DLSSOptions dlssOpt{};
    dlssOpt.mode = ToSlDlssMode(m_quality);
    dlssOpt.outputWidth = outputWidth;
    dlssOpt.outputHeight = outputHeight;
    dlssOpt.colorBuffersHDR = sl::Boolean::eTrue;
    res = m_slDLSSSetOptions(m_viewport, dlssOpt);
    if (res != sl::Result::eOk) {
      fprintf(stderr, "StreamlineManager: slDLSSSetOptions failed (%d)\n",
              (int)res);
      return false;
    }

    sl::DLSSDOptions opt{};
    opt.mode = ToSlDlssMode(m_quality);
    opt.outputWidth = outputWidth;
    opt.outputHeight = outputHeight;
    // DLSS-RR only supports HDR pipelines.
    opt.colorBuffersHDR = sl::Boolean::eTrue;
    opt.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
    opt.worldToCameraView = MakeWorldToCameraView(cam);
    opt.cameraViewToWorld = cameraViewToWorld;
    res = m_slDLSSDSetOptions(m_viewport, opt);
    if (res != sl::Result::eOk) {
      fprintf(stderr, "StreamlineManager: slDLSSDSetOptions failed (%d)\n",
              (int)res);
      return false;
    }
  }

  // Build local tags
  sl::Extent renderExtent{};
  FillExtent(renderExtent, renderWidth, renderHeight);
  sl::Extent outputExtent{};
  FillExtent(outputExtent, outputWidth, outputHeight);

  sl::Resource depthRes(sl::ResourceType::eTex2d, depth, (uint32_t)depthState);
  sl::Resource mvecRes(sl::ResourceType::eTex2d, mvec, (uint32_t)mvecState);
  sl::Resource inRes(sl::ResourceType::eTex2d, colorIn, (uint32_t)colorInState);
  sl::Resource outRes(sl::ResourceType::eTex2d, colorOut, (uint32_t)colorOutState);

  sl::ResourceTag depthTag{&depthRes, sl::kBufferTypeDepth,
                           sl::ResourceLifecycle::eValidUntilEvaluate,
                           &renderExtent};
  sl::ResourceTag mvecTag{&mvecRes, sl::kBufferTypeMotionVectors,
                          sl::ResourceLifecycle::eValidUntilEvaluate,
                          &renderExtent};
  sl::ResourceTag colorInTag{&inRes, sl::kBufferTypeScalingInputColor,
                             sl::ResourceLifecycle::eValidUntilEvaluate,
                             &renderExtent};
  sl::ResourceTag colorOutTag{&outRes, sl::kBufferTypeScalingOutputColor,
                              sl::ResourceLifecycle::eValidUntilEvaluate,
                              &outputExtent};

  // Optional tags for DLSS-RR
  sl::Resource nrRes{};
  sl::ResourceTag nrTag{};
  sl::Resource albedoRes{};
  sl::ResourceTag albedoTag{};
  sl::Resource specAlbedoRes{};
  sl::ResourceTag specAlbedoTag{};
  sl::Resource specHitDistRes{};
  sl::ResourceTag specHitDistTag{};
  sl::Resource specMvecRes{};
  sl::ResourceTag specMvecTag{};

  std::vector<const sl::BaseStructure*> inputs;
  inputs.reserve(16);
  inputs.push_back(&m_viewport);
  inputs.push_back(&depthTag);
  inputs.push_back(&mvecTag);
  inputs.push_back(&colorInTag);
  inputs.push_back(&colorOutTag);

  if (m_mode == Mode::DLSS_RayReconstruction && normalRoughness && albedo) {
    nrRes = sl::Resource(sl::ResourceType::eTex2d, normalRoughness,
                         (uint32_t)normalRoughnessState);
    nrTag = sl::ResourceTag{&nrRes, sl::kBufferTypeNormalRoughness,
                            sl::ResourceLifecycle::eValidUntilEvaluate,
                            &renderExtent};
    albedoRes = sl::Resource(sl::ResourceType::eTex2d, albedo,
                             (uint32_t)albedoState);
    albedoTag = sl::ResourceTag{&albedoRes, sl::kBufferTypeAlbedo,
                                sl::ResourceLifecycle::eValidUntilEvaluate,
                                &renderExtent};

    inputs.push_back(&nrTag);
    inputs.push_back(&albedoTag);

    if (specularAlbedo) {
      specAlbedoRes = sl::Resource(sl::ResourceType::eTex2d, specularAlbedo,
                                   (uint32_t)specularAlbedoState);
      specAlbedoTag = sl::ResourceTag{&specAlbedoRes,
                                      sl::kBufferTypeSpecularAlbedo,
                                      sl::ResourceLifecycle::eValidUntilEvaluate,
                                      &renderExtent};
      inputs.push_back(&specAlbedoTag);
    }

    if (specularHitDistance) {
      specHitDistRes = sl::Resource(sl::ResourceType::eTex2d, specularHitDistance,
                                    (uint32_t)specularHitDistanceState);
      specHitDistTag = sl::ResourceTag{&specHitDistRes,
                                       sl::kBufferTypeSpecularHitDistance,
                                       sl::ResourceLifecycle::eValidUntilEvaluate,
                                       &renderExtent};
      inputs.push_back(&specHitDistTag);
    }

    if (specularMotionVectors) {
      specMvecRes = sl::Resource(sl::ResourceType::eTex2d, specularMotionVectors,
                                 (uint32_t)specularMotionVectorsState);
      specMvecTag = sl::ResourceTag{&specMvecRes,
                                    sl::kBufferTypeSpecularMotionVectors,
                                    sl::ResourceLifecycle::eValidUntilEvaluate,
                                    &renderExtent};
      inputs.push_back(&specMvecTag);
    }
  }

  sl::Feature feature = (m_mode == Mode::DLSS_SuperResolution) ? sl::kFeatureDLSS
                                                              : sl::kFeatureDLSS_RR;

  res = m_slEvaluateFeature(feature, *frameToken, inputs.data(),
                            (uint32_t)inputs.size(),
                            reinterpret_cast<sl::CommandBuffer*>(cmdList));
  if (res != sl::Result::eOk) {
    fprintf(stderr, "StreamlineManager: slEvaluateFeature failed (%d)\n",
            (int)res);
    return false;
  }

  return true;
}

HRESULT StreamlineManager::CreateDXGIFactory2(UINT flags, REFIID riid,
                                              void** ppFactory) const {
  if (!m_CreateDXGIFactory2)
    return E_NOINTERFACE;
  return m_CreateDXGIFactory2(flags, riid, ppFactory);
}

HRESULT StreamlineManager::D3D12CreateDevice(IUnknown* adapter,
                                            D3D_FEATURE_LEVEL minFeatureLevel,
                                            REFIID riid,
                                            void** ppDevice) const {
  if (!m_D3D12CreateDevice)
    return E_NOINTERFACE;
  return m_D3D12CreateDevice(adapter, minFeatureLevel, riid, ppDevice);
}

bool StreamlineManager::LoadInterposer() {
  if (m_interposerModule)
    return true;

  std::wstring dllPath = GetExecutableDir() + L"\\sl.interposer.dll";

  // Verify signature if present (development DLLs may be unsigned)
  // If verification fails, we still attempt to load in development.
  bool sigOk = sl::security::verifyEmbeddedSignature(dllPath.c_str());
  if (!sigOk) {
    fprintf(stderr,
            "StreamlineManager: sl.interposer.dll signature not verified (dev?)\n");
  }

  m_interposerModule = LoadLibraryW(dllPath.c_str());
  if (!m_interposerModule) {
    return false;
  }

  // Proxy exports for DXGI/D3D12 creation
  m_CreateDXGIFactory2 = reinterpret_cast<PFun_CreateDXGIFactory2>(
      GetProcAddress(m_interposerModule, "CreateDXGIFactory2"));
  m_D3D12CreateDevice = reinterpret_cast<PFun_D3D12CreateDevice>(
      GetProcAddress(m_interposerModule, "D3D12CreateDevice"));

  return true;
}

bool StreamlineManager::LoadCoreFunctions() {
  if (!m_interposerModule)
    return false;

  m_slInit = reinterpret_cast<PFun_slInit*>(
      GetProcAddress(m_interposerModule, "slInit"));
  m_slShutdown = reinterpret_cast<PFun_slShutdown*>(
      GetProcAddress(m_interposerModule, "slShutdown"));
  m_slSetD3DDevice = reinterpret_cast<PFun_slSetD3DDevice*>(
      GetProcAddress(m_interposerModule, "slSetD3DDevice"));
    m_slIsFeatureSupported = reinterpret_cast<PFun_slIsFeatureSupported*>(
      GetProcAddress(m_interposerModule, "slIsFeatureSupported"));
  m_slGetNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken*>(
      GetProcAddress(m_interposerModule, "slGetNewFrameToken"));
  m_slSetConstants = reinterpret_cast<PFun_slSetConstants*>(
      GetProcAddress(m_interposerModule, "slSetConstants"));
  m_slEvaluateFeature = reinterpret_cast<PFun_slEvaluateFeature*>(
      GetProcAddress(m_interposerModule, "slEvaluateFeature"));
  m_slGetFeatureFunction = reinterpret_cast<PFun_slGetFeatureFunction*>(
      GetProcAddress(m_interposerModule, "slGetFeatureFunction"));

    return m_slInit && m_slShutdown && m_slSetD3DDevice && m_slIsFeatureSupported &&
      m_slGetNewFrameToken && m_slSetConstants && m_slEvaluateFeature &&
      m_slGetFeatureFunction;
}

bool StreamlineManager::LoadFeatureFunctions() {
  if (!m_slGetFeatureFunction)
    return false;

  auto isSupported = [&](sl::Feature f) -> bool {
    if (!m_slIsFeatureSupported || !m_hasAdapterLuid)
      return true; // best effort
    sl::AdapterInfo ai{};
    ai.deviceLUID = reinterpret_cast<uint8_t*>(&m_adapterLuid);
    ai.deviceLUIDSizeInBytes = sizeof(LUID);
    return m_slIsFeatureSupported(f, ai) == sl::Result::eOk;
  };

  void* fn = nullptr;
  sl::Result res = m_slGetFeatureFunction(sl::kFeatureDLSS,
                                         "slDLSSGetOptimalSettings", fn);
  if (res == sl::Result::eOk)
    m_slDLSSGetOptimalSettings =
        reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(fn);

  fn = nullptr;
  res = m_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", fn);
  if (res == sl::Result::eOk)
    m_slDLSSSetOptions = reinterpret_cast<PFun_slDLSSSetOptions*>(fn);

  fn = nullptr;
  if (isSupported(sl::kFeatureDLSS_RR)) {
    res = m_slGetFeatureFunction(sl::kFeatureDLSS_RR,
                                 "slDLSSDGetOptimalSettings", fn);
    if (res == sl::Result::eOk)
      m_slDLSSDGetOptimalSettings =
          reinterpret_cast<PFun_slDLSSDGetOptimalSettings*>(fn);

    fn = nullptr;
    res = m_slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", fn);
    if (res == sl::Result::eOk)
      m_slDLSSDSetOptions = reinterpret_cast<PFun_slDLSSDSetOptions*>(fn);
  } else {
    m_slDLSSDGetOptimalSettings = nullptr;
    m_slDLSSDSetOptions = nullptr;
  }

  // It's OK for RR functions to be missing on unsupported hardware.
  return m_slDLSSSetOptions != nullptr;
}

