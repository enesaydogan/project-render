#pragma once

#include <QString>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QElapsedTimer;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

class AnimationPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnimationPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromAnimation();
    void updateSelectedKeyframeControls();
    void updateAnimationSettings();
    void applyPreviewTime(bool updateSlider);
    QString buildModelSignature() const;
    bool hasInteractiveFocus() const;
    int selectedKeyframeIndex() const;

    bool m_syncing = false;
    bool m_previewPlaying = false;
    float m_previewSeconds = 0.0f;
    QString m_lastModelSignature;

    QListWidget *m_keyframeList = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPushButton *m_addSelectedViewButton = nullptr;
    QPushButton *m_addCurrentCameraButton = nullptr;
    QPushButton *m_removeKeyframeButton = nullptr;
    QPushButton *m_moveUpButton = nullptr;
    QPushButton *m_moveDownButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_renderButton = nullptr;
    QLineEdit *m_keyframeName = nullptr;
    QDoubleSpinBox *m_durationToNext = nullptr;
    QComboBox *m_easeInMode = nullptr;
    QComboBox *m_easeOutMode = nullptr;
    QComboBox *m_exportMode = nullptr;
    QComboBox *m_resolutionPreset = nullptr;
    QSpinBox *m_fps = nullptr;
    QSpinBox *m_maxSpp = nullptr;
    QLineEdit *m_baseName = nullptr;
    QSlider *m_scrubSlider = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_playbackTimer = nullptr;
    QElapsedTimer *m_playbackElapsed = nullptr;
};