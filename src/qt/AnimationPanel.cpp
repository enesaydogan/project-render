#include "AnimationPanel.h"

#include "../animation_sequence.h"
#include "../editor_ui.h"
#include "../saved_views.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kKeyframeIndexRole = Qt::UserRole;
constexpr int kScrubResolution = 1000;

QString KeyframeSummary(const AnimationSequence::Keyframe &keyframe, size_t index, bool isLast)
{
    if (isLast) {
        return QObject::tr("%1. %2 | In %3 | Final")
            .arg(static_cast<int>(index + 1))
            .arg(QString::fromUtf8(keyframe.label.c_str()))
            .arg(QString::fromUtf8(AnimationSequence::GetEasingModeLabel(keyframe.easeIn)));
    }

    return QObject::tr("%1. %2 | %3 s | In %4 | Out %5")
        .arg(static_cast<int>(index + 1))
        .arg(QString::fromUtf8(keyframe.label.c_str()))
        .arg(QString::number(keyframe.durationToNextSeconds, 'f', 2))
        .arg(QString::fromUtf8(AnimationSequence::GetEasingModeLabel(keyframe.easeIn)))
        .arg(QString::fromUtf8(AnimationSequence::GetEasingModeLabel(keyframe.easeOut)));
}

} // namespace

AnimationPanel::AnimationPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    syncFromAnimation();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        syncFromAnimation();
    });
    m_refreshTimer->start(250);

    m_playbackTimer = new QTimer(this);
    m_playbackElapsed = new QElapsedTimer();
    connect(m_playbackTimer, &QTimer::timeout, this, [this]() {
        if (!m_previewPlaying) {
            return;
        }
        const float totalDuration = AnimationSequence::GetTotalDurationSeconds();
        if (totalDuration <= 0.0f) {
            m_previewPlaying = false;
            m_playbackTimer->stop();
            return;
        }
        const qint64 elapsedMs = m_playbackElapsed->restart();
        m_previewSeconds += static_cast<float>(elapsedMs) / 1000.0f;
        if (m_previewSeconds >= totalDuration) {
            m_previewSeconds = totalDuration;
            m_previewPlaying = false;
            m_playbackTimer->stop();
        }
        applyPreviewTime(true);
        syncFromAnimation();
    });
    m_playbackTimer->setInterval(33);
}

void AnimationPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(6);
    auto *titleLabel = new QLabel(tr("Camera Path"), this);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(false);
    m_summaryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_addSelectedViewButton = new QPushButton(tr("Add Selected View"), this);
    m_addCurrentCameraButton = new QPushButton(tr("Add Current Camera"), this);
    m_removeKeyframeButton = new QPushButton(tr("Delete"), this);
    m_moveUpButton = new QPushButton(tr("Up"), this);
    m_moveDownButton = new QPushButton(tr("Down"), this);
    buttonRow->addWidget(titleLabel);
    buttonRow->addWidget(m_summaryLabel, 1);
    buttonRow->addWidget(m_addSelectedViewButton);
    buttonRow->addWidget(m_addCurrentCameraButton);
    buttonRow->addWidget(m_removeKeyframeButton);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    layout->addLayout(buttonRow);

    m_keyframeList = new QListWidget(this);
    m_keyframeList->setFlow(QListView::LeftToRight);
    m_keyframeList->setWrapping(false);
    m_keyframeList->setResizeMode(QListView::Adjust);
    m_keyframeList->setMovement(QListView::Static);
    m_keyframeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_keyframeList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_keyframeList->setUniformItemSizes(true);
    m_keyframeList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_keyframeList->setMinimumHeight(64);
    m_keyframeList->setMaximumHeight(82);
    layout->addWidget(m_keyframeList);

    auto *previewRow = new QHBoxLayout();
    previewRow->setContentsMargins(0, 0, 0, 0);
    previewRow->setSpacing(6);
    auto *previewTitle = new QLabel(tr("Preview"), this);
    m_scrubSlider = new QSlider(Qt::Horizontal, this);
    m_scrubSlider->setRange(0, kScrubResolution);
    m_playButton = new QPushButton(tr("Play"), this);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_previewLabel = new QLabel(this);
    m_previewLabel->setMinimumWidth(150);
    previewRow->addWidget(previewTitle);
    previewRow->addWidget(m_scrubSlider, 1);
    previewRow->addWidget(m_previewLabel);
    previewRow->addWidget(m_playButton);
    previewRow->addWidget(m_stopButton);
    layout->addLayout(previewRow);

    auto *settingsGrid = new QGridLayout();
    settingsGrid->setContentsMargins(0, 0, 0, 0);
    settingsGrid->setHorizontalSpacing(8);
    settingsGrid->setVerticalSpacing(4);
    m_keyframeName = new QLineEdit(this);
    m_durationToNext = new QDoubleSpinBox(this);
    m_durationToNext->setRange(0.0, 60.0);
    m_durationToNext->setDecimals(2);
    m_durationToNext->setSingleStep(0.1);
    m_easeInMode = new QComboBox(this);
    m_easeOutMode = new QComboBox(this);
    for (int index = 0; index < AnimationSequence::GetEasingModeCount(); ++index) {
        const QString label = QString::fromUtf8(AnimationSequence::GetEasingModeLabel(index));
        m_easeInMode->addItem(label);
        m_easeOutMode->addItem(label);
    }
    m_resolutionPreset = new QComboBox(this);
    for (int i = 0; i < g_renderResolutionPresetCount; ++i) {
        m_resolutionPreset->addItem(QString::fromUtf8(g_renderResolutionPresets[i].label));
    }
    m_exportMode = new QComboBox(this);
    for (int index = 0; index < AnimationSequence::GetExportModeCount(); ++index) {
        m_exportMode->addItem(QString::fromUtf8(AnimationSequence::GetExportModeLabel(index)));
    }
    m_fps = new QSpinBox(this);
    m_fps->setRange(1, 240);
    m_maxSpp = new QSpinBox(this);
    m_maxSpp->setRange(1, 4096);
    m_baseName = new QLineEdit(this);
    m_baseName->setMinimumWidth(120);
    m_renderButton = new QPushButton(tr("Render Frames..."), this);
    settingsGrid->addWidget(new QLabel(tr("Name"), this), 0, 0);
    settingsGrid->addWidget(m_keyframeName, 0, 1);
    settingsGrid->addWidget(new QLabel(tr("To Next"), this), 0, 2);
    settingsGrid->addWidget(m_durationToNext, 0, 3);
    settingsGrid->addWidget(new QLabel(tr("In"), this), 0, 4);
    settingsGrid->addWidget(m_easeInMode, 0, 5);
    settingsGrid->addWidget(new QLabel(tr("Out"), this), 0, 6);
    settingsGrid->addWidget(m_easeOutMode, 0, 7);
    settingsGrid->addWidget(new QLabel(tr("FPS"), this), 1, 0);
    settingsGrid->addWidget(m_fps, 1, 1);
    settingsGrid->addWidget(new QLabel(tr("Max SPP"), this), 1, 2);
    settingsGrid->addWidget(m_maxSpp, 1, 3);
    settingsGrid->addWidget(new QLabel(tr("Resolution"), this), 1, 4);
    settingsGrid->addWidget(m_resolutionPreset, 1, 5);
    settingsGrid->addWidget(new QLabel(tr("Output"), this), 1, 6);
    settingsGrid->addWidget(m_exportMode, 1, 7);
    settingsGrid->addWidget(new QLabel(tr("Base"), this), 1, 8);
    settingsGrid->addWidget(m_baseName, 1, 9);
    settingsGrid->setColumnStretch(1, 1);
    settingsGrid->setColumnStretch(5, 1);
    settingsGrid->setColumnStretch(7, 1);
    settingsGrid->setColumnStretch(9, 1);
    settingsGrid->addWidget(m_renderButton, 0, 10, 2, 1);
    layout->addLayout(settingsGrid);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(20);
    layout->addWidget(m_statusLabel);

    connect(m_addSelectedViewButton, &QPushButton::clicked, this, [this]() {
        const int selectedView = SavedViews::GetSelectedViewIndex();
        if (selectedView >= 0) {
            AnimationSequence::AddKeyframeFromSavedView(static_cast<size_t>(selectedView));
            syncFromAnimation();
        }
    });
    connect(m_addCurrentCameraButton, &QPushButton::clicked, this, [this]() {
        const auto current = SavedViews::CaptureCurrentState();
        AnimationSequence::AddKeyframeFromView(current, current.name);
        syncFromAnimation();
    });
    connect(m_removeKeyframeButton, &QPushButton::clicked, this, [this]() {
        const int index = selectedKeyframeIndex();
        if (index >= 0) {
            AnimationSequence::RemoveKeyframe(static_cast<size_t>(index));
            syncFromAnimation();
        }
    });
    connect(m_moveUpButton, &QPushButton::clicked, this, [this]() {
        const int index = selectedKeyframeIndex();
        if (index >= 0 && AnimationSequence::MoveKeyframe(static_cast<size_t>(index), -1)) {
            syncFromAnimation();
            if (index > 0) {
                m_keyframeList->setCurrentRow(index - 1);
            }
        }
    });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this]() {
        const int index = selectedKeyframeIndex();
        if (index >= 0 && AnimationSequence::MoveKeyframe(static_cast<size_t>(index), 1)) {
            syncFromAnimation();
            m_keyframeList->setCurrentRow(index + 1);
        }
    });
    connect(m_keyframeList, &QListWidget::currentRowChanged, this, [this](int) {
        updateSelectedKeyframeControls();
    });
    connect(m_keyframeName, &QLineEdit::editingFinished, this, [this]() {
        if (m_syncing) {
            return;
        }
        const int index = selectedKeyframeIndex();
        if (index >= 0) {
            AnimationSequence::RenameKeyframe(static_cast<size_t>(index),
                                              m_keyframeName->text().trimmed().toUtf8().constData());
            syncFromAnimation();
        }
    });
    connect(m_durationToNext, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        const int index = selectedKeyframeIndex();
        if (index >= 0) {
            AnimationSequence::UpdateKeyframe(static_cast<size_t>(index),
                                              static_cast<float>(value),
                                              m_easeInMode->currentIndex(),
                                              m_easeOutMode->currentIndex());
            syncFromAnimation();
        }
    });
    connect(m_easeInMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_syncing) {
            return;
        }
        const int index = selectedKeyframeIndex();
        if (index >= 0) {
            AnimationSequence::UpdateKeyframe(static_cast<size_t>(index),
                                              static_cast<float>(m_durationToNext->value()),
                                              m_easeInMode->currentIndex(),
                                              m_easeOutMode->currentIndex());
            syncFromAnimation();
        }
    });
    connect(m_easeOutMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_syncing) {
            return;
        }
        const int index = selectedKeyframeIndex();
        if (index >= 0) {
            AnimationSequence::UpdateKeyframe(static_cast<size_t>(index),
                                              static_cast<float>(m_durationToNext->value()),
                                              m_easeInMode->currentIndex(),
                                              m_easeOutMode->currentIndex());
            syncFromAnimation();
        }
    });
    connect(m_scrubSlider, &QSlider::valueChanged, this, [this](int) {
        if (m_syncing) {
            return;
        }
        m_previewPlaying = false;
        m_playbackTimer->stop();
        const float totalDuration = AnimationSequence::GetTotalDurationSeconds();
        if (totalDuration <= 0.0f) {
            m_previewSeconds = 0.0f;
        } else {
            m_previewSeconds = totalDuration *
                (static_cast<float>(m_scrubSlider->value()) / static_cast<float>(kScrubResolution));
        }
        applyPreviewTime(false);
        syncFromAnimation();
    });
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
        const float totalDuration = AnimationSequence::GetTotalDurationSeconds();
        if (totalDuration <= 0.0f) {
            return;
        }
        if (m_previewSeconds >= totalDuration) {
            m_previewSeconds = 0.0f;
            applyPreviewTime(true);
        }
        m_previewPlaying = true;
        m_playbackElapsed->restart();
        m_playbackTimer->start();
        syncFromAnimation();
    });
    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        m_previewPlaying = false;
        m_playbackTimer->stop();
    });
    connect(m_resolutionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateAnimationSettings();
    });
    connect(m_fps, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateAnimationSettings();
    });
    connect(m_maxSpp, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        updateAnimationSettings();
    });
    connect(m_exportMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateAnimationSettings();
    });
    connect(m_baseName, &QLineEdit::editingFinished, this, [this]() {
        updateAnimationSettings();
    });
    connect(m_renderButton, &QPushButton::clicked, this, [this]() {
        const int exportMode = m_exportMode ? m_exportMode->currentIndex()
                                            : static_cast<int>(AnimationSequence::ExportMode::Frames);
        const QString outputDir = QFileDialog::getExistingDirectory(
            this,
            exportMode == static_cast<int>(AnimationSequence::ExportMode::Mp4)
                ? tr("Choose Output Folder For MP4 Export")
                : tr("Choose Output Folder For Frame Export"),
            QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!outputDir.isEmpty()) {
            updateAnimationSettings();
            StartAnimationRenderExport(outputDir.toStdWString());
            syncFromAnimation();
        }
    });
}

