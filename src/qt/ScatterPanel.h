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
class QTimer;

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
    void applyObjectEdit();
    int selectedModelIndex() const;
    int selectedObjectIndex() const;
    void setSelectedModel(int row);

    bool m_syncing = false;
    size_t m_sceneChangeListenerId = 0;
    uint64_t m_lastScatterRevision = 0;

    QListWidget *m_modelList = nullptr;
    QListWidget *m_targetList = nullptr;
    QListWidget *m_objectList = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLineEdit *m_modelName = nullptr;
    QCheckBox *m_modelEnabled = nullptr;
    QSpinBox *m_modelSeed = nullptr;
    QPushButton *m_addModelButton = nullptr;
    QPushButton *m_deleteModelButton = nullptr;
    QPushButton *m_addTargetsButton = nullptr;
    QPushButton *m_addObjectsButton = nullptr;
    QPushButton *m_removeTargetButton = nullptr;
    QPushButton *m_removeObjectButton = nullptr;

    QLineEdit *m_objectName = nullptr;
    QCheckBox *m_objectEnabled = nullptr;
    QDoubleSpinBox *m_density = nullptr;
    QDoubleSpinBox *m_weight = nullptr;
    QSpinBox *m_maxInstances = nullptr;
    QDoubleSpinBox *m_minScale = nullptr;
    QDoubleSpinBox *m_maxScale = nullptr;
    QDoubleSpinBox *m_yaw = nullptr;
    QDoubleSpinBox *m_pitch = nullptr;
    QDoubleSpinBox *m_roll = nullptr;
    QDoubleSpinBox *m_normalAlign = nullptr;
    QDoubleSpinBox *m_slopeMin = nullptr;
    QDoubleSpinBox *m_slopeMax = nullptr;
    QDoubleSpinBox *m_jitter = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
