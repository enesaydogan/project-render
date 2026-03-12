#include "MainWindow.h"
#include "DX12View.h"
#include "RenderModePanel.h"
#include "RenderSettingsPanel.h"
#include "ScenePanel.h"
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>

extern bool g_appClosing;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Project Render"));
    resize(1920, 1080);

    // central widget will be the DX12 rendering view
    m_view = new DX12View(this);
    setCentralWidget(m_view);

    createMenus();
    createToolBar();
    createDocks();
    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow()
{
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
}

void MainWindow::createToolBar()
{
    addToolBar(tr("Main"));
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
    auto *renderModeDock = new QDockWidget(tr("Render Mode"), this);
    renderModeDock->setObjectName(tr("Render Mode"));
    renderModeDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    renderModeDock->setWidget(new RenderModePanel(renderModeDock));
    addDockWidget(Qt::RightDockWidgetArea, renderModeDock);
    QDockWidget *lightsDock = createDock(
        tr("Lights"), Qt::BottomDockWidgetArea,
        tr("Light list and parameters will live here."));

    splitDockWidget(materialsDock, renderDock, Qt::Vertical);
    splitDockWidget(renderDock, renderModeDock, Qt::Vertical);
    tabifyDockWidget(sceneDock, lightsDock);
    sceneDock->raise();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    g_appClosing = true;
    QMainWindow::closeEvent(event);
}
