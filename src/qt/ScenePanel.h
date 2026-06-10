#pragma once

#include <QPoint>
#include <QWidget>

#include <cstddef>
#include <cstdint>

class QLabel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;
class QTreeWidget;

class ScenePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScenePanel(QWidget *parent = nullptr);
    ~ScenePanel() override;

private:
    void createUi();
    void refreshSceneList();
    void scheduleRefresh();
    int selectedNodeIndex() const;
    void requestDeleteSelectedNode();
    void showNodeContextMenu(const QPoint &pos);
    void syncVolumeMaterialInspector();
    void applyVolumeMaterialInspector();
    void applyVolumePlaybackInspector();

    QTreeWidget *m_nodeList = nullptr;
    QProgressBar *m_importProgress = nullptr;
    QLabel *m_importStatusLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QGroupBox *m_volumeMaterialGroup = nullptr;
    QDoubleSpinBox *m_volumeDensity = nullptr;
    QDoubleSpinBox *m_volumeAbsorption = nullptr;
    QDoubleSpinBox *m_volumeScattering = nullptr;
    QDoubleSpinBox *m_volumeAmbient = nullptr;
    QDoubleSpinBox *m_volumeEmission = nullptr;
    QDoubleSpinBox *m_volumeLightingStrength = nullptr;
    QLabel *m_volumeLightStats = nullptr;
    QLabel *m_volumeSequenceStats = nullptr;
    QComboBox *m_volumePlaybackMode = nullptr;
    QDoubleSpinBox *m_volumePlaybackFps = nullptr;
    QCheckBox *m_volumePlaybackLoop = nullptr;
    QSpinBox *m_volumeFrameOffset = nullptr;
    QDoubleSpinBox *m_volumeTemperatureLow = nullptr;
    QDoubleSpinBox *m_volumeTemperatureHigh = nullptr;
    QDoubleSpinBox *m_volumeTemperatureGamma = nullptr;
    QDoubleSpinBox *m_volumeJitter = nullptr;
    QSpinBox *m_volumeMarchSteps = nullptr;
    QSpinBox *m_volumeLightSteps = nullptr;
    QPushButton *m_volumeColor = nullptr;
    QPushButton *m_volumeEmissionColor = nullptr;
    QToolButton *m_importButton = nullptr;
    QToolButton *m_reimportButton = nullptr;
    QToolButton *m_addPlaneButton = nullptr;
    QToolButton *m_explodeButton = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_syncing = false;
    bool m_treeDirty = true;
    bool m_lastSceneIoActive = false;
    bool m_lastImportActive = false;
    uint64_t m_treeStructureSignature = 0;
    size_t m_sceneChangeListenerId = 0;
};
