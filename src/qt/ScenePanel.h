#pragma once

#include <QWidget>

#include <cstddef>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
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
    QPushButton *m_importButton = nullptr;
    QPushButton *m_reimportButton = nullptr;
    QPushButton *m_addPlaneButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_syncing = false;
    bool m_refreshQueued = false;
    size_t m_sceneChangeListenerId = 0;
};
