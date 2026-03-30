#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class SliderControl;

class CameraPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void applyLensSettings();
    void applyExposureSettings(bool resetAccumulation);
    void applyTonemapSettings();

    bool m_syncing = false;

    QLabel *m_statusLabel = nullptr;

    SliderControl *m_horizontalFov = nullptr;
    QPushButton *m_resetCameraButton = nullptr;
    SliderControl *m_moveSpeed = nullptr;
    SliderControl *m_mouseSensitivity = nullptr;
    QCheckBox *m_safeFrameEnabled = nullptr;
    QLabel *m_safeFrameInfo = nullptr;

    QCheckBox *m_autoExposure = nullptr;
    QCheckBox *m_physicalCamera = nullptr;
    SliderControl *m_exposureCompensation = nullptr;
    SliderControl *m_manualExposure = nullptr;
    SliderControl *m_iso = nullptr;
    SliderControl *m_shutterSeconds = nullptr;
    SliderControl *m_aperture = nullptr;
    QLabel *m_evLabel = nullptr;
    QPushButton *m_presetDaylight = nullptr;
    QPushButton *m_presetSunny16 = nullptr;
    QPushButton *m_presetInterior = nullptr;
    QPushButton *m_matchSceneEv = nullptr;

    SliderControl *m_tonemapVignette = nullptr;
    SliderControl *m_tonemapSaturation = nullptr;
    SliderControl *m_tonemapContrast = nullptr;
    QLabel *m_dxrAoNote = nullptr;
    QComboBox *m_dxrAoMode = nullptr;
    SliderControl *m_dxrAoIntensity = nullptr;
    SliderControl *m_dxrAoLengthMm = nullptr;

    QTimer *m_refreshTimer = nullptr;
};
