#pragma once

#include <QWidget>

class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

class RenderSettingsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RenderSettingsPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void updateModeNotice();

    bool m_syncing = false;

    QLabel *m_modeNotice = nullptr;

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