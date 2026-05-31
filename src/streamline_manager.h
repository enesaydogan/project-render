#pragma once

// Streamline 2.10 integration (DLSS-SR + DLSS-RR) for D3D12

#include <cstdint>
#include <string>
#include <utility>
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <wrl.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_matrix_helpers.h>

// Sentinel value used for "no valid motion vector" pixels. MUST match
// kInvalidMvec in shaders/wavefront_resolve_primary_cs.hlsl. Defined here
// so the C++ Constants setup and the shader stay in lockstep.
constexpr float kStreamlineInvalidMvecValue = -1.0e6f;

class StreamlineManager {
public:
  enum class Mode {
    Off = 0,
    DLSS_SuperResolution,
    DLSS_RayReconstruction,
  };

  enum class Quality {
    MaxPerformance = 0,
    Balanced,
    MaxQuality,
    UltraPerformance,
    UltraQuality,
    DLAA,
  };

  struct RecommendedRenderSize {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
  };

  // Full DRR (Dynamic Resolution) range reported by Streamline for the
  // current mode + quality + output size. minRenderWidth/Height and
  // maxRenderWidth/Height bound the per-frame render rect the caller may
  // request via SetDrrTargetRenderSize. For non-DRR use just consume
  // optimalRenderWidth/Height (same as RecommendedRenderSize).
  struct RenderSizeRange {
    uint32_t minRenderWidth = 0;
    uint32_t minRenderHeight = 0;
    uint32_t optimalRenderWidth = 0;
    uint32_t optimalRenderHeight = 0;
    uint32_t maxRenderWidth = 0;
    uint32_t maxRenderHeight = 0;
    float optimalSharpness = 0.0f;
  };

  StreamlineManager();
  ~StreamlineManager();

  // Streamline DLSS/DLSS-RR uses NGX and requires a valid NVIDIA-provided
  // application id. This must be set before InitializeEarly().
  void SetApplicationId(uint32_t applicationId);
  uint32_t GetApplicationId() const { return m_applicationId; }

  // Logging options (must be set before InitializeEarly).
  void SetLogToFile(bool enabled);
  bool GetLogToFile() const { return m_logToFile; }
  void SetMirrorLogsToStderr(bool enabled);
  bool GetMirrorLogsToStderr() const { return m_mirrorLogsToStderr; }
  const std::wstring& GetLogDirectory() const { return m_logsDir; }

  // Must be called before any DXGI/D3D calls (per Streamline Programming Guide)
  bool InitializeEarly();

  // Call immediately after ID3D12Device is created
  bool OnD3D12DeviceCreated(ID3D12Device* device);

  void Shutdown();

  bool IsInitialized() const { return m_initialized; }
  bool IsDeviceSet() const { return m_deviceSet; }
  bool AreFeatureFunctionsReady() const { return m_featureFunctionsReady; }

  void SetMode(Mode mode);
  Mode GetMode() const { return m_mode; }

  void SetQuality(Quality quality);
  Quality GetQuality() const { return m_quality; }

  void SetEnabled(bool enabled);
  bool IsEnabled() const { return m_enabled; }
  void ReleaseResourcesForMode(Mode mode);

  // Debug/inspection helpers (used by UI).
  void SetMotionVectorsJittered(bool jittered);
  bool GetMotionVectorsJittered() const;
  std::pair<float, float> GetLastMvecScale() const;

  // Returns optimal/required internal render resolution for the selected mode.
  // If Streamline isn't ready, returns output size. When a DRR target has
  // been set via SetDrrTargetRenderSize(), that value (clamped to the
  // current mode's [min, max] range) is returned instead of the optimal.
  RecommendedRenderSize GetRecommendedRenderSize(uint32_t outputWidth,
                                                 uint32_t outputHeight) const;

