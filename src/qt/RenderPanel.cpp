#include "RenderPanel.h"

#include "SliderControl.h"

#include "../dxr_renderer.h"
#include "../editor_ui.h"
#include "../file_import.h"

#include <QComboBox>
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "../saved_views.h"

extern HWND g_hwnd;
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

int ComputeProgressPercent()
{
    if (!g_renderExportJob.active || g_renderExportJob.targetMaxSpp <= 0) {
        return 0;
    }

    const int currentSpp = static_cast<int>(DxrRenderer::GetDisplayedSampleCount());
    const int clamped = std::clamp(currentSpp, 0, g_renderExportJob.targetMaxSpp);
    return (clamped * 100) / (std::max)(1, g_renderExportJob.targetMaxSpp);
}

QString NoiseStatusText()
{
    if (!DxrRenderer::HasNoiseEstimate()) {
        return QObject::tr("Noise: Calculating...");
    }

    return QObject::tr("Noise: %1% / %2%")
        .arg(QString::number(DxrRenderer::GetCurrentNoiseLevel() * 100.0f, 'f', 2))
        .arg(QString::number(g_renderExportJob.targetNoiseThreshold * 100.0f, 'f', 2));
}

} // namespace

RenderPanel::RenderPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    syncFromRenderer();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        syncFromRenderer();
    });
    m_refreshTimer->start(200);
}

void RenderPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *heroGroup = new QGroupBox(tr("Render Export"), this);
    auto *heroLayout = new QVBoxLayout(heroGroup);
    m_summaryLabel = new QLabel(heroGroup);
    m_summaryLabel->setWordWrap(true);
    m_outputLabel = new QLabel(heroGroup);
    m_outputLabel->setWordWrap(true);
    m_progressBar = new QProgressBar(heroGroup);
    m_progressBar->setRange(0, 100);
    m_progressDetailLabel = new QLabel(heroGroup);
    m_progressDetailLabel->setWordWrap(true);
    m_renderButton = new QPushButton(tr("Render And Export PNG..."), heroGroup);
    m_renderButton->setMinimumHeight(36);
    m_cancelButton = new QPushButton(tr("Cancel Render"), heroGroup);
    heroLayout->addWidget(m_summaryLabel);
    heroLayout->addWidget(m_outputLabel);
    heroLayout->addWidget(m_progressBar);
    heroLayout->addWidget(m_progressDetailLabel);
    heroLayout->addWidget(m_renderButton);
    heroLayout->addWidget(m_cancelButton);
    layout->addWidget(heroGroup);

    auto *settingsGroup = new QGroupBox(tr("DXR Render Settings"), this);
    auto *settingsForm = new QFormLayout(settingsGroup);
    m_resolutionPreset = new QComboBox(settingsGroup);
    for (int i = 0; i < g_renderResolutionPresetCount; ++i) {
        m_resolutionPreset->addItem(QString::fromUtf8(g_renderResolutionPresets[i].label));
    }
    m_maxSpp = CreateSliderControl(16.0, 4096.0, 1.0, 0);
    m_noisePercent = CreateSliderControl(0.1, 30.0, 0.1, 2);
    m_denoiser = new QComboBox(settingsGroup);
    m_denoiser->addItems({tr("Off"), tr("OIDN (CPU)"), tr("OIDN (GPU)"), tr("OptiX")});
    m_batchSavedViews = new QCheckBox(tr("Batch Render Saved Views"), settingsGroup);
    m_batchBaseName = new QLineEdit(settingsGroup);
    settingsForm->addRow(tr("Render Resolution"), m_resolutionPreset);
    settingsForm->addRow(tr("Max SPP"), m_maxSpp);
    settingsForm->addRow(tr("Noise %"), m_noisePercent);
    settingsForm->addRow(tr("Denoiser"), m_denoiser);
    settingsForm->addRow(QString(), m_batchSavedViews);
    settingsForm->addRow(tr("Batch Base Name"), m_batchBaseName);
    layout->addWidget(settingsGroup);

    auto *statusGroup = new QGroupBox(tr("Status"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);
    m_statusLabel = new QLabel(statusGroup);
    m_statusLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusLabel);
    layout->addWidget(statusGroup);
    layout->addStretch(1);

    connect(m_resolutionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateExportSettings();
    });
    connect(m_maxSpp->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateExportSettings();
    });
    connect(m_noisePercent->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateExportSettings();
    });
    connect(m_denoiser, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateExportSettings();
    });
    connect(m_batchSavedViews, &QCheckBox::toggled, this, [this](bool) {
        updateExportSettings();
    });
    connect(m_batchBaseName, &QLineEdit::editingFinished, this, [this]() {
        updateExportSettings();
    });
    connect(m_renderButton, &QPushButton::clicked, this, [this]() {
        startRenderExport();
    });
    connect(m_cancelButton, &QPushButton::clicked, this, []() {
        if (g_renderAnimationExport.active) {
            CancelAnimationRenderExport();
        } else if (g_renderBatchExport.active) {
            CancelBatchRenderExport();
        } else if (g_renderExportJob.active) {
            g_renderExportStatus = "Render canceled.";
            RestoreRenderExportState();
        }
    });
}

