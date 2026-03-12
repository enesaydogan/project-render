#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

class RenderSettingsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RenderSettingsPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void applyCameraSettings();
    void recreateDxrPipeline(const char *context);

    bool m_syncing = false;

    QLabel *m_modeLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QPushButton *m_switchModeButton = nullptr;

    QWidget *m_dxrSection = nullptr;
    QWidget *m_rasterSection = nullptr;

    QDoubleSpinBox *m_reflectionBounces = nullptr;
    QDoubleSpinBox *m_refractionBounces = nullptr;
    QDoubleSpinBox *m_giBounces = nullptr;
    QSpinBox *m_maxSpp = nullptr;
    QCheckBox *m_adaptiveSampling = nullptr;
    QDoubleSpinBox *m_targetNoise = nullptr;

    QComboBox *m_realtimeDenoiser = nullptr;
    QPushButton *m_resetRealtimeHistory = nullptr;

    QCheckBox *m_dlssEnabled = nullptr;
    QComboBox *m_dlssMode = nullptr;
    QComboBox *m_dlssQuality = nullptr;
    QDoubleSpinBox *m_rrJitterScale = nullptr;
    QPushButton *m_resetDlssHistory = nullptr;
    QLabel *m_renderSizeLabel = nullptr;

    QComboBox *m_finalDenoiser = nullptr;
    QComboBox *m_oidnQuality = nullptr;

    QCheckBox *m_enableSsr = nullptr;
    QDoubleSpinBox *m_ssrStepSize = nullptr;
    QDoubleSpinBox *m_ssrThickness = nullptr;
    QDoubleSpinBox *m_ssrIntensity = nullptr;
    QDoubleSpinBox *m_ssrMinSmoothness = nullptr;
    QSpinBox *m_ssrMaxSteps = nullptr;

    QCheckBox *m_enableSsao = nullptr;
    QDoubleSpinBox *m_ssaoRadius = nullptr;
    QDoubleSpinBox *m_ssaoBias = nullptr;
    QDoubleSpinBox *m_ssaoStrength = nullptr;
    QSpinBox *m_ssaoSamples = nullptr;
    QDoubleSpinBox *m_ssaoCompositeWeight = nullptr;

    QCheckBox *m_enableBloom = nullptr;
    QDoubleSpinBox *m_bloomThreshold = nullptr;
    QDoubleSpinBox *m_bloomIntensity = nullptr;

    QDoubleSpinBox *m_tonemapVignette = nullptr;
    QDoubleSpinBox *m_tonemapSaturation = nullptr;
    QDoubleSpinBox *m_tonemapContrast = nullptr;

    QPushButton *m_resetButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
