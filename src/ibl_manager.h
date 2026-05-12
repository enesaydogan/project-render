#pragma once
#include <algorithm>
#include "PragueSkyModel.h"
#include "assets/asset_loader.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl.h>

class IBLManager {
public:
  enum class IBLSource { File, PragueSkyModel };

  static IBLManager &Get() {
    static IBLManager instance;
    return instance;
  }

  bool Initialize(ID3D12Device *device, ID3D12CommandQueue *queue);
  bool LoadEnvironmentMap(const std::string &path);
  const std::string &GetEnvironmentMapPath() const { return m_envMapPath; }

  // Sky Model Integration
  bool InitializeSkyModel(const std::string &datasetPath);
  void UpdateSkyModel();
  void SetSkyModelUpdatesSuspended(bool suspended) {
    m_skyUpdateSuspended = suspended;
  }
  bool AreSkyModelUpdatesSuspended() const { return m_skyUpdateSuspended; }

  // Parameters
  void SetIBLSource(IBLSource source);
  IBLSource GetIBLSource() const { return m_source; }

  void SetSkyVisibility(float km) {
    km = std::clamp(km, 10.0f, 120.0f);
    if (m_visibility != km) {
      m_visibility = km;
      m_skyDirty = true;
    }
  }
  float GetSkyVisibility() const { return m_visibility; }

  void SetSkyAlbedo(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (m_albedo != v) {
      m_albedo = v;
      m_skyDirty = true;
    }
  }
  float GetSkyAlbedo() const { return m_albedo; }

  void SetSolarAltitude(float v) {
    if (m_solarElevation != v) {
      m_solarElevation = v;
      m_skyDirty = true;
    }
  }
  float GetSolarAltitude() const { return m_solarElevation; }

  void SetSolarAzimuth(float v) {
    if (m_solarAzimuth != v) {
      m_solarAzimuth = v;
      m_skyDirty = true;
    }
  }
  float GetSolarAzimuth() const { return m_solarAzimuth; }

  void SetObserverAltitude(float meters) {
    meters = std::clamp(meters, 0.0f, 15000.0f);
    if (m_altitude != meters) {
      m_altitude = meters;
      m_skyDirty = true;
    }
  }
  float GetObserverAltitude() const { return m_altitude; }

  void SetSkyIntensity(float v) {
    if (m_physicalCalibrationEnabled) {
      return;
    }
    v = std::clamp(v, 0.0f, 5.0f);
    if (m_skyIntensity != v) {
      m_skyIntensity = v;
      m_skyDirty = true;
    }
  }
  float GetSkyIntensity() const { return m_skyIntensity; }

  void SetSunIntensity(float v) {
    if (m_physicalCalibrationEnabled) {
      return;
    }
    v = (std::max)(0.0f, v);
    if (m_sunIntensity != v) {
      m_sunIntensity = v;
      m_skyDirty = true;
    }
  }
  float GetSunIntensity() const { return m_sunIntensity; }

  DirectX::XMFLOAT3 GetSunColor() const;
  float GetSkyAvgLuminanceCdM2() const { return m_dbgSkyAvgLuminanceCdM2; }
  float GetSkyHorizonLuminanceCdM2() const {
    return m_dbgSkyHorizonLuminanceCdM2;
  }
  float GetSkyMaxLuminanceCdM2() const { return m_dbgSkyMaxLuminanceCdM2; }

  // Core members
  void SetSunSize(float degrees) {
    if (m_physicalCalibrationEnabled) {
      return;
    }
    degrees = std::clamp(degrees, 0.05f, 5.0f);
    if (m_sunSize != degrees) {
      m_sunSize = degrees;
      m_skyDirty = true;
    }
  }
  float GetSunSize() const { return m_sunSize; }

  void SetPhysicalCalibrationEnabled(bool enabled);
  bool IsPhysicalCalibrationEnabled() const {
    return m_physicalCalibrationEnabled;
  }

  void SetIblRotationDegrees(float degrees) {
    float wrapped = fmodf(degrees, 360.0f);
    if (wrapped < 0.0f)
      wrapped += 360.0f;
    m_iblRotationDegrees = wrapped;
  }
  float GetIblRotationDegrees() const { return m_iblRotationDegrees; }

  Asset::Texture &GetEnvMap() { return m_envMap; }
  bool IsLoaded() const { return m_envMap.resource != nullptr; }
  Asset::Texture &GetEnvConditionalCdf() { return m_envConditionalCdf; }
  Asset::Texture &GetEnvMarginalCdf() { return m_envMarginalCdf; }
  bool HasEnvImportanceData() const {
    return m_envConditionalCdf.resource != nullptr &&
           m_envMarginalCdf.resource != nullptr;
  }