int AnimationPanel::selectedKeyframeIndex() const
{
    if (!m_keyframeList || !m_keyframeList->currentItem()) {
        return -1;
    }
    return m_keyframeList->currentItem()->data(kKeyframeIndexRole).toInt();
}

QString AnimationPanel::buildModelSignature() const
{
    QString signature;
    const auto &keyframes = AnimationSequence::GetKeyframes();
    signature.reserve(static_cast<int>(keyframes.size() * 48 + 64));
    signature += QString::number(static_cast<int>(keyframes.size()));
    signature += QLatin1Char(';');
    for (const auto &keyframe : keyframes) {
        signature += QString::fromUtf8(keyframe.label.c_str());
        signature += QLatin1Char('|');
        signature += QString::number(keyframe.durationToNextSeconds, 'f', 4);
        signature += QLatin1Char('|');
        signature += QString::number(keyframe.easeIn);
        signature += QLatin1Char('|');
        signature += QString::number(keyframe.easeOut);
        signature += QLatin1Char(';');
    }

    const auto &settings = AnimationSequence::GetExportSettings();
    signature += QStringLiteral("res=%1;fps=%2;spp=%3;base=%4;mode=%5;view=%6;render=%7;batch=%8;anim=%9;enc=%10")
        .arg(settings.resolutionPreset)
        .arg(settings.fps)
        .arg(settings.maxSpp)
        .arg(QString::fromUtf8(settings.baseName.c_str()))
        .arg(settings.exportMode)
        .arg(SavedViews::GetSelectedViewIndex())
        .arg(g_renderExportJob.active ? 1 : 0)
        .arg(g_renderBatchExport.active ? 1 : 0)
        .arg(g_renderAnimationExport.active ? 1 : 0)
        .arg(g_renderAnimationExport.encoding ? 1 : 0);
    return signature;
}

