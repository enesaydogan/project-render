#pragma once
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

  // Sky Model Integration
  bool InitializeSkyModel(const std::string &datasetPath);
  void UpdateSkyModel();

  // Parameters
  void SetIBLSource(IBLSource source);
  IBLSource GetIBLSource() const { return m_source; }

  void SetSkyVisibility(float km) {
    if (m_visibility != km) {
      m_visibility = km;
      m_skyDirty = true;
    }
  }
  float GetSkyVisibility() const { return m_visibility; }

  void SetSkyAlbedo(float v) {
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

  Asset::Texture m_envMap;            // Current API-facing env map
  Asset::Texture m_fileTexture;       // Backing store for file IBL
  Asset::Texture m_proceduralTexture; // Backing store for Sky Model

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

  float m_visibility = 30.0f; // km (20 - 100 range usually)
  float m_albedo = 0.5f;
  float m_solarElevation = 0.5f;
  float m_solarAzimuth = 0.0f;
  float m_altitude = 200.0f; // meters (0 - 15000)
  float m_skyIntensity = 5.0f; //boost sky to balance sun
  float m_sunIntensity = 10000.0f; //reduce default intensity
  float m_sunSize = 1.5f; // degrees (actual sun size is ~0.5 deg)
  bool m_physicalCalibrationEnabled = false;

  static constexpr float kPhysicalSkyIntensity = 1.0f;
  static constexpr float kPhysicalSunIntensityLux = 110000.0f;
  static constexpr float kPhysicalSunSizeDeg = 0.53f;

  float m_iblRotationDegrees = 0.0f;

  float m_dbgSkyAvgLuminanceCdM2 = 0.0f;
  float m_dbgSkyHorizonLuminanceCdM2 = 0.0f;
  float m_dbgSkyMaxLuminanceCdM2 = 0.0f;
};
