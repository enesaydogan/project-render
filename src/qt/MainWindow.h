#pragma once

#include <QByteArray>
#include <QMainWindow>

#include <vector>

class DX12View;
class QAction;
class QActionGroup;
class QComboBox;
class QDragEnterEvent;
class QDockWidget;
class QDropEvent;
class QLabel;
class QMenu;
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
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createMenus();
    void createToolBar();
    void createDocks();
    void registerDockPanel(QDockWidget *dockWidget);
    void restoreDefaultDockLayout();
    void showAllDockPanels();
    void updateTransformUi();
    void startSaveScene();
    void startSaveSceneAs();
    void startLoadScene();
    void startPreviewRender();
    void showRenderPopup();
    void toggleQtUiVisibility();
    void updateSceneIoUi();

    DX12View *m_view;
    QAction *m_saveSceneAction = nullptr;
    QAction *m_saveSceneAsAction = nullptr;
    QAction *m_loadSceneAction = nullptr;
    QAction *m_previewRenderAction = nullptr;
    QAction *m_renderPopupAction = nullptr;
    QAction *m_importModelAction = nullptr;
    QAction *m_importHdrAction = nullptr;
    QAction *m_transformTranslateAction = nullptr;
    QAction *m_transformRotateAction = nullptr;
    QAction *m_transformScaleAction = nullptr;
    QAction *m_transformLocalAction = nullptr;
    QAction *m_transformWorldAction = nullptr;
    QActionGroup *m_transformOperationGroup = nullptr;
    QActionGroup *m_transformSpaceGroup = nullptr;
    QMenu *m_viewMenu = nullptr;
    QByteArray m_defaultDockState;
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