bool AnimationPanel::hasInteractiveFocus() const
{
    QWidget *focus = QApplication::focusWidget();
    if (!focus || !isAncestorOf(focus)) {
        return false;
    }

    return focus == m_keyframeName ||
           focus == m_baseName ||
           focus == m_durationToNext ||
           focus == m_fps ||
           focus == m_maxSpp ||
           focus == m_easeInMode ||
           focus == m_easeOutMode ||
           focus == m_exportMode ||
           focus == m_resolutionPreset ||
           focus == m_keyframeList ||
           m_scrubSlider->isSliderDown();
}

void AnimationPanel::applyPreviewTime(bool updateSlider)
{
    const float totalDuration = AnimationSequence::GetTotalDurationSeconds();
    if (AnimationSequence::GetKeyframes().empty()) {
        return;
    }
    if (totalDuration <= 0.0f) {
        AnimationSequence::ApplyAtFrame(0, 1);
        m_previewSeconds = 0.0f;
        if (updateSlider) {
            const bool wasSyncing = m_syncing;
            m_syncing = true;
            m_scrubSlider->setValue(0);
            m_syncing = wasSyncing;
        }
        return;
    }

    m_previewSeconds = (std::clamp)(m_previewSeconds, 0.0f, totalDuration);
    if (updateSlider) {
        const int scrubValue = static_cast<int>(std::round(
            (m_previewSeconds / totalDuration) * static_cast<float>(kScrubResolution)));
        const bool wasSyncing = m_syncing;
        m_syncing = true;
        m_scrubSlider->setValue(scrubValue);
        m_syncing = wasSyncing;
    }
    AnimationSequence::ApplyAtTime(m_previewSeconds);
}

