#pragma once

#include <QWidget>
#include <QColor>
#include <cstdint>
#include <functional>

struct LightPrototype;
struct LightInstance;

class QLabel;
class QListWidget;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class LightsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LightsPanel(QWidget *parent = nullptr);

private:
    void createUi();
    void refreshLights();
    void updateColorButton(const QColor &color);
    void updatePropertyVisibility(uint32_t type);
    bool hasPropertyEditorFocus() const;
    uint64_t lightListSignature() const;

    // Returns the currently selected instance index, or -1
    int selectedInstanceIndex() const;
    // Returns the prototype index for the selected tree item, or -1
    int selectedPrototypeIndex() const;

    bool m_syncing = false;
    uint64_t m_lastLightListSignature = 0;
    int m_lastInspectorProtoIndex = -2;
    int m_lastInspectorInstIndex = -2;

    QLabel *m_loadNotice = nullptr;
    QGroupBox *m_listGroup = nullptr;
    QTreeWidget *m_lightTree = nullptr;
    QGroupBox *m_propertiesGroup = nullptr;

    QPushButton *m_addPointButton = nullptr;
    QPushButton *m_addSpotButton = nullptr;
    QPushButton *m_addRectButton = nullptr;
    QPushButton *m_addDiskButton = nullptr;
    QPushButton *m_addIESButton = nullptr;

    // Prototype property widgets
    QLabel *m_typeLabel = nullptr;
    QWidget *m_protoPropsWidget = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_protoEnabled = nullptr;
    QLabel *m_instCountLabel = nullptr;

    QPushButton *m_colorButton = nullptr;
    QColor m_currentColor;

    QDoubleSpinBox *m_intensity = nullptr;
    QDoubleSpinBox *m_radius = nullptr;

    QGroupBox *m_spotGroup = nullptr;
    QDoubleSpinBox *m_innerAngle = nullptr;
    QDoubleSpinBox *m_outerAngle = nullptr;

    QGroupBox *m_areaGroup = nullptr;
    QDoubleSpinBox *m_areaWidth = nullptr;
    QDoubleSpinBox *m_areaHeight = nullptr;

    QGroupBox *m_iesGroup = nullptr;
    QLabel *m_iesLabel = nullptr;
    QPushButton *m_loadIESButton = nullptr;
    QPushButton *m_clearIESButton = nullptr;

    // Instance transform widgets
    QWidget *m_instancePropsWidget = nullptr;
    QLabel *m_instanceLabel = nullptr;
    QCheckBox *m_instanceEnabled = nullptr;
    QLabel *m_directionLabel = nullptr;
    QWidget *m_directionRow = nullptr;

    QDoubleSpinBox *m_posX = nullptr;
    QDoubleSpinBox *m_posY = nullptr;
    QDoubleSpinBox *m_posZ = nullptr;

    QDoubleSpinBox *m_dirX = nullptr;
    QDoubleSpinBox *m_dirY = nullptr;
    QDoubleSpinBox *m_dirZ = nullptr;

    QPushButton *m_selectButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_addInstanceButton = nullptr;
    QPushButton *m_duplicateCopyButton = nullptr;
    QPushButton *m_mergeCopiesButton = nullptr;
    QPushButton *m_moveToSurfaceButton = nullptr;
    QComboBox *m_placeTypeCombo = nullptr;
    QPushButton *m_createAtClickButton = nullptr;
    QLabel *m_placementStatusLabel = nullptr;

    QTimer *m_refreshTimer = nullptr;
};
