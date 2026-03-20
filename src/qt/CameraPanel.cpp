#include "CameraPanel.h"

#include "SliderControl.h"

#include "../camera.h"
#include "../dxr_renderer.h"
#include "../raster_renderer.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

SliderControl *CreateSliderControl(double minValue,
                                   double maxValue,
                                   double step,
                                   int decimals)
{
    return new SliderControl(minValue, maxValue, step, decimals);
}

double CurrentHorizontalFovDegrees()
{
    const double aspect = (std::max)(0.0001, static_cast<double>(g_cameraData.aspect));
    const double currentVHalf = static_cast<double>(g_cameraData.fov) * 0.5 * (kPi / 180.0);
    const double currentH = 2.0 * std::atan(std::tan(currentVHalf) * aspect) * (180.0 / kPi);
    return currentH;
}

void SetHorizontalFovDegrees(double horizontalDegrees)
{
    const double aspect = (std::max)(0.0001, static_cast<double>(g_cameraData.aspect));
    const double hHalfRad = horizontalDegrees * 0.5 * (kPi / 180.0);
    const double vHalfRad = std::atan(std::tan(hHalfRad) / aspect);
    g_cameraData.fov = static_cast<float>(2.0 * vHalfRad * (180.0 / kPi));
}

} // namespace

CameraPanel::CameraPanel(QWidget *parent)
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

void CameraPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *lensGroup = new QGroupBox(tr("Lens & Navigation"), this);
    auto *lensForm = new QFormLayout(lensGroup);
    m_horizontalFov = CreateSliderControl(20.0, 160.0, 1.0, 1);
    m_moveSpeed = CreateSliderControl(0.1, 20.0, 0.1, 2);
    m_mouseSensitivity = CreateSliderControl(0.001, 0.05, 0.001, 3);
    m_resetCameraButton = new QPushButton(tr("Reset Camera"), lensGroup);
    lensForm->addRow(tr("Horizontal FOV"), m_horizontalFov);
    lensForm->addRow(tr("Move Speed"), m_moveSpeed);
    lensForm->addRow(tr("Mouse Sensitivity"), m_mouseSensitivity);
    lensForm->addRow(m_resetCameraButton);
    layout->addWidget(lensGroup);

    auto *exposureGroup = new QGroupBox(tr("Exposure"), this);
    auto *exposureLayout = new QVBoxLayout(exposureGroup);
    auto *modeForm = new QFormLayout();
    m_autoExposure = new QCheckBox(tr("Enable Auto Exposure"), exposureGroup);
    m_physicalCamera = new QCheckBox(tr("Use Physical Camera"), exposureGroup);
    m_exposureCompensation = CreateSliderControl(0.1, 10.0, 0.05, 2);
    m_manualExposure = CreateSliderControl(0.0001, 2.0, 0.01, 4);
    m_manualExposure->setLogarithmic(true);
    m_manualExposure->spinBox()->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    modeForm->addRow(m_autoExposure);
    modeForm->addRow(m_physicalCamera);
    modeForm->addRow(tr("Exposure Compensation"), m_exposureCompensation);
    modeForm->addRow(tr("Manual Exposure Scale"), m_manualExposure);
    exposureLayout->addLayout(modeForm);

    auto *presetRow = new QHBoxLayout();
    m_presetDaylight = new QPushButton(tr("Engine Daylight"), exposureGroup);
    m_presetSunny16 = new QPushButton(tr("Sunny 16"), exposureGroup);
    m_presetInterior = new QPushButton(tr("Interior"), exposureGroup);
    presetRow->addWidget(m_presetDaylight);
    presetRow->addWidget(m_presetSunny16);
    presetRow->addWidget(m_presetInterior);
    exposureLayout->addLayout(presetRow);

    auto *physicalForm = new QFormLayout();
    m_iso = CreateSliderControl(25.0, 6400.0, 25.0, 0);
    m_shutterSeconds = CreateSliderControl(1.0 / 8000.0, 30.0, 0.001, 4);
    m_shutterSeconds->setLogarithmic(true);
    m_shutterSeconds->spinBox()->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    m_aperture = CreateSliderControl(1.0, 22.0, 0.1, 1);
    physicalForm->addRow(tr("ISO"), m_iso);
    physicalForm->addRow(tr("Shutter (s)"), m_shutterSeconds);
    physicalForm->addRow(tr("Aperture (f/N)"), m_aperture);
    exposureLayout->addLayout(physicalForm);

    auto *evRow = new QHBoxLayout();
    m_evLabel = new QLabel(exposureGroup);
    m_matchSceneEv = new QPushButton(tr("Match Scene EV"), exposureGroup);
    evRow->addWidget(m_evLabel);
    evRow->addStretch(1);
    evRow->addWidget(m_matchSceneEv);
    exposureLayout->addLayout(evRow);
    layout->addWidget(exposureGroup);

    auto *tonemapGroup = new QGroupBox(tr("Tone Map"), this);
    auto *tonemapForm = new QFormLayout(tonemapGroup);
    m_tonemapVignette = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_tonemapSaturation = CreateSliderControl(0.0, 2.0, 0.01, 2);
    m_tonemapContrast = CreateSliderControl(0.0, 2.0, 0.01, 2);
    tonemapForm->addRow(tr("Vignette"), m_tonemapVignette);
    tonemapForm->addRow(tr("Saturation"), m_tonemapSaturation);
    tonemapForm->addRow(tr("Contrast"), m_tonemapContrast);
    layout->addWidget(tonemapGroup);

    layout->addStretch(1);

    connect(m_horizontalFov->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLensSettings();
    });
    connect(m_moveSpeed->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLensSettings();
    });
    connect(m_mouseSensitivity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLensSettings();
    });
    connect(m_resetCameraButton, &QPushButton::clicked, this, [this]() {
        ResetCamera();
        UpdateCameraCB();
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });

    connect(m_autoExposure, &QCheckBox::toggled, this, [this](bool) {
        applyExposureSettings(true);
    });
    connect(m_physicalCamera, &QCheckBox::toggled, this, [this](bool) {
        applyExposureSettings(true);
    });
    connect(m_exposureCompensation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyExposureSettings(true);
    });
    connect(m_manualExposure->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyExposureSettings(true);
    });
    connect(m_iso->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyExposureSettings(true);
    });
    connect(m_shutterSeconds->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyExposureSettings(true);
    });
    connect(m_aperture->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyExposureSettings(true);
    });

    connect(m_presetDaylight, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 30.0f, 2.8f);
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });
    connect(m_presetSunny16, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetPhysicalCameraSettings(100.0f, 1.0f / 125.0f, 16.0f);
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });
    connect(m_presetInterior, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetPhysicalCameraSettings(800.0f, 1.0f / 15.0f, 2.8f);
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });
    connect(m_matchSceneEv, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        const float sceneEv = DxrRenderer::GetCurrentEV100();
        const float iso = 100.0f;
        const float aperture = 8.0f;
        float shutterSeconds = (aperture * aperture * 100.0f) /
                               ((std::max)(0.001f, iso) * std::pow(2.0f, sceneEv));
        shutterSeconds = std::clamp(shutterSeconds, 1.0f / 8000.0f, 30.0f);
        DxrRenderer::SetPhysicalCameraSettings(iso, shutterSeconds, aperture);
        DxrRenderer::ResetAccumulation();
        syncFromRenderer();
    });

    connect(m_tonemapVignette->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyTonemapSettings();
    });
    connect(m_tonemapSaturation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyTonemapSettings();
    });
    connect(m_tonemapContrast->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyTonemapSettings();
    });
}

