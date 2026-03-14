#include "EnvironmentPanel.h"

#include "SliderControl.h"

#include "../camera.h"
#include "../clouds.h"
#include "../dxr_renderer.h"
#include "../ibl_manager.h"
#include "../scene.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

extern HWND g_hwnd;
extern bool g_cloudRenderingEnabled;
extern float g_timeOfDay;
extern float g_northOffset;
extern float g_latitudeDeg;
extern float g_dayOfYear;

namespace {

SliderControl *CreateSliderControl(double minValue,
                                   double maxValue,
                                   double step,
                                   int decimals)
{
    return new SliderControl(minValue, maxValue, step, decimals);
}

} // namespace

EnvironmentPanel::EnvironmentPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    syncFromRenderer();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        syncFromRenderer();
    });
    m_refreshTimer->start(250);
}

void EnvironmentPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    auto *tabs = new QTabWidget(this);
    auto *lightingTab = new QWidget(tabs);
    auto *lightingLayout = new QVBoxLayout(lightingTab);

    auto *sourceGroup = new QGroupBox(tr("IBL & Sky"), lightingTab);
    auto *sourceForm = new QFormLayout(sourceGroup);
    m_iblSource = new QComboBox(sourceGroup);
    m_iblSource->addItems({tr("File IBL"), tr("Prague Sky")});
    m_loadHdrButton = new QPushButton(tr("Load HDR / EXR"), sourceGroup);
    m_filePathLabel = new QLabel(sourceGroup);
    m_filePathLabel->setWordWrap(true);
    m_iblRotation = CreateSliderControl(0.0, 360.0, 1.0, 1);
    m_solidAngleSampling = new QCheckBox(tr("Use Solid-Angle Env Sampling"), sourceGroup);
    m_analyticSunIntensity = CreateSliderControl(0.0, 150000.0, 100.0, 0);
    m_fileSunIntensity = CreateSliderControl(0.01, 200.0, 0.1, 2);
    m_fileSunIntensity->setLogarithmic(true);
    m_fileSunIntensity->spinBox()->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    m_fileSunSize = CreateSliderControl(0.01, 5.0, 0.01, 2);
    sourceForm->addRow(tr("Source"), m_iblSource);
    sourceForm->addRow(m_loadHdrButton);
    sourceForm->addRow(tr("Loaded Map"), m_filePathLabel);
    sourceForm->addRow(tr("IBL Rotation (deg)"), m_iblRotation);
    sourceForm->addRow(m_solidAngleSampling);
    sourceForm->addRow(tr("Analytic Sun Intensity"), m_analyticSunIntensity);
    sourceForm->addRow(tr("File Sun Scale"), m_fileSunIntensity);
    sourceForm->addRow(tr("File Sun Size (deg)"), m_fileSunSize);
    lightingLayout->addWidget(sourceGroup);

    auto *skyGroup = new QGroupBox(tr("Procedural Sky"), lightingTab);
    auto *skyForm = new QFormLayout(skyGroup);
    m_physicalCalibration = new QCheckBox(tr("Physical Calibration"), skyGroup);
    m_visibility = CreateSliderControl(10.0, 120.0, 1.0, 1);
    m_albedo = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_altitude = CreateSliderControl(0.0, 15000.0, 10.0, 0);
    m_skyIntensity = CreateSliderControl(0.0, 5.0, 0.05, 3);
    m_sunIntensity = CreateSliderControl(0.0, 150000.0, 100.0, 0);
    m_sunSize = CreateSliderControl(0.1, 5.0, 0.05, 2);
    m_timeOfDay = CreateSliderControl(6.0, 18.0, 0.1, 1);
    m_northOffset = CreateSliderControl(0.0, 360.0, 1.0, 1);
    m_latitude = CreateSliderControl(-66.5, 66.5, 0.1, 1);
    m_dayOfYear = CreateSliderControl(1.0, 365.0, 1.0, 0);
    m_solarLabel = new QLabel(skyGroup);
    skyForm->addRow(m_physicalCalibration);
    skyForm->addRow(tr("Visibility (km)"), m_visibility);
    skyForm->addRow(tr("Earth Albedo"), m_albedo);
    skyForm->addRow(tr("Observer Altitude (m)"), m_altitude);
    skyForm->addRow(tr("Sky Intensity"), m_skyIntensity);
    skyForm->addRow(tr("Sun Intensity (Lux)"), m_sunIntensity);
    skyForm->addRow(tr("Sun Size (deg)"), m_sunSize);
    skyForm->addRow(tr("Time Of Day"), m_timeOfDay);
    skyForm->addRow(tr("North Offset"), m_northOffset);
    skyForm->addRow(tr("Latitude (deg)"), m_latitude);
    skyForm->addRow(tr("Day Of Year"), m_dayOfYear);
    skyForm->addRow(tr("Solar Model"), m_solarLabel);
    lightingLayout->addWidget(skyGroup);
    lightingLayout->addStretch(1);
    tabs->addTab(lightingTab, tr("Lighting"));

    auto *cloudTab = new QWidget(tabs);
    auto *cloudLayout = new QVBoxLayout(cloudTab);
    auto *cloudGroup = new QGroupBox(tr("Clouds"), cloudTab);
    auto *cloudForm = new QFormLayout(cloudGroup);
    m_cloudEnabled = new QCheckBox(tr("Enable Cloud Rendering"), cloudGroup);
    m_resetCloudsButton = new QPushButton(tr("Reset To Defaults"), cloudGroup);
    m_cloudDensity = CreateSliderControl(0.0, 5.0, 0.05, 2);
    m_cloudAbsorption = CreateSliderControl(0.0, 2.0, 0.05, 2);
    m_cloudCoverage = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_cloudScattering = CreateSliderControl(-0.99, 0.99, 0.01, 2);
    m_cloudSunIntensity = CreateSliderControl(0.0, 20.0, 0.1, 2);
    m_cloudTop = CreateSliderControl(200.0, 1000.0, 5.0, 1);
    m_cloudBottom = CreateSliderControl(50.0, 300.0, 5.0, 1);
    m_cloudWindSpeed = CreateSliderControl(0.0, 50.0, 0.5, 2);
    m_baseScale = CreateSliderControl(0.0001, 0.0020, 0.0001, 5);
    m_baseScale->setLogarithmic(true);
    m_detailScale = CreateSliderControl(0.0005, 0.01, 0.0001, 5);
    m_detailScale->setLogarithmic(true);
    m_coverageScale = CreateSliderControl(0.00005, 0.0010, 0.00001, 5);
    m_coverageScale->setLogarithmic(true);
    m_coverageVariation = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_erosion = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_warpStrength = CreateSliderControl(0.0, 2.0, 0.05, 2);
    m_shapePower = CreateSliderControl(0.4, 3.0, 0.05, 2);
    m_powderStrength = CreateSliderControl(0.0, 1.5, 0.05, 2);
    m_shadowSteps = CreateSliderControl(1.0, 16.0, 1.0, 0);
    m_shadowStepSize = CreateSliderControl(10.0, 500.0, 5.0, 1);
    m_shadowLod = CreateSliderControl(0.0, 5.0, 0.1, 2);
    cloudForm->addRow(m_cloudEnabled);
    cloudForm->addRow(m_resetCloudsButton);
    cloudForm->addRow(tr("Density"), m_cloudDensity);
    cloudForm->addRow(tr("Absorption"), m_cloudAbsorption);
    cloudForm->addRow(tr("Coverage"), m_cloudCoverage);
    cloudForm->addRow(tr("Scattering (g)"), m_cloudScattering);
    cloudForm->addRow(tr("Sun Intensity"), m_cloudSunIntensity);
    cloudForm->addRow(tr("Cloud Top"), m_cloudTop);
    cloudForm->addRow(tr("Cloud Bottom"), m_cloudBottom);
    cloudForm->addRow(tr("Wind Speed"), m_cloudWindSpeed);
    cloudForm->addRow(tr("Base Scale"), m_baseScale);
    cloudForm->addRow(tr("Detail Scale"), m_detailScale);
    cloudForm->addRow(tr("Coverage Scale"), m_coverageScale);
    cloudForm->addRow(tr("Coverage Variation"), m_coverageVariation);
    cloudForm->addRow(tr("Erosion"), m_erosion);
    cloudForm->addRow(tr("Warp Strength"), m_warpStrength);
    cloudForm->addRow(tr("Shape Power"), m_shapePower);
    cloudForm->addRow(tr("Powder Strength"), m_powderStrength);
    cloudForm->addRow(tr("Shadow Steps"), m_shadowSteps);
    cloudForm->addRow(tr("Shadow Step Size"), m_shadowStepSize);
    cloudForm->addRow(tr("Shadow LOD"), m_shadowLod);
    cloudLayout->addWidget(cloudGroup);
    cloudLayout->addStretch(1);
    tabs->addTab(cloudTab, tr("Clouds"));

    layout->addWidget(tabs);

    connect(m_loadHdrButton, &QPushButton::clicked, this, [this]() {
        if (Scene::ImportHDRWithDialog(g_hwnd)) {
            DxrRenderer::ResetAccumulation();
            syncFromRenderer();
        }
    });
    connect(m_iblSource, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        applyLightingSettings(true, false);
    });
    connect(m_iblRotation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLightingSettings(false, true);
    });
    connect(m_solidAngleSampling, &QCheckBox::toggled, this, [this](bool) {
        applyLightingSettings(false, true);
    });
    connect(m_analyticSunIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLightingSettings(false, false);
    });
    connect(m_fileSunIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLightingSettings(false, false);
    });
    connect(m_fileSunSize->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLightingSettings(false, false);
    });

    auto connectSkyControl = [this](SliderControl *control) {
        connect(control->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            applyLightingSettings(true, false);
        });
    };
    connect(m_physicalCalibration, &QCheckBox::toggled, this, [this](bool) {
        applyLightingSettings(true, false);
    });
    connectSkyControl(m_visibility);
    connectSkyControl(m_albedo);
    connectSkyControl(m_altitude);
    connectSkyControl(m_skyIntensity);
    connectSkyControl(m_sunIntensity);
    connectSkyControl(m_sunSize);
    connectSkyControl(m_timeOfDay);
    connectSkyControl(m_northOffset);
    connectSkyControl(m_latitude);
    connectSkyControl(m_dayOfYear);

    connect(m_cloudEnabled, &QCheckBox::toggled, this, [this](bool) {
        applyCloudSettings();
    });
    connect(m_resetCloudsButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        g_cloudManager.ResetToDefaults();
        g_cloudManager.RequestBake();
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });
    auto connectCloudControl = [this](SliderControl *control) {
        connect(control->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            applyCloudSettings();
        });
    };
    connectCloudControl(m_cloudDensity);
    connectCloudControl(m_cloudAbsorption);
    connectCloudControl(m_cloudCoverage);
    connectCloudControl(m_cloudScattering);
    connectCloudControl(m_cloudSunIntensity);
    connectCloudControl(m_cloudTop);
    connectCloudControl(m_cloudBottom);
    connectCloudControl(m_cloudWindSpeed);
    connectCloudControl(m_baseScale);
    connectCloudControl(m_detailScale);
    connectCloudControl(m_coverageScale);
    connectCloudControl(m_coverageVariation);
    connectCloudControl(m_erosion);
    connectCloudControl(m_warpStrength);
    connectCloudControl(m_shapePower);
    connectCloudControl(m_powderStrength);
    connectCloudControl(m_shadowSteps);
    connectCloudControl(m_shadowStepSize);
    connectCloudControl(m_shadowLod);
}

