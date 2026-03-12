#include "MainWindow.h"
#include "DX12View.h"
#include "RenderSettingsPanel.h"
#include "ScenePanel.h"
#include "../editor_ui.h"
#include "../file_import.h"
#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QIcon>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMenuBar>
#include <QProgressBar>
#include <QToolBar>
#include <QStatusBar>
#include <QTimer>

extern bool g_appClosing;
extern HWND g_hwnd;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Project Render"));
    resize(1920, 1080);
    {
        QString iconPath = QCoreApplication::applicationDirPath() +
                           QStringLiteral("/resources/app.ico");
        if (!QFileInfo::exists(iconPath)) {
            iconPath = QStringLiteral("resources/app.ico");
        }
        if (QFileInfo::exists(iconPath)) {
            setWindowIcon(QIcon(iconPath));
        }
    }

    // central widget will be the DX12 rendering view
    m_view = new DX12View(this);
    setCentralWidget(m_view);

    createMenus();
    createToolBar();
    createDocks();
    updateSceneIoUi();
    m_sceneIoTimer = new QTimer(this);
    connect(m_sceneIoTimer, &QTimer::timeout, this, [this]() {
        updateSceneIoUi();
    });
    m_sceneIoTimer->start(100);
    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow()
{
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_saveSceneAction = fileMenu->addAction(tr("Save Scene..."), this, [this]() {
        startSaveScene();
    });
    m_loadSceneAction = fileMenu->addAction(tr("Load Scene..."), this, [this]() {
        startLoadScene();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
}

void MainWindow::createToolBar()
{
    QToolBar *toolBar = addToolBar(tr("Main"));
    if (m_saveSceneAction) {
        toolBar->addAction(m_saveSceneAction);
    }
    if (m_loadSceneAction) {
        toolBar->addAction(m_loadSceneAction);
    }
    // toolbar actions will be added later
}

void MainWindow::createDocks()
{
    auto createDock = [this](const QString &title,
                             Qt::DockWidgetArea area,
                             const QString &text) {
        auto *dock = new QDockWidget(title, this);
        dock->setObjectName(title);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);

        auto *label = new QLabel(text, dock);
        label->setWordWrap(true);
        label->setMargin(12);
        dock->setWidget(label);

        addDockWidget(area, dock);
        return dock;
    };

    auto *sceneDock = new QDockWidget(tr("Scene"), this);
    sceneDock->setObjectName(tr("Scene"));
    sceneDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    sceneDock->setWidget(new ScenePanel(sceneDock));
    addDockWidget(Qt::LeftDockWidgetArea, sceneDock);
    QDockWidget *materialsDock = createDock(
        tr("Materials"), Qt::RightDockWidgetArea,
        tr("Material editor controls will live here."));
    auto *renderDock = new QDockWidget(tr("Render Settings"), this);
    renderDock->setObjectName(tr("Render Settings"));
    renderDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    renderDock->setWidget(new RenderSettingsPanel(renderDock));
    addDockWidget(Qt::RightDockWidgetArea, renderDock);
    QDockWidget *lightsDock = createDock(
        tr("Lights"), Qt::BottomDockWidgetArea,
        tr("Light list and parameters will live here."));

    splitDockWidget(materialsDock, renderDock, Qt::Vertical);
    tabifyDockWidget(sceneDock, lightsDock);
    sceneDock->raise();
}

void MainWindow::startSaveScene()
{
    if (IsSceneIoJobActive()) {
        return;
    }
    std::wstring chosen;
    HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
    if (!owner) {
        owner = g_hwnd;
    }
    if (SaveSceneFileDialog(owner, chosen)) {
        StartSceneIoJob(true, WStringToUtf8(chosen));
    }
}

void MainWindow::startLoadScene()
{
    if (IsSceneIoJobActive()) {
        return;
    }
    std::wstring chosen;
    HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
    if (!owner) {
        owner = g_hwnd;
    }
    if (OpenSceneFileDialog(owner, chosen)) {
        StartSceneIoJob(false, WStringToUtf8(chosen));
    }
}

void MainWindow::updateSceneIoUi()
{
    if (!m_sceneIoProgress) {
        m_sceneIoProgress = new QProgressBar(this);
        m_sceneIoProgress->setRange(0, 100);
        m_sceneIoProgress->setTextVisible(false);
        m_sceneIoProgress->setMinimumWidth(180);
        statusBar()->addPermanentWidget(m_sceneIoProgress);
        m_sceneIoProgress->hide();
    }
    if (!m_sceneIoLabel) {
        m_sceneIoLabel = new QLabel(this);
        m_sceneIoLabel->setMinimumWidth(260);
        statusBar()->addPermanentWidget(m_sceneIoLabel);
        m_sceneIoLabel->hide();
    }

    const bool active = IsSceneIoJobActive();
    if (m_saveSceneAction) {
        m_saveSceneAction->setEnabled(!active);
    }
    if (m_loadSceneAction) {
        m_loadSceneAction->setEnabled(!active);
    }

    if (!active) {
        m_sceneIoProgress->hide();
        m_sceneIoLabel->hide();
        return;
    }

    float progress = GetSceneIoProgress();
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    m_sceneIoProgress->setValue(static_cast<int>(progress * 100.0f + 0.5f));

    const QString title = IsSceneIoSaveJob() ? tr("Saving scene") : tr("Loading scene");
    const QString stage = QString::fromStdString(GetSceneIoStage());
    if (stage.isEmpty()) {
        m_sceneIoLabel->setText(title);
    } else {
        m_sceneIoLabel->setText(QString("%1: %2").arg(title, stage));
    }
    m_sceneIoProgress->show();
    m_sceneIoLabel->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    g_appClosing = true;
    QMainWindow::closeEvent(event);
}
