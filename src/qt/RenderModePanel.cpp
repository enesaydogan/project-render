#include "RenderModePanel.h"

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

bool IsWidgetBeingEdited(QWidget *widget)
{
    if (!widget) {
        return false;
    }

    QWidget *focus = QApplication::focusWidget();
    return widget->hasFocus() ||
           (focus && (focus == widget || widget->isAncestorOf(focus)));
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

RenderModePanel::RenderModePanel(QWidget *parent)
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

void RenderModePanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_modeLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    m_switchModeButton = new QPushButton(this);

    layout->addWidget(m_modeLabel);
    layout->addWidget(m_statsLabel);
    layout->addWidget(m_switchModeButton);

    auto *pathGroup = new QGroupBox(tr("DXR Path Tracing"), this);
    auto *pathForm = new QFormLayout(pathGroup);
    pathForm->setContentsMargins(8, 16, 8, 8);
    pathForm->setVerticalSpacing(6);
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
    layout->addWidget(pathGroup);

    auto *realtimeGroup = new QGroupBox(tr("Realtime Denoiser"), this);
    auto *realtimeForm = new QFormLayout(realtimeGroup);
    m_realtimeDenoiser = new QComboBox(realtimeGroup);
    m_realtimeDenoiser->addItems({tr("Off"), tr("SVGF"), tr("NRD (ReLAX)")});
    m_realtimeDenoiser->setEnabled(false);
    m_realtimeDenoiser->setToolTip(
        tr("Realtime denoisers are temporarily disabled in the Qt UI."));
    m_resetRealtimeHistory = new QPushButton(tr("Reset History"), realtimeGroup);
    realtimeForm->addRow(tr("Mode"), m_realtimeDenoiser);
    realtimeForm->addRow(m_resetRealtimeHistory);
    layout->addWidget(realtimeGroup);

    auto *dlssGroup = new QGroupBox(tr("Streamline / DLSS"), this);
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
    layout->addWidget(dlssGroup);

    auto *finalGroup = new QGroupBox(tr("Final / Export Denoiser"), this);
    auto *finalForm = new QFormLayout(finalGroup);
    m_finalDenoiser = new QComboBox(finalGroup);
    m_finalDenoiser->addItems({tr("Off"), tr("OIDN (CPU)"), tr("OIDN (GPU)")});
    m_oidnQuality = new QComboBox(finalGroup);
    m_oidnQuality->addItems({tr("Fast"), tr("Balanced"), tr("High")});
    finalForm->addRow(tr("Denoiser"), m_finalDenoiser);
    finalForm->addRow(tr("OIDN Quality"), m_oidnQuality);
    layout->addWidget(finalGroup);

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
}

void RenderModePanel::syncFromRenderer()
{
    m_syncing = true;

    const bool dxrMode = (g_currentRenderMode == RenderMode::DXR);
    m_modeLabel->setText(tr("Current Mode: %1").arg(dxrMode ? tr("DXR") : tr("Raster")));
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

    m_realtimeDenoiser->setCurrentIndex(
        RealtimeDenoiserIndexFromMode(DxrRenderer::GetRealtimeDenoiserMode()));
    m_realtimeDenoiser->setEnabled(false);

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

    m_syncing = false;
}

void RenderModePanel::applyCameraSettings()
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

void RenderModePanel::recreateDxrPipeline(const char *context)
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
