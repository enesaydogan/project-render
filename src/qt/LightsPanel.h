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
class QToolButton;
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
    void removeSelectedItems();
    void beginCreateForType(uint32_t lightType);

    int selectedInstanceIndex() const;
    int selectedPrototypeIndex() const;

    bool m_syncing = false;
    uint64_t m_lastLightListSignature = 0;
    int m_lastInspectorProtoIndex = -2;
    int m_lastInspectorInstIndex = -2;
    int m_armedCreateType = -1;

    QLabel *m_loadNotice = nullptr;
    QWidget *m_toolbar = nullptr;
    QTreeWidget *m_lightTree = nullptr;
    QWidget *m_propertiesGroup = nullptr;
    QLabel *m_placementStatusLabel = nullptr;

    QToolButton *m_addPointButton = nullptr;
    QToolButton *m_addSpotButton = nullptr;
    QToolButton *m_addRectButton = nullptr;
    QToolButton *m_addDiskButton = nullptr;
    QToolButton *m_addIESButton = nullptr;

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

    QToolButton *m_moveToSurfaceButton = nullptr;
    QToolButton *m_selectButton = nullptr;
    QToolButton *m_removeButton = nullptr;
    QToolButton *m_addInstanceButton = nullptr;
    QToolButton *m_duplicateCopyButton = nullptr;
    QToolButton *m_mergeCopiesButton = nullptr;

    QTimer *m_refreshTimer = nullptr;
};
