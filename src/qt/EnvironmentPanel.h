#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTimer;
class SliderControl;

class EnvironmentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EnvironmentPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void syncFromRenderer();
    void applyLightingSettings(bool updateSkyModel, bool updateCameraBuffer);
    void applyCloudSettings();

    bool m_syncing = false;

    QLabel *m_summaryLabel = nullptr;
    QLabel *m_filePathLabel = nullptr;
    QComboBox *m_iblSource = nullptr;
    QPushButton *m_loadHdrButton = nullptr;
    SliderControl *m_iblRotation = nullptr;
    QCheckBox *m_solidAngleSampling = nullptr;

    QCheckBox *m_physicalCalibration = nullptr;
    SliderControl *m_analyticSunIntensity = nullptr;
    SliderControl *m_visibility = nullptr;
    SliderControl *m_albedo = nullptr;
    SliderControl *m_altitude = nullptr;
    SliderControl *m_skyIntensity = nullptr;
    SliderControl *m_sunIntensity = nullptr;
    SliderControl *m_sunSize = nullptr;
    SliderControl *m_timeOfDay = nullptr;
    SliderControl *m_northOffset = nullptr;
    SliderControl *m_latitude = nullptr;
    SliderControl *m_dayOfYear = nullptr;
    QLabel *m_solarLabel = nullptr;
    SliderControl *m_fileSunIntensity = nullptr;
    SliderControl *m_fileSunSize = nullptr;

    QCheckBox *m_cloudEnabled = nullptr;
    QPushButton *m_resetCloudsButton = nullptr;
    SliderControl *m_cloudDensity = nullptr;
    SliderControl *m_cloudAbsorption = nullptr;
    SliderControl *m_cloudCoverage = nullptr;
    SliderControl *m_cloudScattering = nullptr;
    SliderControl *m_cloudSunIntensity = nullptr;
    SliderControl *m_cloudTop = nullptr;
    SliderControl *m_cloudBottom = nullptr;
    SliderControl *m_cloudWindSpeed = nullptr;
    SliderControl *m_baseScale = nullptr;
    SliderControl *m_detailScale = nullptr;
    SliderControl *m_coverageScale = nullptr;
    SliderControl *m_coverageVariation = nullptr;
    SliderControl *m_erosion = nullptr;
    SliderControl *m_warpStrength = nullptr;
    SliderControl *m_shapePower = nullptr;
    SliderControl *m_powderStrength = nullptr;
    SliderControl *m_shadowSteps = nullptr;
    SliderControl *m_shadowStepSize = nullptr;
    SliderControl *m_shadowLod = nullptr;

    QTimer *m_refreshTimer = nullptr;
};