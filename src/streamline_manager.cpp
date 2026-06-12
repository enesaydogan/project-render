#include "streamline_manager.h"

#include "camera.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sl_security.h>
#include <sl_helpers.h>

using Microsoft::WRL::ComPtr;

namespace {
static void SLLogCallback(sl::LogType type, const char *msg) {
  // Keep it simple: mirror to stderr.
  const char *prefix = "SL";
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
                                              float nearZ, float farZ,
                                              float verticalCenterShift = 0.0f) {
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
  m[2] = sl::float4(0, -verticalCenterShift * f, A, 1);
  m[3] = sl::float4(0, 0, B, 0);
  return m;
}

static sl::float3 ToSl3(const float v[3]) {
  return sl::float3(v[0], v[1], v[2]);
}

static void ResolveProjectionBasis(const CameraCB &cam, sl::float3 &fwd,
                                   sl::float3 &right, sl::float3 &up,
                                   float &verticalCenterShift) {
  // Build an orthonormal basis like in the shaders.
  fwd = ToSl3(cam.forward);
  up = ToSl3(cam.up);
  sl::vectorCrossProduct(right, fwd, up);
  sl::vectorNormalize(right);
  sl::vectorCrossProduct(up, fwd, right);
  sl::vectorNormalize(up);
  sl::vectorNormalize(fwd);
  verticalCenterShift = 0.0f;

  if (cam.verticalTiltCorrection <= 0.5f) {
    return;
  }

  const sl::float3 pitchedFwd = fwd;
  fwd.y = 0.0f;
  const float levelLengthSq = fwd.x * fwd.x + fwd.z * fwd.z;
  if (levelLengthSq <= 1.0e-6f) {
    fwd = pitchedFwd;
    return;
  }

  sl::vectorNormalize(fwd);
  const sl::float3 worldUp(0.0f, 1.0f, 0.0f);
  sl::vectorCrossProduct(right, fwd, worldUp);
  sl::vectorNormalize(right);
  sl::vectorCrossProduct(up, fwd, right);
  sl::vectorNormalize(up);
  const float levelDot =
      (std::max)(0.025f, pitchedFwd.x * fwd.x + pitchedFwd.y * fwd.y +
                             pitchedFwd.z * fwd.z);
  verticalCenterShift = std::clamp(
      (pitchedFwd.x * up.x + pitchedFwd.y * up.y + pitchedFwd.z * up.z) /
          levelDot,
      -40.0f, 40.0f);
}

static sl::float4x4 MakeCameraViewToWorld(const CameraCB &cam) {
  sl::float3 fwd;
  sl::float3 up;
  sl::float3 right;
  float verticalCenterShift = 0.0f;
  ResolveProjectionBasis(cam, fwd, right, up, verticalCenterShift);

  sl::float4x4 m{};
  m[0] = sl::float4(right.x, right.y, right.z, 0.0f);
  m[1] = sl::float4(up.x, up.y, up.z, 0.0f);
  m[2] = sl::float4(fwd.x, fwd.y, fwd.z, 0.0f);
  m[3] = sl::float4(cam.pos[0], cam.pos[1], cam.pos[2], 1.0f);
  return m;
}

static sl::float4x4 MakeWorldToCameraView(const CameraCB &cam) {
  sl::float4x4 camViewToWorld = MakeCameraViewToWorld(cam);
  sl::float4x4 worldToCamera{};
  sl::matrixOrthoNormalInvert(worldToCamera, camViewToWorld);
  return worldToCamera;
}

static void FillExtent(sl::Extent &e, uint32_t w, uint32_t h) {
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

static uint32_t ParseUint32(const std::string &s) {
  // Accept decimal or 0x-prefixed hex.
  char *end = nullptr;
  unsigned long v = std::strtoul(s.c_str(), &end, 0);
  if (!end || end == s.c_str())
    return 0;
  if (v > 0xFFFFFFFFul)
    return 0;
  return (uint32_t)v;
}

static uint32_t ReadApplicationIdFromFile(const std::wstring &exeDir) {
  // Simple convention: drop a text file next to the exe with the NGX app id.
  // Example content: 1234567
  const std::filesystem::path p =
      std::filesystem::path(exeDir) / L"sl_appid.txt";
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
  DWORD n =
      GetEnvironmentVariableW(L"SL_APPLICATION_ID", buf, (DWORD)_countof(buf));
  if (n > 0 && n < _countof(buf)) {
    wchar_t *end = nullptr;
    unsigned long v = std::wcstoul(buf, &end, 0);
    return (end && end != buf) ? (uint32_t)v : 0;
  }
  n = GetEnvironmentVariableW(L"PROJECT_RENDER_SL_APPLICATION_ID", buf,
                              (DWORD)_countof(buf));
  if (n > 0 && n < _countof(buf)) {
    wchar_t *end = nullptr;
    unsigned long v = std::wcstoul(buf, &end, 0);
    return (end && end != buf) ? (uint32_t)v : 0;
  }
  return 0;
}

static bool ReadBoolEnv(const wchar_t *name, bool defaultValue) {
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
  // A non-zero id from the caller is treated as a real production id; only
  // a 0 (= "use defaults") keeps the placeholder flag set so the env / file
  // / built-in fallback chain still runs in InitializeEarly.
  if (applicationId != 0) {
    m_applicationIdIsPlaceholder = false;
  }
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
  if (m_initialized)
    return true;

  m_pluginDir = GetExecutableDir();

  // Allow lightweight overrides without code changes.
  // - PROJECT_RENDER_SL_LOG_TO_FILE=0 disables file logging.
  // - PROJECT_RENDER_SL_MIRROR_LOGS=1 mirrors SL log messages to stderr.
  m_logToFile = ReadBoolEnv(L"PROJECT_RENDER_SL_LOG_TO_FILE", m_logToFile);
  m_mirrorLogsToStderr =
      ReadBoolEnv(L"PROJECT_RENDER_SL_MIRROR_LOGS", m_mirrorLogsToStderr);

  // Track whether the id came from a real source (caller / env / sl_appid.txt)
  // or fell back to the built-in dev placeholder. Production NGX DLLs require
  // a registered id; the placeholder works only with the development NGX
  // binaries shipped alongside Streamline.
  if (m_applicationId != 0) {
    m_applicationIdIsPlaceholder = false;
  } else {
    m_applicationId = ReadApplicationIdFromEnv();
    if (m_applicationId != 0) {
      m_applicationIdIsPlaceholder = false;
    } else {
      m_applicationId = ReadApplicationIdFromFile(m_pluginDir);
      if (m_applicationId != 0) {
        m_applicationIdIsPlaceholder = false;
      }
    }
  }
  if (m_applicationId == 0) {
    m_applicationId = kDefaultNgxApplicationId;
    m_applicationIdIsPlaceholder = true;
  }

  if (!LoadInterposer()) {
    fprintf(
        stderr,
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

  const wchar_t *pluginPaths[] = {m_pluginDir.c_str()};

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
  pref.applicationId = m_applicationId;
  pref.engine = sl::EngineType::eCustom;
#ifdef APP_VERSION
  pref.engineVersion = APP_VERSION;
#else
  pref.engineVersion = "0.1.0";
#endif
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

  if (m_applicationIdIsPlaceholder) {
    fprintf(stderr,
            "StreamlineManager: WARNING: NGX applicationId not configured; "
            "using placeholder id=%u. DLSS / DLSS-RR will work only with the "
            "development NGX DLLs shipped beside the app. For production "
            "set SL_APPLICATION_ID, PROJECT_RENDER_SL_APPLICATION_ID, or "
            "drop your NVIDIA-issued id into sl_appid.txt next to the "
            "executable.\n",
            m_applicationId);
  }

  m_initialized = true;
  return true;
}

bool StreamlineManager::OnD3D12DeviceCreated(ID3D12Device *device) {
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

  // Null every pointer loaded from the now-unloaded interposer module.
  // Without this, any subsequent public call (or a retry of
  // InitializeEarly that fails mid-way before LoadCoreFunctions runs)
  // would dispatch through a dangling function address from the freed DLL.
  m_slInit = nullptr;
  m_slShutdown = nullptr;
  m_slSetD3DDevice = nullptr;
  m_slIsFeatureSupported = nullptr;
  m_slGetNewFrameToken = nullptr;
  m_slSetConstants = nullptr;
  m_slEvaluateFeature = nullptr;
  m_slGetFeatureFunction = nullptr;
  m_slFreeResources = nullptr;
  m_slDLSSGetOptimalSettings = nullptr;
  m_slDLSSSetOptions = nullptr;
  m_slDLSSDGetOptimalSettings = nullptr;
  m_slDLSSDSetOptions = nullptr;
  m_CreateDXGIFactory2 = nullptr;
  m_D3D12CreateDevice = nullptr;

  m_supportsDlss = false;
  m_supportsDlssRr = false;
  m_featureSupportCached = false;
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

void StreamlineManager::ReleaseResourcesForMode(Mode mode) {
  if (!m_initialized || !m_deviceSet || !m_slFreeResources) {
    return;
  }

  auto freeFeature = [&](sl::Feature feature, const char *name) {
    const sl::Result res = m_slFreeResources(feature, m_viewport);
    if (res != sl::Result::eOk && m_mirrorLogsToStderr) {
      fprintf(stderr,
              "StreamlineManager: slFreeResources(%s) returned %d=%s\n",
              name, (int)res, sl::getResultAsStr(res));
    }
  };

  if (mode == Mode::DLSS_SuperResolution || mode == Mode::Off) {
    freeFeature(sl::kFeatureDLSS, "DLSS");
  }
  if (mode == Mode::DLSS_RayReconstruction || mode == Mode::Off) {
    freeFeature(sl::kFeatureDLSS_RR, "DLSS-RR");
  }
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
      m_cachedOptimalOutW != outputWidth ||
      m_cachedOptimalOutH != outputHeight || m_cachedOptimalMode != m_mode ||
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
      // We feed DLSS with linear HDR (pre-tonemap) color.
      options.colorBuffersHDR = sl::Boolean::eTrue;

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

  // Dynamic input resolution is disabled for DLSS-RR in this SDK. Keep the
  // clamp path behind m_drrEnabled so future SDK support has one place to
  // re-enable it after validating Streamline's RR behavior.
  if (m_drrEnabled && m_mode == Mode::DLSS_RayReconstruction &&
      m_drrTargetWidth > 0 && m_drrTargetHeight > 0) {
    const uint32_t minW = m_cachedDlssdOptimal.renderWidthMin;
    const uint32_t minH = m_cachedDlssdOptimal.renderHeightMin;
    const uint32_t maxW = m_cachedDlssdOptimal.renderWidthMax;
    const uint32_t maxH = m_cachedDlssdOptimal.renderHeightMax;
    if (maxW > 0 && maxH > 0) {
      uint32_t targetW = m_drrTargetWidth;
      uint32_t targetH = m_drrTargetHeight;
      if (targetW < minW)
        targetW = minW;
      if (targetH < minH)
        targetH = minH;
      if (targetW > maxW)
        targetW = maxW;
      if (targetH > maxH)
        targetH = maxH;
      out.renderWidth = targetW;
      out.renderHeight = targetH;
    }
  }

  return out;
}

StreamlineManager::RenderSizeRange
StreamlineManager::GetRenderSizeRange(uint32_t outputWidth,
                                      uint32_t outputHeight) const {
  RenderSizeRange range{};

  // Drive the cache: GetRecommendedRenderSize populates m_cachedDlssOptimal
  // / m_cachedDlssdOptimal for the (mode, quality, outputWidth, outputHeight)
  // we're asking about.
  (void)GetRecommendedRenderSize(outputWidth, outputHeight);

  if (!m_initialized || !m_deviceSet || !m_cachedOptimalValid ||
      m_cachedOptimalOutW != outputWidth ||
      m_cachedOptimalOutH != outputHeight) {
    // Cache miss / not ready — fall back to outputSize for all fields so
    // callers can still allocate buffers without crashing.
    range.minRenderWidth = outputWidth;
    range.minRenderHeight = outputHeight;
    range.optimalRenderWidth = outputWidth;
    range.optimalRenderHeight = outputHeight;
    range.maxRenderWidth = outputWidth;
    range.maxRenderHeight = outputHeight;
    return range;
  }

  if (m_mode == Mode::DLSS_RayReconstruction) {
    range.minRenderWidth = m_cachedDlssdOptimal.renderWidthMin;
    range.minRenderHeight = m_cachedDlssdOptimal.renderHeightMin;
    range.optimalRenderWidth = m_cachedDlssdOptimal.optimalRenderWidth;
    range.optimalRenderHeight = m_cachedDlssdOptimal.optimalRenderHeight;
    range.maxRenderWidth = m_cachedDlssdOptimal.renderWidthMax;
    range.maxRenderHeight = m_cachedDlssdOptimal.renderHeightMax;
    range.optimalSharpness = m_cachedDlssdOptimal.optimalSharpness;
  } else if (m_mode == Mode::DLSS_SuperResolution) {
    range.minRenderWidth = m_cachedDlssOptimal.renderWidthMin;
    range.minRenderHeight = m_cachedDlssOptimal.renderHeightMin;
    range.optimalRenderWidth = m_cachedDlssOptimal.optimalRenderWidth;
    range.optimalRenderHeight = m_cachedDlssOptimal.optimalRenderHeight;
    range.maxRenderWidth = m_cachedDlssOptimal.renderWidthMax;
    range.maxRenderHeight = m_cachedDlssOptimal.renderHeightMax;
    range.optimalSharpness = m_cachedDlssOptimal.optimalSharpness;
  } else {
    range.minRenderWidth = outputWidth;
    range.minRenderHeight = outputHeight;
    range.optimalRenderWidth = outputWidth;
    range.optimalRenderHeight = outputHeight;
    range.maxRenderWidth = outputWidth;
    range.maxRenderHeight = outputHeight;
  }

  return range;
}

bool StreamlineManager::Evaluate(
    ID3D12GraphicsCommandList *cmdList, ID3D12Resource *colorIn,
    D3D12_RESOURCE_STATES colorInState, ID3D12Resource *colorOut,
    D3D12_RESOURCE_STATES colorOutState, uint32_t renderWidth,
    uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
    ID3D12Resource *depth, D3D12_RESOURCE_STATES depthState,
    ID3D12Resource *mvec, D3D12_RESOURCE_STATES mvecState,
    ID3D12Resource *normalRoughness, D3D12_RESOURCE_STATES normalRoughnessState,
    ID3D12Resource *albedo, D3D12_RESOURCE_STATES albedoState,
    ID3D12Resource *specularAlbedo, D3D12_RESOURCE_STATES specularAlbedoState,
    ID3D12Resource *specularHitDistance,
    D3D12_RESOURCE_STATES specularHitDistanceState,
    ID3D12Resource *specularMotionVectors,
    D3D12_RESOURCE_STATES specularMotionVectorsState, bool resetHistory,
    float jitterX, float jitterY) {
  if (!m_enabled || m_mode == Mode::Off)
    return false;
  if (!m_initialized || !m_deviceSet)
    return false;
  if (!m_featureFunctionsReady)
    return false;
  // Skip evaluation when the requested feature isn't supported on this
  // adapter; avoids per-frame slEvaluateFeature errors.
  if (m_mode == Mode::DLSS_SuperResolution && !m_supportsDlss)
    return false;
  if (m_mode == Mode::DLSS_RayReconstruction && !m_supportsDlssRr)
    return false;
  if (!cmdList || !colorIn || !colorOut || !depth || !mvec)
    return false;

  // Frame token
  sl::FrameToken *frameToken = nullptr;
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
  const CameraCB &cam = g_cameraData;

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

  sl::float3 projectionFwd;
  sl::float3 projectionRight;
  sl::float3 projectionUp;
  float verticalCenterShift = 0.0f;
  ResolveProjectionBasis(cam, projectionFwd, projectionRight, projectionUp,
                         verticalCenterShift);
  c.cameraViewToClip = MakePerspectiveViewToClip(
      cam.fov, cam.aspect, cam.nearZ, cam.farZ, verticalCenterShift);
  sl::matrixFullInvert(c.clipToCameraView, c.cameraViewToClip);

  // Camera vectors
  c.cameraPos = sl::float3(cam.pos[0], cam.pos[1], cam.pos[2]);
  c.cameraFwd = projectionFwd;
  c.cameraRight = projectionRight;
  c.cameraUp = projectionUp;

  // Prev transforms
  sl::float4x4 cameraViewToWorld = MakeCameraViewToWorld(cam);
  sl::float4x4 cameraViewToWorldPrev = MakeCameraViewToWorld(prevCam);

  sl::float4x4 cameraViewToPrevCameraView;
  sl::calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld,
                             cameraViewToWorldPrev);

  sl::float3 prevProjectionFwd;
  sl::float3 prevProjectionRight;
  sl::float3 prevProjectionUp;
  float prevVerticalCenterShift = 0.0f;
  ResolveProjectionBasis(prevCam, prevProjectionFwd, prevProjectionRight,
                         prevProjectionUp, prevVerticalCenterShift);
  sl::float4x4 prevViewToClip = MakePerspectiveViewToClip(
      prevCam.fov, prevCam.aspect, prevCam.nearZ, prevCam.farZ,
      prevVerticalCenterShift);

  sl::float4x4 clipToPrevCameraView;
  sl::matrixMul(clipToPrevCameraView, c.clipToCameraView,
                cameraViewToPrevCameraView);
  sl::matrixMul(c.clipToPrevClip, clipToPrevCameraView, prevViewToClip);
  sl::matrixFullInvert(c.prevClipToClip, c.clipToPrevClip);

  // Match wavefront primary jitter: Halton jitter in
  // pixel units.
  // Using passed jitter values to ensure synchronization with shader.
  c.jitterOffset = sl::float2(jitterX, jitterY);

  // Off-center principal-point support: the project's vertical-tilt
  // correction shifts the principal point by `verticalCenterShift` in
  // unit-less NDC terms (see MakePerspectiveViewToClip — the projection
  // adds `-verticalCenterShift * f * z` to clip.y so the principal point
  // ends up at NDC.y = -verticalCenterShift). Hand the same offset to
  // Streamline so RR's reprojection logic understands the optical axis
  // isn't at image centre when tilt correction is engaged.
  c.cameraPinholeOffset = sl::float2(0.0f, -verticalCenterShift);

  // Motion vector convention:
  //   shader writes `prev_pixel - curr_pixel` at render resolution into
  //   g_motionVectors (range roughly [-renderWidth, +renderWidth]).
  // Streamline wants mvec * mvecScale to land in [-1, 1] (per
  // ProgrammingGuideDLSS_RR.md §6 example: pixel-space motion uses
  //   mvecScale = {1.0f / renderWidth, 1.0f / renderHeight}).
  c.mvecScale = sl::float2(1.0f / (float)renderWidth, 1.0f / (float)renderHeight);
  m_lastMvecScaleX = c.mvecScale.x;
  m_lastMvecScaleY = c.mvecScale.y;

  c.cameraNear = cam.nearZ;
  c.cameraFar = cam.farZ;
  c.cameraFOV = cam.fov * 3.14159265359f / 180.0f;
  c.cameraAspectRatio = cam.aspect;

  // c.depthInverted: the shader writes NDC z from
  //   A = far / (far - near); B = -near * far / (far - near);
  //   ndc = A + B / z;    // near→0, far→1  (non-reverse-Z)
  // for both SR and RR. True view-space depth is kept in a separate renderer
  // buffer and is not tagged as Streamline depth.
  c.depthInverted = sl::Boolean::eFalse;
  c.cameraMotionIncluded = sl::Boolean::eTrue;
  c.motionVectors3D = sl::Boolean::eFalse;
  c.motionVectorsDilated = sl::Boolean::eFalse;
  // Shader calculates motion vectors without the jitter offset applied.
  // Allow overriding for debugging via SetMotionVectorsJittered.
  c.motionVectorsJittered =
      m_motionVectorsJittered ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  c.reset = resetHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  // Streamline 2.10 Constants has no 'renderingGameFrames' field.

  // Sentinel for "no valid motion vector at this pixel" (occlusion, sky
  // outside re-projection, etc.). MUST match kInvalidMvec in
  // shaders/wavefront_resolve_primary_cs.hlsl. If you change one, change
  // the other.
  c.motionVectorsInvalidValue = kStreamlineInvalidMvecValue;

  res = m_slSetConstants(c, *frameToken, m_viewport);
  if (res != sl::Result::eOk) {
    fprintf(stderr, "StreamlineManager: slSetConstants failed (%d)\n",
            (int)res);
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
    opt.colorBuffersHDR = sl::Boolean::eTrue;
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
  sl::Resource outRes(sl::ResourceType::eTex2d, colorOut,
                      (uint32_t)colorOutState);

  // Depth tagging: pre-1b55db1 used kBufferTypeDepth for BOTH SR and RR
  // even though the shader writes view-space linear Z for RR mode. RR
  // tolerated the "mislabelled" tag and produced instantly-plausible
  // images. Switching to kBufferTypeLinearDepth (which is technically
  // more correct per the SDK docs) routes RR through its linear-depth
  // reprojection code path, which depends on the worldToCameraView /
  // cameraViewToWorld matrices set in DLSSDOptions. If those matrices
  // have any row/column-major mismatch with what SL expects, RR's
  // reprojection collapses and temporal accumulation falls back to
  // pure color history — producing the "starts flat, converges over
  // many frames, never quite reaches energy" symptom plus the residual
  // checker that the network can't smooth without good depth.
  //
  // Pass kBufferTypeDepth for both modes to match the working pre-1b55db1
  // configuration. If we want true linear-depth tagging in the future,
  // first audit MakeWorldToCameraView / MakeCameraViewToWorld against
  // sl_consts.h conventions and validate with sl::eLogLevelVerbose.
  // g_depth is normalized device depth in both modes. In particular, RR sky
  // pixels are 1.0 rather than camera farZ, which keeps low-resolution
  // reconstruction neighborhoods inside the declared depth domain.
  const sl::BufferType depthType = sl::kBufferTypeDepth;
  // Lifecycle: all RR inputs use eValidUntilEvaluate. The caller transitions
  // depth and mvec back to UAV state immediately after Evaluate returns
  // (dxr_renderer.cpp ~9860), which violates the eValidUntilPresent
  // contract — SL is allowed to dereference those resources up to Present
  // and would find them in the wrong state. A previous experiment with
  // eValidUntilPresent (commit 1b55db1) appeared to cause the macroblock
  // / brightness instability artifacts on DLSS-RR; revert to the
  // conservative lifecycle that matches when we actually own the
  // resources in NON_PIXEL_SHADER_RESOURCE state.
  sl::ResourceTag depthTag{&depthRes, depthType,
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

  std::vector<const sl::BaseStructure *> inputs;
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
    albedoRes =
        sl::Resource(sl::ResourceType::eTex2d, albedo, (uint32_t)albedoState);
    albedoTag = sl::ResourceTag{&albedoRes, sl::kBufferTypeAlbedo,
                                sl::ResourceLifecycle::eValidUntilEvaluate,
                                &renderExtent};

    inputs.push_back(&nrTag);
    inputs.push_back(&albedoTag);

    if (specularAlbedo) {
      specAlbedoRes = sl::Resource(sl::ResourceType::eTex2d, specularAlbedo,
                                   (uint32_t)specularAlbedoState);
      specAlbedoTag = sl::ResourceTag{
          &specAlbedoRes, sl::kBufferTypeSpecularAlbedo,
          sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent};
      inputs.push_back(&specAlbedoTag);
    }

    if (specularMotionVectors) {
      specMvecRes =
          sl::Resource(sl::ResourceType::eTex2d, specularMotionVectors,
                       (uint32_t)specularMotionVectorsState);
      specMvecTag = sl::ResourceTag{
          &specMvecRes, sl::kBufferTypeSpecularMotionVectors,
          sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent};
      inputs.push_back(&specMvecTag);
    } else if (specularHitDistance) {
      specHitDistRes =
          sl::Resource(sl::ResourceType::eTex2d, specularHitDistance,
                       (uint32_t)specularHitDistanceState);
      specHitDistTag = sl::ResourceTag{
          &specHitDistRes, sl::kBufferTypeSpecularHitDistance,
          sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent};
      inputs.push_back(&specHitDistTag);
    }
  }

  sl::Feature feature = (m_mode == Mode::DLSS_SuperResolution)
                            ? sl::kFeatureDLSS
                            : sl::kFeatureDLSS_RR;

  res = m_slEvaluateFeature(feature, *frameToken, inputs.data(),
                            (uint32_t)inputs.size(),
                            reinterpret_cast<sl::CommandBuffer *>(cmdList));
  if (res != sl::Result::eOk) {
      fprintf(stderr, "StreamlineManager: slEvaluateFeature failed (%d=%s)\n",
        (int)res, sl::getResultAsStr(res));
    return false;
  }

  return true;
}

void StreamlineManager::SetMotionVectorsJittered(bool jittered) {
  m_motionVectorsJittered = jittered;
}

bool StreamlineManager::GetMotionVectorsJittered() const {
  return m_motionVectorsJittered;
}

std::pair<float, float> StreamlineManager::GetLastMvecScale() const {
  return {m_lastMvecScaleX, m_lastMvecScaleY};
}

HRESULT StreamlineManager::CreateDXGIFactory2(UINT flags, REFIID riid,
                                              void **ppFactory) const {
  if (!m_CreateDXGIFactory2)
    return E_NOINTERFACE;
  return m_CreateDXGIFactory2(flags, riid, ppFactory);
}

HRESULT StreamlineManager::D3D12CreateDevice(IUnknown *adapter,
                                             D3D_FEATURE_LEVEL minFeatureLevel,
                                             REFIID riid,
                                             void **ppDevice) const {
  if (!m_D3D12CreateDevice)
    return E_NOINTERFACE;
  return m_D3D12CreateDevice(adapter, minFeatureLevel, riid, ppDevice);
}

bool StreamlineManager::LoadInterposer() {
  if (m_interposerModule)
    return true;

  std::wstring dllPath = GetExecutableDir() + L"\\sl.interposer.dll";

  // Verify signature. In release builds we refuse to load an unsigned
  // interposer (Streamline Programming Guide recommendation: production
  // apps must reject unsigned plugins). Debug builds keep the historical
  // soft-warning behaviour so devs can iterate on local rebuilds without
  // re-signing.
  const bool sigOk = sl::security::verifyEmbeddedSignature(dllPath.c_str());
  if (!sigOk) {
#ifdef NDEBUG
    fprintf(stderr,
            "StreamlineManager: sl.interposer.dll failed signature "
            "verification; refusing to load in release build. Place a "
            "signed copy next to the executable.\n");
    return false;
#else
    fprintf(stderr,
            "StreamlineManager: sl.interposer.dll signature not verified "
            "(debug build, allowing load).\n");
#endif
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

  m_slInit = reinterpret_cast<PFun_slInit *>(
      GetProcAddress(m_interposerModule, "slInit"));
  m_slShutdown = reinterpret_cast<PFun_slShutdown *>(
      GetProcAddress(m_interposerModule, "slShutdown"));
  m_slSetD3DDevice = reinterpret_cast<PFun_slSetD3DDevice *>(
      GetProcAddress(m_interposerModule, "slSetD3DDevice"));
  m_slIsFeatureSupported = reinterpret_cast<PFun_slIsFeatureSupported *>(
      GetProcAddress(m_interposerModule, "slIsFeatureSupported"));
  m_slGetNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken *>(
      GetProcAddress(m_interposerModule, "slGetNewFrameToken"));
  m_slSetConstants = reinterpret_cast<PFun_slSetConstants *>(
      GetProcAddress(m_interposerModule, "slSetConstants"));
  m_slEvaluateFeature = reinterpret_cast<PFun_slEvaluateFeature *>(
      GetProcAddress(m_interposerModule, "slEvaluateFeature"));
  m_slGetFeatureFunction = reinterpret_cast<PFun_slGetFeatureFunction *>(
      GetProcAddress(m_interposerModule, "slGetFeatureFunction"));
  m_slFreeResources = reinterpret_cast<PFun_slFreeResources *>(
      GetProcAddress(m_interposerModule, "slFreeResources"));

  return m_slInit && m_slShutdown && m_slSetD3DDevice &&
         m_slIsFeatureSupported && m_slGetNewFrameToken && m_slSetConstants &&
         m_slEvaluateFeature && m_slGetFeatureFunction && m_slFreeResources;
}

bool StreamlineManager::LoadFeatureFunctions() {
  if (!m_slGetFeatureFunction)
    return false;

  auto isSupported = [&](sl::Feature f) -> bool {
    if (!m_slIsFeatureSupported || !m_hasAdapterLuid)
      return true; // best effort
    sl::AdapterInfo ai{};
    ai.deviceLUID = reinterpret_cast<uint8_t *>(&m_adapterLuid);
    ai.deviceLUIDSizeInBytes = sizeof(LUID);
    return m_slIsFeatureSupported(f, ai) == sl::Result::eOk;
  };

  // Probe support once. The UI uses this to grey out unsupported modes and
  // Evaluate uses it to short-circuit instead of calling slEvaluateFeature
  // (which would log an error every frame on non-RTX adapters).
  m_supportsDlss = isSupported(sl::kFeatureDLSS);
  m_supportsDlssRr = isSupported(sl::kFeatureDLSS_RR);
  m_featureSupportCached = true;

  void *fn = nullptr;
  sl::Result res = sl::Result::eOk;

  if (m_supportsDlss) {
    res = m_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings",
                                 fn);
    if (res == sl::Result::eOk)
      m_slDLSSGetOptimalSettings =
          reinterpret_cast<PFun_slDLSSGetOptimalSettings *>(fn);

    fn = nullptr;
    res = m_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", fn);
    if (res == sl::Result::eOk)
      m_slDLSSSetOptions = reinterpret_cast<PFun_slDLSSSetOptions *>(fn);
  } else {
    m_slDLSSGetOptimalSettings = nullptr;
    m_slDLSSSetOptions = nullptr;
  }

  if (m_supportsDlssRr) {
    fn = nullptr;
    res = m_slGetFeatureFunction(sl::kFeatureDLSS_RR,
                                 "slDLSSDGetOptimalSettings", fn);
    if (res == sl::Result::eOk)
      m_slDLSSDGetOptimalSettings =
          reinterpret_cast<PFun_slDLSSDGetOptimalSettings *>(fn);

    fn = nullptr;
    res = m_slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", fn);
    if (res == sl::Result::eOk)
      m_slDLSSDSetOptions = reinterpret_cast<PFun_slDLSSDSetOptions *>(fn);
  } else {
    m_slDLSSDGetOptimalSettings = nullptr;
    m_slDLSSDSetOptions = nullptr;
  }

  // It's OK for RR functions to be missing on unsupported hardware.
  return m_slDLSSSetOptions != nullptr;
}