  // File-sun accessors
  bool HasFileSun() const { return m_hasFileSun; }
  // direction in unrotated (map-local) space
  DirectX::XMFLOAT3 GetFileSunLocalDir() const { return m_fileSunLocalDir; }
  // world-space direction accounting for current IBL rotation
  DirectX::XMFLOAT3 GetFileSunWorldDir() const;
  DirectX::XMFLOAT3 GetFileSunRadiance() const { return m_fileSunRadiance; }
  float GetFileSunIntensity() const { return m_fileSunIntensity; }
  float GetFileSunRadiusDeg() const { return m_fileSunRadiusDeg; }
  void SetFileSunIntensity(float v) { m_fileSunIntensity = v; }
  void SetFileSunRadiusDeg(float deg) { m_fileSunRadiusDeg = deg; }


  // Environment importance textures are always solid-angle weighted. The setter
  // is retained for old UI/scene plumbing and clamps back to the physical mode.
  void SetEnvSolidAngleSampling(bool enabled);
  bool GetEnvSolidAngleSampling() const { return m_envSolidAngleSampling; }

  void SetGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    m_gpuHandle = handle;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return m_gpuHandle; }

  void SetCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    m_cpuHandle = handle;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return m_cpuHandle; }

private:
  IBLManager() = default;
  IBLManager(const IBLManager &) = delete;
  IBLManager &operator=(const IBLManager &) = delete;

  void UpdateTextureFromSkyModel();
  void CreateDescriptor();
  bool BuildEnvironmentImportanceTextures(const Asset::Texture &envTex);

  Asset::Texture m_envMap;            // Current API-facing env map
  Asset::Texture m_fileTexture;       // Backing store for file IBL
  Asset::Texture m_proceduralTexture; // Backing store for Sky Model
  std::string m_envMapPath;           // Last loaded file IBL path
  Asset::Texture m_envConditionalCdf; // RGBA32F: x=conditional CDF, y=texel PMF
  Asset::Texture m_envMarginalCdf;    // RGBA32F: x=marginal CDF per row

  // When using a file-based IBL we mute the analytic sun/light.  We cache
  // the previous sun parameters here so they can be restored when the user
  // switches back to a procedural sky model.
  float m_savedSunIntensity = 0.0f;
  float m_savedSunSize = 0.0f;
  bool m_savedSunValid = false; // indicates cache contains meaningful values

  IBLSource m_source = IBLSource::File;

  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {0};
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {0};
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;

  // Sky Model Data
  std::unique_ptr<PragueSkyModel> m_pragueSkyModel;
  bool m_skyInitialized = false;
  bool m_skyDirty = true;
  bool m_skyUpdateSuspended = false;

  float m_visibility = 60.0f; // km; clear-air daylight default
  float m_albedo = 0.3f;      // typical mixed terrain/concrete reflectance
  float m_solarElevation = 0.5f;
  float m_solarAzimuth = 0.0f;
  float m_altitude = 0.0f; // meters (0 - 15000)
  float m_skyIntensity = 1.0f; // non-physical sky gain (UI range 0..5)
  float m_sunIntensity = 110000.0f; // clear midday sun illuminance in lux
  float m_sunSize = 0.53f; // degrees (actual solar angular diameter)
  bool m_physicalCalibrationEnabled = false;
  // Always true: importance textures are generated using solid-angle weights
  // (sin(theta)) so light PDFs stay invariant for material evaluation.
  bool m_envSolidAngleSampling = true;

  // data for automatically extracted sun from a file-based IBL.  When
  // m_hasFileSun is true the environment CDF has had the sun pixels removed
  // and the analytic sun parameters below describe the light.
  bool m_hasFileSun = false;
  // direction in local (unrotated) map space; rotation applied when using
  // world-space value.
  DirectX::XMFLOAT3 m_fileSunLocalDir = {0.0f, 1.0f, 0.0f};
  DirectX::XMFLOAT3 m_fileSunRadiance = {1.0f, 1.0f, 1.0f};
  float m_fileSunIntensity = 1.0f;
  float m_fileSunRadiusDeg = 0.53f;

  static constexpr float kPhysicalSkyIntensity = 1.0f;
  static constexpr float kPhysicalSunIntensityLux = 110000.0f;
  static constexpr float kPhysicalSunSizeDeg = 0.53f;

  float m_iblRotationDegrees = 0.0f;

  float m_dbgSkyAvgLuminanceCdM2 = 0.0f;
  float m_dbgSkyHorizonLuminanceCdM2 = 0.0f;
  float m_dbgSkyMaxLuminanceCdM2 = 0.0f;
};
