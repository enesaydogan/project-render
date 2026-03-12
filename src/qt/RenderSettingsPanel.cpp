#include "RenderSettingsPanel.h"

#include "../camera.h"
#include "../dx12_context.h"
#include "../dxr_renderer.h"
#include "../raster_renderer.h"
#include "../scene.h"
#include "../streamline_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

extern RenderMode g_currentRenderMode;
extern bool g_rayTracingSupported;

namespace {

QDoubleSpinBox *CreateDoubleSpinBox(double minValue,
                                    double maxValue,
                                    double step,
                                    int decimals)
{
    auto *spinBox = new QDoubleSpinBox();
    spinBox->setRange(minValue, maxValue);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(decimals);
    spinBox->setAccelerated(true);
    return spinBox;
}

QSpinBox *CreateSpinBox(int minValue, int maxValue)
{
    auto *spinBox = new QSpinBox();
    spinBox->setRange(minValue, maxValue);
    spinBox->setAccelerated(true);
    return spinBox;
}

int RealtimeDenoiserIndexFromMode(DxrRenderer::RealtimeDenoiserMode mode)
{
    switch (mode) {
    case DxrRenderer::RealtimeDenoiserMode::SVGF: return 1;
    case DxrRenderer::RealtimeDenoiserMode::NRD: return 2;
    default: return 0;
    }
}

DxrRenderer::RealtimeDenoiserMode RealtimeDenoiserModeFromIndex(int index)
{
    if (index == 1) {
        return DxrRenderer::RealtimeDenoiserMode::SVGF;
    }
    if (index == 2) {
        return DxrRenderer::RealtimeDenoiserMode::NRD;
    }
    return DxrRenderer::RealtimeDenoiserMode::Off;
}

int DenoiserIndexFromMode(DxrRenderer::DenoiserMode mode)
{
    switch (mode) {
    case DxrRenderer::DenoiserMode::OIDN_CPU: return 1;
    case DxrRenderer::DenoiserMode::OIDN_GPU: return 2;
    default: return 0;
    }
}

DxrRenderer::DenoiserMode DenoiserModeFromIndex(int index)
{
    if (index == 1) {
        return DxrRenderer::DenoiserMode::OIDN_CPU;
    }
    if (index == 2) {
        return DxrRenderer::DenoiserMode::OIDN_GPU;
    }
    return DxrRenderer::DenoiserMode::Off;
}

int StreamlineModeIndex(StreamlineManager::Mode mode)
{
    switch (mode) {
    case StreamlineManager::Mode::DLSS_SuperResolution: return 1;
    case StreamlineManager::Mode::DLSS_RayReconstruction: return 2;
    default: return 0;
    }
}

StreamlineManager::Mode StreamlineModeFromIndex(int index)
{
    if (index == 1) {
        return StreamlineManager::Mode::DLSS_SuperResolution;
    }
    if (index == 2) {
        return StreamlineManager::Mode::DLSS_RayReconstruction;
    }
    return StreamlineManager::Mode::Off;
}

int StreamlineQualityIndex(StreamlineManager::Quality quality)
{
    switch (quality) {
    case StreamlineManager::Quality::MaxPerformance: return 0;
    case StreamlineManager::Quality::Balanced: return 1;
    case StreamlineManager::Quality::MaxQuality: return 2;
    case StreamlineManager::Quality::UltraPerformance: return 3;
    case StreamlineManager::Quality::DLAA: return 4;
    default: return 1;
    }
}

StreamlineManager::Quality StreamlineQualityFromIndex(int index)
{
    switch (index) {
    case 0: return StreamlineManager::Quality::MaxPerformance;
    case 2: return StreamlineManager::Quality::MaxQuality;
    case 3: return StreamlineManager::Quality::UltraPerformance;
    case 4: return StreamlineManager::Quality::DLAA;
    default: return StreamlineManager::Quality::Balanced;
    }
}

} // namespace

RenderSettingsPanel::RenderSettingsPanel(QWidget *parent)
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

void RenderSettingsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *modeGroup = new QGroupBox(tr("Render Mode"), this);
    auto *modeLayout = new QVBoxLayout(modeGroup);

    m_modeLabel = new QLabel(modeGroup);
    m_statsLabel = new QLabel(modeGroup);
    m_statsLabel->setWordWrap(true);
    m_switchModeButton = new QPushButton(modeGroup);

    modeLayout->addWidget(m_modeLabel);
    modeLayout->addWidget(m_statsLabel);
    modeLayout->addWidget(m_switchModeButton);
    layout->addWidget(modeGroup);

    m_dxrSection = new QWidget(this);
    auto *dxrLayout = new QVBoxLayout(m_dxrSection);
    dxrLayout->setContentsMargins(0, 0, 0, 0);
    auto *dxrHeader = new QLabel(tr("DXR Settings"), m_dxrSection);
    dxrHeader->setStyleSheet("font-weight: 600;");
    dxrLayout->addWidget(dxrHeader);

    auto *pathGroup = new QGroupBox(tr("DXR Path Tracing"), m_dxrSection);
    auto *pathForm = new QFormLayout(pathGroup);
    m_reflectionBounces = CreateDoubleSpinBox(0.0, 16.0, 1.0, 0);
    m_refractionBounces = CreateDoubleSpinBox(0.0, 16.0, 1.0, 0);
    m_giBounces = CreateDoubleSpinBox(0.0, 16.0, 1.0, 0);
    m_maxSpp = CreateSpinBox(10, 1000);
    m_adaptiveSampling = new QCheckBox(tr("Enable Adaptive Sampling"), pathGroup);
    m_targetNoise = CreateDoubleSpinBox(1.0, 30.0, 0.1, 1);
    pathForm->addRow(tr("Reflection Bounces"), m_reflectionBounces);
    pathForm->addRow(tr("Refraction Bounces"), m_refractionBounces);
    pathForm->addRow(tr("GI Bounces"), m_giBounces);
    pathForm->addRow(tr("Max SPP"), m_maxSpp);
    pathForm->addRow(m_adaptiveSampling);
    pathForm->addRow(tr("Target Noise %"), m_targetNoise);
    dxrLayout->addWidget(pathGroup);

    auto *realtimeGroup = new QGroupBox(tr("Realtime Denoiser"), m_dxrSection);
    auto *realtimeForm = new QFormLayout(realtimeGroup);
    m_realtimeDenoiser = new QComboBox(realtimeGroup);
    m_realtimeDenoiser->addItems({tr("Off"), tr("SVGF"), tr("NRD (ReLAX)")});
    m_resetRealtimeHistory = new QPushButton(tr("Reset History"), realtimeGroup);
    realtimeForm->addRow(tr("Mode"), m_realtimeDenoiser);
    realtimeForm->addRow(m_resetRealtimeHistory);
    dxrLayout->addWidget(realtimeGroup);

    auto *dlssGroup = new QGroupBox(tr("Streamline / DLSS"), m_dxrSection);
    auto *dlssForm = new QFormLayout(dlssGroup);
    m_dlssEnabled = new QCheckBox(tr("Enable"), dlssGroup);
    m_dlssMode = new QComboBox(dlssGroup);
    m_dlssMode->addItems({tr("Off"), tr("DLSS Super Resolution"), tr("DLSS Ray Reconstruction")});
    m_dlssQuality = new QComboBox(dlssGroup);
    m_dlssQuality->addItems({tr("Max Performance"), tr("Balanced"), tr("Max Quality"), tr("Ultra Performance"), tr("DLAA")});
    m_rrJitterScale = CreateDoubleSpinBox(0.0, 1.0, 0.01, 2);
    m_resetDlssHistory = new QPushButton(tr("Reset DLSS History"), dlssGroup);
    m_renderSizeLabel = new QLabel(dlssGroup);
    m_renderSizeLabel->setWordWrap(true);
    dlssForm->addRow(m_dlssEnabled);
    dlssForm->addRow(tr("Mode"), m_dlssMode);
    dlssForm->addRow(tr("Quality"), m_dlssQuality);
    dlssForm->addRow(tr("RR Jitter Scale"), m_rrJitterScale);
    dlssForm->addRow(m_resetDlssHistory);
    dlssForm->addRow(tr("Render Resolution"), m_renderSizeLabel);
    dxrLayout->addWidget(dlssGroup);

    auto *finalGroup = new QGroupBox(tr("Final / Export Denoiser"), m_dxrSection);
    auto *finalForm = new QFormLayout(finalGroup);
    m_finalDenoiser = new QComboBox(finalGroup);
    m_finalDenoiser->addItems({tr("Off"), tr("OIDN (CPU)"), tr("OIDN (GPU)")});
    m_oidnQuality = new QComboBox(finalGroup);
    m_oidnQuality->addItems({tr("Fast"), tr("Balanced"), tr("High")});
    finalForm->addRow(tr("Denoiser"), m_finalDenoiser);
    finalForm->addRow(tr("OIDN Quality"), m_oidnQuality);
    dxrLayout->addWidget(finalGroup);

    layout->addWidget(m_dxrSection);

    m_rasterSection = new QWidget(this);
    auto *rasterLayout = new QVBoxLayout(m_rasterSection);
    rasterLayout->setContentsMargins(0, 0, 0, 0);
    auto *rasterHeader = new QLabel(tr("Raster Settings"), m_rasterSection);
    rasterHeader->setStyleSheet("font-weight: 600;");
    rasterLayout->addWidget(rasterHeader);

    m_resetButton = new QPushButton(tr("Reset Raster Settings"), m_rasterSection);
    rasterLayout->addWidget(m_resetButton);

    auto *ssrGroup = new QGroupBox(tr("Reflections (SSR)"), m_rasterSection);
    auto *ssrForm = new QFormLayout(ssrGroup);
    m_enableSsr = new QCheckBox(tr("Enable SSR"), ssrGroup);
    m_ssrStepSize = CreateDoubleSpinBox(0.02, 2.0, 0.01, 3);
    m_ssrThickness = CreateDoubleSpinBox(0.001, 1.0, 0.001, 3);
    m_ssrIntensity = CreateDoubleSpinBox(0.0, 2.0, 0.05, 2);
    m_ssrMinSmoothness = CreateDoubleSpinBox(0.0, 1.0, 0.01, 2);
    m_ssrMaxSteps = CreateSpinBox(1, 256);
    ssrForm->addRow(m_enableSsr);
    ssrForm->addRow(tr("Step Size"), m_ssrStepSize);
    ssrForm->addRow(tr("Thickness"), m_ssrThickness);
    ssrForm->addRow(tr("Intensity"), m_ssrIntensity);
    ssrForm->addRow(tr("Min Smoothness"), m_ssrMinSmoothness);
    ssrForm->addRow(tr("Max Steps"), m_ssrMaxSteps);
    rasterLayout->addWidget(ssrGroup);

    auto *ssaoGroup = new QGroupBox(tr("Ambient Occlusion (SSAO)"), m_rasterSection);
    auto *ssaoForm = new QFormLayout(ssaoGroup);
    m_enableSsao = new QCheckBox(tr("Enable SSAO"), ssaoGroup);
    m_ssaoRadius = CreateDoubleSpinBox(0.01, 5.0, 0.01, 3);
    m_ssaoBias = CreateDoubleSpinBox(0.0001, 0.25, 0.0005, 4);
    m_ssaoStrength = CreateDoubleSpinBox(0.0, 4.0, 0.05, 2);
    m_ssaoSamples = CreateSpinBox(1, 32);
    m_ssaoCompositeWeight = CreateDoubleSpinBox(0.0, 1.0, 0.01, 2);
    ssaoForm->addRow(m_enableSsao);
    ssaoForm->addRow(tr("Radius"), m_ssaoRadius);
    ssaoForm->addRow(tr("Bias"), m_ssaoBias);
    ssaoForm->addRow(tr("Strength"), m_ssaoStrength);
    ssaoForm->addRow(tr("Samples"), m_ssaoSamples);
    ssaoForm->addRow(tr("Composite Weight"), m_ssaoCompositeWeight);
    rasterLayout->addWidget(ssaoGroup);

    auto *bloomGroup = new QGroupBox(tr("Bloom"), m_rasterSection);
    auto *bloomForm = new QFormLayout(bloomGroup);
    m_enableBloom = new QCheckBox(tr("Enable Bloom"), bloomGroup);
    m_bloomThreshold = CreateDoubleSpinBox(0.0, 10.0, 0.05, 2);
    m_bloomIntensity = CreateDoubleSpinBox(0.0, 4.0, 0.05, 2);
    bloomForm->addRow(m_enableBloom);
    bloomForm->addRow(tr("Threshold"), m_bloomThreshold);
    bloomForm->addRow(tr("Intensity"), m_bloomIntensity);
    rasterLayout->addWidget(bloomGroup);

    auto *tonemapGroup = new QGroupBox(tr("Tonemap"), m_rasterSection);
    auto *tonemapForm = new QFormLayout(tonemapGroup);
    m_tonemapVignette = CreateDoubleSpinBox(0.0, 1.0, 0.01, 2);
    m_tonemapSaturation = CreateDoubleSpinBox(0.0, 2.0, 0.01, 2);
    m_tonemapContrast = CreateDoubleSpinBox(0.0, 2.0, 0.01, 2);
    tonemapForm->addRow(tr("Vignette"), m_tonemapVignette);
    tonemapForm->addRow(tr("Saturation"), m_tonemapSaturation);
    tonemapForm->addRow(tr("Contrast"), m_tonemapContrast);
    rasterLayout->addWidget(tonemapGroup);

    layout->addWidget(m_rasterSection);
    layout->addStretch(1);

    connect(m_switchModeButton, &QPushButton::clicked, this, [this]() {
        if (g_currentRenderMode == RenderMode::Raster) {
            g_currentRenderMode = RenderMode::DXR;
            Scene::RebuildAccelerationStructures();
            recreateDxrPipeline("Qt render mode switch");
        } else {
            g_currentRenderMode = RenderMode::Raster;
        }
        syncFromRenderer();
    });

    auto applyCamera = [this]() { applyCameraSettings(); };
    connect(m_reflectionBounces, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_refractionBounces, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_giBounces, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_maxSpp, qOverload<int>(&QSpinBox::valueChanged), this, [applyCamera](int) { applyCamera(); });
    connect(m_adaptiveSampling, &QCheckBox::toggled, this, [applyCamera](bool) { applyCamera(); });
    connect(m_targetNoise, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });

    connect(m_realtimeDenoiser, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetRealtimeDenoiserMode(RealtimeDenoiserModeFromIndex(index));
        DxrRenderer::ResetAccumulation();
        recreateDxrPipeline("Qt realtime denoiser mode change");
    });
    connect(m_resetRealtimeHistory, &QPushButton::clicked, this, []() {
        DxrRenderer::ResetRealtimeDenoiserHistory();
    });

    connect(m_dlssEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        DX12Context::g_streamline.SetEnabled(enabled);
        DxrRenderer::ResetStreamlineHistory();
        recreateDxrPipeline("Qt DLSS enable toggle");
    });
    connect(m_dlssMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        const auto newMode = StreamlineModeFromIndex(index);
        DX12Context::g_streamline.SetMode(newMode);
        if (newMode == StreamlineManager::Mode::DLSS_RayReconstruction) {
            DxrRenderer::SetRrJitterScale(0.5f);
        }
        DxrRenderer::ResetStreamlineHistory();
        recreateDxrPipeline("Qt DLSS mode change");
    });
    connect(m_dlssQuality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DX12Context::g_streamline.SetQuality(StreamlineQualityFromIndex(index));
        DxrRenderer::ResetStreamlineHistory();
        recreateDxrPipeline("Qt DLSS quality change");
    });
    connect(m_rrJitterScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetRrJitterScale(static_cast<float>(value));
        DxrRenderer::ResetStreamlineHistory();
    });
    connect(m_resetDlssHistory, &QPushButton::clicked, this, []() {
        DxrRenderer::ResetStreamlineHistory();
    });

    connect(m_finalDenoiser, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetDenoiserMode(DenoiserModeFromIndex(index));
        DxrRenderer::ResetAccumulation();
        recreateDxrPipeline("Qt final denoiser mode change");
    });
    connect(m_oidnQuality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetOidnQuality(static_cast<OidnDenoiser::Quality>(index));
    });

    auto applyChange = [this]() {
        if (m_syncing) {
            return;
        }

        auto &rs = RasterRenderer::GetRenderSettings();
        rs.enableSSR = m_enableSsr->isChecked();
        rs.ssrStepSize = static_cast<float>(m_ssrStepSize->value());
        rs.ssrThickness = static_cast<float>(m_ssrThickness->value());
        rs.ssrIntensity = static_cast<float>(m_ssrIntensity->value());
        rs.ssrMinSmoothness = static_cast<float>(m_ssrMinSmoothness->value());
        rs.ssrMaxSteps = m_ssrMaxSteps->value();

        rs.enableSSAO = m_enableSsao->isChecked();
        rs.ssaoRadius = static_cast<float>(m_ssaoRadius->value());
        rs.ssaoBias = static_cast<float>(m_ssaoBias->value());
        rs.ssaoStrength = static_cast<float>(m_ssaoStrength->value());
        rs.ssaoSamples = m_ssaoSamples->value();
        rs.ssaoCompositeWeight = static_cast<float>(m_ssaoCompositeWeight->value());

        rs.enableBloom = m_enableBloom->isChecked();
        rs.bloomThreshold = static_cast<float>(m_bloomThreshold->value());
        rs.bloomIntensity = static_cast<float>(m_bloomIntensity->value());

        rs.tonemapVignette = static_cast<float>(m_tonemapVignette->value());
        rs.tonemapSaturation = static_cast<float>(m_tonemapSaturation->value());
        rs.tonemapContrast = static_cast<float>(m_tonemapContrast->value());
    };

    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        RasterRenderer::ResetRenderSettings();
        syncFromRenderer();
    });

    connect(m_enableSsr, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_ssrStepSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrThickness, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrIntensity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrMinSmoothness, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrMaxSteps, qOverload<int>(&QSpinBox::valueChanged), this, [applyChange](int) { applyChange(); });

    connect(m_enableSsao, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_ssaoRadius, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoBias, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoStrength, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoSamples, qOverload<int>(&QSpinBox::valueChanged), this, [applyChange](int) { applyChange(); });
    connect(m_ssaoCompositeWeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });

    connect(m_enableBloom, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_bloomThreshold, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_bloomIntensity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });

    connect(m_tonemapVignette, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_tonemapSaturation, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_tonemapContrast, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
}

