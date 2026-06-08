#pragma once

#include <QWidget>

#include <cstddef>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QToolButton;

class ScatterPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScatterPanel(QWidget *parent = nullptr);
    ~ScatterPanel() override;

private:
    void createUi();
    void refreshUi();
    void syncInspector();
    void applyModelEdit();
    void applyTargetEdit();
    void applyObjectEdit();
    int selectedModelIndex() const;
    int selectedObjectIndex() const;
    void setSelectedModel(int row);

    bool m_syncing = false;
    size_t m_sceneChangeListenerId = 0;
    uint64_t m_lastScatterRevision = 0;

    // Model list + per-model header --------------------------------------
    QListWidget *m_modelList = nullptr;
    QLineEdit *m_modelName = nullptr;
    QCheckBox *m_modelEnabled = nullptr;
    QSpinBox *m_modelSeed = nullptr;
    QToolButton *m_reseedButton = nullptr;
    QDoubleSpinBox *m_previewDensityScale = nullptr;
    QSpinBox *m_previewBudget = nullptr;

    // Header toolbar buttons --------------------------------------------
    QToolButton *m_addModelButton = nullptr;
    QToolButton *m_deleteModelButton = nullptr;
    QToolButton *m_addTargetsButton = nullptr;
    QToolButton *m_pickTargetButton = nullptr;
    QToolButton *m_addObjectsButton = nullptr;
    QToolButton *m_cleanupObjectsButton = nullptr;
    QToolButton *m_bakeToNodesButton = nullptr;
    QLabel *m_pickStatusLabel = nullptr;

    // Tabs --------------------------------------------------------------
    QTabWidget *m_tabs = nullptr;

    // Targets tab --------------------------------------------------------
    QListWidget *m_targetList = nullptr;
    QToolButton *m_removeTargetButton = nullptr;
    QCheckBox *m_targetEnabled = nullptr;
    QDoubleSpinBox *m_targetWeight = nullptr;

    // Objects tab --------------------------------------------------------
    QListWidget *m_objectList = nullptr;
    QToolButton *m_removeObjectButton = nullptr;
    QToolButton *m_hideSourceButton = nullptr;

    // Placement tab ------------------------------------------------------
    QLineEdit *m_objectName = nullptr;
    QCheckBox *m_objectEnabled = nullptr;
    QDoubleSpinBox *m_density = nullptr;
    QDoubleSpinBox *m_weight = nullptr;
    QSpinBox *m_maxInstances = nullptr;
    QSpinBox *m_previewMaxInstances = nullptr;
    QDoubleSpinBox *m_minScale = nullptr;
    QDoubleSpinBox *m_maxScale = nullptr;
    QDoubleSpinBox *m_yaw = nullptr;
    QDoubleSpinBox *m_pitch = nullptr;
    QDoubleSpinBox *m_roll = nullptr;
    QDoubleSpinBox *m_normalAlign = nullptr;
    QDoubleSpinBox *m_slopeMin = nullptr;
    QDoubleSpinBox *m_slopeMax = nullptr;
    QDoubleSpinBox *m_jitter = nullptr;
    QDoubleSpinBox *m_heightMin = nullptr;
    QDoubleSpinBox *m_heightMax = nullptr;
    QDoubleSpinBox *m_minDistance = nullptr;
    QDoubleSpinBox *m_maxDistance = nullptr;
    QDoubleSpinBox *m_distanceFade = nullptr;
    QDoubleSpinBox *m_avoidLightRadius = nullptr;
    QDoubleSpinBox *m_clumpScale = nullptr;
    QDoubleSpinBox *m_clumpStrength = nullptr;
    QDoubleSpinBox *m_edgeAvoidance = nullptr;
    QDoubleSpinBox *m_collisionAvoidance = nullptr;
    QDoubleSpinBox *m_meshClearance = nullptr;

    QLabel *m_statsLabel = nullptr;
};
