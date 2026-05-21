#include "MainWindow.h"
#include "AnimationPanel.h"
#include "CameraPanel.h"
#include "DX12View.h"
#include "EnvironmentPanel.h"
#include "LightsPanel.h"
#include "MaterialEditorPanel.h"
#include "RenderPanel.h"
#include "RenderSettingsPanel.h"
#include "ScatterPanel.h"
#include "ScenePanel.h"
#include "ViewsPanel.h"
#include "../animation_sequence.h"
#include "../d3d12_helpers.h"
#include "../dx12_context.h"
#include "../dxr_renderer.h"
#include "../editor_ui.h"
#include "../file_import.h"
#include "../livelink/livelink_runtime.h"
#include "../livelink/livelink_scene_sync.h"
#include "../material/material_system.h"
#include "../saved_views.h"
#include "../scene.h"
#include "../streamline_manager.h"
#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QKeySequence>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QProgressBar>
#include <QShortcut>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
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
extern DescriptorHeapAllocator g_cbvSrvAllocator;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;
extern UINT g_textureDescriptorCount;
extern UINT g_textureDescriptorCapacity;

namespace {

enum class ToolbarIcon {
    Save,
    SaveAs,
    Open,
    Render,
    Teapot,
    ImportModel,
    ImportHdr,
    Undo,
    Redo,
    Translate,
    Rotate,
    Scale,
    Mirror,
    Local,
    World,
};

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

UINT64 ResourceAllocationBytes(ID3D12Resource *resource)
{
    if (!DX12Context::g_device || !resource) {
        return 0;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info =
        DX12Context::g_device->GetResourceAllocationInfo(0, 1, &desc);
    return static_cast<UINT64>(info.SizeInBytes);
}

UINT64 AlignTo(UINT64 value, UINT64 alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

QString FormatBytes(UINT64 bytes)
{
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        return QObject::tr("%1 GB").arg(gb, 0, 'f', 2);
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        return QObject::tr("%1 MB").arg(mb, 0, 'f', 1);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb >= 1.0) {
        return QObject::tr("%1 KB").arg(kb, 0, 'f', 1);
    }
    return QObject::tr("%1 B").arg(static_cast<qulonglong>(bytes));
}

QString FormatDxgiFormat(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return QStringLiteral("RGBA8");
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return QStringLiteral("RGBA8 sRGB");
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return QStringLiteral("RGBA32F");
    case DXGI_FORMAT_BC1_UNORM:
        return QStringLiteral("BC1");
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return QStringLiteral("BC1 sRGB");
    case DXGI_FORMAT_BC3_UNORM:
        return QStringLiteral("BC3");
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return QStringLiteral("BC3 sRGB");
    case DXGI_FORMAT_BC4_UNORM:
        return QStringLiteral("BC4");
    case DXGI_FORMAT_BC4_SNORM:
        return QStringLiteral("BC4 SNORM");
    case DXGI_FORMAT_BC5_UNORM:
        return QStringLiteral("BC5");
    case DXGI_FORMAT_BC6H_UF16:
        return QStringLiteral("BC6H");
    case DXGI_FORMAT_BC7_UNORM:
        return QStringLiteral("BC7");
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return QStringLiteral("BC7 sRGB");
    default:
        return QObject::tr("DXGI %1").arg(static_cast<unsigned>(format));
    }
}

struct SceneMemoryBreakdown {
    UINT64 textureBytes = 0;
    UINT64 meshBytes = 0;
    UINT64 materialPayloadBytes = 0;
    UINT64 materialReservedBytes = 0;
    UINT64 meshMappingPayloadBytes = 0;
    UINT64 descriptorRecordBytes = 0;
    UINT64 globalDescriptorHeapBytes = 0;
    UINT64 sceneAssetBytes = 0;
    UINT64 sceneWithAccelerationBytes = 0;
    UINT64 accountedRuntimeBytes = 0;
    size_t textureCount = 0;
    size_t hiddenTextureCount = 0;
    size_t meshCount = 0;
    size_t materialCount = 0;
    UINT64 vertexCount = 0;
    UINT64 triangleCount = 0;
    UINT activeDescriptorRecords = 0;
};

SceneMemoryBreakdown BuildSceneMemoryBreakdown(
    const DxrRenderer::GpuMemoryBreakdown &dxrBreakdown)
{
    SceneMemoryBreakdown breakdown = {};
    breakdown.textureCount = g_loadedTextures.size();
    breakdown.meshCount = g_loadedMeshes.size();
    breakdown.materialCount = g_loadedMaterials.size();

    for (const Asset::Texture &texture : g_loadedTextures) {
        breakdown.textureBytes += ResourceAllocationBytes(texture.resource.Get());
        if (texture.hiddenInEditor) {
            ++breakdown.hiddenTextureCount;
        }
    }

    for (const Asset::GpuMesh &mesh : g_loadedMeshes) {
        breakdown.meshBytes += ResourceAllocationBytes(mesh.vertexBuffer.Get());
        breakdown.meshBytes += ResourceAllocationBytes(mesh.indexBuffer.Get());
        breakdown.vertexCount += mesh.vertexCount;
        breakdown.triangleCount += mesh.indexCount / 3;
    }

    using RasterMaterial = MaterialSystem::RuntimeRasterMaterialConstants;
    using DxrMaterial = MaterialSystem::RuntimeDxrMaterialData;
    using DxrMaterialExtra = MaterialSystem::RuntimeDxrMaterialExtraData;
    const UINT64 materialCount = static_cast<UINT64>(g_loadedMaterials.size());
    constexpr UINT64 kMaterialReserveCount = 16384;
    const UINT64 rasterStride = AlignTo(sizeof(RasterMaterial), 256);
    breakdown.materialPayloadBytes =
        materialCount * (rasterStride + sizeof(DxrMaterial) + sizeof(DxrMaterialExtra));
    breakdown.materialReservedBytes =
        kMaterialReserveCount *
        (rasterStride + sizeof(DxrMaterial) + sizeof(DxrMaterialExtra));
    breakdown.meshMappingPayloadBytes =
        static_cast<UINT64>(g_loadedMeshes.size()) * sizeof(int) * 4;

    const UINT descriptorSize = g_cbvSrvAllocator.DescriptorSize();
    const UINT meshSrvRecords =
        static_cast<UINT>((std::min)(g_loadedMeshes.size(),
                                     static_cast<size_t>(
                                         (std::numeric_limits<UINT>::max)() / 2))) *
        2u;
    breakdown.activeDescriptorRecords =
        g_textureDescriptorCount + meshSrvRecords + dxrBreakdown.descriptorCount;
    breakdown.descriptorRecordBytes =
        static_cast<UINT64>(breakdown.activeDescriptorRecords) * descriptorSize;
    breakdown.globalDescriptorHeapBytes =
        static_cast<UINT64>(g_cbvSrvAllocator.Capacity()) * descriptorSize;

    breakdown.sceneAssetBytes = breakdown.textureBytes + breakdown.meshBytes +
                                breakdown.materialPayloadBytes +
                                breakdown.meshMappingPayloadBytes;
    breakdown.sceneWithAccelerationBytes =
        breakdown.sceneAssetBytes + dxrBreakdown.accelerationStructureBytes;
    breakdown.accountedRuntimeBytes = breakdown.sceneAssetBytes +
                                      dxrBreakdown.totalBytes +
                                      breakdown.globalDescriptorHeapBytes;
    return breakdown;
}

QString FormatLiveLinkSyncStats(const LiveLink::CoordinatorStats& coordinatorStats,
                                const LiveLink::LiveLinkSceneSync::StatsSnapshot& syncStats,
                                size_t sessionCount)
{
    return QObject::tr("DCC Sync nodes=%1 meshes=%2 lights=%3 materials=%4 bindings=%5 active=%6 sessions=%7 camera=%8 env=%9 | Transport accepted=%10/%11 rejected=%12/%13 queued=%14/%15 | Mesh apply count=%16 load=%17ms replace=%18ms last=%19/%20ms bytes=%21 | GPU upload batches=%22 last=%23ms/%24 meshes total=%25ms")
        .arg(static_cast<qulonglong>(syncStats.nodeCount))
        .arg(static_cast<qulonglong>(syncStats.meshCount))
        .arg(static_cast<qulonglong>(syncStats.lightCount))
        .arg(static_cast<qulonglong>(syncStats.materialCount))
        .arg(static_cast<qulonglong>(syncStats.totalBindingCount))
        .arg(static_cast<qulonglong>(syncStats.activeSessionBindingCount))
        .arg(static_cast<qulonglong>(sessionCount))
        .arg(syncStats.cameraBound ? QObject::tr("yes") : QObject::tr("no"))
        .arg(syncStats.environmentBound ? QObject::tr("yes") : QObject::tr("no"))
        .arg(static_cast<qulonglong>(coordinatorStats.batchesAccepted))
        .arg(static_cast<qulonglong>(coordinatorStats.deltasAccepted))
        .arg(static_cast<qulonglong>(coordinatorStats.batchesRejected))
        .arg(static_cast<qulonglong>(coordinatorStats.deltasRejected))
        .arg(static_cast<qulonglong>(coordinatorStats.queuedBatchCount))
        .arg(static_cast<qulonglong>(coordinatorStats.queuedDeltaCount))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadApplyCount))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadTotalLoadMs))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadTotalReplaceMs))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadLastLoadMs))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadLastReplaceMs))
        .arg(static_cast<qulonglong>(syncStats.meshPayloadLastBytes))
        .arg(static_cast<qulonglong>(syncStats.gpuUploadBatchCount))
        .arg(static_cast<qulonglong>(syncStats.gpuUploadLastMs))
        .arg(static_cast<qulonglong>(syncStats.gpuUploadLastMeshCount))
        .arg(static_cast<qulonglong>(syncStats.gpuUploadTotalMs));
}

QString FormatLiveLinkProviderDisplayName(const std::string &providerName)
{
    if (providerName == "3dsMax2025Pipe" ||
        providerName == "3dsMax2024Pipe") {
        return QObject::tr("3ds Max");
    }
    if (providerName == "Archicad28Pipe") {
        return QObject::tr("Archicad");
    }
    if (providerName == "MockLiveLink") {
        return QObject::tr("Mock");
    }
    return QString::fromStdString(providerName);
}

QString FormatCompactLiveLinkStatus(
    const std::vector<LiveLink::ProviderSnapshot> &providers)
{
    if (providers.empty()) {
        return QObject::tr("LiveLink: None");
    }

    QStringList connectedProviders;
    for (const auto &provider : providers) {
        if (provider.connectionState == LiveLink::ConnectionState::Connected) {
            connectedProviders << FormatLiveLinkProviderDisplayName(
                provider.providerName);
        }
    }

    if (connectedProviders.isEmpty()) {
        return QObject::tr("LiveLink: None");
    }

    return QObject::tr("LiveLink: %1 Connected")
        .arg(connectedProviders.join(QStringLiteral(", ")));
}

