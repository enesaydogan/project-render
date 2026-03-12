#include "RenderSettingsPanel.h"

#include "../raster_renderer.h"
#include "../scene.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

extern RenderMode g_currentRenderMode;

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

} // namespace

RenderSettingsPanel::RenderSettingsPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    syncFromRenderer();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        updateModeNotice();
        syncFromRenderer();
    });
    m_refreshTimer->start(250);
}

void RenderSettingsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_modeNotice = new QLabel(this);
    m_modeNotice->setWordWrap(true);
    layout->addWidget(m_modeNotice);

    m_resetButton = new QPushButton(tr("Reset Raster Settings"), this);
    layout->addWidget(m_resetButton);

    auto *ssrGroup = new QGroupBox(tr("Reflections (SSR)"), this);
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
    layout->addWidget(ssrGroup);

    auto *ssaoGroup = new QGroupBox(tr("Ambient Occlusion (SSAO)"), this);
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
    layout->addWidget(ssaoGroup);

    auto *bloomGroup = new QGroupBox(tr("Bloom"), this);
    auto *bloomForm = new QFormLayout(bloomGroup);
    m_enableBloom = new QCheckBox(tr("Enable Bloom"), bloomGroup);
    m_bloomThreshold = CreateDoubleSpinBox(0.0, 10.0, 0.05, 2);
    m_bloomIntensity = CreateDoubleSpinBox(0.0, 4.0, 0.05, 2);
    bloomForm->addRow(m_enableBloom);
    bloomForm->addRow(tr("Threshold"), m_bloomThreshold);
    bloomForm->addRow(tr("Intensity"), m_bloomIntensity);
    layout->addWidget(bloomGroup);

    auto *tonemapGroup = new QGroupBox(tr("Tonemap"), this);
    auto *tonemapForm = new QFormLayout(tonemapGroup);
    m_tonemapVignette = CreateDoubleSpinBox(0.0, 1.0, 0.01, 2);
    m_tonemapSaturation = CreateDoubleSpinBox(0.0, 2.0, 0.01, 2);
    m_tonemapContrast = CreateDoubleSpinBox(0.0, 2.0, 0.01, 2);
    tonemapForm->addRow(tr("Vignette"), m_tonemapVignette);
    tonemapForm->addRow(tr("Saturation"), m_tonemapSaturation);
    tonemapForm->addRow(tr("Contrast"), m_tonemapContrast);
    layout->addWidget(tonemapGroup);

    layout->addStretch(1);

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

    updateModeNotice();
    m_syncing = false;
}

void RenderSettingsPanel::updateModeNotice()
{
    if (g_currentRenderMode != RenderMode::Raster) {
        m_modeNotice->setText(tr("These controls affect the raster renderer and will apply the next time Raster mode is active."));
        m_modeNotice->show();
    } else {
        m_modeNotice->hide();
    }
}