  // Returns the full DRR range (min / optimal / max render dims +
  // optimal sharpness) for the current mode and quality at the given
  // output resolution. Zeros-out fields when Streamline / the selected
  // feature aren't ready. Used by the renderer to allocate buffers at
  // maxRender* dimensions so per-frame DRR adjustments don't trigger
  // reallocation.
  RenderSizeRange GetRenderSizeRange(uint32_t outputWidth,
                                     uint32_t outputHeight) const;

  // DRR (Dynamic Resolution) controls.
  //
  // Streamline DLSS-RR does not support Dynamic Resolution Scaling in the
  // SDK used here; changing input size reinitializes the denoiser. Enable
  // requests are ignored until a future SDK explicitly supports it.
  void SetDrrEnabled(bool enabled) {
    (void)enabled;
    m_drrEnabled = false;
    m_drrTargetWidth = 0;
    m_drrTargetHeight = 0;
  }
  bool IsDrrEnabled() const { return m_drrEnabled; }

  // Override the per-frame render rect. Clamped to the current mode's
  // [min, max] range inside GetRecommendedRenderSize. Pass (0, 0) to
  // clear the override and fall back to optimal.
  void SetDrrTargetRenderSize(uint32_t renderWidth, uint32_t renderHeight) {
    m_drrTargetWidth = renderWidth;
    m_drrTargetHeight = renderHeight;
  }
  std::pair<uint32_t, uint32_t> GetDrrTargetRenderSize() const {
    return {m_drrTargetWidth, m_drrTargetHeight};
  }

  // Evaluates the selected feature on the provided resources.
  //
  // Expected resource conventions:
  // - depth: D3D-style non-linear depth in [0,1]
  // - mvec: 2D motion vectors in pixel space (xy)
  // - normalRoughness: normal.xyz (world) and roughness in alpha (optional but recommended for DLSS-RR)
  // - albedo: baseColor/albedo (optional but recommended for DLSS-RR)
  // - colorIn: jittered render input
  // - colorOut: upscaled output (UAV-capable)
  bool Evaluate(ID3D12GraphicsCommandList* cmdList,
                ID3D12Resource* colorIn,
                D3D12_RESOURCE_STATES colorInState,
                ID3D12Resource* colorOut,
                D3D12_RESOURCE_STATES colorOutState,
                uint32_t renderWidth,
                uint32_t renderHeight,
                uint32_t outputWidth,
                uint32_t outputHeight,
                ID3D12Resource* depth,
                D3D12_RESOURCE_STATES depthState,
                ID3D12Resource* mvec,
                D3D12_RESOURCE_STATES mvecState,
                ID3D12Resource* normalRoughness,
                D3D12_RESOURCE_STATES normalRoughnessState,
                ID3D12Resource* albedo,
                D3D12_RESOURCE_STATES albedoState,
                ID3D12Resource* specularAlbedo,
                D3D12_RESOURCE_STATES specularAlbedoState,
                ID3D12Resource* specularHitDistance,
                D3D12_RESOURCE_STATES specularHitDistanceState,
                ID3D12Resource* specularMotionVectors,
                D3D12_RESOURCE_STATES specularMotionVectorsState,
                bool resetHistory, float jitterX = 0.0f, float jitterY = 0.0f);

  // DXGI/D3D12 proxy exports from sl.interposer.dll
  HRESULT CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) const;
  HRESULT D3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL minFeatureLevel,
                            REFIID riid, void** ppDevice) const;

  const std::wstring& GetPluginDirectory() const { return m_pluginDir; }

