#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;

class ScenePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScenePanel(QWidget *parent = nullptr);

private:
    void createUi();
    void refreshSceneList();

    QListWidget *m_nodeList = nullptr;
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
};
