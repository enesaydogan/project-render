#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;
class SliderControl;

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

    SliderControl *m_reflectionBounces = nullptr;
    SliderControl *m_refractionBounces = nullptr;
    SliderControl *m_giBounces = nullptr;
    SliderControl *m_maxSpp = nullptr;
    QCheckBox *m_adaptiveSampling = nullptr;
    SliderControl *m_targetNoise = nullptr;

    QComboBox *m_realtimeDenoiser = nullptr;
    QPushButton *m_resetRealtimeHistory = nullptr;

    QCheckBox *m_dlssEnabled = nullptr;
    QComboBox *m_dlssMode = nullptr;
    QComboBox *m_dlssQuality = nullptr;
    SliderControl *m_rrJitterScale = nullptr;
    QPushButton *m_resetDlssHistory = nullptr;
    QLabel *m_renderSizeLabel = nullptr;

    QComboBox *m_finalDenoiser = nullptr;
    QComboBox *m_oidnQuality = nullptr;

    QCheckBox *m_enableSsr = nullptr;
    SliderControl *m_ssrStepSize = nullptr;
    SliderControl *m_ssrThickness = nullptr;
    SliderControl *m_ssrIntensity = nullptr;
    SliderControl *m_ssrMinSmoothness = nullptr;
    SliderControl *m_ssrMaxSteps = nullptr;

    QCheckBox *m_enableSsao = nullptr;
    SliderControl *m_ssaoRadius = nullptr;
    SliderControl *m_ssaoBias = nullptr;
    SliderControl *m_ssaoStrength = nullptr;
    SliderControl *m_ssaoSamples = nullptr;
    SliderControl *m_ssaoCompositeWeight = nullptr;

    QCheckBox *m_enableBloom = nullptr;
    SliderControl *m_bloomThreshold = nullptr;
    SliderControl *m_bloomIntensity = nullptr;

    QPushButton *m_resetButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