void EnvironmentPanel::syncFromRenderer()
{
    m_syncing = true;

    auto &ibl = IBLManager::Get();
    const bool usingFileIbl = ibl.GetIBLSource() == IBLManager::IBLSource::File;
    const bool physicalSky = ibl.IsPhysicalCalibrationEnabled();

    m_summaryLabel->setText(
        tr("Active source: %1\nSky avg luminance: %2 cd/m²")
            .arg(usingFileIbl ? tr("File IBL") : tr("Prague Sky"))
            .arg(ibl.GetSkyAvgLuminanceCdM2(), 0, 'f', 1));
    const QString envPath = ibl.GetEnvironmentMapPath().empty()
        ? tr("None loaded")
        : QString::fromUtf8(ibl.GetEnvironmentMapPath().c_str());
    m_filePathLabel->setText(envPath);

    m_iblSource->setCurrentIndex(usingFileIbl ? 0 : 1);
    m_iblRotation->setValue(ibl.GetIblRotationDegrees());
    m_solidAngleSampling->setChecked(g_cameraData.sampleEnvSolidAngle > 0.5f);
    m_analyticSunIntensity->setValue(ibl.GetSunIntensity());
    m_fileSunIntensity->setValue(ibl.GetFileSunIntensity());
    m_fileSunSize->setValue(ibl.GetFileSunRadiusDeg());

    m_physicalCalibration->setChecked(physicalSky);
    m_visibility->setValue(ibl.GetSkyVisibility());
    m_albedo->setValue(ibl.GetSkyAlbedo());
    m_altitude->setValue(ibl.GetObserverAltitude());
    m_skyIntensity->setValue(ibl.GetSkyIntensity());
    m_sunIntensity->setValue(ibl.GetSunIntensity());
    m_sunSize->setValue(ibl.GetSunSize());
    m_timeOfDay->setValue(g_timeOfDay);
    m_northOffset->setValue(g_northOffset);
    m_latitude->setValue(g_latitudeDeg);
    m_dayOfYear->setValue(g_dayOfYear);
    m_solarLabel->setText(
        tr("Altitude %1, azimuth %2")
            .arg(ibl.GetSolarAltitude(), 0, 'f', 2)
            .arg(ibl.GetSolarAzimuth(), 0, 'f', 2));

    const bool proceduralControls = !usingFileIbl;
    m_physicalCalibration->setEnabled(proceduralControls);
    m_visibility->setEnabled(proceduralControls);
    m_albedo->setEnabled(proceduralControls);
    m_altitude->setEnabled(proceduralControls);
    m_timeOfDay->setEnabled(proceduralControls);
    m_northOffset->setEnabled(proceduralControls);
    m_latitude->setEnabled(proceduralControls);
    m_dayOfYear->setEnabled(proceduralControls);
    m_solarLabel->setEnabled(proceduralControls);
    m_skyIntensity->setEnabled(proceduralControls && !physicalSky);
    m_sunIntensity->setEnabled(proceduralControls && !physicalSky);
    m_sunSize->setEnabled(proceduralControls && !physicalSky);
    m_fileSunIntensity->setEnabled(usingFileIbl && ibl.HasFileSun());
    m_fileSunSize->setEnabled(usingFileIbl && ibl.HasFileSun());

    CloudParams &cp = g_cloudManager.GetParams();
    m_cloudEnabled->setChecked(g_cloudRenderingEnabled);
    m_cloudDensity->setValue(cp.density);
    m_cloudAbsorption->setValue(cp.absorption);
    m_cloudCoverage->setValue(cp.coverage);
    m_cloudScattering->setValue(cp.scattering);
    m_cloudSunIntensity->setValue(cp.sunIntensity);
    m_cloudTop->setValue(cp.cloudTop);
    m_cloudBottom->setValue(cp.cloudBottom);
    m_cloudWindSpeed->setValue(cp.windSpeed);
    m_baseScale->setValue(cp.baseScale);
    m_detailScale->setValue(cp.detailScale);
    m_coverageScale->setValue(cp.coverageScale);
    m_coverageVariation->setValue(cp.coverageVariation);
    m_erosion->setValue(cp.erosion);
    m_warpStrength->setValue(cp.warpStrength);
    m_shapePower->setValue(cp.shapePower);
    m_powderStrength->setValue(cp.powderStrength);
    m_shadowSteps->setValue(cp.shadowSteps);
    m_shadowStepSize->setValue(cp.shadowStepSize);
    m_shadowLod->setValue(cp.shadowLod);

    const bool cloudsEditable = g_cloudRenderingEnabled;
    m_cloudDensity->setEnabled(cloudsEditable);
    m_cloudAbsorption->setEnabled(cloudsEditable);
    m_cloudCoverage->setEnabled(cloudsEditable);
    m_cloudScattering->setEnabled(cloudsEditable);
    m_cloudSunIntensity->setEnabled(cloudsEditable);
    m_cloudTop->setEnabled(cloudsEditable);
    m_cloudBottom->setEnabled(cloudsEditable);
    m_cloudWindSpeed->setEnabled(cloudsEditable);
    m_baseScale->setEnabled(cloudsEditable);
    m_detailScale->setEnabled(cloudsEditable);
    m_coverageScale->setEnabled(cloudsEditable);
    m_coverageVariation->setEnabled(cloudsEditable);
    m_erosion->setEnabled(cloudsEditable);
    m_warpStrength->setEnabled(cloudsEditable);
    m_shapePower->setEnabled(cloudsEditable);
    m_powderStrength->setEnabled(cloudsEditable);
    m_shadowSteps->setEnabled(cloudsEditable);
    m_shadowStepSize->setEnabled(cloudsEditable);
    m_shadowLod->setEnabled(cloudsEditable);

    m_syncing = false;
}

