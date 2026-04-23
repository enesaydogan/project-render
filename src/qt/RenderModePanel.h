#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

class RenderModePanel : public QWidget
{
    Q_OBJECT

public:
    explicit RenderModePanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void applyCameraSettings();
    void recreateDxrPipeline(const char *context);

    bool m_syncing = false;

    QLabel *m_modeLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QPushButton *m_switchModeButton = nullptr;

    QDoubleSpinBox *m_reflectionBounces = nullptr;
    QDoubleSpinBox *m_refractionBounces = nullptr;
    QDoubleSpinBox *m_giBounces = nullptr;
    QSpinBox *m_maxSpp = nullptr;
    QCheckBox *m_adaptiveSampling = nullptr;
    QDoubleSpinBox *m_targetNoise = nullptr;

    QComboBox *m_dlssMode = nullptr;
    QComboBox *m_dlssQuality = nullptr;
    QDoubleSpinBox *m_rrJitterScale = nullptr;
    QPushButton *m_resetDlssHistory = nullptr;
    QLabel *m_renderSizeLabel = nullptr;

    QComboBox *m_finalDenoiser = nullptr;
    QComboBox *m_oidnQuality = nullptr;

    QTimer *m_refreshTimer = nullptr;
};