private:
  bool LoadInterposer();
  bool LoadCoreFunctions();
  bool LoadFeatureFunctions();

  sl::DLSSMode ToSlDlssMode(Quality q) const;

  void Log(sl::LogType type, const char* msg);

  // Core SL API (loaded from sl.interposer.dll)
  PFun_slInit* m_slInit = nullptr;
  PFun_slShutdown* m_slShutdown = nullptr;
  PFun_slSetD3DDevice* m_slSetD3DDevice = nullptr;
  PFun_slIsFeatureSupported* m_slIsFeatureSupported = nullptr;
  PFun_slGetNewFrameToken* m_slGetNewFrameToken = nullptr;
  PFun_slSetConstants* m_slSetConstants = nullptr;
  PFun_slEvaluateFeature* m_slEvaluateFeature = nullptr;
  PFun_slGetFeatureFunction* m_slGetFeatureFunction = nullptr;
  PFun_slFreeResources* m_slFreeResources = nullptr;

  // Feature APIs (resolved via slGetFeatureFunction after device set)
  // NOTE: Streamline headers declare PFun_* typedefs in the global namespace
  // (outside of namespace sl).
  PFun_slDLSSGetOptimalSettings* m_slDLSSGetOptimalSettings = nullptr;
  PFun_slDLSSSetOptions* m_slDLSSSetOptions = nullptr;
  PFun_slDLSSDGetOptimalSettings* m_slDLSSDGetOptimalSettings = nullptr;
  PFun_slDLSSDSetOptions* m_slDLSSDSetOptions = nullptr;

  // Interposer exports for DXGI/D3D12 creation
  using PFun_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
  using PFun_D3D12CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL,
                                                  REFIID, void**);

  PFun_CreateDXGIFactory2 m_CreateDXGIFactory2 = nullptr;
  PFun_D3D12CreateDevice m_D3D12CreateDevice = nullptr;

  HMODULE m_interposerModule = nullptr;

  bool m_enabled = true;
  bool m_initialized = false;
  bool m_deviceSet = false;
  bool m_featureFunctionsReady = false;

  // 0 = unset; InitializeEarly walks the caller/env/file/placeholder
  // fallback chain to assign the final value.
  uint32_t m_applicationId = 0;
  // True when the final m_applicationId is the built-in placeholder
  // (no caller-supplied id, no env var, no sl_appid.txt). Lets us emit a
  // one-shot warning instead of pretending we're in production NGX mode.
  bool m_applicationIdIsPlaceholder = false;

  // Cached results of slIsFeatureSupported, queried once after the device
  // is set so we can fail fast in Evaluate instead of spamming stderr
  // every frame on adapters that don't support DLSS / DLSS-RR.
  bool m_supportsDlss = false;
  bool m_supportsDlssRr = false;
  bool m_featureSupportCached = false;

public:
  bool SupportsDlssSuperResolution() const { return m_supportsDlss; }
  bool SupportsDlssRayReconstruction() const { return m_supportsDlssRr; }

private:

  Mode m_mode = Mode::DLSS_RayReconstruction;
  Quality m_quality = Quality::Balanced;

  // DRR state. Kept for future SDK support, but disabled because current
  // Streamline DLSS-RR reinitializes when the input resolution changes.
  bool m_drrEnabled = false;
  uint32_t m_drrTargetWidth = 0;
  uint32_t m_drrTargetHeight = 0;

  // TODO: per-render-target viewport handle once multi-viewport rendering
  // (editor preview / split-screen) is needed. For now the app drives a
  // single Streamline viewport identified by this fixed handle.
  sl::ViewportHandle m_viewport{123};
  uint32_t m_frameCounter = 0;

  // Debug state
  bool m_motionVectorsJittered = false;
  float m_lastMvecScaleX = 1.0f;
  float m_lastMvecScaleY = 1.0f;

  std::wstring m_pluginDir;
  std::wstring m_logsDir;

  bool m_logToFile = true;
  bool m_mirrorLogsToStderr = false;

  LUID m_adapterLuid{};
  bool m_hasAdapterLuid = false;

  mutable sl::DLSSOptimalSettings m_cachedDlssOptimal{};
  mutable sl::DLSSDOptimalSettings m_cachedDlssdOptimal{};
  mutable bool m_cachedOptimalValid = false;
  mutable uint32_t m_cachedOptimalOutW = 0;
  mutable uint32_t m_cachedOptimalOutH = 0;
  mutable Mode m_cachedOptimalMode = Mode::Off;
  mutable Quality m_cachedOptimalQuality = Quality::Balanced;
};