void RenderPanel::syncFromRenderer()
{
    m_syncing = true;

    if (g_renderExportSettings.resolutionPreset < 0 ||
        g_renderExportSettings.resolutionPreset >= g_renderResolutionPresetCount) {
        g_renderExportSettings.resolutionPreset = 0;
    }

    if (!IsWidgetBeingEdited(m_resolutionPreset)) {
        m_resolutionPreset->setCurrentIndex(g_renderExportSettings.resolutionPreset);
    }
    if (!m_maxSpp->isInteracting()) {
        m_maxSpp->setValue(g_renderExportSettings.maxSpp);
    }
    if (!m_noisePercent->isInteracting()) {
        m_noisePercent->setValue(g_renderExportSettings.noisePercent);
    }
    if (!IsWidgetBeingEdited(m_denoiser)) {
        m_denoiser->setCurrentIndex(std::clamp(g_renderExportSettings.denoiserIndex, 0, 3));
    }
    if (!IsWidgetBeingEdited(m_batchSavedViews)) {
        m_batchSavedViews->setChecked(g_renderExportSettings.batchSavedViews);
    }
    if (!IsWidgetBeingEdited(m_batchBaseName)) {
        m_batchBaseName->setText(QString::fromUtf8(g_renderExportSettings.batchBaseName.c_str()));
    }

    const RenderResolutionPreset &preset =
        g_renderResolutionPresets[g_renderExportSettings.resolutionPreset];
    m_summaryLabel->setText(
        tr("Offline DXR render with export-specific resolution, SPP stop, noise stop, and final denoiser."));
    m_outputLabel->setText(
        tr("Current target: %1 x %2")
            .arg(preset.width)
            .arg(preset.height));

    const bool active = IsRenderExportActive();
    const bool batchEnabled = g_renderExportSettings.batchSavedViews;
    const int savedViewCount = static_cast<int>(SavedViews::GetViews().size());
    m_progressBar->setValue(active ? ComputeProgressPercent() : 0);
    m_progressBar->setVisible(active);
    m_cancelButton->setVisible(active);
    m_cancelButton->setEnabled(active);
    m_renderButton->setEnabled(g_rayTracingSupported && !active &&
                               (!batchEnabled || savedViewCount > 0));
    m_resolutionPreset->setEnabled(!active);
    m_maxSpp->setEnabled(!active);
    m_noisePercent->setEnabled(!active);
    m_denoiser->setEnabled(!active);
    m_batchSavedViews->setEnabled(!active);
    m_batchBaseName->setEnabled(!active && batchEnabled);
    m_renderButton->setText(batchEnabled ? tr("Render Saved Views...")
                                         : tr("Render And Export PNG..."));

    if (g_renderExportJob.active) {
        const bool denoiserEnabled = (g_renderExportJob.targetDenoiserIndex != 0);
        QStringList lines;
        lines << tr("Progress: %1 / %2 SPP")
                     .arg(DxrRenderer::GetDisplayedSampleCount())
                     .arg(g_renderExportJob.targetMaxSpp);
        if (g_renderExportJob.tileState.enabled) {
            const RenderExportTileState &tile = g_renderExportJob.tileState;
            lines << tr("Output: %1 x %2")
                         .arg(tile.fullWidth)
                         .arg(tile.fullHeight);
            lines << tr("Tile: %1 / %2 (%3 x %4)")
                         .arg(tile.currentTileIndex + 1)
                         .arg(tile.tileCountX * tile.tileCountY)
                         .arg(tile.tileWidth)
                         .arg(tile.tileHeight);
        } else {
            lines << tr("Output: %1 x %2")
                         .arg(g_renderExportJob.targetWidth)
                         .arg(g_renderExportJob.targetHeight);
        }
        lines << NoiseStatusText();
        lines << tr("Min SPP before noise-stop: %1")
                     .arg(g_renderExportJob.minSppBeforeNoiseStop);
        if (denoiserEnabled) {
            lines << tr("Denoiser output: %1")
                         .arg(DxrRenderer::HasDenoisedOutput() ? tr("Ready") : tr("Waiting"));
        }
        if (g_renderExportJob.completionArmed) {
            lines << tr("Finalizing... (%1)")
                         .arg(g_renderExportJob.settleFramesRemaining);
        }
        if (g_renderBatchExport.active) {
            lines << tr("Batch view: %1")
                         .arg(QString::fromUtf8(g_renderBatchExport.currentViewName.c_str()));
        }
        m_progressDetailLabel->setText(lines.join('\n'));
    } else {
        if (batchEnabled) {
            m_progressDetailLabel->setText(
                tr("Ready to batch render %1 saved views as %2-viewname.png")
                    .arg(savedViewCount)
                    .arg(QString::fromUtf8(g_renderExportSettings.batchBaseName.c_str())));
        } else {
            m_progressDetailLabel->setText(tr("Ready to start a PNG render export."));
        }
    }

    QString statusText;
    if (!g_rayTracingSupported) {
        statusText = tr("DXR is not supported on this device.");
    } else if (g_renderAnimationExport.active) {
        const QString progressText = QString::fromUtf8(GetAnimationExportProgressText().c_str());
        statusText = progressText;
        if (g_renderAnimationExport.encoding && !g_renderExportStatus.empty()) {
            const QString detailText = QString::fromUtf8(g_renderExportStatus.c_str());
            if (detailText != progressText) {
                statusText += QLatin1Char('\n');
                statusText += detailText;
            }
        }
    } else if (!g_renderExportStatus.empty()) {
        statusText = QString::fromUtf8(g_renderExportStatus.c_str());
    } else {
        statusText = tr("The render button uses the same export path as the ImGui popup.");
    }
    m_statusLabel->setText(statusText);

    m_syncing = false;
}