QString FormatDurationCompact(double seconds)
{
    const int rounded = static_cast<int>(std::max(0.0, seconds) + 0.5);
    const int hours = rounded / 3600;
    const int minutes = (rounded / 60) % 60;
    const int secs = rounded % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

double SecondsSinceTick(qulonglong startedTickMs)
{
    if (startedTickMs == 0) {
        return 0.0;
    }
    const qulonglong now = GetTickCount64();
    return now >= startedTickMs
        ? static_cast<double>(now - startedTickMs) / 1000.0
        : 0.0;
}

double CurrentRenderItemProgress()
{
    if (!g_renderExportJob.active || g_renderExportJob.targetMaxSpp <= 0) {
        return 0.0;
    }
    double progress =
        static_cast<double>(DxrRenderer::GetDisplayedSampleCount()) /
        static_cast<double>(std::max(1, g_renderExportJob.targetMaxSpp));
    progress = std::clamp(progress, 0.0, 1.0);
    if (g_renderExportJob.completionArmed) {
        const int settleTotal = g_renderExportJob.targetDenoiserIndex == 0 ? 1 : 3;
        const double settleProgress =
            1.0 - static_cast<double>(std::max(0, g_renderExportJob.settleFramesRemaining)) /
                      static_cast<double>(std::max(1, settleTotal));
        progress = 0.96 + std::clamp(settleProgress, 0.0, 1.0) * 0.04;
    }
    return std::clamp(progress, 0.0, 0.995);
}

QString BasenameForDisplay(const std::wstring &path)
{
    if (path.empty()) {
        return {};
    }
    return QFileInfo(QString::fromStdWString(path)).fileName();
}

void CancelActiveExport()
{
    if (g_renderAnimationExport.active) {
        CancelAnimationRenderExport();
        return;
    }
    if (g_renderBatchExport.active) {
        CancelBatchRenderExport();
        return;
    }
    if (g_renderExportJob.active) {
        g_renderExportStatus = g_renderExportJob.isPreview
            ? "Preview canceled."
            : "Render canceled.";
        RestoreRenderExportState();
    }
}

QIcon MakeToolbarIcon(ToolbarIcon icon,
                      QColor line = QColor(205, 205, 205),
                      QColor accent = QColor(69, 196, 238))
{
    constexpr int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(line, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen accentPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case ToolbarIcon::Save:
        painter.drawRoundedRect(QRectF(8, 6, 16, 20), 2, 2);
        painter.drawLine(QPointF(11, 10), QPointF(21, 10));
        painter.drawLine(QPointF(12, 20), QPointF(20, 20));
        painter.drawLine(QPointF(12, 23), QPointF(20, 23));
        break;
    case ToolbarIcon::SaveAs:
        painter.drawRoundedRect(QRectF(7, 6, 15, 19), 2, 2);
        painter.drawLine(QPointF(10, 10), QPointF(19, 10));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(20, 21), QPointF(26, 15));
        painter.drawLine(QPointF(23, 14), QPointF(27, 18));
        break;
    case ToolbarIcon::Open:
        painter.drawPath([&]() {
            QPainterPath path;
            path.moveTo(5, 12);
            path.lineTo(13, 12);
            path.lineTo(15, 9);
            path.lineTo(23, 9);
            path.lineTo(27, 13);
            path.lineTo(24, 24);
            path.lineTo(7, 24);
            path.closeSubpath();
            return path;
        }());
        painter.setPen(accentPen);
        painter.drawLine(QPointF(10, 18), QPointF(22, 18));
        break;
    case ToolbarIcon::Render:
        painter.setPen(accentPen);
        painter.drawEllipse(QRectF(8, 8, 16, 16));
        painter.drawLine(QPointF(14, 12), QPointF(21, 16));
        painter.drawLine(QPointF(21, 16), QPointF(14, 20));
        break;
    case ToolbarIcon::Teapot:
        painter.setPen(accentPen);
        painter.drawPath([&]() {
            QPainterPath body;
            body.moveTo(9, 14);
            body.cubicTo(10, 10, 20, 10, 21, 14);
            body.lineTo(20, 22);
            body.cubicTo(19, 25, 11, 25, 10, 22);
            body.closeSubpath();
            return body;
        }());
        painter.setPen(pen);
        painter.drawLine(QPointF(13, 10), QPointF(18, 10));
        painter.drawArc(QRectF(19, 14, 8, 7), 275 * 16, 215 * 16);
        painter.drawPath([&]() {
            QPainterPath spout;
            spout.moveTo(9, 16);
            spout.cubicTo(5, 15, 4, 12, 3, 12);
            spout.cubicTo(5, 17, 7, 19, 10, 19);
            return spout;
        }());
        painter.setPen(accentPen);
        painter.drawLine(QPointF(12, 24), QPointF(19, 24));
        break;
    case ToolbarIcon::ImportModel:
        painter.drawLine(QPointF(16, 6), QPointF(25, 11));
        painter.drawLine(QPointF(16, 6), QPointF(7, 11));
        painter.drawLine(QPointF(7, 11), QPointF(16, 16));
        painter.drawLine(QPointF(25, 11), QPointF(16, 16));
        painter.drawLine(QPointF(7, 11), QPointF(7, 21));
        painter.drawLine(QPointF(25, 11), QPointF(25, 21));
        painter.drawLine(QPointF(16, 16), QPointF(16, 26));
        painter.drawLine(QPointF(7, 21), QPointF(16, 26));
        painter.drawLine(QPointF(25, 21), QPointF(16, 26));
        break;
    case ToolbarIcon::ImportHdr:
        painter.setPen(accentPen);
        painter.drawEllipse(QRectF(11, 11, 10, 10));
        painter.setPen(pen);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265358979323846 / 4.0;
            const QPointF p0(16 + std::cos(a) * 8.0, 16 + std::sin(a) * 8.0);
            const QPointF p1(16 + std::cos(a) * 12.0, 16 + std::sin(a) * 12.0);
            painter.drawLine(p0, p1);
        }
        break;
    case ToolbarIcon::Undo:
        painter.setPen(accentPen);
        painter.drawArc(QRectF(7, 8, 18, 16), 35 * 16, 285 * 16);
        painter.drawLine(QPointF(8, 15), QPointF(5, 9));
        painter.drawLine(QPointF(8, 15), QPointF(14, 14));
        break;
    case ToolbarIcon::Redo:
        painter.setPen(accentPen);
        painter.drawArc(QRectF(7, 8, 18, 16), 220 * 16, 285 * 16);
        painter.drawLine(QPointF(24, 15), QPointF(27, 9));
        painter.drawLine(QPointF(24, 15), QPointF(18, 14));
        break;
    case ToolbarIcon::Translate:
        painter.setPen(accentPen);
        painter.drawLine(QPointF(16, 6), QPointF(16, 26));
        painter.drawLine(QPointF(6, 16), QPointF(26, 16));
        painter.drawLine(QPointF(16, 6), QPointF(12, 10));
        painter.drawLine(QPointF(16, 6), QPointF(20, 10));
        painter.drawLine(QPointF(26, 16), QPointF(22, 12));
        painter.drawLine(QPointF(26, 16), QPointF(22, 20));
        break;
    case ToolbarIcon::Rotate:
        painter.setPen(accentPen);
        painter.drawArc(QRectF(7, 7, 18, 18), 30 * 16, 285 * 16);
        painter.drawLine(QPointF(23, 8), QPointF(26, 15));
        painter.drawLine(QPointF(23, 8), QPointF(16, 8));
        break;
    case ToolbarIcon::Scale:
        painter.drawRect(QRectF(9, 9, 14, 14));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(14, 18), QPointF(23, 9));
        painter.drawLine(QPointF(23, 9), QPointF(23, 15));
        painter.drawLine(QPointF(23, 9), QPointF(17, 9));
        break;
    case ToolbarIcon::Mirror:
        painter.drawLine(QPointF(16, 5), QPointF(16, 27));
        painter.setPen(QPen(line, 1.2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(4, 16), QPointF(28, 16));
        painter.setPen(pen);
        painter.drawPolygon(QPolygonF({
            QPointF(6, 9), QPointF(13, 12), QPointF(13, 20), QPointF(6, 23)
        }));
        painter.setPen(accentPen);
        painter.drawPolygon(QPolygonF({
            QPointF(26, 9), QPointF(19, 12), QPointF(19, 20), QPointF(26, 23)
        }));
        break;
    case ToolbarIcon::Local:
        painter.setPen(accentPen);
        painter.drawLine(QPointF(10, 23), QPointF(22, 11));
        painter.drawLine(QPointF(10, 23), QPointF(10, 10));
        painter.drawLine(QPointF(10, 23), QPointF(24, 23));
        painter.setPen(pen);
        painter.drawEllipse(QRectF(7, 20, 6, 6));
        break;
    case ToolbarIcon::World:
        painter.drawEllipse(QRectF(7, 7, 18, 18));
        painter.drawLine(QPointF(16, 7), QPointF(16, 25));
        painter.drawLine(QPointF(7, 16), QPointF(25, 16));
        painter.setPen(accentPen);
        painter.drawArc(QRectF(10, 7, 12, 18), 90 * 16, 180 * 16);
        painter.drawArc(QRectF(10, 7, 12, 18), -90 * 16, 180 * 16);
        break;
    }

    return QIcon(pixmap);
}

void ConfigureToolbarAction(QAction *action, const QString &text,
                            const QString &tooltip, ToolbarIcon icon)
{
    if (!action) {
        return;
    }
    action->setText(text);
    action->setToolTip(tooltip);
    action->setIcon(MakeToolbarIcon(icon));
}

Scene::MirrorPivot MirrorPivotFromIndex(int index)
{
    switch (index) {
    case 1:
        return Scene::MirrorPivot::WorldOrigin;
    case 2:
        return Scene::MirrorPivot::ActiveNode;
    case 0:
    default:
        return Scene::MirrorPivot::SelectionCenter;
    }
}

Scene::MirrorSpace MirrorSpaceFromIndex(int index)
{
    switch (index) {
    case 1:
        return Scene::MirrorSpace::World;
    case 2:
        return Scene::MirrorSpace::Local;
    case 0:
    default:
        return Scene::GetGizmoSpace() == Scene::GizmoSpace::Local
                   ? Scene::MirrorSpace::Local
                   : Scene::MirrorSpace::World;
    }
}