void EnvironmentPanel::applyLightingSettings(bool updateSkyModel, bool updateCameraBuffer)
{
    if (m_syncing) {
        return;
    }

    auto &ibl = IBLManager::Get();
    ibl.SetIBLSource(m_iblSource->currentIndex() == 0
                         ? IBLManager::IBLSource::File
                         : IBLManager::IBLSource::PragueSkyModel);
    ibl.SetIblRotationDegrees(static_cast<float>(m_iblRotation->value()));
    g_cameraData.iblRotationDegrees = static_cast<float>(m_iblRotation->value());
    ibl.SetEnvSolidAngleSampling(m_solidAngleSampling->isChecked());
    g_cameraData.sampleEnvSolidAngle = m_solidAngleSampling->isChecked() ? 1.0f : 0.0f;
    ibl.SetSunIntensity(static_cast<float>(m_analyticSunIntensity->value()));

    ibl.SetPhysicalCalibrationEnabled(m_physicalCalibration->isChecked());
    ibl.SetSkyVisibility(static_cast<float>(m_visibility->value()));
    ibl.SetSkyAlbedo(static_cast<float>(m_albedo->value()));
    ibl.SetObserverAltitude(static_cast<float>(m_altitude->value()));
    ibl.SetSkyIntensity(static_cast<float>(m_skyIntensity->value()));
    ibl.SetSunIntensity(static_cast<float>(m_sunIntensity->value()));
    ibl.SetSunSize(static_cast<float>(m_sunSize->value()));
    ibl.SetFileSunIntensity(static_cast<float>(m_fileSunIntensity->value()));
    ibl.SetFileSunRadiusDeg(static_cast<float>(m_fileSunSize->value()));

    g_timeOfDay = static_cast<float>(m_timeOfDay->value());
    g_northOffset = static_cast<float>(m_northOffset->value());
    g_latitudeDeg = static_cast<float>(m_latitude->value());
    g_dayOfYear = static_cast<float>(m_dayOfYear->value());

    if (updateSkyModel && ibl.GetIBLSource() == IBLManager::IBLSource::PragueSkyModel) {
        ibl.UpdateSkyModel();
    }
    if (updateCameraBuffer) {
        UpdateCameraCB();
    }
    DxrRenderer::ResetAccumulation();
    syncFromRenderer();
}