void CameraPanel::syncFromRenderer()
{
    m_syncing = true;

    const bool autoExposure = DxrRenderer::GetAutoExposure();
    const bool physicalCamera = DxrRenderer::GetPhysicalCameraExposure();
    const float sceneEv = DxrRenderer::GetCurrentEV100();
    const float avgLum = DxrRenderer::GetCurrentAvgLuminance();

    m_statusLabel->setText(
        tr("Scene EV100: %1\nAverage Luminance: %2 cd/m²")
            .arg(sceneEv, 0, 'f', 2)
            .arg(avgLum, 0, 'f', 2));

    m_horizontalFov->setValue(CurrentHorizontalFovDegrees());
    m_moveSpeed->setValue(g_camSpeed);
    m_mouseSensitivity->setValue(g_mouseSensitivity);

    m_autoExposure->setChecked(autoExposure);
    m_physicalCamera->setChecked(physicalCamera);
    m_exposureCompensation->setValue(DxrRenderer::GetExposureCompensation());
    m_manualExposure->setValue(g_cameraData.intensity);

    float iso = 100.0f;
    float shutterSeconds = 1.0f / 125.0f;
    float aperture = 16.0f;
    DxrRenderer::GetPhysicalCameraSettings(iso, shutterSeconds, aperture);
    m_iso->setValue(iso);
    m_shutterSeconds->setValue(shutterSeconds);
    m_aperture->setValue(aperture);
    m_evLabel->setText(tr("Camera EV100: %1").arg(DxrRenderer::GetPhysicalCameraEV100(), 0, 'f', 2));

    const bool usePhysicalControls = (!autoExposure && physicalCamera);
    m_exposureCompensation->setEnabled(autoExposure);
    m_manualExposure->setEnabled(!autoExposure && !physicalCamera);
    m_iso->setEnabled(usePhysicalControls);
    m_shutterSeconds->setEnabled(usePhysicalControls);
    m_aperture->setEnabled(usePhysicalControls);
    m_presetDaylight->setEnabled(usePhysicalControls);
    m_presetSunny16->setEnabled(usePhysicalControls);
    m_presetInterior->setEnabled(usePhysicalControls);
    m_matchSceneEv->setEnabled(usePhysicalControls);

    const auto &rs = RasterRenderer::GetRenderSettings();
    m_tonemapVignette->setValue(rs.tonemapVignette);
    m_tonemapSaturation->setValue(rs.tonemapSaturation);
    m_tonemapContrast->setValue(rs.tonemapContrast);

    m_syncing = false;
}

void CameraPanel::applyLensSettings()
{
    if (m_syncing) {
        return;
    }

    SetHorizontalFovDegrees(m_horizontalFov->value());
    g_camSpeed = static_cast<float>(m_moveSpeed->value());
    g_mouseSensitivity = static_cast<float>(m_mouseSensitivity->value());
    UpdateCameraCB();
    DxrRenderer::ResetAccumulation();
}

void CameraPanel::applyExposureSettings(bool resetAccumulation)
{
    if (m_syncing) {
        return;
    }

    DxrRenderer::SetAutoExposure(m_autoExposure->isChecked());
    DxrRenderer::SetPhysicalCameraExposure(m_physicalCamera->isChecked());
    DxrRenderer::SetExposureCompensation(static_cast<float>(m_exposureCompensation->value()));
    g_cameraData.intensity = static_cast<float>(m_manualExposure->value());
    DxrRenderer::SetPhysicalCameraSettings(static_cast<float>(m_iso->value()),
                                           static_cast<float>(m_shutterSeconds->value()),
                                           static_cast<float>(m_aperture->value()));
    UpdateCameraCB();
    if (resetAccumulation) {
        DxrRenderer::ResetAccumulation();
    }
    syncFromRenderer();
}

void CameraPanel::applyTonemapSettings()
{
    if (m_syncing) {
        return;
    }

    auto &rs = RasterRenderer::GetRenderSettings();
    rs.tonemapVignette = static_cast<float>(m_tonemapVignette->value());
    rs.tonemapSaturation = static_cast<float>(m_tonemapSaturation->value());
    rs.tonemapContrast = static_cast<float>(m_tonemapContrast->value());
}
