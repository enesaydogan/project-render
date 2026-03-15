#pragma once

#include <QMainWindow>

class DX12View;
class QAction;
class QLabel;
class QProgressBar;
class QFrame;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // access to the central DX12 rendering widget
    DX12View *view() const { return m_view; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createToolBar();
    void createDocks();
    void startSaveScene();
    void startSaveSceneAs();
    void startLoadScene();
    void updateSceneIoUi();

    DX12View *m_view;
    QAction *m_saveSceneAction = nullptr;
    QAction *m_saveSceneAsAction = nullptr;
    QAction *m_loadSceneAction = nullptr;
    QProgressBar *m_sceneIoProgress = nullptr;
    QLabel *m_sceneIoLabel = nullptr;
    QLabel *m_statusStatsLabel = nullptr;
    QFrame *m_statusDivider = nullptr;
    QTimer *m_sceneIoTimer = nullptr;
};
