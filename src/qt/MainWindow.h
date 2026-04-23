#pragma once

#include <QMainWindow>

#include <vector>

class DX12View;
class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QFrame;
class QTimer;
class QPushButton;
class QWidget;

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
    void startPreviewRender();
    void toggleQtUiVisibility();
    void updateSceneIoUi();

    DX12View *m_view;
    QAction *m_saveSceneAction = nullptr;
    QAction *m_saveSceneAsAction = nullptr;
    QAction *m_loadSceneAction = nullptr;
    QAction *m_previewRenderAction = nullptr;
    QProgressBar *m_sceneIoProgress = nullptr;
    QLabel *m_sceneIoLabel = nullptr;
    QLabel *m_statusStatsLabel = nullptr;
    QLabel *m_liveLinkLabel = nullptr;
    QLabel *m_liveLinkSummaryLabel = nullptr;
    QPlainTextEdit *m_liveLinkDiagnosticsView = nullptr;
    QComboBox *m_liveLinkProviderCombo = nullptr;
    QPushButton *m_liveLinkConnectButton = nullptr;
    QPushButton *m_liveLinkDisconnectButton = nullptr;
    QPushButton *m_liveLinkReconnectButton = nullptr;
    QPushButton *m_liveLinkTakeCameraButton = nullptr;
    QFrame *m_statusDivider = nullptr;
    QTimer *m_sceneIoTimer = nullptr;
    bool m_qtUiHidden = false;
    std::vector<QWidget *> m_hiddenQtUiWidgets;
};
