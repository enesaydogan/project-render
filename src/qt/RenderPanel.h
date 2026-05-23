#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class SliderControl;

class RenderPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RenderPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void updateExportSettings();
    void startRenderExport();

    bool m_syncing = false;

    QLabel *m_summaryLabel = nullptr;
    QLabel *m_outputLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_progressDetailLabel = nullptr;

    QComboBox *m_resolutionPreset = nullptr;
    SliderControl *m_maxSpp = nullptr;
    SliderControl *m_noisePercent = nullptr;
    QComboBox *m_denoiser = nullptr;
    QCheckBox *m_batchSavedViews = nullptr;
    QCheckBox *m_tileRendering = nullptr;
    QLineEdit *m_batchBaseName = nullptr;
    QLabel *m_tileRenderingWarning = nullptr;

    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_renderButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QTimer *m_refreshTimer = nullptr;
};
