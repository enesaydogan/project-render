#pragma once

#include <QWidget>

#include <cstddef>
#include <cstdint>

class QLabel;
class QProgressBar;
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

    QTreeWidget *m_nodeList = nullptr;
    QProgressBar *m_importProgress = nullptr;
    QLabel *m_importStatusLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QToolButton *m_importButton = nullptr;
    QToolButton *m_reimportButton = nullptr;
    QToolButton *m_addPlaneButton = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_syncing = false;
    bool m_treeDirty = true;
    bool m_lastSceneIoActive = false;
    bool m_lastImportActive = false;
    uint64_t m_treeStructureSignature = 0;
    size_t m_sceneChangeListenerId = 0;
};
