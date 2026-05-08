#pragma once

#include <QWidget>
#include <QColor>
#include <cstdint>
#include <functional>

struct Light;

class QLabel;
class QListWidget;
class QPushButton;
class QDoubleSpinBox;
class QGroupBox;
class QTimer;

class LightsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LightsPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void refreshLights();
    void applyLight(const std::function<void(struct Light &)> &fn);
    void updateColorButton(const QColor &color);
    void updatePropertyVisibility(uint32_t type);
    bool hasPropertyEditorFocus() const;
    uint64_t lightListSignature() const;

    bool m_syncing = false;
    uint64_t m_lastLightListSignature = 0;
    int m_lastInspectorLightIndex = -2;

    QLabel *m_loadNotice = nullptr;
    QGroupBox *m_listGroup = nullptr;
    QListWidget *m_lightList = nullptr;
    QGroupBox *m_propertiesGroup = nullptr;

    QPushButton *m_addPointButton = nullptr;
    QPushButton *m_addSpotButton = nullptr;
    QPushButton *m_addRectButton = nullptr;
    QPushButton *m_addDiskButton = nullptr;

    QLabel *m_typeLabel = nullptr;
    QLabel *m_directionLabel = nullptr;
    QWidget *m_directionRow = nullptr;

    QPushButton *m_colorButton = nullptr;
    QColor m_currentColor;

    QDoubleSpinBox *m_intensity = nullptr;

    QDoubleSpinBox *m_posX = nullptr;
    QDoubleSpinBox *m_posY = nullptr;
    QDoubleSpinBox *m_posZ = nullptr;

    QDoubleSpinBox *m_dirX = nullptr;
    QDoubleSpinBox *m_dirY = nullptr;
    QDoubleSpinBox *m_dirZ = nullptr;

    QDoubleSpinBox *m_radius = nullptr;

    QGroupBox *m_spotGroup = nullptr;
    QDoubleSpinBox *m_innerAngle = nullptr;
    QDoubleSpinBox *m_outerAngle = nullptr;

    QGroupBox *m_areaGroup = nullptr;
    QDoubleSpinBox *m_areaWidth = nullptr;
    QDoubleSpinBox *m_areaHeight = nullptr;

    QGroupBox *m_iesGroup = nullptr;
    QLabel *m_iesLabel = nullptr;

    QPushButton *m_selectButton = nullptr;
    QPushButton *m_removeButton = nullptr;

    QTimer *m_refreshTimer = nullptr;
};
