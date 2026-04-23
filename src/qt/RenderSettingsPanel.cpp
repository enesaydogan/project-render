#include "RenderSettingsPanel.h"

#include "SliderControl.h"

#include "../camera.h"
#include "../dx12_context.h"
#include "../dxr_renderer.h"
#include "../raster_renderer.h"
#include "../scene.h"
#include "../streamline_manager.h"

#include <QApplication>
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

SliderControl *CreateSliderControl(double minValue,
                                   double maxValue,
                                   double step,
                                   int decimals)
{
    return new SliderControl(minValue, maxValue, step, decimals);
}

bool IsWidgetBeingEdited(QWidget *widget)
{
    if (!widget) {
        return false;
    }

    QWidget *focus = QApplication::focusWidget();
    return widget->hasFocus() ||
           (focus && (focus == widget || widget->isAncestorOf(focus)));
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
    dxrLayout->setContentsMargins(8, 8, 8, 8);
    dxrLayout->setSpacing(8);
    auto *dxrHeader = new QLabel(tr("DXR Settings"), m_dxrSection);
    dxrHeader->setStyleSheet("font-weight: 600;");
    dxrLayout->addWidget(dxrHeader);

    auto *pathGroup = new QGroupBox(tr("DXR Path Tracing"), m_dxrSection);
    auto *pathForm = new QFormLayout(pathGroup);
    pathForm->setContentsMargins(8, 16, 8, 8);
    pathForm->setVerticalSpacing(6);
    m_reflectionBounces = CreateSliderControl(0.0, 16.0, 1.0, 0);
    m_refractionBounces = CreateSliderControl(0.0, 16.0, 1.0, 0);
    m_giBounces = CreateSliderControl(0.0, 16.0, 1.0, 0);
    m_maxSpp = CreateSliderControl(10.0, 1000.0, 1.0, 0);
    m_adaptiveSampling = new QCheckBox(tr("Enable Adaptive Sampling"), pathGroup);
    m_targetNoise = CreateSliderControl(1.0, 30.0, 0.1, 1);
    pathForm->addRow(tr("Reflection Bounces"), m_reflectionBounces);
    pathForm->addRow(tr("Refraction Bounces"), m_refractionBounces);
    pathForm->addRow(tr("GI Bounces"), m_giBounces);
    pathForm->addRow(tr("Max SPP"), m_maxSpp);
    pathForm->addRow(m_adaptiveSampling);
    pathForm->addRow(tr("Target Noise %"), m_targetNoise);
    dxrLayout->addWidget(pathGroup);

    auto *dlssGroup = new QGroupBox(tr("Streamline / DLSS"), m_dxrSection);
    auto *dlssForm = new QFormLayout(dlssGroup);
    m_dlssEnabled = new QCheckBox(tr("Enable"), dlssGroup);
    m_dlssMode = new QComboBox(dlssGroup);
    m_dlssMode->addItems({tr("Off"), tr("DLSS Super Resolution"), tr("DLSS Ray Reconstruction")});
    m_dlssQuality = new QComboBox(dlssGroup);
    m_dlssQuality->addItems({tr("Max Performance"), tr("Balanced"), tr("Max Quality"), tr("Ultra Performance"), tr("DLAA")});
    m_rrJitterScale = CreateSliderControl(0.0, 1.0, 0.01, 2);
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
    rasterLayout->setContentsMargins(8, 8, 8, 8);
    rasterLayout->setSpacing(8);
    auto *rasterHeader = new QLabel(tr("Raster Settings"), m_rasterSection);
    rasterHeader->setStyleSheet("font-weight: 600;");
    rasterLayout->addWidget(rasterHeader);

    m_resetButton = new QPushButton(tr("Reset Raster Settings"), m_rasterSection);
    rasterLayout->addWidget(m_resetButton);

    auto *ssrGroup = new QGroupBox(tr("Reflections (SSR)"), m_rasterSection);
    auto *ssrForm = new QFormLayout(ssrGroup);
    m_enableSsr = new QCheckBox(tr("Enable SSR"), ssrGroup);
    m_ssrStepSize = CreateSliderControl(0.02, 2.0, 0.01, 3);
    m_ssrThickness = CreateSliderControl(0.001, 1.0, 0.001, 3);
    m_ssrIntensity = CreateSliderControl(0.0, 2.0, 0.05, 2);
    m_ssrMinSmoothness = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_ssrMaxSteps = CreateSliderControl(1.0, 256.0, 1.0, 0);
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
    m_ssaoRadius = CreateSliderControl(0.01, 5.0, 0.01, 3);
    m_ssaoBias = CreateSliderControl(0.0001, 0.25, 0.0005, 4);
    m_ssaoStrength = CreateSliderControl(0.0, 4.0, 0.05, 2);
    m_ssaoSamples = CreateSliderControl(1.0, 32.0, 1.0, 0);
    m_ssaoCompositeWeight = CreateSliderControl(0.0, 1.0, 0.01, 2);
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
    m_bloomThreshold = CreateSliderControl(0.0, 10.0, 0.05, 2);
    m_bloomIntensity = CreateSliderControl(0.0, 4.0, 0.05, 2);
    bloomForm->addRow(m_enableBloom);
    bloomForm->addRow(tr("Threshold"), m_bloomThreshold);
    bloomForm->addRow(tr("Intensity"), m_bloomIntensity);
    rasterLayout->addWidget(bloomGroup);

    layout->addWidget(m_rasterSection);
    layout->addStretch(1);

    connect(m_switchModeButton, &QPushButton::clicked, this, [this]() {
        if (g_currentRenderMode == RenderMode::Raster) {
            g_currentRenderMode = RenderMode::DXR;
            Scene::RequestRendererFullRebuild();
            recreateDxrPipeline("Qt render mode switch");
        } else {
            g_currentRenderMode = RenderMode::Raster;
        }
        syncFromRenderer();
    });

    auto applyCamera = [this]() { applyCameraSettings(); };
    connect(m_reflectionBounces->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_refractionBounces->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_giBounces->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_maxSpp->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });
    connect(m_adaptiveSampling, &QCheckBox::toggled, this, [applyCamera](bool) { applyCamera(); });
    connect(m_targetNoise->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCamera](double) { applyCamera(); });

    connect(m_dlssEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        DX12Context::g_streamline.SetEnabled(enabled);
        DxrRenderer::ResetAccumulation();
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
        DxrRenderer::ResetAccumulation();
        recreateDxrPipeline("Qt DLSS mode change");
    });
    connect(m_dlssQuality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DX12Context::g_streamline.SetQuality(StreamlineQualityFromIndex(index));
        DxrRenderer::ResetAccumulation();
        recreateDxrPipeline("Qt DLSS quality change");
    });
    connect(m_rrJitterScale->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
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
        rs.ssrMaxSteps = static_cast<int>(m_ssrMaxSteps->value());

        rs.enableSSAO = m_enableSsao->isChecked();
        rs.ssaoRadius = static_cast<float>(m_ssaoRadius->value());
        rs.ssaoBias = static_cast<float>(m_ssaoBias->value());
        rs.ssaoStrength = static_cast<float>(m_ssaoStrength->value());
        rs.ssaoSamples = static_cast<int>(m_ssaoSamples->value());
        rs.ssaoCompositeWeight = static_cast<float>(m_ssaoCompositeWeight->value());

        rs.enableBloom = m_enableBloom->isChecked();
        rs.bloomThreshold = static_cast<float>(m_bloomThreshold->value());
        rs.bloomIntensity = static_cast<float>(m_bloomIntensity->value());
    };

    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        RasterRenderer::ResetRenderSettings();
        syncFromRenderer();
    });

    connect(m_enableSsr, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_ssrStepSize->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrThickness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrMinSmoothness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssrMaxSteps->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });

    connect(m_enableSsao, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_ssaoRadius->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoBias->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoStrength->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoSamples->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_ssaoCompositeWeight->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });

    connect(m_enableBloom, &QCheckBox::toggled, this, [applyChange](bool) { applyChange(); });
    connect(m_bloomThreshold->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });
    connect(m_bloomIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyChange](double) { applyChange(); });

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

    if (!IsWidgetBeingEdited(m_reflectionBounces)) {
        m_reflectionBounces->setValue(g_cameraData.maxSpecularBounces);
    }
    if (!IsWidgetBeingEdited(m_refractionBounces)) {
        m_refractionBounces->setValue(g_cameraData.maxRefractiveBounces);
    }
    if (!IsWidgetBeingEdited(m_giBounces)) {
        m_giBounces->setValue(g_cameraData.maxGIBounces);
    }
    if (!IsWidgetBeingEdited(m_maxSpp)) {
        m_maxSpp->setValue(static_cast<int>(g_cameraData.maxSPP));
    }
    if (!IsWidgetBeingEdited(m_adaptiveSampling)) {
        m_adaptiveSampling->setChecked(g_cameraData.useAdaptiveSampling > 0.5f);
    }
    if (!IsWidgetBeingEdited(m_targetNoise)) {
        m_targetNoise->setValue(g_cameraData.noiseThreshold * 100.0f);
    }
    m_targetNoise->setEnabled(m_adaptiveSampling->isChecked());

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
    DxrRenderer::ResetAccumulation();
}

void RenderSettingsPanel::recreateDxrPipeline(const char *context)
{
    if (!g_rayTracingSupported) {
        return;
    }

    try {
        DxrRenderer::WaitForAsyncRestirIdle();
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
