#include "MainWindow.h"
#include "CameraPanel.h"
#include "DX12View.h"
#include "EnvironmentPanel.h"
#include "LightsPanel.h"
#include "MaterialEditorPanel.h"
#include "RenderPanel.h"
#include "RenderSettingsPanel.h"
#include "ScenePanel.h"
#include "../dx12_context.h"
#include "../dxr_renderer.h"
#include "../editor_ui.h"
#include "../file_import.h"
#include "../scene.h"
#include "../streamline_manager.h"
#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QProgressBar>
#include <QScrollArea>
#include <QToolBar>
#include <QStatusBar>
#include <QTimer>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winreg.h>
#include <winver.h>

#pragma comment(lib, "Version.lib")

extern bool g_appClosing;
extern HWND g_hwnd;
extern RenderMode g_currentRenderMode;

namespace {

struct MemoryStats {
    double usedGb = 0.0;
    double totalGb = 0.0;
    bool valid = false;
};

double BytesToGb(UINT64 bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

MemoryStats ReadRamStats()
{
    MEMORYSTATUSEX statex = {};
    statex.dwLength = sizeof(statex);
    if (!GlobalMemoryStatusEx(&statex)) {
        return {};
    }
    MemoryStats stats;
    const UINT64 total = statex.ullTotalPhys;
    const UINT64 avail = statex.ullAvailPhys;
    stats.usedGb = BytesToGb(total - avail);
    stats.totalGb = BytesToGb(total);
    stats.valid = true;
    return stats;
}

MemoryStats ReadGpuMemoryStats()
{
    if (!DX12Context::g_device) {
        return {};
    }

    const LUID luid = DX12Context::g_device->GetAdapterLuid();
    ComPtr<IDXGIAdapter1> adapter;
    if (DX12Context::g_factory) {
        DX12Context::g_factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    }
    if (!adapter) {
        ComPtr<IDXGIFactory4> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
        }
    }
    if (!adapter) {
        return {};
    }

    ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(adapter.As(&adapter3)) || !adapter3) {
        return {};
    }

    MemoryStats stats;
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    HRESULT hr = adapter3->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (SUCCEEDED(hr) && info.Budget > 0) {
        stats.usedGb = BytesToGb(info.CurrentUsage);
        stats.totalGb = BytesToGb(info.Budget);
        stats.valid = true;
        return stats;
    }

    hr = adapter3->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info);
    if (SUCCEEDED(hr) && info.Budget > 0) {
        stats.usedGb = BytesToGb(info.CurrentUsage);
        stats.totalGb = BytesToGb(info.Budget);
        stats.valid = true;
    }

    return stats;
}

QString ReadGitHash()
{
    QProcess git;
    git.setProgram(QStringLiteral("git"));
    git.setArguments({QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")});
    git.setWorkingDirectory(QDir::currentPath());
    git.start();
    if (git.waitForFinished(1000) &&
        git.exitStatus() == QProcess::NormalExit &&
        git.exitCode() == 0) {
        const QString out = QString::fromLocal8Bit(git.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            return out;
        }
    }
    return QString();
}

QString ReadCpuName()
{
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString",
                     RRF_RT_REG_SZ,
                     nullptr,
                     buffer,
                     &size) == ERROR_SUCCESS) {
        return QString::fromWCharArray(buffer).trimmed();
    }
    return QString();
}

QString ReadTotalRam()
{
    MEMORYSTATUSEX statex = {};
    statex.dwLength = sizeof(statex);
    if (!GlobalMemoryStatusEx(&statex)) {
        return QString();
    }
    const double gb = static_cast<double>(statex.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    return QString::number(gb, 'f', 1) + QStringLiteral(" GB");
}

QString ReadVersionValue(const wchar_t *key)
{
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return QString();
    }

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(modulePath, &handle);
    if (size == 0) {
        return QString();
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(modulePath, 0, size, data.data())) {
        return QString();
    }

    struct LangAndCodePage {
        WORD language;
        WORD codePage;
    };
    LangAndCodePage *translation = nullptr;
    UINT translationBytes = 0;
    if (VerQueryValueW(data.data(),
                       L"\\VarFileInfo\\Translation",
                       reinterpret_cast<void **>(&translation),
                       &translationBytes) &&
        translationBytes >= sizeof(LangAndCodePage)) {
        wchar_t subBlock[64] = {};
        swprintf_s(subBlock,
                   L"\\StringFileInfo\\%04x%04x\\%s",
                   translation[0].language,
                   translation[0].codePage,
                   key);
        wchar_t *value = nullptr;
        UINT valueBytes = 0;
        if (VerQueryValueW(data.data(),
                           subBlock,
                           reinterpret_cast<void **>(&value),
                           &valueBytes) &&
            value && valueBytes > 0) {
            return QString::fromWCharArray(value).trimmed();
        }
    }

    wchar_t fallbackBlock[64] = {};
    swprintf_s(fallbackBlock,
               L"\\StringFileInfo\\040904B0\\%s",
               key);
    wchar_t *fallbackValue = nullptr;
    UINT fallbackBytes = 0;
    if (VerQueryValueW(data.data(),
                       fallbackBlock,
                       reinterpret_cast<void **>(&fallbackValue),
                       &fallbackBytes) &&
        fallbackValue && fallbackBytes > 0) {
        return QString::fromWCharArray(fallbackValue).trimmed();
    }

    return QString();
}