void RenderSettingsPanel::syncFromRenderer()
{
    m_syncing = true;

    const bool dxrMode = (g_currentRenderMode == RenderMode::DXR);
    m_modeLabel->setText(tr("Active Mode: %1").arg(dxrMode ? tr("DXR") : tr("Raster")));
    m_switchModeButton->setText(dxrMode ? tr("Switch To Raster") : tr("Switch To DXR"));

    QString stats;
    if (dxrMode) {
        stats = tr("Samples: %1\nNoise: %2\nAvg Luminance: %3\nEV100: %4")
                    .arg(DxrRenderer::GetDisplayedSampleCount())
                    .arg(DxrRenderer::HasNoiseEstimate()
                             ? QString::number(DxrRenderer::GetCurrentNoiseLevel() * 100.0f, 'f', 2) + '%'
                             : tr("Calculating..."))
                    .arg(DxrRenderer::GetCurrentAvgLuminance(), 0, 'f', 2)
                    .arg(DxrRenderer::GetCurrentEV100(), 0, 'f', 2);
    } else {
        stats = tr("Avg Luminance: %1\nEV100: %2")
                    .arg(QString::number(RasterRenderer::GetCurrentAvgLuminance(), 'f', 2))
                    .arg(QString::number(RasterRenderer::GetCurrentEV100(), 'f', 2));
    }
    m_statsLabel->setText(stats);

    m_reflectionBounces->setValue(g_cameraData.maxSpecularBounces);
    m_refractionBounces->setValue(g_cameraData.maxRefractiveBounces);
    m_giBounces->setValue(g_cameraData.maxGIBounces);
    m_maxSpp->setValue(static_cast<int>(g_cameraData.maxSPP));
    m_adaptiveSampling->setChecked(g_cameraData.useAdaptiveSampling > 0.5f);
    m_targetNoise->setValue(g_cameraData.noiseThreshold * 100.0f);
    m_targetNoise->setEnabled(m_adaptiveSampling->isChecked());

    m_realtimeDenoiser->setCurrentIndex(
        RealtimeDenoiserIndexFromMode(DxrRenderer::GetRealtimeDenoiserMode()));

    m_dlssEnabled->setChecked(DX12Context::g_streamline.IsEnabled());
    m_dlssMode->setCurrentIndex(StreamlineModeIndex(DX12Context::g_streamline.GetMode()));
    m_dlssQuality->setCurrentIndex(StreamlineQualityIndex(DX12Context::g_streamline.GetQuality()));
    m_rrJitterScale->setValue(DxrRenderer::GetRrJitterScale());
    const auto rec = DX12Context::g_streamline.GetRecommendedRenderSize(
        DX12Context::g_windowWidth, DX12Context::g_windowHeight);
    m_renderSizeLabel->setText(
        tr("%1 x %2 -> %3 x %4")
            .arg(rec.renderWidth)
            .arg(rec.renderHeight)
            .arg(DX12Context::g_windowWidth)
            .arg(DX12Context::g_windowHeight));
    m_rrJitterScale->setEnabled(DX12Context::g_streamline.GetMode() ==
                                StreamlineManager::Mode::DLSS_RayReconstruction);

    m_finalDenoiser->setCurrentIndex(DenoiserIndexFromMode(DxrRenderer::GetDenoiserMode()));
    m_oidnQuality->setCurrentIndex(static_cast<int>(DxrRenderer::GetOidnQuality()));
    const bool oidnActive = DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_CPU ||
                            DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_GPU;
    m_oidnQuality->setEnabled(oidnActive);

    const auto &rs = RasterRenderer::GetRenderSettings();

    m_enableSsr->setChecked(rs.enableSSR);
    m_ssrStepSize->setValue(rs.ssrStepSize);
    m_ssrThickness->setValue(rs.ssrThickness);
    m_ssrIntensity->setValue(rs.ssrIntensity);
    m_ssrMinSmoothness->setValue(rs.ssrMinSmoothness);
    m_ssrMaxSteps->setValue(rs.ssrMaxSteps);

    m_enableSsao->setChecked(rs.enableSSAO);
    m_ssaoRadius->setValue(rs.ssaoRadius);
    m_ssaoBias->setValue(rs.ssaoBias);
    m_ssaoStrength->setValue(rs.ssaoStrength);
    m_ssaoSamples->setValue(rs.ssaoSamples);
    m_ssaoCompositeWeight->setValue(rs.ssaoCompositeWeight);

    m_enableBloom->setChecked(rs.enableBloom);
    m_bloomThreshold->setValue(rs.bloomThreshold);
    m_bloomIntensity->setValue(rs.bloomIntensity);

    m_tonemapVignette->setValue(rs.tonemapVignette);
    m_tonemapSaturation->setValue(rs.tonemapSaturation);
    m_tonemapContrast->setValue(rs.tonemapContrast);

    m_ssrStepSize->setEnabled(rs.enableSSR);
    m_ssrThickness->setEnabled(rs.enableSSR);
    m_ssrIntensity->setEnabled(rs.enableSSR);
    m_ssrMinSmoothness->setEnabled(rs.enableSSR);
    m_ssrMaxSteps->setEnabled(rs.enableSSR);

    m_ssaoRadius->setEnabled(rs.enableSSAO);
    m_ssaoBias->setEnabled(rs.enableSSAO);
    m_ssaoStrength->setEnabled(rs.enableSSAO);
    m_ssaoSamples->setEnabled(rs.enableSSAO);
    m_ssaoCompositeWeight->setEnabled(rs.enableSSAO);

    m_bloomThreshold->setEnabled(rs.enableBloom);
    m_bloomIntensity->setEnabled(rs.enableBloom);

    m_dxrSection->setVisible(dxrMode);
    m_rasterSection->setVisible(!dxrMode);

    m_syncing = false;
}

