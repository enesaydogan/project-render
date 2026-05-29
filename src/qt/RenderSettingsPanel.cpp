#include "RenderSettingsPanel.h"

#include "SliderControl.h"

#include "../camera.h"
#include "../assets/asset_loader.h"
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

#include <algorithm>

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
    case DxrRenderer::DenoiserMode::OptiX: return 3;
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
    if (index == 3) {
        return DxrRenderer::DenoiserMode::OptiX;
    }
    return DxrRenderer::DenoiserMode::Off;
}

int PathBackendIndex(DxrRenderer::PathTracingBackend backend)
{
    switch (backend) {
    case DxrRenderer::PathTracingBackend::WavefrontParity: return 1;
    case DxrRenderer::PathTracingBackend::WavefrontOptimized: return 0;
    default: return 0;
    }
}

DxrRenderer::PathTracingBackend PathBackendFromIndex(int index)
{
    if (index == 1) {
        return DxrRenderer::PathTracingBackend::WavefrontParity;
    }
    return DxrRenderer::PathTracingBackend::WavefrontOptimized;
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
    m_clayMode = new QCheckBox(tr("Clay Material Override"), pathGroup);
    m_clayPreserveTransparency =
        new QCheckBox(tr("Clay: Preserve Transparency"), pathGroup);
    m_clayPreserveEmission =
        new QCheckBox(tr("Clay: Preserve Emission"), pathGroup);
    m_pathBackend = new QComboBox(pathGroup);
    m_pathBackend->addItems({
        tr("Wavefront Optimized"),
        tr("Wavefront Surface Diagnostics")
    });
    m_textureCompression = new QComboBox(pathGroup);
    m_textureCompression->addItems({
        tr("Off"),
        tr("Balanced BC"),
        tr("High Quality BC")
    });
    m_pathBackendWarning = new QLabel(pathGroup);
    m_pathBackendWarning->setWordWrap(true);
    m_pathBackendWarning->setStyleSheet(
        "QLabel { color: #ff4d4d; font-weight: 600; }");
    pathForm->addRow(tr("Reflection Bounces"), m_reflectionBounces);
    pathForm->addRow(tr("Refraction Bounces"), m_refractionBounces);
    pathForm->addRow(tr("GI Bounces"), m_giBounces);
    pathForm->addRow(tr("Max SPP"), m_maxSpp);
    pathForm->addRow(m_adaptiveSampling);
    pathForm->addRow(tr("Target Noise %"), m_targetNoise);
    pathForm->addRow(m_clayMode);
    pathForm->addRow(m_clayPreserveTransparency);
    pathForm->addRow(m_clayPreserveEmission);
    pathForm->addRow(tr("Backend"), m_pathBackend);
    pathForm->addRow(tr("Texture Compression"), m_textureCompression);
    pathForm->addRow(m_pathBackendWarning);
    dxrLayout->addWidget(pathGroup);

    auto *dlssGroup = new QGroupBox(tr("Streamline / DLSS"), m_dxrSection);
    auto *dlssForm = new QFormLayout(dlssGroup);
    m_dlssMode = new QComboBox(dlssGroup);
    m_dlssMode->addItems({tr("Off"), tr("DLSS Super Resolution"), tr("DLSS Ray Reconstruction")});
    m_dlssQuality = new QComboBox(dlssGroup);
    m_dlssQuality->addItems({tr("Max Performance"), tr("Balanced"), tr("Max Quality"), tr("Ultra Performance"), tr("DLAA")});
    m_rrJitterScale = CreateSliderControl(0.0, 1.0, 0.01, 2);
    m_resetDlssHistory = new QPushButton(tr("Reset DLSS History"), dlssGroup);
    m_renderSizeLabel = new QLabel(dlssGroup);
    m_renderSizeLabel->setWordWrap(true);
    m_dlssSpecProbe = new QCheckBox(tr("RR Specular Probe"), dlssGroup);
    m_dlssSpecProbe->setToolTip(
        tr("Trace a mirror-direction RayQuery on glossy primary surfaces "
           "to give DLSS-RR accurate specular-hit-distance and "
           "specular-motion-vector guidance. Disable if you see boiling "
           "on reflective surfaces."));
    m_drrEnabled = new QCheckBox(tr("Dynamic Resolution (DRR)"), dlssGroup);
    m_drrEnabled->setToolTip(
        tr("Vary the internal DLSS-RR render resolution per frame to hold "
           "the target frame time. DLSS-RR handles in-range size changes "
           "without temporal history reset."));
    m_drrTargetFps = CreateSliderControl(30.0, 240.0, 1.0, 0);
    m_drrStatusLabel = new QLabel(dlssGroup);
    m_drrStatusLabel->setWordWrap(true);
    dlssForm->addRow(tr("Mode"), m_dlssMode);
    dlssForm->addRow(tr("Quality"), m_dlssQuality);
    dlssForm->addRow(tr("RR Jitter Scale"), m_rrJitterScale);
    dlssForm->addRow(m_dlssSpecProbe);
    dlssForm->addRow(m_resetDlssHistory);
    dlssForm->addRow(tr("Render Resolution"), m_renderSizeLabel);
    dlssForm->addRow(m_drrEnabled);
    dlssForm->addRow(tr("DRR Target FPS"), m_drrTargetFps);
    dlssForm->addRow(tr("DRR Status"), m_drrStatusLabel);
    dxrLayout->addWidget(dlssGroup);

    auto *finalGroup = new QGroupBox(tr("Final / Export Denoiser"), m_dxrSection);
    auto *finalForm = new QFormLayout(finalGroup);
    m_finalDenoiser = new QComboBox(finalGroup);
    m_finalDenoiser->addItems({tr("Off"), tr("OIDN (CPU)"), tr("OIDN (GPU)"), tr("OptiX")});
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
    connect(m_clayMode, &QCheckBox::toggled, this, [applyCamera](bool) { applyCamera(); });
    connect(m_clayPreserveTransparency, &QCheckBox::toggled, this, [applyCamera](bool) { applyCamera(); });
    connect(m_clayPreserveEmission, &QCheckBox::toggled, this, [applyCamera](bool) { applyCamera(); });
    connect(m_pathBackend, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetPathTracingBackend(PathBackendFromIndex(index));
        syncFromRenderer();
    });
    connect(m_textureCompression, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        const int clamped = std::clamp(index, 0, 2);
        Asset::SetTextureCompressionMode(
            static_cast<Asset::TextureCompressionMode>(clamped));
        Scene::RefreshTextureCompression(true);
        syncFromRenderer();
    });

    connect(m_dlssMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        const auto newMode = StreamlineModeFromIndex(index);
        DX12Context::g_streamline.SetEnabled(newMode != StreamlineManager::Mode::Off);
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
    connect(m_dlssSpecProbe, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetDlssSpecularProbeEnabled(checked);
    });
    connect(m_drrEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_syncing) {
            return;
        }
        DxrRenderer::SetDrrEnabled(checked);
    });
    connect(m_drrTargetFps->spinBox(),
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
                if (m_syncing) {
                    return;
                }
                if (value > 1.0) {
                    DxrRenderer::SetDrrTargetFrameTimeMs(
                        1000.0f / static_cast<float>(value));
                }
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
    if (DxrRenderer::HasPipelineRecreateRequest()) {
        if (!stats.isEmpty()) {
            stats += QLatin1Char('\n');
        }
        stats += tr("Pipeline change queued...");
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
    if (!IsWidgetBeingEdited(m_clayMode)) {
        m_clayMode->setChecked(g_cameraData.debugVisualizationMode > 1.5f &&
                               g_cameraData.debugVisualizationMode < 2.5f);
    }
    const uint32_t clayFeatureFlags =
        static_cast<uint32_t>(g_cameraData.dxrFeatureFlags);
    if (!IsWidgetBeingEdited(m_clayPreserveTransparency)) {
        m_clayPreserveTransparency->setChecked(
            (clayFeatureFlags & kDxrFeatureClayPreserveTransparency) != 0);
    }
    if (!IsWidgetBeingEdited(m_clayPreserveEmission)) {
        m_clayPreserveEmission->setChecked(
            (clayFeatureFlags & kDxrFeatureClayPreserveEmission) != 0);
    }
    if (!IsWidgetBeingEdited(m_pathBackend)) {
        m_pathBackend->setCurrentIndex(PathBackendIndex(DxrRenderer::GetPathTracingBackend()));
    }
    if (!IsWidgetBeingEdited(m_textureCompression)) {
        m_textureCompression->setCurrentIndex(
            static_cast<int>(Asset::GetTextureCompressionMode()));
    }
    if (DxrRenderer::GetPathTracingBackend() == DxrRenderer::PathTracingBackend::WavefrontParity) {
        m_pathBackendWarning->setText(
            tr("Wavefront diagnostics mode only resolves primary surfaces and AOVs."));
    } else {
        m_pathBackendWarning->setText(
            tr("Wavefront optimized is the production path."));
    }
    m_targetNoise->setEnabled(m_adaptiveSampling->isChecked());
    const bool clayMode = m_clayMode->isChecked();
    m_clayPreserveTransparency->setEnabled(clayMode);
    m_clayPreserveEmission->setEnabled(clayMode);

    const auto streamlineMode = DX12Context::g_streamline.IsEnabled()
                                    ? DX12Context::g_streamline.GetMode()
                                    : StreamlineManager::Mode::Off;
    if (!IsWidgetBeingEdited(m_dlssMode)) {
        m_dlssMode->setCurrentIndex(StreamlineModeIndex(streamlineMode));
    }
    if (!IsWidgetBeingEdited(m_dlssQuality)) {
        m_dlssQuality->setCurrentIndex(StreamlineQualityIndex(DX12Context::g_streamline.GetQuality()));
    }
    if (!m_rrJitterScale->isInteracting()) {
        m_rrJitterScale->setValue(DxrRenderer::GetRrJitterScale());
    }
    const auto rec = DX12Context::g_streamline.GetRecommendedRenderSize(
        DX12Context::g_windowWidth, DX12Context::g_windowHeight);
    m_renderSizeLabel->setText(
        tr("%1 x %2 -> %3 x %4")
            .arg(rec.renderWidth)
            .arg(rec.renderHeight)
            .arg(DX12Context::g_windowWidth)
            .arg(DX12Context::g_windowHeight));
    m_dlssQuality->setEnabled(streamlineMode != StreamlineManager::Mode::Off);
    m_rrJitterScale->setEnabled(streamlineMode ==
                                StreamlineManager::Mode::DLSS_RayReconstruction);
    m_resetDlssHistory->setEnabled(streamlineMode != StreamlineManager::Mode::Off);

    const bool rrActive =
        streamlineMode == StreamlineManager::Mode::DLSS_RayReconstruction;
    if (!IsWidgetBeingEdited(m_dlssSpecProbe)) {
        m_dlssSpecProbe->setChecked(DxrRenderer::GetDlssSpecularProbeEnabled());
    }
    m_dlssSpecProbe->setEnabled(rrActive);

    if (!IsWidgetBeingEdited(m_drrEnabled)) {
        m_drrEnabled->setChecked(DxrRenderer::GetDrrEnabled());
    }
    m_drrEnabled->setEnabled(rrActive);

    if (!m_drrTargetFps->isInteracting()) {
        const float ms = DxrRenderer::GetDrrTargetFrameTimeMs();
        const double fps = (ms > 0.0f) ? (1000.0 / static_cast<double>(ms))
                                       : 60.0;
        m_drrTargetFps->setValue(fps);
    }
    m_drrTargetFps->setEnabled(rrActive && DxrRenderer::GetDrrEnabled());

    if (rrActive && DxrRenderer::GetDrrEnabled()) {
        uint32_t curW = 0, curH = 0;
        DxrRenderer::GetDrrCurrentRenderSize(curW, curH);
        const auto range = DxrRenderer::GetDrrRange();
        m_drrStatusLabel->setText(
            tr("Current %1 x %2  |  Range [%3 x %4 .. %5 x %6]")
                .arg(curW)
                .arg(curH)
                .arg(range.minRenderWidth)
                .arg(range.minRenderHeight)
                .arg(range.maxRenderWidth)
                .arg(range.maxRenderHeight));
    } else {
        m_drrStatusLabel->setText(
            rrActive ? tr("(disabled — render size locked to optimal)")
                     : tr("(requires DLSS Ray Reconstruction)"));
    }

    if (!IsWidgetBeingEdited(m_finalDenoiser)) {
        m_finalDenoiser->setCurrentIndex(DenoiserIndexFromMode(DxrRenderer::GetDenoiserMode()));
    }
    if (!IsWidgetBeingEdited(m_oidnQuality)) {
        m_oidnQuality->setCurrentIndex(static_cast<int>(DxrRenderer::GetOidnQuality()));
    }
    const bool oidnActive = DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_CPU ||
                            DxrRenderer::GetDenoiserMode() == DxrRenderer::DenoiserMode::OIDN_GPU;
    m_oidnQuality->setEnabled(oidnActive);

    const auto &rs = RasterRenderer::GetRenderSettings();

    if (!IsWidgetBeingEdited(m_enableSsr)) {
        m_enableSsr->setChecked(rs.enableSSR);
    }
    if (!m_ssrStepSize->isInteracting()) {
        m_ssrStepSize->setValue(rs.ssrStepSize);
    }
    if (!m_ssrThickness->isInteracting()) {
        m_ssrThickness->setValue(rs.ssrThickness);
    }
    if (!m_ssrIntensity->isInteracting()) {
        m_ssrIntensity->setValue(rs.ssrIntensity);
    }
    if (!m_ssrMinSmoothness->isInteracting()) {
        m_ssrMinSmoothness->setValue(rs.ssrMinSmoothness);
    }
    if (!m_ssrMaxSteps->isInteracting()) {
        m_ssrMaxSteps->setValue(rs.ssrMaxSteps);
    }

    if (!IsWidgetBeingEdited(m_enableSsao)) {
        m_enableSsao->setChecked(rs.enableSSAO);
    }
    if (!m_ssaoRadius->isInteracting()) {
        m_ssaoRadius->setValue(rs.ssaoRadius);
    }
    if (!m_ssaoBias->isInteracting()) {
        m_ssaoBias->setValue(rs.ssaoBias);
    }
    if (!m_ssaoStrength->isInteracting()) {
        m_ssaoStrength->setValue(rs.ssaoStrength);
    }
    if (!m_ssaoSamples->isInteracting()) {
        m_ssaoSamples->setValue(rs.ssaoSamples);
    }
    if (!m_ssaoCompositeWeight->isInteracting()) {
        m_ssaoCompositeWeight->setValue(rs.ssaoCompositeWeight);
    }

    if (!IsWidgetBeingEdited(m_enableBloom)) {
        m_enableBloom->setChecked(rs.enableBloom);
    }
    if (!m_bloomThreshold->isInteracting()) {
        m_bloomThreshold->setValue(rs.bloomThreshold);
    }
    if (!m_bloomIntensity->isInteracting()) {
        m_bloomIntensity->setValue(rs.bloomIntensity);
    }

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
    if (m_clayMode->isChecked()) {
        g_cameraData.debugVisualizationMode = 2.0f;
    } else if (g_cameraData.debugVisualizationMode > 1.5f &&
               g_cameraData.debugVisualizationMode < 2.5f) {
        g_cameraData.debugVisualizationMode = 0.0f;
    }
    uint32_t clayFeatureFlags =
        static_cast<uint32_t>(g_cameraData.dxrFeatureFlags);
    clayFeatureFlags &= ~(kDxrFeatureClayPreserveTransparency |
                          kDxrFeatureClayPreserveEmission);
    if (m_clayPreserveTransparency->isChecked()) {
        clayFeatureFlags |= kDxrFeatureClayPreserveTransparency;
    }
    if (m_clayPreserveEmission->isChecked()) {
        clayFeatureFlags |= kDxrFeatureClayPreserveEmission;
    }
    g_cameraData.dxrFeatureFlags = static_cast<float>(clayFeatureFlags);
    UpdateCameraCB();
    DxrRenderer::ResetAccumulation();
}

void RenderSettingsPanel::recreateDxrPipeline(const char *context)
{
    if (!g_rayTracingSupported) {
        return;
    }
    DxrRenderer::RequestPipelineRecreate(context);
    syncFromRenderer();
}