QString ReadCompanyName()
{
    return ReadVersionValue(L"CompanyName");
}

QString ReadAppVersion()
{
    QString version = ReadVersionValue(L"ProductVersion");
    if (version.isEmpty()) {
        version = ReadVersionValue(L"FileVersion");
    }
    return version;
}

QString ReadGpuName()
{
    if (!DX12Context::g_device) {
        return QString();
    }
    const LUID luid = DX12Context::g_device->GetAdapterLuid();
    ComPtr<IDXGIAdapter1> adapter;
    if (DX12Context::g_factory) {
        DX12Context::g_factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    }
    if (!adapter) {
        ComPtr<IDXGIFactory4> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
        }
    }
    if (!adapter) {
        return QString();
    }
    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(adapter->GetDesc1(&desc))) {
        return QString();
    }
    QString name = QString::fromWCharArray(desc.Description).trimmed();
    const double vramGb = static_cast<double>(desc.DedicatedVideoMemory) /
                          (1024.0 * 1024.0 * 1024.0);
    if (vramGb > 0.1) {
        name += QStringLiteral(" (%1 GB)").arg(vramGb, 0, 'f', 1);
    }
    return name;
}

} // namespace

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

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this]() {
        QString version = ReadAppVersion();
        if (version.isEmpty()) {
#ifdef APP_VERSION
            version = QStringLiteral(APP_VERSION);
#else
            version = QStringLiteral("dev");
#endif
        }
        const QString gitHash = ReadGitHash();
        const QString gitSuffix = gitHash.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" (%1)").arg(gitHash);
        const QString cpuName = ReadCpuName();
        SYSTEM_INFO sysInfo = {};
        GetNativeSystemInfo(&sysInfo);
        QString cpuLine = cpuName.isEmpty() ? tr("Unknown") : cpuName;
        if (sysInfo.dwNumberOfProcessors > 0) {
            cpuLine += QStringLiteral(" (%1 threads)")
                           .arg(static_cast<int>(sysInfo.dwNumberOfProcessors));
        }
        const QString ramValue = ReadTotalRam();
        const QString ramLine = ramValue.isEmpty() ? tr("Unknown") : ramValue;
        const QString gpuValue = ReadGpuName();
        const QString gpuLine = gpuValue.isEmpty() ? tr("Unknown") : gpuValue;
        const QString companyValue = ReadCompanyName();
        const QString companyLine =
            companyValue.isEmpty() ? tr("Company: Unknown") : tr("Company: %1").arg(companyValue);

        QDialog dialog(this);
        dialog.setWindowTitle(tr("About Project Render"));
        dialog.setModal(true);

        auto *mainLayout = new QVBoxLayout(&dialog);
        auto *title = new QLabel(tr("Project Render"), &dialog);
        title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
        mainLayout->addWidget(title);

        auto *versionLabel =
            new QLabel(tr("Version %1%2").arg(version, gitSuffix), &dialog);
        mainLayout->addWidget(versionLabel);

        auto *hardwareLabel =
            new QLabel(tr("CPU: %1\nGPU: %2\nRAM: %3").arg(cpuLine, gpuLine, ramLine), &dialog);
        hardwareLabel->setWordWrap(true);
        mainLayout->addWidget(hardwareLabel);

        auto *bottomRow = new QHBoxLayout();
        auto *companyLabel = new QLabel(companyLine, &dialog);
        bottomRow->addWidget(companyLabel);
        bottomRow->addStretch(1);
        auto *okButton = new QPushButton(tr("OK"), &dialog);
        connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        bottomRow->addWidget(okButton);
        mainLayout->addLayout(bottomRow);

        dialog.exec();
    });
}

void MainWindow::createToolBar()
{
    addToolBar(tr("Main"));
    // toolbar actions will be added later
}