void EnvironmentPanel::applyCloudSettings()
{
    if (m_syncing) {
        return;
    }

    g_cloudRenderingEnabled = m_cloudEnabled->isChecked();

    CloudParams &cp = g_cloudManager.GetParams();
    cp.density = static_cast<float>(m_cloudDensity->value());
    cp.absorption = static_cast<float>(m_cloudAbsorption->value());
    cp.coverage = static_cast<float>(m_cloudCoverage->value());
    cp.scattering = static_cast<float>(m_cloudScattering->value());
    cp.sunIntensity = static_cast<float>(m_cloudSunIntensity->value());
    cp.cloudTop = static_cast<float>(m_cloudTop->value());
    cp.cloudBottom = static_cast<float>(m_cloudBottom->value());
    cp.windSpeed = static_cast<float>(m_cloudWindSpeed->value());
    cp.baseScale = static_cast<float>(m_baseScale->value());
    cp.detailScale = static_cast<float>(m_detailScale->value());
    cp.coverageScale = static_cast<float>(m_coverageScale->value());
    cp.coverageVariation = static_cast<float>(m_coverageVariation->value());
    cp.erosion = static_cast<float>(m_erosion->value());
    cp.warpStrength = static_cast<float>(m_warpStrength->value());
    cp.shapePower = static_cast<float>(m_shapePower->value());
    cp.powderStrength = static_cast<float>(m_powderStrength->value());
    cp.shadowSteps = static_cast<int>(m_shadowSteps->value());
    cp.shadowStepSize = static_cast<float>(m_shadowStepSize->value());
    cp.shadowLod = static_cast<float>(m_shadowLod->value());

    g_cloudManager.RequestBake();
    DxrRenderer::ResetAccumulation();
    syncFromRenderer();
}