void AnimationPanel::updateAnimationSettings()
{
    if (m_syncing) {
        return;
    }
    AnimationSequence::ExportSettings settings = AnimationSequence::GetExportSettings();
    settings.resolutionPreset = m_resolutionPreset->currentIndex();
    settings.fps = m_fps->value();
    settings.maxSpp = m_maxSpp->value();
    settings.exportMode = m_exportMode->currentIndex();
    settings.baseName = m_baseName->text().trimmed().toUtf8().constData();
    if (settings.baseName.empty()) {
        settings.baseName = "final";
    }
    AnimationSequence::SetExportSettings(settings);
    syncFromAnimation();
}

void AnimationPanel::updateSelectedKeyframeControls()
{
    m_syncing = true;

    const int index = selectedKeyframeIndex();
    const auto &keyframes = AnimationSequence::GetKeyframes();
    const bool valid = index >= 0 && index < static_cast<int>(keyframes.size());
    const bool hasPreviousSegment = valid && index > 0;
    const bool hasNextSegment = valid && index + 1 < static_cast<int>(keyframes.size());
    m_keyframeName->setEnabled(valid);
    m_durationToNext->setEnabled(hasNextSegment);
    m_easeInMode->setEnabled(hasNextSegment);
    m_easeOutMode->setEnabled(hasPreviousSegment);
    m_removeKeyframeButton->setEnabled(valid);
    m_moveUpButton->setEnabled(valid && index > 0);
    m_moveDownButton->setEnabled(valid && index + 1 < static_cast<int>(keyframes.size()));

    if (valid) {
        m_keyframeName->setText(QString::fromUtf8(keyframes[static_cast<size_t>(index)].label.c_str()));
        m_durationToNext->setValue(keyframes[static_cast<size_t>(index)].durationToNextSeconds);
        m_easeInMode->setCurrentIndex(keyframes[static_cast<size_t>(index)].easeIn);
        m_easeOutMode->setCurrentIndex(keyframes[static_cast<size_t>(index)].easeOut);
    } else {
        m_keyframeName->clear();
        m_durationToNext->setValue(0.0);
        m_easeInMode->setCurrentIndex(0);
        m_easeOutMode->setCurrentIndex(0);
    }

    m_syncing = false;
}