QString MirrorAxisLabel(Scene::MirrorAxis axis)
{
    switch (axis) {
    case Scene::MirrorAxis::X:
        return QObject::tr("X");
    case Scene::MirrorAxis::Y:
        return QObject::tr("Y");
    case Scene::MirrorAxis::Z:
        return QObject::tr("Z");
    }
    return QString();
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

bool IsSupportedDroppedModelPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("skp") ||
           suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb") ||
           suffix == QStringLiteral("obj") ||
           suffix == QStringLiteral("stl") ||
           suffix == QStringLiteral("fbx") ||
           suffix == QStringLiteral("ltm") ||
           suffix == QStringLiteral("lmod");
}

QString FirstSupportedDroppedModelPath(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return {};
    }
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (IsSupportedDroppedModelPath(path)) {
            return path;
        }
    }
    return {};
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
    setAcceptDrops(true);
    setDocumentMode(true);
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
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

    m_view = new DX12View(this);
    setCentralWidget(m_view);

    createMenus();
    createToolBar();
    createDocks();
    auto *toggleQtUiShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    toggleQtUiShortcut->setContext(Qt::ApplicationShortcut);
    connect(toggleQtUiShortcut, &QShortcut::activated, this, [this]() {
        toggleQtUiVisibility();
    });
    updateSceneIoUi();
    m_sceneIoTimer = new QTimer(this);
    connect(m_sceneIoTimer, &QTimer::timeout, this, [this]() {
        updateSceneIoUi();
        updateRenderExportProgressUi();
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
    m_saveSceneAction = fileMenu->addAction(tr("Save Scene"), this, [this]() {
        startSaveScene();
    });
    m_saveSceneAction->setShortcut(QKeySequence::Save);
    m_saveSceneAction->setShortcutContext(Qt::ApplicationShortcut);
    m_saveSceneAsAction = fileMenu->addAction(tr("Save Scene As..."), this, [this]() {
        startSaveSceneAs();
    });
    m_saveSceneAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveSceneAsAction->setShortcutContext(Qt::ApplicationShortcut);
    m_loadSceneAction = fileMenu->addAction(tr("Load Scene..."), this, [this]() {
        startLoadScene();
    });
    m_loadSceneAction->setShortcut(QKeySequence::Open);
    m_loadSceneAction->setShortcutContext(Qt::ApplicationShortcut);
    fileMenu->addSeparator();
    m_importModelAction = fileMenu->addAction(tr("Import Model..."), this, []() {
        HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
        if (!owner) {
            owner = g_hwnd;
        }
        Scene::ImportModelWithDialog(owner);
    });
    m_importHdrAction = fileMenu->addAction(tr("Import HDR..."), this, []() {
        HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
        if (!owner) {
            owner = g_hwnd;
        }
        Scene::ImportHDRWithDialog(owner);
    });
    
    fileMenu->addSeparator();
    m_previewRenderAction = fileMenu->addAction(tr("Preview Render"), this, [this]() {
        startPreviewRender();
    });
    m_previewRenderAction->setShortcut(QKeySequence(Qt::Key_F2));
    m_previewRenderAction->setShortcutContext(Qt::ApplicationShortcut);
    m_renderPopupAction = new QAction(this);
    connect(m_renderPopupAction, &QAction::triggered, this, [this]() {
        showRenderPopup();
    });
    
    m_exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_undoTransformAction =
        editMenu->addAction(tr("Undo Transform"), this, [this]() {
            if (Scene::UndoTransform()) {
                statusBar()->showMessage(tr("Undo transform"), 2000);
            }
            updateSceneIoUi();
        });
    m_undoTransformAction->setShortcut(QKeySequence::Undo);
    m_undoTransformAction->setShortcutContext(Qt::ApplicationShortcut);
    m_redoTransformAction =
        editMenu->addAction(tr("Redo Transform"), this, [this]() {
            if (Scene::RedoTransform()) {
                statusBar()->showMessage(tr("Redo transform"), 2000);
            }
            updateSceneIoUi();
        });
    m_redoTransformAction->setShortcut(QKeySequence::Redo);
    m_redoTransformAction->setShortcutContext(Qt::ApplicationShortcut);
    editMenu->addSeparator();

    m_transformOperationGroup = new QActionGroup(this);
    m_transformOperationGroup->setExclusive(true);
    m_transformTranslateAction =
        editMenu->addAction(tr("Translate"), this, []() {
            Scene::SetGizmoOperation(Scene::GizmoOperation::Translate);
        });
    m_transformRotateAction =
        editMenu->addAction(tr("Rotate"), this, []() {
            Scene::SetGizmoOperation(Scene::GizmoOperation::Rotate);
        });
    m_transformScaleAction =
        editMenu->addAction(tr("Scale"), this, []() {
            Scene::SetGizmoOperation(Scene::GizmoOperation::Scale);
        });
    for (QAction *action : {m_transformTranslateAction, m_transformRotateAction,
                            m_transformScaleAction}) {
        action->setCheckable(true);
        m_transformOperationGroup->addAction(action);
    }

    editMenu->addSeparator();
    m_transformSpaceGroup = new QActionGroup(this);
    m_transformSpaceGroup->setExclusive(true);
    m_transformLocalAction =
        editMenu->addAction(tr("Local Space"), this, []() {
            Scene::SetGizmoSpace(Scene::GizmoSpace::Local);
        });
    m_transformWorldAction =
        editMenu->addAction(tr("World Space"), this, []() {
            Scene::SetGizmoSpace(Scene::GizmoSpace::World);
        });
    for (QAction *action : {m_transformLocalAction, m_transformWorldAction}) {
        action->setCheckable(true);
        m_transformSpaceGroup->addAction(action);
    }

    editMenu->addSeparator();
    m_mirrorMenu = editMenu->addMenu(tr("Mirror"));
    m_mirrorMenu->setIcon(MakeToolbarIcon(ToolbarIcon::Mirror));
    auto triggerMirror = [this](Scene::MirrorAxis axis) {
        const Scene::MirrorPivot pivot = MirrorPivotFromIndex(m_mirrorPivot);
        const Scene::MirrorSpace space = MirrorSpaceFromIndex(m_mirrorSpace);
        if (Scene::MirrorSelectedNodes(axis, pivot, space)) {
            statusBar()->showMessage(
                tr("Mirrored selection on %1").arg(MirrorAxisLabel(axis)), 2000);
        }
        updateTransformUi();
    };
    m_mirrorXAction = m_mirrorMenu->addAction(tr("Flip X"), this, [triggerMirror]() {
        triggerMirror(Scene::MirrorAxis::X);
    });
    m_mirrorXAction->setToolTip(tr("Mirror across the YZ plane"));
    m_mirrorYAction = m_mirrorMenu->addAction(tr("Flip Y"), this, [triggerMirror]() {
        triggerMirror(Scene::MirrorAxis::Y);
    });
    m_mirrorYAction->setToolTip(tr("Mirror across the XZ plane"));
    m_mirrorZAction = m_mirrorMenu->addAction(tr("Flip Z"), this, [triggerMirror]() {
        triggerMirror(Scene::MirrorAxis::Z);
    });
    m_mirrorZAction->setToolTip(tr("Mirror across the XY plane"));
    m_mirrorMenu->addSeparator();
    QMenu *pivotMenu = m_mirrorMenu->addMenu(tr("Pivot"));
    m_mirrorPivotGroup = new QActionGroup(this);
    m_mirrorPivotGroup->setExclusive(true);
    m_mirrorPivotSelectionAction = pivotMenu->addAction(tr("Selection Center"), this, [this]() {
        m_mirrorPivot = 0;
    });
    m_mirrorPivotWorldAction = pivotMenu->addAction(tr("World Origin"), this, [this]() {
        m_mirrorPivot = 1;
    });
    m_mirrorPivotActiveAction = pivotMenu->addAction(tr("Active Node"), this, [this]() {
        m_mirrorPivot = 2;
    });
    for (QAction *action : {m_mirrorPivotSelectionAction, m_mirrorPivotWorldAction,
                            m_mirrorPivotActiveAction}) {
        action->setCheckable(true);
        m_mirrorPivotGroup->addAction(action);
    }
    m_mirrorPivotSelectionAction->setChecked(true);

    QMenu *spaceMenu = m_mirrorMenu->addMenu(tr("Space"));
    m_mirrorSpaceGroup = new QActionGroup(this);
    m_mirrorSpaceGroup->setExclusive(true);
    m_mirrorSpaceCurrentAction = spaceMenu->addAction(tr("Current Gizmo Space"), this, [this]() {
        m_mirrorSpace = 0;
    });
    m_mirrorSpaceWorldAction = spaceMenu->addAction(tr("World"), this, [this]() {
        m_mirrorSpace = 1;
    });
    m_mirrorSpaceLocalAction = spaceMenu->addAction(tr("Local"), this, [this]() {
        m_mirrorSpace = 2;
    });
    for (QAction *action : {m_mirrorSpaceCurrentAction, m_mirrorSpaceWorldAction,
                            m_mirrorSpaceLocalAction}) {
        action->setCheckable(true);
        m_mirrorSpaceGroup->addAction(action);
    }
    m_mirrorSpaceCurrentAction->setChecked(true);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(tr("Toggle UI (F1)"), this, [this]() {
        toggleQtUiVisibility();
    });
    m_viewMenu->addAction(tr("Show All Panels"), this, [this]() {
        showAllDockPanels();
    });
    m_viewMenu->addAction(tr("Restore Default Panel Layout"), this, [this]() {
        restoreDefaultDockLayout();
    });
    m_viewMenu->addSeparator();

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("VRAM Breakdown..."), this, [this]() {
        showMemoryBreakdownPopup();
    });
    toolsMenu->addAction(tr("Clean Orphaned Data..."), this, [this]() {
        std::string result = Scene::CleanOrphanedData();
        statusBar()->showMessage(QString::fromStdString(result), 5000);
    });

    QMenu *renderMenu = menuBar()->addMenu(tr("&Render"));
    renderMenu->addAction(m_renderPopupAction);
    renderMenu->addAction(m_previewRenderAction);

    QMenu *liveLinkMenu = menuBar()->addMenu(tr("&Live Link"));
    liveLinkMenu->addAction(tr("Show LiveLink Panel"), this, [this]() {
        for (QDockWidget *dockWidget : findChildren<QDockWidget *>()) {
            if (dockWidget && dockWidget->objectName() == tr("LiveLink")) {
                dockWidget->show();
                dockWidget->raise();
                break;
            }
        }
    });

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
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(tr("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(22, 22));

    ConfigureToolbarAction(m_saveSceneAction,
                           tr("Save Scene"),
                           tr("Save scene (Ctrl+S)"),
                           ToolbarIcon::Save);
    ConfigureToolbarAction(m_saveSceneAsAction,
                           tr("Save Scene As"),
                           tr("Save scene as (Ctrl+Shift+S)"),
                           ToolbarIcon::SaveAs);
    ConfigureToolbarAction(m_loadSceneAction,
                           tr("Load Scene"),
                           tr("Load scene (Ctrl+O)"),
                           ToolbarIcon::Open);
    ConfigureToolbarAction(m_previewRenderAction,
                           tr("Preview Render"),
                           tr("Preview render (F2)"),
                           ToolbarIcon::Render);
    ConfigureToolbarAction(m_renderPopupAction,
                           tr("Render..."),
                           tr("Render settings and export"),
                           ToolbarIcon::Teapot);
    ConfigureToolbarAction(m_importModelAction,
                           tr("Import Model"),
                           tr("Import model"),
                           ToolbarIcon::ImportModel);
    ConfigureToolbarAction(m_importHdrAction,
                           tr("Import HDR"),
                           tr("Import HDR environment"),
                           ToolbarIcon::ImportHdr);
    ConfigureToolbarAction(m_undoTransformAction,
                           tr("Undo Transform"),
                           tr("Undo transform (Ctrl+Z)"),
                           ToolbarIcon::Undo);
    ConfigureToolbarAction(m_redoTransformAction,
                           tr("Redo Transform"),
                           tr("Redo transform (Ctrl+Y)"),
                           ToolbarIcon::Redo);
    ConfigureToolbarAction(m_transformTranslateAction,
                           tr("Translate"),
                           tr("Translate gizmo (G)"),
                           ToolbarIcon::Translate);
    ConfigureToolbarAction(m_transformRotateAction,
                           tr("Rotate"),
                           tr("Rotate gizmo (R)"),
                           ToolbarIcon::Rotate);
    ConfigureToolbarAction(m_transformScaleAction,
                           tr("Scale"),
                           tr("Scale gizmo (T)"),
                           ToolbarIcon::Scale);
    ConfigureToolbarAction(m_transformLocalAction,
                           tr("Local"),
                           tr("Local transform space (L toggles in viewport)"),
                           ToolbarIcon::Local);
    ConfigureToolbarAction(m_transformWorldAction,
                           tr("World"),
                           tr("World transform space (L toggles in viewport)"),
                           ToolbarIcon::World);

    if (m_saveSceneAction) {
        toolbar->addAction(m_saveSceneAction);
    }
    if (m_saveSceneAsAction) {
        toolbar->addAction(m_saveSceneAsAction);
    }
    if (m_loadSceneAction) {
        toolbar->addAction(m_loadSceneAction);
    }
    toolbar->addSeparator();
    if (m_importModelAction) {
        toolbar->addAction(m_importModelAction);
    }
    if (m_importHdrAction) {
        toolbar->addAction(m_importHdrAction);
    }
    toolbar->addSeparator();
    if (m_undoTransformAction) {
        toolbar->addAction(m_undoTransformAction);
    }
    if (m_redoTransformAction) {
        toolbar->addAction(m_redoTransformAction);
    }
    toolbar->addSeparator();
    if (m_transformTranslateAction) {
        toolbar->addAction(m_transformTranslateAction);
    }
    if (m_transformRotateAction) {
        toolbar->addAction(m_transformRotateAction);
    }
    if (m_transformScaleAction) {
        toolbar->addAction(m_transformScaleAction);
    }
    if (m_mirrorMenu) {
        auto *mirrorButton = new QToolButton(toolbar);
        mirrorButton->setToolTip(tr("Mirror selected nodes"));
        mirrorButton->setStatusTip(tr("Mirror selected nodes"));
        mirrorButton->setIcon(MakeToolbarIcon(ToolbarIcon::Mirror));
        mirrorButton->setIconSize(QSize(22, 22));
        mirrorButton->setMenu(m_mirrorMenu);
        mirrorButton->setPopupMode(QToolButton::InstantPopup);
        mirrorButton->setAutoRaise(false);
        toolbar->addWidget(mirrorButton);
    }
    toolbar->addSeparator();
    if (m_transformLocalAction) {
        toolbar->addAction(m_transformLocalAction);
    }
    if (m_transformWorldAction) {
        toolbar->addAction(m_transformWorldAction);
    }
    auto *debugViewCombo = new QComboBox(toolbar);
    debugViewCombo->setObjectName(QStringLiteral("ToolbarCombo"));
    debugViewCombo->addItem(tr("Beauty"), 0);
    debugViewCombo->addItem(tr("Albedo"), 1);
    debugViewCombo->addItem(tr("Normal"), 2);
    debugViewCombo->addItem(tr("Emissive"), 3);
    debugViewCombo->addItem(tr("Roughness"), 4);
    debugViewCombo->addItem(tr("AO"), 7);
    debugViewCombo->setToolTip(tr("Viewport display pass"));
    debugViewCombo->setMinimumWidth(112);
    connect(debugViewCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [debugViewCombo]() {
                g_debugMode = debugViewCombo->currentData().toInt();
                DxrRenderer::ResetAccumulation();
            });
    toolbar->addWidget(debugViewCombo);

    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    if (m_renderPopupAction) {
        toolbar->addAction(m_renderPopupAction);
    }

    updateTransformUi();
}

void MainWindow::updateTransformUi()
{
    const Scene::GizmoOperation operation = Scene::GetGizmoOperation();
    if (m_transformTranslateAction) {
        m_transformTranslateAction->setChecked(
            operation == Scene::GizmoOperation::Translate);
    }
    if (m_transformRotateAction) {
        m_transformRotateAction->setChecked(
            operation == Scene::GizmoOperation::Rotate);
    }
    if (m_transformScaleAction) {
        m_transformScaleAction->setChecked(
            operation == Scene::GizmoOperation::Scale);
    }

    const Scene::GizmoSpace space = Scene::GetGizmoSpace();
    if (m_transformLocalAction) {
        m_transformLocalAction->setChecked(space == Scene::GizmoSpace::Local);
    }
    if (m_transformWorldAction) {
        m_transformWorldAction->setChecked(space == Scene::GizmoSpace::World);
    }
    if (m_undoTransformAction) {
        m_undoTransformAction->setEnabled(Scene::CanUndoTransform());
    }
    if (m_redoTransformAction) {
        m_redoTransformAction->setEnabled(Scene::CanRedoTransform());
    }
    const bool hasSelectedNodes = !Scene::GetSelectedNodeIndices().empty();
    for (QAction *action : {m_mirrorXAction, m_mirrorYAction, m_mirrorZAction}) {
        if (action) {
            action->setEnabled(hasSelectedNodes);
        }
    }
}

void MainWindow::registerDockPanel(QDockWidget *dockWidget)
{
    if (!dockWidget) {
        return;
    }

    dockWidget->setAllowedAreas(Qt::AllDockWidgetAreas);
    dockWidget->setFeatures(QDockWidget::DockWidgetClosable |
                            QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable);

    if (m_viewMenu) {
        QAction *toggleAction = dockWidget->toggleViewAction();
        toggleAction->setText(dockWidget->windowTitle());
        m_viewMenu->addAction(toggleAction);
    }
}

void MainWindow::showAllDockPanels()
{
    const auto dockWidgets = findChildren<QDockWidget *>();
    for (QDockWidget *dockWidget : dockWidgets) {
        if (!dockWidget) {
            continue;
        }
        dockWidget->show();
        dockWidget->raise();
    }
}

void MainWindow::restoreDefaultDockLayout()
{
    if (m_defaultDockState.isEmpty()) {
        showAllDockPanels();
        return;
    }
    restoreState(m_defaultDockState);
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
    sceneDock->setWidget(wrapScroll(new ScenePanel(sceneDock), sceneDock));
    registerDockPanel(sceneDock);
    addDockWidget(Qt::RightDockWidgetArea, sceneDock);
    auto *materialsDock = new QDockWidget(tr("Materials"), this);
    materialsDock->setObjectName(tr("Materials"));
    materialsDock->setWidget(wrapScroll(new MaterialEditorPanel(materialsDock), materialsDock));
    registerDockPanel(materialsDock);
    addDockWidget(Qt::RightDockWidgetArea, materialsDock);
    auto *scatterDock = new QDockWidget(tr("Scatter"), this);
    scatterDock->setObjectName(tr("Scatter"));
    scatterDock->setWidget(wrapScroll(new ScatterPanel(scatterDock), scatterDock));
    registerDockPanel(scatterDock);
    addDockWidget(Qt::RightDockWidgetArea, scatterDock);
    auto *renderDock = new QDockWidget(tr("Render Settings"), this);
    renderDock->setObjectName(tr("Render Settings"));
    {
        auto *renderPanel = new RenderSettingsPanel(renderDock);
        renderDock->setWidget(wrapScroll(renderPanel, renderDock));
    }
    registerDockPanel(renderDock);
    addDockWidget(Qt::LeftDockWidgetArea, renderDock);
    auto *renderExportDock = new QDockWidget(tr("Render"), this);
    renderExportDock->setObjectName(tr("Render"));
    renderExportDock->setWidget(wrapScroll(new RenderPanel(renderExportDock), renderExportDock));
    registerDockPanel(renderExportDock);
    addDockWidget(Qt::RightDockWidgetArea, renderExportDock);
    auto *environmentDock = new QDockWidget(tr("Environment"), this);
    environmentDock->setObjectName(tr("Environment"));
    environmentDock->setWidget(wrapScroll(new EnvironmentPanel(environmentDock), environmentDock));
    registerDockPanel(environmentDock);
    addDockWidget(Qt::LeftDockWidgetArea, environmentDock);
    auto *cameraDock = new QDockWidget(tr("Camera"), this);
    cameraDock->setObjectName(tr("Camera"));
    cameraDock->setWidget(wrapScroll(new CameraPanel(cameraDock), cameraDock));
    registerDockPanel(cameraDock);
    addDockWidget(Qt::LeftDockWidgetArea, cameraDock);
    auto *viewsDock = new QDockWidget(tr("Views"), this);
    viewsDock->setWindowTitle(tr("Camera Lister"));
    viewsDock->setObjectName(tr("Camera Lister"));
    viewsDock->setWidget(wrapScroll(new ViewsPanel(viewsDock), viewsDock));
    registerDockPanel(viewsDock);
    addDockWidget(Qt::LeftDockWidgetArea, viewsDock);
    auto *lightsDock = new QDockWidget(tr("Lights"), this);
    lightsDock->setObjectName(tr("Lights"));
    lightsDock->setWidget(wrapScroll(new LightsPanel(lightsDock), lightsDock));
    registerDockPanel(lightsDock);
    addDockWidget(Qt::RightDockWidgetArea, lightsDock);
    auto *animationDock = new QDockWidget(tr("Animation"), this);
    animationDock->setObjectName(tr("Animation"));
    auto *animationPanel = new AnimationPanel(animationDock);
    animationPanel->setMinimumHeight(170);
    animationDock->setWidget(animationPanel);
    registerDockPanel(animationDock);
    addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    auto *liveLinkDock = new QDockWidget(tr("LiveLink"), this);
    liveLinkDock->setObjectName(tr("LiveLink"));
    {
        auto *panel = new QWidget(liveLinkDock);
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->setSpacing(6);

        m_liveLinkProviderCombo = new QComboBox(panel);
        buttonRow->addWidget(m_liveLinkProviderCombo, 1);

        m_liveLinkConnectButton = new QPushButton(tr("Enable"), panel);
        connect(m_liveLinkConnectButton, &QPushButton::clicked, this, [this]() {
            if (!m_liveLinkProviderCombo) {
                return;
            }
            const QVariant providerIdValue = m_liveLinkProviderCombo->currentData();
            if (!providerIdValue.isValid()) {
                return;
            }
            auto &coordinator = LiveLink::GetCoordinator();
            coordinator.ConnectProvider(providerIdValue.toULongLong());
        });
        buttonRow->addWidget(m_liveLinkConnectButton);

        m_liveLinkDisconnectButton = new QPushButton(tr("Disable"), panel);
        connect(m_liveLinkDisconnectButton, &QPushButton::clicked, this, [this]() {
            if (!m_liveLinkProviderCombo) {
                return;
            }
            const QVariant providerIdValue = m_liveLinkProviderCombo->currentData();
            if (!providerIdValue.isValid()) {
                return;
            }
            auto &coordinator = LiveLink::GetCoordinator();
            coordinator.DisconnectProvider(providerIdValue.toULongLong());
        });
        buttonRow->addWidget(m_liveLinkDisconnectButton);

        m_liveLinkReconnectButton = new QPushButton(tr("Reconnect"), panel);
        connect(m_liveLinkReconnectButton, &QPushButton::clicked, this, [this]() {
            if (!m_liveLinkProviderCombo) {
                return;
            }
            const QVariant providerIdValue = m_liveLinkProviderCombo->currentData();
            if (!providerIdValue.isValid()) {
                return;
            }
            const auto providerId = providerIdValue.toULongLong();
            auto &coordinator = LiveLink::GetCoordinator();
            coordinator.DisconnectProvider(providerId);
            coordinator.ConnectProvider(providerId);
        });
        buttonRow->addWidget(m_liveLinkReconnectButton);

        m_liveLinkTakeCameraButton = new QPushButton(tr("Take Camera"), panel);
        connect(m_liveLinkTakeCameraButton, &QPushButton::clicked, this, []() {
            auto &sceneSync = LiveLink::GetSceneSync();
            if (sceneSync.IsCameraControlDetached()) {
                sceneSync.ResumeCameraControl();
            } else {
                sceneSync.DetachCameraControl();
            }
        });
        buttonRow->addWidget(m_liveLinkTakeCameraButton);

        buttonRow->addStretch(1);
        layout->addLayout(buttonRow);

        m_liveLinkSummaryLabel = new QLabel(panel);
        m_liveLinkSummaryLabel->setWordWrap(true);
        m_liveLinkSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_liveLinkSummaryLabel);

        m_liveLinkDiagnosticsView = new QPlainTextEdit(panel);
        m_liveLinkDiagnosticsView->setReadOnly(true);
        m_liveLinkDiagnosticsView->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_liveLinkDiagnosticsView->setMinimumHeight(160);
        layout->addWidget(m_liveLinkDiagnosticsView, 1);

        liveLinkDock->setWidget(panel);
    }
    registerDockPanel(liveLinkDock);
    addDockWidget(Qt::LeftDockWidgetArea, liveLinkDock);

    tabifyDockWidget(sceneDock, materialsDock);
    tabifyDockWidget(sceneDock, lightsDock);
    tabifyDockWidget(sceneDock, scatterDock);
    sceneDock->raise();

    tabifyDockWidget(environmentDock, cameraDock);
    tabifyDockWidget(environmentDock, renderDock);
    tabifyDockWidget(environmentDock, liveLinkDock);
    splitDockWidget(environmentDock, viewsDock, Qt::Vertical);
    environmentDock->raise();

    animationDock->raise();

    QTimer::singleShot(0, this, [this, sceneDock, renderExportDock, environmentDock, viewsDock, animationDock]() {
        resizeDocks({environmentDock, sceneDock},
                    {415, 420},
                    Qt::Horizontal);
        resizeDocks({environmentDock, viewsDock},
                    {640, 210},
                    Qt::Vertical);
        resizeDocks({animationDock},
                    {220},
                    Qt::Vertical);
        environmentDock->raise();
        sceneDock->raise();
        renderExportDock->hide();
        animationDock->hide();
        m_defaultDockState = saveState();
    });
}

void MainWindow::startSaveScene()
{
    if (IsSceneIoJobActive()) {
        return;
    }
    if (HasCurrentScenePath()) {
        StartSceneIoJob(true, GetCurrentScenePath());
        return;
    }
    startSaveSceneAs();
}

void MainWindow::startSaveSceneAs()
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

void MainWindow::startPreviewRender()
{
    StartPreviewRenderJob();
}

void MainWindow::showRenderPopup()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Render"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);
    dialog.setObjectName(QStringLiteral("RenderPopup"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *commonGroup = new QGroupBox(tr("Common settings"), &dialog);
    auto *commonLayout = new QHBoxLayout(commonGroup);
    auto *stillMode = new QRadioButton(tr("Still image"), commonGroup);
    auto *sequenceMode = new QRadioButton(tr("Sequence"), commonGroup);
    stillMode->setChecked(true);
    commonLayout->addWidget(stillMode);
    commonLayout->addWidget(sequenceMode);
    layout->addWidget(commonGroup);

    auto *resolutionGroup = new QGroupBox(tr("Resolution"), &dialog);
    auto *resolutionForm = new QFormLayout(resolutionGroup);
    auto *resolutionPreset = new QComboBox(resolutionGroup);
    for (int i = 0; i < g_renderResolutionPresetCount; ++i) {
        resolutionPreset->addItem(QString::fromUtf8(g_renderResolutionPresets[i].label));
    }
    resolutionPreset->setCurrentIndex(std::clamp(g_renderExportSettings.resolutionPreset,
                                                 0,
                                                 std::max(0, g_renderResolutionPresetCount - 1)));
    auto *resolutionInfo = new QLabel(resolutionGroup);
    resolutionForm->addRow(tr("Preset"), resolutionPreset);
    resolutionForm->addRow(tr("Output"), resolutionInfo);
    layout->addWidget(resolutionGroup);

    auto *stillGroup = new QGroupBox(tr("Render"), &dialog);
    auto *stillForm = new QFormLayout(stillGroup);
    auto *qualityPreset = new QComboBox(stillGroup);
    qualityPreset->addItem(tr("Custom"));
    auto *samples = new QSpinBox(stillGroup);
    samples->setRange(16, 4096);
    samples->setValue(std::clamp(g_renderExportSettings.maxSpp, 16, 4096));
    auto *noise = new QDoubleSpinBox(stillGroup);
    noise->setRange(0.1, 30.0);
    noise->setDecimals(2);
    noise->setSingleStep(0.1);
    noise->setSuffix(tr(" %"));
    noise->setValue(std::clamp(static_cast<double>(g_renderExportSettings.noisePercent),
                               0.1,
                               30.0));
    auto *denoiser = new QComboBox(stillGroup);
    denoiser->addItems({tr("Off"), tr("OIDN (CPU)"), tr("OIDN (GPU)"), tr("OptiX")});
    denoiser->setCurrentIndex(std::clamp(g_renderExportSettings.denoiserIndex, 0, 3));
    auto *batchSavedViews = new QCheckBox(tr("Render saved camera lister views"), stillGroup);
    batchSavedViews->setChecked(g_renderExportSettings.batchSavedViews);
    auto *batchBaseName = new QLineEdit(stillGroup);
    batchBaseName->setText(QString::fromUtf8(g_renderExportSettings.batchBaseName.c_str()));
    stillForm->addRow(tr("Quality preset"), qualityPreset);
    stillForm->addRow(tr("Samples"), samples);
    stillForm->addRow(tr("Noise stop"), noise);
    stillForm->addRow(tr("Denoiser"), denoiser);
    stillForm->addRow(QString(), batchSavedViews);
    stillForm->addRow(tr("Batch base name"), batchBaseName);
    layout->addWidget(stillGroup);

    auto *sequenceGroup = new QGroupBox(tr("Sequence"), &dialog);
    auto *sequenceForm = new QFormLayout(sequenceGroup);
    const auto animationSettings = AnimationSequence::GetExportSettings();
    auto *sequenceFps = new QSpinBox(sequenceGroup);
    sequenceFps->setRange(1, 240);
    sequenceFps->setValue(animationSettings.fps);
    auto *sequenceSamples = new QSpinBox(sequenceGroup);
    sequenceSamples->setRange(1, 4096);
    sequenceSamples->setValue(animationSettings.maxSpp);
    auto *sequenceOutput = new QComboBox(sequenceGroup);
    for (int index = 0; index < AnimationSequence::GetExportModeCount(); ++index) {
        sequenceOutput->addItem(QString::fromUtf8(AnimationSequence::GetExportModeLabel(index)));
    }
    sequenceOutput->setCurrentIndex(std::clamp(animationSettings.exportMode,
                                               0,
                                               std::max(0, AnimationSequence::GetExportModeCount() - 1)));
    auto *sequenceBaseName = new QLineEdit(sequenceGroup);
    sequenceBaseName->setText(QString::fromUtf8(animationSettings.baseName.c_str()));
    auto *sequenceInfo = new QLabel(sequenceGroup);
    sequenceInfo->setWordWrap(true);
    sequenceForm->addRow(tr("FPS"), sequenceFps);
    sequenceForm->addRow(tr("Samples"), sequenceSamples);
    sequenceForm->addRow(tr("Output"), sequenceOutput);
    sequenceForm->addRow(tr("Base name"), sequenceBaseName);
    sequenceForm->addRow(tr("Camera path"), sequenceInfo);
    layout->addWidget(sequenceGroup);

    auto *statusLabel = new QLabel(&dialog);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *buttonRow = new QHBoxLayout();
    auto *previewButton = new QPushButton(tr("Preview"), &dialog);
    auto *renderButton = new QPushButton(tr("Render..."), &dialog);
    auto *cancelActiveButton = new QPushButton(tr("Cancel Active Render"), &dialog);
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    buttonRow->addWidget(previewButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelActiveButton);
    buttonRow->addWidget(closeButton);
    buttonRow->addWidget(renderButton);
    layout->addLayout(buttonRow);

    auto applyStillSettings = [&]() {
        g_renderExportSettings.resolutionPreset = resolutionPreset->currentIndex();
        g_renderExportSettings.maxSpp = samples->value();
        g_renderExportSettings.noisePercent = static_cast<float>(noise->value());
        g_renderExportSettings.denoiserIndex = denoiser->currentIndex();
        g_renderExportSettings.batchSavedViews = batchSavedViews->isChecked();
        g_renderExportSettings.batchBaseName =
            batchBaseName->text().trimmed().toUtf8().constData();
        if (g_renderExportSettings.batchBaseName.empty()) {
            g_renderExportSettings.batchBaseName = "final";
        }
    };

    auto applySequenceSettings = [&]() {
        AnimationSequence::ExportSettings settings =
            AnimationSequence::GetExportSettings();
        settings.resolutionPreset = resolutionPreset->currentIndex();
        settings.fps = sequenceFps->value();
        settings.maxSpp = sequenceSamples->value();
        settings.exportMode = sequenceOutput->currentIndex();
        settings.baseName = sequenceBaseName->text().trimmed().toUtf8().constData();
        if (settings.baseName.empty()) {
            settings.baseName = "final";
        }
        AnimationSequence::SetExportSettings(settings);
    };

    auto updateSummary = [&]() {
        const int presetIndex = std::clamp(resolutionPreset->currentIndex(),
                                           0,
                                           std::max(0, g_renderResolutionPresetCount - 1));
        if (g_renderResolutionPresetCount > 0) {
            const RenderResolutionPreset &preset = g_renderResolutionPresets[presetIndex];
            resolutionInfo->setText(tr("%1 x %2").arg(preset.width).arg(preset.height));
        }
        const int keyframeCount =
            static_cast<int>(AnimationSequence::GetKeyframes().size());
        sequenceInfo->setText(tr("%1 keys | %2 frames @ %3 fps")
                                  .arg(keyframeCount)
                                  .arg(AnimationSequence::GetTotalFrameCount(sequenceFps->value()))
                                  .arg(sequenceFps->value()));
        stillGroup->setEnabled(stillMode->isChecked());
        sequenceGroup->setEnabled(sequenceMode->isChecked());
        batchBaseName->setEnabled(batchSavedViews->isChecked());
        previewButton->setEnabled(stillMode->isChecked() && g_rayTracingSupported &&
                                  !IsRenderExportActive());
        renderButton->setEnabled(g_rayTracingSupported &&
                                 !IsRenderExportActive() &&
                                 (stillMode->isChecked() || keyframeCount > 0));
        renderButton->setText(sequenceMode->isChecked()
                                  ? tr("Render Sequence...")
                                  : (batchSavedViews->isChecked()
                                         ? tr("Render Saved Views...")
                                         : tr("Render Image...")));
        cancelActiveButton->setEnabled(IsRenderExportActive());
        if (!g_rayTracingSupported) {
            statusLabel->setText(tr("DXR is not supported on this device."));
        } else if (sequenceMode->isChecked() && keyframeCount == 0) {
            statusLabel->setText(tr("Create camera path keys before rendering a sequence."));
        } else if (!g_renderExportStatus.empty()) {
            statusLabel->setText(QString::fromUtf8(g_renderExportStatus.c_str()));
        } else {
            statusLabel->setText(tr("Ready."));
        }
    };

    connect(stillMode, &QRadioButton::toggled, &dialog, updateSummary);
    connect(sequenceMode, &QRadioButton::toggled, &dialog, updateSummary);
    connect(resolutionPreset, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, updateSummary);
    connect(sequenceFps, qOverload<int>(&QSpinBox::valueChanged),
            &dialog, updateSummary);
    connect(batchSavedViews, &QCheckBox::toggled, &dialog, updateSummary);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(previewButton, &QPushButton::clicked, &dialog, [&]() {
        applyStillSettings();
        StartPreviewRenderJob();
        updateSummary();
    });
    connect(cancelActiveButton, &QPushButton::clicked, &dialog, [&]() {
        CancelActiveExport();
        updateSummary();
    });
    connect(renderButton, &QPushButton::clicked, &dialog, [&]() {
        if (sequenceMode->isChecked()) {
            applySequenceSettings();
            const int exportMode = sequenceOutput->currentIndex();
            const QString outputDir = QFileDialog::getExistingDirectory(
                &dialog,
                exportMode == static_cast<int>(AnimationSequence::ExportMode::Mp4)
                    ? tr("Choose Output Folder For MP4 Export")
                    : tr("Choose Output Folder For Frame Export"),
                QString(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!outputDir.isEmpty()) {
                StartAnimationRenderExport(outputDir.toStdWString());
                dialog.accept();
            }
            return;
        }

        applyStillSettings();
        if (g_renderExportSettings.batchSavedViews) {
            const QString outputDir = QFileDialog::getExistingDirectory(
                &dialog,
                tr("Choose Output Folder For Saved Views"),
                QString(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!outputDir.isEmpty()) {
                StartBatchRenderExportJobs(
                    outputDir.toStdWString(),
                    QString::fromUtf8(g_renderExportSettings.batchBaseName.c_str()).toStdWString());
                dialog.accept();
            }
        } else {
            std::wstring chosenPath;
            if (SaveRenderImageFileDialog(g_hwnd, chosenPath)) {
                StartRenderExportJob(chosenPath);
                dialog.accept();
            }
        }
    });

    updateSummary();
    dialog.exec();
}

void MainWindow::showMemoryBreakdownPopup()
{
    const DxrRenderer::GpuMemoryBreakdown dxrBreakdown =
        DxrRenderer::GetGpuMemoryBreakdown();
    const SceneMemoryBreakdown sceneBreakdown =
        BuildSceneMemoryBreakdown(dxrBreakdown);
    const MemoryStats adapterMemory = ReadGpuMemoryStats();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("VRAM Breakdown"));
    dialog.setModal(true);
    dialog.resize(780, 520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    const QString adapterText =
        (adapterMemory.valid && adapterMemory.totalGb > 0.1)
            ? tr("Adapter current usage: %1 / %2 GB")
                  .arg(adapterMemory.usedGb, 0, 'f', 2)
                  .arg(adapterMemory.totalGb, 0, 'f', 2)
            : tr("Adapter current usage: n/a");
    summary->setText(
        tr("%1\nScene assets + DXR acceleration structures: %2\nAccounted renderer/runtime allocation: %3")
            .arg(adapterText,
                 FormatBytes(sceneBreakdown.sceneWithAccelerationBytes),
                 FormatBytes(sceneBreakdown.accountedRuntimeBytes)));
    layout->addWidget(summary);

    auto *tabs = new QTabWidget(&dialog);
    auto *breakdownPage = new QWidget(tabs);
    auto *breakdownLayout = new QVBoxLayout(breakdownPage);
    breakdownLayout->setContentsMargins(0, 0, 0, 0);
    breakdownLayout->setSpacing(0);

    auto *table = new QTableWidget(breakdownPage);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({tr("Category"), tr("Details"), tr("Memory")});
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);

    auto addRow = [&](const QString &category, const QString &details,
                      UINT64 bytes) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *categoryItem = new QTableWidgetItem(category);
        auto *detailsItem = new QTableWidgetItem(details);
        auto *bytesItem = new QTableWidgetItem(FormatBytes(bytes));
        bytesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 0, categoryItem);
        table->setItem(row, 1, detailsItem);
        table->setItem(row, 2, bytesItem);
    };

    addRow(tr("Textures"),
           tr("%1 loaded, %2 hidden/runtime, %3 active texture SRVs / %4 reserved")
               .arg(static_cast<qulonglong>(sceneBreakdown.textureCount))
               .arg(static_cast<qulonglong>(sceneBreakdown.hiddenTextureCount))
               .arg(g_textureDescriptorCount)
               .arg(g_textureDescriptorCapacity),
           sceneBreakdown.textureBytes);
    addRow(tr("Meshes"),
           tr("%1 meshes, %2 vertices, %3 triangles")
               .arg(static_cast<qulonglong>(sceneBreakdown.meshCount))
               .arg(static_cast<qulonglong>(sceneBreakdown.vertexCount))
               .arg(static_cast<qulonglong>(sceneBreakdown.triangleCount)),
           sceneBreakdown.meshBytes);
    addRow(tr("Materials"),
           tr("%1 materials, active runtime payload; reserved GPU-visible material buffers: %2")
               .arg(static_cast<qulonglong>(sceneBreakdown.materialCount))
               .arg(FormatBytes(sceneBreakdown.materialReservedBytes)),
           sceneBreakdown.materialPayloadBytes);
    addRow(tr("Mesh/material mapping"),
           tr("DXR mesh mapping payload for scene mesh material/index lookup"),
           sceneBreakdown.meshMappingPayloadBytes);
    addRow(tr("DXR acceleration structures"),
           tr("%1 BLAS plus TLAS, including scratch/result buffers")
               .arg(static_cast<qulonglong>(dxrBreakdown.blasCount)),
           dxrBreakdown.accelerationStructureBytes);
    addRow(tr("Render targets and AOVs"),
           tr("DXR output, depth, motion vectors, denoiser guides, tonemap targets"),
           dxrBreakdown.renderTargetBytes);
    addRow(tr("Accumulation"),
           tr("Beauty/transmission accumulation and variance buffers"),
           dxrBreakdown.accumulationBytes);
    addRow(tr("ReSTIR reservoirs"),
           tr("DI and GI reservoir textures"),
           dxrBreakdown.reservoirBytes);
    addRow(tr("Wavefront queues"),
           tr("Path, hit, shadow, dispatch, binning, and shadow contribution buffers"),
           dxrBreakdown.wavefrontQueueBytes);
    addRow(tr("Other buffers"),
           tr("Shader table, light upload, and diagnostics buffers"),
           dxrBreakdown.shaderTableBytes + dxrBreakdown.lightBufferBytes +
               dxrBreakdown.diagnosticBufferBytes);
    addRow(tr("SRV/UAV descriptor records"),
           tr("%1 active records, global heap %2/%3 persistent/reserved, DXR heap %4 records")
               .arg(sceneBreakdown.activeDescriptorRecords)
               .arg(g_cbvSrvAllocator.PersistentCount())
               .arg(g_cbvSrvAllocator.Capacity())
               .arg(dxrBreakdown.descriptorCount),
           sceneBreakdown.globalDescriptorHeapBytes + dxrBreakdown.descriptorHeapBytes);
    addRow(tr("Scene asset subtotal"),
           tr("Textures + mesh buffers + active material payload + mesh/material mapping"),
           sceneBreakdown.sceneAssetBytes);
    addRow(tr("Scene with DXR AS"),
           tr("Scene asset subtotal plus BLAS/TLAS allocation"),
           sceneBreakdown.sceneWithAccelerationBytes);
    addRow(tr("Accounted runtime total"),
           tr("Scene assets + DXR renderer allocations + global descriptor heap estimate"),
           sceneBreakdown.accountedRuntimeBytes);

    table->resizeRowsToContents();
    breakdownLayout->addWidget(table, 1);
    tabs->addTab(breakdownPage, tr("Breakdown"));

    auto *texturePage = new QWidget(tabs);
    auto *textureLayout = new QVBoxLayout(texturePage);
    textureLayout->setContentsMargins(0, 0, 0, 0);
    textureLayout->setSpacing(0);

    auto *textureTable = new QTableWidget(texturePage);
    textureTable->setColumnCount(9);
    textureTable->setHorizontalHeaderLabels(
        {tr("#"), tr("Resolution"), tr("GPU Format"), tr("Source Format"),
         tr("Usage"), tr("Compression"), tr("GPU"), tr("CPU Source"),
         tr("Hidden")});
    textureTable->horizontalHeader()->setStretchLastSection(false);
    textureTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        5, QHeaderView::Stretch);
    textureTable->horizontalHeader()->setSectionResizeMode(
        6, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        7, QHeaderView::ResizeToContents);
    textureTable->horizontalHeader()->setSectionResizeMode(
        8, QHeaderView::ResizeToContents);
    textureTable->verticalHeader()->setVisible(false);
    textureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    textureTable->setSelectionMode(QAbstractItemView::NoSelection);
    textureTable->setAlternatingRowColors(true);

    auto setTextureCell = [textureTable](int row, int column,
                                         const QString &text,
                                         Qt::Alignment alignment =
                                             Qt::AlignLeft | Qt::AlignVCenter) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(alignment);
        textureTable->setItem(row, column, item);
    };

    for (size_t i = 0; i < g_loadedTextures.size(); ++i) {
        const Asset::Texture &texture = g_loadedTextures[i];
        const int row = textureTable->rowCount();
        textureTable->insertRow(row);

        const QString resolution =
            tr("%1 x %2, %3 mip%4")
                .arg(texture.width)
                .arg(texture.height)
                .arg(texture.mipLevels)
                .arg(texture.mipLevels == 1 ? QString() : QStringLiteral("s"));
        QString compression =
            QString::fromLatin1(
                Asset::TextureCompressionModeName(texture.compressionMode));
        if (texture.gpuCompressed) {
            compression += tr(" (GPU compressed)");
        }

        setTextureCell(row, 0, QString::number(i),
                       Qt::AlignRight | Qt::AlignVCenter);
        setTextureCell(row, 1, resolution);
        setTextureCell(row, 2, FormatDxgiFormat(texture.format));
        setTextureCell(row, 3, FormatDxgiFormat(texture.cpuFormat));
        setTextureCell(
            row, 4,
            QString::fromLatin1(
                Asset::TextureUsageSemanticName(texture.usageSemantic)));
        setTextureCell(row, 5, compression);
        setTextureCell(row, 6,
                       FormatBytes(ResourceAllocationBytes(
                           texture.resource.Get())),
                       Qt::AlignRight | Qt::AlignVCenter);
        setTextureCell(row, 7,
                       FormatBytes(static_cast<UINT64>(texture.cpuData.size())),
                       Qt::AlignRight | Qt::AlignVCenter);
        setTextureCell(row, 8,
                       texture.hiddenInEditor ? tr("Yes") : tr("No"),
                       Qt::AlignCenter);
    }

    textureTable->resizeRowsToContents();
    textureLayout->addWidget(textureTable, 1);
    tabs->addTab(texturePage, tr("Textures"));
    layout->addWidget(tabs, 1);

    auto *note = new QLabel(
        tr("Values are D3D12 allocation-size estimates for committed resources. "
           "Adapter usage includes driver residency, swapchain, external denoisers, and other allocations outside this scene table."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *closeButton = new QPushButton(tr("Close"), &dialog);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    dialog.exec();
}

void MainWindow::toggleQtUiVisibility()
{
    if (!m_qtUiHidden) {
        m_hiddenQtUiWidgets.clear();

        const auto rememberVisible = [this](QWidget *widget) {
            if (!widget || !widget->isVisible()) {
                return;
            }
            m_hiddenQtUiWidgets.push_back(widget);
            widget->hide();
        };

        rememberVisible(menuBar());
        rememberVisible(statusBar());

        const auto toolBars = findChildren<QToolBar *>();
        for (QToolBar *toolBar : toolBars) {
            rememberVisible(toolBar);
        }

        const auto dockWidgets = findChildren<QDockWidget *>();
        for (QDockWidget *dockWidget : dockWidgets) {
            rememberVisible(dockWidget);
        }

        m_qtUiHidden = true;
        return;
    }

    for (QWidget *widget : m_hiddenQtUiWidgets) {
        if (widget) {
            widget->show();
        }
    }
    m_hiddenQtUiWidgets.clear();
    m_qtUiHidden = false;
}

void MainWindow::closeRenderExportProgressUi()
{
    if (!m_renderProgressDialog) {
        return;
    }
    QDialog *dialog = m_renderProgressDialog;
    m_renderProgressDialog = nullptr;
    m_renderProgressBar = nullptr;
    m_renderProgressTitle = nullptr;
    m_renderProgressDetails = nullptr;
    m_renderProgressTiming = nullptr;
    m_renderProgressCancel = nullptr;
    dialog->close();
    dialog->deleteLater();
}

void MainWindow::updateRenderExportProgressUi()
{
    if (!IsRenderExportActive()) {
        closeRenderExportProgressUi();
        return;
    }

    if (!m_renderProgressDialog) {
        m_renderProgressDialog = new QDialog(this);
        m_renderProgressDialog->setWindowTitle(tr("Render Export Progress"));
        m_renderProgressDialog->setModal(false);
        m_renderProgressDialog->setMinimumWidth(520);
        m_renderProgressDialog->setObjectName(QStringLiteral("RenderProgressPopup"));
        m_renderProgressDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);

        auto *layout = new QVBoxLayout(m_renderProgressDialog);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(10);

        m_renderProgressTitle = new QLabel(m_renderProgressDialog);
        QFont titleFont = m_renderProgressTitle->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 1);
        m_renderProgressTitle->setFont(titleFont);
        layout->addWidget(m_renderProgressTitle);

        m_renderProgressBar = new QProgressBar(m_renderProgressDialog);
        m_renderProgressBar->setRange(0, 1000);
        m_renderProgressBar->setTextVisible(true);
        layout->addWidget(m_renderProgressBar);

        m_renderProgressDetails = new QLabel(m_renderProgressDialog);
        m_renderProgressDetails->setWordWrap(true);
        m_renderProgressDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_renderProgressDetails);

        m_renderProgressTiming = new QLabel(m_renderProgressDialog);
        m_renderProgressTiming->setWordWrap(true);
        layout->addWidget(m_renderProgressTiming);

        m_renderProgressCancel = new QPushButton(tr("Cancel Export"), m_renderProgressDialog);
        connect(m_renderProgressCancel, &QPushButton::clicked, this, [this]() {
            CancelActiveExport();
            closeRenderExportProgressUi();
        });
        layout->addWidget(m_renderProgressCancel);
        connect(m_renderProgressDialog, &QObject::destroyed, this, [this]() {
            m_renderProgressDialog = nullptr;
            m_renderProgressBar = nullptr;
            m_renderProgressTitle = nullptr;
            m_renderProgressDetails = nullptr;
            m_renderProgressTiming = nullptr;
            m_renderProgressCancel = nullptr;
        });
        m_renderProgressDialog->show();
    } else if (!m_renderProgressDialog->isVisible()) {
        m_renderProgressDialog->show();
    }
    m_renderProgressDialog->raise();

    const bool animation = g_renderAnimationExport.active;
    const bool batch = g_renderBatchExport.active;
    const bool preview = g_renderExportJob.active && g_renderExportJob.isPreview;
    const bool encoding = animation && g_renderAnimationExport.encoding;
    const double itemProgress = encoding ? 1.0 : CurrentRenderItemProgress();
    double overallProgress = itemProgress;
    double elapsed = SecondsSinceTick(g_renderExportJob.startedTickMs);
    int finished = 0;
    int total = 1;
    QString title = preview ? tr("Rendering Preview") : tr("Rendering PNG");
    QString currentItem = BasenameForDisplay(g_renderExportJob.outputPath);

    if (batch) {
        title = tr("Rendering Camera Lister Batch");
        total = static_cast<int>(std::max<size_t>(1, g_renderBatchExport.viewIndices.size()));
        finished = static_cast<int>(g_renderBatchExport.currentViewListIndex == 0
                                        ? 0
                                        : g_renderBatchExport.currentViewListIndex - 1);
        overallProgress = (static_cast<double>(finished) + itemProgress) /
                          static_cast<double>(total);
        elapsed = SecondsSinceTick(g_renderBatchExport.startedTickMs);
        currentItem = g_renderBatchExport.currentViewName.empty()
                          ? BasenameForDisplay(g_renderBatchExport.currentOutputPath)
                          : QString::fromUtf8(g_renderBatchExport.currentViewName.c_str());
    } else if (animation) {
        title = encoding ? tr("Encoding Animation") : tr("Rendering Animation");
        total = std::max(1, g_renderAnimationExport.totalFrames);
        finished = encoding ? total
                            : std::max(0, g_renderAnimationExport.currentFrameIndex - 1);
        overallProgress = encoding
                              ? 1.0
                              : (static_cast<double>(finished) + itemProgress) /
                                    static_cast<double>(total);
        elapsed = SecondsSinceTick(g_renderAnimationExport.startedTickMs);
        currentItem = g_renderAnimationExport.currentLabel.empty()
                          ? BasenameForDisplay(g_renderAnimationExport.currentOutputPath)
                          : QString::fromUtf8(g_renderAnimationExport.currentLabel.c_str());
    }

    overallProgress = std::clamp(overallProgress, 0.0, 1.0);
    const int progressPermille = static_cast<int>(overallProgress * 1000.0 + 0.5);
    m_renderProgressBar->setValue(progressPermille);
    m_renderProgressBar->setFormat(QStringLiteral("%1%").arg(overallProgress * 100.0, 0, 'f', 1));
    m_renderProgressTitle->setText(title);

    QStringList details;
    details << tr("Current item: %1").arg(currentItem.isEmpty() ? tr("export") : currentItem);
    details << tr("Resolution: %1 x %2")
                   .arg(g_renderExportJob.targetWidth)
                   .arg(g_renderExportJob.targetHeight);
    details << tr("Samples: %1 / %2 SPP")
                   .arg(DxrRenderer::GetDisplayedSampleCount())
                   .arg(g_renderExportJob.targetMaxSpp);
    if (DxrRenderer::HasNoiseEstimate()) {
        details << tr("Noise: %1% / %2%")
                       .arg(DxrRenderer::GetCurrentNoiseLevel() * 100.0f, 0, 'f', 2)
                       .arg(g_renderExportJob.targetNoiseThreshold * 100.0f, 0, 'f', 2);
    } else {
        details << tr("Noise: calculating");
    }
    if (g_renderExportJob.targetDenoiserIndex != 0) {
        details << tr("Denoiser: %1")
                       .arg(DxrRenderer::HasDenoisedOutput() ? tr("ready") : tr("waiting"));
    }
    if (batch) {
        details << tr("PNG files: %1 finished / %2 total").arg(finished).arg(total);
        details << tr("Saved view: %1 / %2").arg((std::min)(finished + 1, total)).arg(total);
    } else if (animation) {
        details << tr("Frames: %1 finished / %2 total").arg(finished).arg(total);
        details << tr("Animation FPS: %1").arg(std::max(1, g_renderAnimationExport.fps));
        details << tr("Output: %1")
                       .arg(g_renderAnimationExport.exportMode ==
                                    static_cast<int>(AnimationSequence::ExportMode::Mp4)
                                ? (encoding ? tr("MP4 encoding") : tr("MP4 frame render"))
                                : tr("PNG sequence"));
    }
    m_renderProgressDetails->setText(details.join(QLatin1Char('\n')));

    const double totalEstimate =
        overallProgress > 0.001 ? elapsed / (std::min)(overallProgress, 0.999) : 0.0;
    const double remaining =
        totalEstimate > 0.0 ? std::max(0.0, totalEstimate - elapsed) : 0.0;
    QStringList timing;
    timing << tr("Elapsed: %1").arg(FormatDurationCompact(elapsed));
    timing << tr("Estimated left: %1")
                  .arg(remaining > 0.0 ? FormatDurationCompact(remaining)
                                       : tr("calculating"));
    timing << tr("Estimated total: %1")
                  .arg(totalEstimate > 0.0 ? FormatDurationCompact(totalEstimate)
                                           : tr("calculating"));
    if (!g_renderExportStatus.empty()) {
        timing << QString::fromUtf8(g_renderExportStatus.c_str());
    }
    m_renderProgressTiming->setText(timing.join(QStringLiteral(" | ")));

    const bool canCancel = !(animation && encoding);
    m_renderProgressCancel->setEnabled(canCancel);
    m_renderProgressCancel->setText(canCancel ? tr("Cancel Export")
                                              : tr("Encoding Cannot Be Canceled"));
}