void MainWindow::createDocks()
{
    auto wrapScroll = [](QWidget *content, QWidget *parent) {
        auto *scroll = new QScrollArea(parent);
        scroll->setWidget(content);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        return scroll;
    };

    auto *sceneDock = new QDockWidget(tr("Scene"), this);
    sceneDock->setObjectName(tr("Scene"));
    sceneDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    sceneDock->setWidget(wrapScroll(new ScenePanel(sceneDock), sceneDock));
    addDockWidget(Qt::LeftDockWidgetArea, sceneDock);
    auto *materialsDock = new QDockWidget(tr("Materials"), this);
    materialsDock->setObjectName(tr("Materials"));
    materialsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    materialsDock->setWidget(wrapScroll(new MaterialEditorPanel(materialsDock), materialsDock));
    addDockWidget(Qt::LeftDockWidgetArea, materialsDock);
    auto *renderDock = new QDockWidget(tr("Render Settings"), this);
    renderDock->setObjectName(tr("Render Settings"));
    renderDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    {
        auto *renderPanel = new RenderSettingsPanel(renderDock);
        renderDock->setWidget(wrapScroll(renderPanel, renderDock));
    }
    addDockWidget(Qt::RightDockWidgetArea, renderDock);
    auto *renderExportDock = new QDockWidget(tr("Render"), this);
    renderExportDock->setObjectName(tr("Render"));
    renderExportDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    renderExportDock->setWidget(wrapScroll(new RenderPanel(renderExportDock), renderExportDock));
    addDockWidget(Qt::RightDockWidgetArea, renderExportDock);
    auto *environmentDock = new QDockWidget(tr("Environment"), this);
    environmentDock->setObjectName(tr("Environment"));
    environmentDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    environmentDock->setWidget(wrapScroll(new EnvironmentPanel(environmentDock), environmentDock));
    addDockWidget(Qt::RightDockWidgetArea, environmentDock);
    auto *cameraDock = new QDockWidget(tr("Camera"), this);
    cameraDock->setObjectName(tr("Camera"));
    cameraDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    cameraDock->setWidget(wrapScroll(new CameraPanel(cameraDock), cameraDock));
    addDockWidget(Qt::RightDockWidgetArea, cameraDock);
    auto *lightsDock = new QDockWidget(tr("Lights"), this);
    lightsDock->setObjectName(tr("Lights"));
    lightsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    lightsDock->setWidget(wrapScroll(new LightsPanel(lightsDock), lightsDock));
    addDockWidget(Qt::BottomDockWidgetArea, lightsDock);

    tabifyDockWidget(sceneDock, materialsDock);
    tabifyDockWidget(sceneDock, lightsDock);
    sceneDock->raise();
    tabifyDockWidget(renderDock, renderExportDock);
    renderDock->raise();
    splitDockWidget(renderDock, environmentDock, Qt::Vertical);
    tabifyDockWidget(environmentDock, cameraDock);
    environmentDock->raise();
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
        statusBar()->addWidget(m_sceneIoProgress);
        m_sceneIoProgress->hide();
    }
    if (!m_sceneIoLabel) {
        m_sceneIoLabel = new QLabel(this);
        m_sceneIoLabel->setMinimumWidth(260);
        statusBar()->addWidget(m_sceneIoLabel);
        m_sceneIoLabel->hide();
    }
    if (!m_statusDivider) {
        m_statusDivider = new QFrame(this);
        m_statusDivider->setFrameShape(QFrame::VLine);
        m_statusDivider->setFrameShadow(QFrame::Sunken);
        statusBar()->addPermanentWidget(m_statusDivider);
    }
    if (!m_statusStatsLabel) {
        m_statusStatsLabel = new QLabel(this);
        m_statusStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusBar()->addPermanentWidget(m_statusStatsLabel);
    }

    {
        QStringList parts;
        const bool dxrMode = (g_currentRenderMode == RenderMode::DXR);
        if (dxrMode) {
            parts << tr("SPP %1 (%2)")
                         .arg(DxrRenderer::GetDisplayedSampleCount())
                         .arg(DxrRenderer::CanIdleWithoutRendering()
                                  ? tr("Idle")
                                  : tr("Rendering"));
            const float gpuMs = DxrRenderer::GetGPUFrameTimeMs();
            if (gpuMs > 0.01f) {
                parts << tr("GPU %1 ms").arg(QString::number(gpuMs, 'f', 2));
            } else {
                parts << tr("GPU n/a");
            }
        } else {
            parts << tr("SPP -");
            parts << tr("GPU -");
        }

        const UINT outW = DX12Context::g_windowWidth;
        const UINT outH = DX12Context::g_windowHeight;
        if (outW > 0 && outH > 0) {
            const auto rec =
                DX12Context::g_streamline.GetRecommendedRenderSize(outW, outH);
            parts << tr("Res %1x%2 -> %3x%4")
                         .arg(rec.renderWidth)
                         .arg(rec.renderHeight)
                         .arg(outW)
                         .arg(outH);
        } else {
            parts << tr("Res n/a");
        }

        const MemoryStats vram = ReadGpuMemoryStats();
        if (vram.valid && vram.totalGb > 0.1) {
            parts << tr("VRAM %1/%2 GB")
                         .arg(vram.usedGb, 0, 'f', 1)
                         .arg(vram.totalGb, 0, 'f', 1);
        } else {
            parts << tr("VRAM n/a");
        }

        const MemoryStats ram = ReadRamStats();
        if (ram.valid && ram.totalGb > 0.1) {
            parts << tr("RAM %1/%2 GB")
                         .arg(ram.usedGb, 0, 'f', 1)
                         .arg(ram.totalGb, 0, 'f', 1);
        } else {
            parts << tr("RAM n/a");
        }

        m_statusStatsLabel->setText(parts.join(QStringLiteral(" | ")));
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