void AnimationPanel::syncFromAnimation()
{
    const auto &keyframes = AnimationSequence::GetKeyframes();
    const auto &settings = AnimationSequence::GetExportSettings();
    const QString signature = buildModelSignature();
    const bool modelChanged = signature != m_lastModelSignature;
    const bool canRebuildModelUi = !hasInteractiveFocus() && !m_previewPlaying;

    if (modelChanged && canRebuildModelUi) {
        m_syncing = true;

        const int previousSelection = selectedKeyframeIndex();
        m_keyframeList->clear();
        for (size_t index = 0; index < keyframes.size(); ++index) {
            auto *item = new QListWidgetItem(
                KeyframeSummary(keyframes[index], index, index + 1 == keyframes.size()),
                m_keyframeList);
            item->setData(kKeyframeIndexRole, static_cast<int>(index));
        }
        if (previousSelection >= 0 && previousSelection < m_keyframeList->count()) {
            m_keyframeList->setCurrentRow(previousSelection);
        } else if (!keyframes.empty() && m_keyframeList->currentRow() < 0) {
            m_keyframeList->setCurrentRow(0);
        }

        m_resolutionPreset->setCurrentIndex(settings.resolutionPreset);
        m_fps->setValue(settings.fps);
        m_maxSpp->setValue(settings.maxSpp);
        m_exportMode->setCurrentIndex(settings.exportMode);
        m_baseName->setText(QString::fromUtf8(settings.baseName.c_str()));

        updateSelectedKeyframeControls();
        m_lastModelSignature = signature;
        m_syncing = false;
    }

    const float totalDuration = AnimationSequence::GetTotalDurationSeconds();
    const int totalFrames = AnimationSequence::GetTotalFrameCount(settings.fps);
    m_summaryLabel->setText(
        tr("%1 keys | %2 s | %3 frames @ %4 fps")
            .arg(static_cast<int>(keyframes.size()))
            .arg(QString::number(totalDuration, 'f', 2))
            .arg(totalFrames)
            .arg(settings.fps));

    const bool canRender = !keyframes.empty() && !g_renderExportJob.active &&
                           !g_renderBatchExport.active && !g_renderAnimationExport.active;
    m_renderButton->setEnabled(canRender);
    m_renderButton->setText(settings.exportMode == static_cast<int>(AnimationSequence::ExportMode::Mp4)
                                ? tr("Export MP4...")
                                : tr("Export Frames..."));
    m_addSelectedViewButton->setEnabled(SavedViews::GetSelectedViewIndex() >= 0);
    m_playButton->setEnabled(totalDuration > 0.0f);
    m_stopButton->setEnabled(m_previewPlaying);

    int scrubValue = 0;
    if (totalDuration > 0.0f) {
        scrubValue = static_cast<int>(std::round((std::clamp)(m_previewSeconds / totalDuration, 0.0f, 1.0f) * kScrubResolution));
    }
    m_scrubSlider->setEnabled(!keyframes.empty());
    if (!m_scrubSlider->isSliderDown()) {
        m_syncing = true;
        m_scrubSlider->setValue(scrubValue);
        m_syncing = false;
    }
    m_previewLabel->setText(
        tr("%1 s / %2 s")
            .arg(QString::number(m_previewSeconds, 'f', 2))
            .arg(QString::number(totalDuration, 'f', 2)));

    QString statusText = QString::fromUtf8(AnimationSequence::GetLastStatus().c_str());
    if (g_renderAnimationExport.active && g_renderAnimationExport.encoding) {
        const QString progressText = QString::fromUtf8(GetAnimationExportProgressText().c_str());
        statusText = progressText;
        if (!g_renderExportStatus.empty()) {
            const QString detailText = QString::fromUtf8(g_renderExportStatus.c_str());
            if (detailText != progressText) {
                statusText += QLatin1Char('\n');
                statusText += detailText;
            }
        }
    } else if (g_renderAnimationExport.active) {
        statusText = QString::fromUtf8(GetAnimationExportProgressText().c_str());
    } else if (!g_renderExportStatus.empty()) {
        statusText = QString::fromUtf8(g_renderExportStatus.c_str());
    }
    m_statusLabel->setText(statusText);
}