void RenderSettingsPanel::applyCameraSettings()
{
    if (m_syncing) {
        return;
    }

    g_cameraData.maxSpecularBounces = static_cast<float>(m_reflectionBounces->value());
    g_cameraData.maxRefractiveBounces = static_cast<float>(m_refractionBounces->value());
    g_cameraData.maxGIBounces = static_cast<float>(m_giBounces->value());
    g_cameraData.maxSPP = static_cast<float>(m_maxSpp->value());
    g_cameraData.useAdaptiveSampling = m_adaptiveSampling->isChecked() ? 1.0f : 0.0f;
    if (g_cameraData.useAdaptiveSampling > 0.5f && g_cameraData.noiseThreshold <= 0.0f) {
        g_cameraData.noiseThreshold = 0.05f;
    }
    g_cameraData.noiseThreshold = static_cast<float>(m_targetNoise->value() / 100.0);
    UpdateCameraCB();
}

void RenderSettingsPanel::recreateDxrPipeline(const char *context)
{
    if (!g_rayTracingSupported) {
        return;
    }

    try {
        DX12Context::WaitGPUIdle();
        DxrRenderer::CreateRayTracingPipeline(DX12Context::g_windowWidth,
                                              DX12Context::g_windowHeight);
    } catch (const std::exception &e) {
        fprintf(stderr, "Qt DXR pipeline recreate failed (%s): %s\n",
                context ? context : "unknown", e.what());
    } catch (...) {
        fprintf(stderr, "Qt DXR pipeline recreate failed (%s): unknown exception\n",
                context ? context : "unknown");
    }
}