void RenderPanel::updateExportSettings()
{
    if (m_syncing) {
        return;
    }

    g_renderExportSettings.resolutionPreset = m_resolutionPreset->currentIndex();
    g_renderExportSettings.maxSpp = static_cast<int>(m_maxSpp->value());
    g_renderExportSettings.noisePercent = static_cast<float>(m_noisePercent->value());
    g_renderExportSettings.denoiserIndex = m_denoiser->currentIndex();
    g_renderExportSettings.batchSavedViews = m_batchSavedViews->isChecked();
    g_renderExportSettings.batchBaseName = m_batchBaseName->text().trimmed().toUtf8().constData();
    if (g_renderExportSettings.batchBaseName.empty()) {
        g_renderExportSettings.batchBaseName = "final";
    }
    syncFromRenderer();
}

void RenderPanel::startRenderExport()
{
    if (!g_rayTracingSupported || IsRenderExportActive()) {
        return;
    }

    if (g_renderExportSettings.batchSavedViews) {
        const QString outputDir = QFileDialog::getExistingDirectory(
            this,
            tr("Choose Output Folder For Saved Views"),
            QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!outputDir.isEmpty()) {
            StartBatchRenderExportJobs(outputDir.toStdWString(),
                                       QString::fromUtf8(g_renderExportSettings.batchBaseName.c_str()).toStdWString());
            syncFromRenderer();
        }
    } else {
        std::wstring chosenPath;
        if (SaveRenderImageFileDialog(g_hwnd, chosenPath)) {
            StartRenderExportJob(chosenPath);
            syncFromRenderer();
        }
    }
}