void MainWindow::updateSceneIoUi()
{
    updateTransformUi();

    const bool active = IsSceneIoJobActive();
    const bool exportActive = IsRenderExportActive();
    setWindowCloseEnabled(!active);
    if (m_previewRenderAction) {
        m_previewRenderAction->setEnabled(g_rayTracingSupported &&
                                          !exportActive &&
                                          !active);
    }

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
    if (!m_liveLinkLabel) {
        m_liveLinkLabel = new QLabel(this);
        m_liveLinkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusBar()->addPermanentWidget(m_liveLinkLabel);
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

    {
        const auto stats = LiveLink::GetCoordinator().GetStatsSnapshot();
        const auto providers = LiveLink::GetCoordinator().GetProviderSnapshots();
        const auto diagnostics = LiveLink::GetSceneSync().GetRecentDiagnostics();
        m_liveLinkLabel->setText(FormatCompactLiveLinkStatus(providers));

        if (m_liveLinkSummaryLabel) {
            const auto syncStats = LiveLink::GetSceneSync().GetStatsSnapshot();
            size_t sessionCount = 0;
            int warningCount = 0;
            int errorCount = 0;
            for (const auto &entry : diagnostics) {
                if (entry.level == "Error") {
                    ++errorCount;
                } else if (entry.level == "Warning") {
                    ++warningCount;
                }
            }

            QStringList summary;
            summary << tr("Providers %1 connected of %2")
                           .arg(static_cast<qulonglong>(stats.connectedProviderCount))
                           .arg(static_cast<qulonglong>(stats.providerCount));
            if (!providers.empty()) {
                QStringList providerLines;
                for (const auto &provider : providers) {
                    sessionCount += provider.sessions.size();
                    QStringList sessions;
                    for (const auto &session : provider.sessions) {
                        const std::string sessionLabel =
                            session.displayName.empty() ? session.documentId
                                                        : session.displayName;
                        QString sessionText = QStringLiteral("%1 (%2)")
                                                  .arg(QString::fromStdString(sessionLabel),
                                                       QString::fromStdString(session.sessionId));
                        if (!session.documentPath.empty()) {
                            sessionText += QStringLiteral(" path=%1")
                                               .arg(QString::fromStdString(session.documentPath));
                        }
                        sessions << sessionText;
                    }
                    providerLines << QStringLiteral("%1 [%2] caps=%3 err=%4 sessions=%5")
                                         .arg(QString::fromStdString(provider.providerName),
                                              QString::fromLatin1(LiveLink::ToString(provider.connectionState)),
                                              QString::fromStdString(LiveLink::ToString(provider.capabilities)),
                                              QString::fromStdString(provider.lastError.empty() ? std::string("-") : provider.lastError),
                                              sessions.isEmpty() ? tr("none") : sessions.join(QStringLiteral(", ")));
                }
                summary << providerLines.join(QStringLiteral("\n"));
            }
            summary << FormatLiveLinkSyncStats(stats, syncStats, sessionCount);
            summary << tr("Camera %1")
                           .arg(LiveLink::GetSceneSync().IsCameraControlDetached()
                                    ? tr("engine override until dcc moves")
                                    : tr("dcc linked"));
            summary << tr("Recent issues %1 warnings / %2 errors")
                           .arg(warningCount)
                           .arg(errorCount);
            m_liveLinkSummaryLabel->setText(summary.join(QStringLiteral(" | ")));
        }

        if (m_liveLinkConnectButton && m_liveLinkDisconnectButton &&
            m_liveLinkReconnectButton && m_liveLinkTakeCameraButton) {
            uint64_t selectedProviderId = 0;
            if (m_liveLinkProviderCombo && m_liveLinkProviderCombo->currentData().isValid()) {
                selectedProviderId = m_liveLinkProviderCombo->currentData().toULongLong();
            }
            if (m_liveLinkProviderCombo) {
                m_liveLinkProviderCombo->blockSignals(true);
                m_liveLinkProviderCombo->clear();
                for (const auto &provider : providers) {
                    const QString label = QStringLiteral("%1 [%2]")
                                              .arg(QString::fromStdString(provider.providerName),
                                                   QString::fromLatin1(LiveLink::ToString(provider.connectionState)));
                    m_liveLinkProviderCombo->addItem(label,
                                                     QVariant::fromValue<qulonglong>(provider.providerId));
                }
                if (!providers.empty()) {
                    int selectedIndex = 0;
                    for (int index = 0; index < m_liveLinkProviderCombo->count(); ++index) {
                        if (m_liveLinkProviderCombo->itemData(index).toULongLong() == selectedProviderId) {
                            selectedIndex = index;
                            break;
                        }
                    }
                    m_liveLinkProviderCombo->setCurrentIndex(selectedIndex);
                }
                m_liveLinkProviderCombo->setEnabled(!providers.empty() && !active);
                m_liveLinkProviderCombo->blockSignals(false);
            }

            const bool hasProviders = !providers.empty();
            bool anyConnected = false;
            bool anyDisconnected = false;
            bool selectedConnected = false;
            bool selectedDisconnected = false;
            for (const auto &provider : providers) {
                if (provider.connectionState == LiveLink::ConnectionState::Connected) {
                    anyConnected = true;
                } else {
                    anyDisconnected = true;
                }
                if (provider.providerId == selectedProviderId) {
                    if (provider.connectionState == LiveLink::ConnectionState::Connected) {
                        selectedConnected = true;
                    } else {
                        selectedDisconnected = true;
                    }
                }
            }
            m_liveLinkConnectButton->setEnabled(hasProviders && selectedDisconnected && !active);
            m_liveLinkDisconnectButton->setEnabled(hasProviders && selectedConnected && !active);
            m_liveLinkReconnectButton->setEnabled(hasProviders && (selectedConnected || selectedDisconnected) && !active);
            m_liveLinkTakeCameraButton->setEnabled(anyConnected && !active);
            m_liveLinkTakeCameraButton->setText(
                LiveLink::GetSceneSync().IsCameraControlDetached()
                    ? tr("Return Camera")
                    : tr("Take Camera"));
        }

        if (m_liveLinkDiagnosticsView) {
            QStringList lines;
            if (diagnostics.empty()) {
                lines << tr("No live-link validation or apply issues recorded.");
            } else {
                for (const auto &entry : diagnostics) {
                    lines << QStringLiteral("#%1 [%2] provider=%3 session=%4 delta=%5 target=%6 %7")
                                 .arg(static_cast<qulonglong>(entry.sequence))
                                 .arg(QString::fromStdString(entry.level))
                                 .arg(QString::fromStdString(entry.providerName),
                                      QString::fromStdString(entry.sessionId),
                                      QString::fromStdString(entry.deltaKind),
                                      QString::fromStdString(entry.targetId),
                                      QString::fromStdString(entry.message));
                }
            }
            const QString text = lines.join(QLatin1Char('\n'));
            if (m_liveLinkDiagnosticsView->toPlainText() != text) {
                m_liveLinkDiagnosticsView->setPlainText(text);
                m_liveLinkDiagnosticsView->verticalScrollBar()->setValue(
                    m_liveLinkDiagnosticsView->verticalScrollBar()->maximum());
            }
        }
    }

    if (m_saveSceneAction) {
        m_saveSceneAction->setEnabled(!active);
    }
    if (m_saveSceneAsAction) {
        m_saveSceneAsAction->setEnabled(!active);
    }
    if (m_loadSceneAction) {
        m_loadSceneAction->setEnabled(!active);
    }
    if (m_exitAction) {
        m_exitAction->setEnabled(!active);
    }
    if (m_importModelAction) {
        m_importModelAction->setEnabled(!active);
    }
    if (m_importHdrAction) {
        m_importHdrAction->setEnabled(!active);
    }

    const auto dockWidgets = findChildren<QDockWidget *>();
    for (QDockWidget *dockWidget : dockWidgets) {
        if (!dockWidget || dockWidget->objectName() == tr("LiveLink")) {
            continue;
        }
        if (QWidget *dockContent = dockWidget->widget()) {
            dockContent->setEnabled(!active);
        }
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

void MainWindow::setWindowCloseEnabled(bool enabled)
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }

    HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
    if (!systemMenu) {
        return;
    }

    EnableMenuItem(systemMenu, SC_CLOSE,
                   MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    DrawMenuBar(hwnd);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (IsSceneIoJobActive()) {
        event->ignore();
        g_appClosing = false;
        statusBar()->showMessage(
            IsSceneIoSaveJob()
                ? tr("Please wait: scene save is still in progress.")
                : tr("Please wait: scene load is still in progress."),
            5000);
        return;
    }
    g_appClosing = true;
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!FirstSupportedDroppedModelPath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString path = FirstSupportedDroppedModelPath(event->mimeData());
    if (!path.isEmpty()) {
        Scene::ImportModelAsync(path.toUtf8().constData());
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}
