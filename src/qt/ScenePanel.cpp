#include "ScenePanel.h"

#include "../editor_ui.h"
#include "../scene.h"
#include "../volumetric_renderer.h"
#include "../asset_library/asset_id.h"
#include "../asset_library/asset_registry.h"
#include "../asset_library/asset_types.h"
#include "../asset_library/global_registry.h"

#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QInputDialog>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern HWND g_hwnd;

namespace {

bool IsTextEntryFocused()
{
    QWidget *focusedWidget = QApplication::focusWidget();
    if (!focusedWidget) {
        return false;
    }

    if (qobject_cast<QLineEdit *>(focusedWidget) ||
        qobject_cast<QTextEdit *>(focusedWidget) ||
        qobject_cast<QPlainTextEdit *>(focusedWidget) ||
        qobject_cast<QAbstractSpinBox *>(focusedWidget)) {
        return true;
    }

    if (auto *comboBox = qobject_cast<QComboBox *>(focusedWidget)) {
        return comboBox->isEditable();
    }

    return false;
}

constexpr int kNodeIndexRole = Qt::UserRole;
constexpr int kNodeBaseTooltipRole = Qt::UserRole + 1;
constexpr int kNodeNameColumn = 0;
constexpr int kNodeVisibilityColumn = 1;
constexpr int kNodeLockColumn = 2;
constexpr int kSceneToolButtonSize = 30;

enum class OutlinerIcon {
    Object,
    Group,
    Volume,
    LiveSync,
    Visible,
    Hidden,
    Locked,
    Unlocked
};

enum class SceneToolIcon {
    ImportModel,
    Reimport,
    AddGroundPlane,
    FrameSelected,
    ExplodeSelected,
    DeleteSelected
};

struct TreeUiState {
    bool liveSyncExpanded = true;
    std::unordered_set<int> expandedNodeIndices;
};

QString BuildNodeLabel(const Scene::Node &node)
{
    return QString::fromStdString(node.name);
}

QIcon MakeOutlinerIcon(OutlinerIcon icon)
{
    constexpr int kIconSize = 18;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor stroke(190, 199, 204);
    const QColor muted(104, 112, 117);
    const QColor accent(88, 208, 244);
    painter.setPen(
        QPen(stroke, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case OutlinerIcon::Object:
        painter.drawPolygon(QPolygonF({QPointF(9, 2.5), QPointF(15, 5.8),
                                       QPointF(15, 12.2), QPointF(9, 15.5),
                                       QPointF(3, 12.2), QPointF(3, 5.8)}));
        painter.drawLine(QPointF(3, 5.8), QPointF(9, 9.2));
        painter.drawLine(QPointF(15, 5.8), QPointF(9, 9.2));
        painter.drawLine(QPointF(9, 9.2), QPointF(9, 15.2));
        break;
    case OutlinerIcon::Group:
        painter.setBrush(QColor(69, 91, 101));
        painter.drawRect(QRectF(3, 5, 12, 9));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(4, 5), QPointF(6.5, 2.8));
        painter.drawLine(QPointF(6.5, 2.8), QPointF(11, 2.8));
        painter.drawLine(QPointF(11, 2.8), QPointF(13.5, 5));
        break;
    case OutlinerIcon::Volume:
        painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(3, 3, 12, 12), 2, 2);
        painter.drawEllipse(QPointF(6.5, 7), 1.1, 1.1);
        painter.drawEllipse(QPointF(11.5, 6), 1.3, 1.3);
        painter.drawEllipse(QPointF(9, 11.5), 1.5, 1.5);
        break;
    case OutlinerIcon::LiveSync:
        painter.setPen(QPen(accent, 1.55, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawArc(QRectF(3, 3, 12, 12), 35 * 16, 120 * 16);
        painter.drawArc(QRectF(3, 3, 12, 12), 215 * 16, 120 * 16);
        painter.drawLine(QPointF(13.8, 3.8), QPointF(15.2, 6.7));
        painter.drawLine(QPointF(13.8, 3.8), QPointF(10.8, 4.2));
        painter.drawLine(QPointF(4.2, 14.2), QPointF(2.8, 11.3));
        painter.drawLine(QPointF(4.2, 14.2), QPointF(7.2, 13.8));
        break;
    case OutlinerIcon::Visible:
    case OutlinerIcon::Hidden:
        painter.setPen(QPen(icon == OutlinerIcon::Visible ? stroke : muted,
                            1.45, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawPath([] {
            QPainterPath path;
            path.moveTo(2.5, 9);
            path.cubicTo(5.2, 4.5, 12.8, 4.5, 15.5, 9);
            path.cubicTo(12.8, 13.5, 5.2, 13.5, 2.5, 9);
            return path;
        }());
        painter.drawEllipse(QPointF(9, 9), 2.1, 2.1);
        if (icon == OutlinerIcon::Hidden) {
            painter.setPen(QPen(muted, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(3.2, 3.2), QPointF(14.8, 14.8));
        }
        break;
    case OutlinerIcon::Locked:
    case OutlinerIcon::Unlocked:
        painter.setPen(QPen(icon == OutlinerIcon::Locked ? accent : muted,
                            1.5, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawRoundedRect(QRectF(4.5, 8, 9, 7), 1.4, 1.4);
        if (icon == OutlinerIcon::Locked) {
            painter.drawArc(QRectF(5.8, 2.8, 6.4, 8), 0, 180 * 16);
        } else {
            painter.drawArc(QRectF(7.2, 2.8, 6.4, 8), 0, 180 * 16);
        }
        break;
    }
    return QIcon(pixmap);
}

QIcon NodeOutlinerIcon(const Scene::Node &node)
{
    if (node.liveLinkManaged)
        return MakeOutlinerIcon(OutlinerIcon::LiveSync);
    if (!node.volumeAssetId.empty())
        return MakeOutlinerIcon(OutlinerIcon::Volume);
    if (node.importGroupRoot || node.meshIndices.empty())
        return MakeOutlinerIcon(OutlinerIcon::Group);
    return MakeOutlinerIcon(OutlinerIcon::Object);
}

class SceneOutliner final : public QTreeWidget
{
public:
    using ControlHandler = std::function<void(QTreeWidgetItem *, int)>;

    explicit SceneOutliner(QWidget *parent) : QTreeWidget(parent) {}

    void setControlHandler(ControlHandler handler)
    {
        m_controlHandler = std::move(handler);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QTreeWidgetItem *item = itemAt(event->position().toPoint());
        const int column = columnAt(event->position().toPoint().x());
        if (event->button() == Qt::LeftButton && item &&
            (column == kNodeVisibilityColumn || column == kNodeLockColumn) &&
            item->data(kNodeNameColumn, kNodeIndexRole).isValid()) {
            if (m_controlHandler)
                m_controlHandler(item, column);
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

private:
    ControlHandler m_controlHandler;
};

void CaptureTreeItemState(const QTreeWidgetItem *item, TreeUiState *state)
{
    if (!item || !state) {
        return;
    }

    const QVariant nodeIndexData = item->data(kNodeNameColumn, kNodeIndexRole);
    if (nodeIndexData.isValid() && item->isExpanded()) {
        state->expandedNodeIndices.insert(nodeIndexData.toInt());
    }

    for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
        CaptureTreeItemState(item->child(childIndex), state);
    }
}

TreeUiState CaptureTreeUiState(const QTreeWidget *treeWidget)
{
    TreeUiState state;
    if (!treeWidget) {
        return state;
    }

    for (int topLevelIndex = 0; topLevelIndex < treeWidget->topLevelItemCount(); ++topLevelIndex) {
        const QTreeWidgetItem *item = treeWidget->topLevelItem(topLevelIndex);
        if (!item) {
            continue;
        }

        if (item->data(kNodeNameColumn, kNodeIndexRole).isValid()) {
            CaptureTreeItemState(item, &state);
            continue;
        }

        if (item->text(0) == QObject::tr("Live Sync")) {
            state.liveSyncExpanded = item->isExpanded();
            CaptureTreeItemState(item, &state);
        }
    }

    return state;
}

QIcon MakeSceneToolIcon(SceneToolIcon icon)
{
    constexpr int kIconSize = 18;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor stroke(206, 214, 218);
    const QColor accent(88, 208, 244);
    const QColor muted(132, 140, 144);

    painter.setPen(QPen(stroke, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case SceneToolIcon::ImportModel:
        painter.drawRect(QRectF(3.0, 6.0, 12.0, 8.0));
        painter.drawLine(QPointF(5.0, 6.0), QPointF(7.0, 3.0));
        painter.drawLine(QPointF(7.0, 3.0), QPointF(13.0, 3.0));
        painter.drawLine(QPointF(13.0, 3.0), QPointF(15.0, 6.0));
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(9.0, 7.0), QPointF(9.0, 12.0));
        painter.drawLine(QPointF(6.5, 9.5), QPointF(9.0, 12.0));
        painter.drawLine(QPointF(11.5, 9.5), QPointF(9.0, 12.0));
        break;
    case SceneToolIcon::Reimport:
        painter.drawArc(QRectF(3.0, 3.0, 12.0, 12.0), 35 * 16, 285 * 16);
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(14.0, 5.0), QPointF(14.5, 1.8));
        painter.drawLine(QPointF(14.0, 5.0), QPointF(10.8, 4.5));
        break;
    case SceneToolIcon::AddGroundPlane:
        painter.drawPolygon(QPolygonF({QPointF(3.0, 11.0), QPointF(9.0, 7.0),
                                       QPointF(15.0, 11.0), QPointF(9.0, 15.0)}));
        painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(9.0, 2.5), QPointF(9.0, 7.5));
        painter.drawLine(QPointF(6.5, 5.0), QPointF(11.5, 5.0));
        break;
    case SceneToolIcon::FrameSelected:
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawLine(QPointF(3, 7), QPointF(3, 3));
        painter.drawLine(QPointF(3, 3), QPointF(7, 3));
        painter.drawLine(QPointF(11, 3), QPointF(15, 3));
        painter.drawLine(QPointF(15, 3), QPointF(15, 7));
        painter.drawLine(QPointF(15, 11), QPointF(15, 15));
        painter.drawLine(QPointF(15, 15), QPointF(11, 15));
        painter.drawLine(QPointF(7, 15), QPointF(3, 15));
        painter.drawLine(QPointF(3, 15), QPointF(3, 11));
        painter.setPen(QPen(stroke, 1.4, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawEllipse(QPointF(9, 9), 2.7, 2.7);
        break;
    case SceneToolIcon::ExplodeSelected:
        painter.drawRect(QRectF(6.0, 6.0, 6.0, 6.0));
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawLine(QPointF(6.0, 6.0), QPointF(3.0, 3.0));
        painter.drawLine(QPointF(12.0, 6.0), QPointF(15.0, 3.0));
        painter.drawLine(QPointF(6.0, 12.0), QPointF(3.0, 15.0));
        painter.drawLine(QPointF(12.0, 12.0), QPointF(15.0, 15.0));
        painter.drawLine(QPointF(3.0, 3.0), QPointF(5.0, 3.0));
        painter.drawLine(QPointF(3.0, 3.0), QPointF(3.0, 5.0));
        painter.drawLine(QPointF(15.0, 3.0), QPointF(13.0, 3.0));
        painter.drawLine(QPointF(15.0, 3.0), QPointF(15.0, 5.0));
        painter.drawLine(QPointF(3.0, 15.0), QPointF(5.0, 15.0));
        painter.drawLine(QPointF(3.0, 15.0), QPointF(3.0, 13.0));
        painter.drawLine(QPointF(15.0, 15.0), QPointF(13.0, 15.0));
        painter.drawLine(QPointF(15.0, 15.0), QPointF(15.0, 13.0));
        break;
    case SceneToolIcon::DeleteSelected:
        painter.drawLine(QPointF(5.0, 6.0), QPointF(14.0, 6.0));
        painter.drawLine(QPointF(7.0, 4.0), QPointF(12.0, 4.0));
        painter.drawLine(QPointF(7.0, 7.5), QPointF(8.0, 14.0));
        painter.drawLine(QPointF(12.0, 7.5), QPointF(11.0, 14.0));
        painter.setPen(QPen(muted, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(9.5, 8.0), QPointF(9.5, 13.5));
        break;
    }

    return QIcon(pixmap);
}

QToolButton *CreateSceneToolButton(QWidget *parent, const QString &toolTip, SceneToolIcon icon)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(toolTip);
    button->setStatusTip(toolTip);
    button->setIcon(MakeSceneToolIcon(icon));
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(kSceneToolButtonSize, kSceneToolButtonSize);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void SetColorButton(QPushButton *button, const float color[3])
{
    if (!button) {
        return;
    }
    const QColor q = QColor::fromRgbF(
        std::clamp(color[0], 0.0f, 1.0f),
        std::clamp(color[1], 0.0f, 1.0f),
        std::clamp(color[2], 0.0f, 1.0f));
    button->setProperty("volumeColor", q);
    button->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(q.name()));
}

void HashCombine(uint64_t *hash, uint64_t value)
{
    *hash ^= value;
    *hash *= 1099511628211ull;
}

void HashString(uint64_t *hash, const std::string &value)
{
    HashCombine(hash, static_cast<uint64_t>(value.size()));
    for (unsigned char c : value) {
        HashCombine(hash, c);
    }
}

uint64_t BuildTreeStructureSignature(const std::vector<Scene::Node> &nodes)
{
    uint64_t hash = 1469598103934665603ull;
    HashCombine(&hash, static_cast<uint64_t>(nodes.size()));
    for (const Scene::Node &node : nodes) {
        HashString(&hash, node.name);
        HashCombine(&hash, static_cast<uint64_t>(node.meshIndices.size()));
        HashCombine(&hash, node.visible ? 1ull : 0ull);
        HashCombine(&hash, node.selectionLocked ? 1ull : 0ull);
        HashCombine(&hash, node.liveLinkManaged ? 1ull : 0ull);
        HashCombine(&hash, static_cast<uint64_t>(node.parentIndex + 1));
        HashString(&hash, node.sourcePath);
        HashString(&hash, node.importGroupKey);
    }
    return hash;
}

void SyncTreeSelection(QTreeWidgetItem *item,
                       const std::unordered_set<int> &selectedRows,
                       QTreeWidgetItem **firstSelectedItem)
{
    if (!item) {
        return;
    }

    const QVariant nodeIndexData = item->data(kNodeNameColumn, kNodeIndexRole);
    if (nodeIndexData.isValid()) {
        const bool selected = selectedRows.find(nodeIndexData.toInt()) != selectedRows.end();
        item->setSelected(selected);
        if (selected && firstSelectedItem && !*firstSelectedItem) {
            *firstSelectedItem = item;
        }
    }

    for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
        SyncTreeSelection(item->child(childIndex), selectedRows, firstSelectedItem);
    }
}

}

ScenePanel::ScenePanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshSceneList();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        const bool sceneIoActive = IsSceneIoJobActive();
        const bool importActive = Scene::IsImportInProgress();
        const bool sceneIoTransition = (sceneIoActive != m_lastSceneIoActive);
        const bool importTransition = (importActive != m_lastImportActive);
        const bool refreshNeeded = m_treeDirty || sceneIoTransition ||
                                   importTransition || sceneIoActive ||
                                   importActive ||
                                   (m_importProgress && m_importProgress->isVisible());
        if (refreshNeeded) {
            refreshSceneList();
        } else if (m_volumeMaterialGroup &&
                   m_volumeMaterialGroup->isVisible()) {
            syncVolumeMaterialInspector();
        }
    });
    m_refreshTimer->start(200);

    m_sceneChangeListenerId = Scene::RegisterChangeListener([this]() {
        scheduleRefresh();
    });
}

ScenePanel::~ScenePanel()
{
    if (m_sceneChangeListenerId != 0) {
        Scene::UnregisterChangeListener(m_sceneChangeListenerId);
    }
}

void ScenePanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_importProgress = new QProgressBar(this);
    m_importProgress->setRange(0, 100);
    m_importProgress->setTextVisible(false);
    m_importProgress->hide();
    layout->addWidget(m_importProgress);

    m_importStatusLabel = new QLabel(this);
    m_importStatusLabel->setWordWrap(true);
    m_importStatusLabel->hide();
    layout->addWidget(m_importStatusLabel);

    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    m_importButton = CreateSceneToolButton(this, tr("Import Model"), SceneToolIcon::ImportModel);
    m_reimportButton = CreateSceneToolButton(this, tr("Reimport Selected"), SceneToolIcon::Reimport);
    m_addPlaneButton = CreateSceneToolButton(this, tr("Add Ground Plane"), SceneToolIcon::AddGroundPlane);
    m_frameSelectedButton = CreateSceneToolButton(
        this, tr("Frame Selected (F)"), SceneToolIcon::FrameSelected);
    m_explodeButton = CreateSceneToolButton(
        this,
        tr("Explode Selected - split a multi-mesh node into independent child "
           "nodes. The exploded branch stops following source reimport."),
        SceneToolIcon::ExplodeSelected);
    m_deleteButton = CreateSceneToolButton(this, tr("Delete Selected (Del)"), SceneToolIcon::DeleteSelected);
    m_deleteButton->setText(tr("Del"));
    m_deleteButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolRow->addWidget(m_importButton);
    toolRow->addWidget(m_reimportButton);
    toolRow->addWidget(m_addPlaneButton);
    toolRow->addWidget(m_frameSelectedButton);
    toolRow->addWidget(m_explodeButton);
    toolRow->addWidget(m_deleteButton);
    toolRow->addStretch(1);
    layout->addLayout(toolRow);

    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextFormat(Qt::RichText);
    m_sourceLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_sourceLabel->setOpenExternalLinks(false);
    layout->addWidget(m_sourceLabel);

    auto *outliner = new SceneOutliner(this);
    m_nodeList = outliner;
    m_nodeList->setColumnCount(3);
    m_nodeList->setHeaderLabels({tr("Scene Collection"), QString(), QString()});
    m_nodeList->setHeaderHidden(false);
    m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nodeList->setAlternatingRowColors(true);
    m_nodeList->setRootIsDecorated(true);
    m_nodeList->setIndentation(16);
    m_nodeList->setUniformRowHeights(true);
    m_nodeList->setIconSize(QSize(18, 18));
    m_nodeList->setAllColumnsShowFocus(true);
    m_nodeList->setStyleSheet(QStringLiteral(
        "QTreeWidget {"
        "  background: #181b1d;"
        "  alternate-background-color: #202427;"
        "  border: 1px solid #2b3033;"
        "  border-radius: 4px;"
        "  outline: 0;"
        "}"
        "QTreeWidget::item {"
        "  min-height: 25px;"
        "  padding: 1px 3px;"
        "  color: #cdd4d8;"
        "}"
        "QTreeWidget::item:hover { background: #293136; }"
        "QTreeWidget::item:selected {"
        "  background: #315463;"
        "  color: #eefcff;"
        "}"
        "QHeaderView::section {"
        "  background: #22272a;"
        "  color: #8f999e;"
        "  border: 0;"
        "  border-bottom: 1px solid #30363a;"
        "  padding: 5px 7px;"
        "  font-weight: 600;"
        "}"));
    m_nodeList->header()->setSectionResizeMode(kNodeNameColumn, QHeaderView::Stretch);
    m_nodeList->header()->setSectionResizeMode(kNodeVisibilityColumn,
                                               QHeaderView::Fixed);
    m_nodeList->header()->setSectionResizeMode(kNodeLockColumn,
                                               QHeaderView::Fixed);
    m_nodeList->header()->resizeSection(kNodeVisibilityColumn, 30);
    m_nodeList->header()->resizeSection(kNodeLockColumn, 30);
    m_nodeList->headerItem()->setIcon(
        kNodeVisibilityColumn, MakeOutlinerIcon(OutlinerIcon::Visible));
    m_nodeList->headerItem()->setIcon(
        kNodeLockColumn, MakeOutlinerIcon(OutlinerIcon::Locked));
    m_nodeList->headerItem()->setToolTip(kNodeVisibilityColumn,
                                        tr("Viewport and render visibility"));
    m_nodeList->headerItem()->setToolTip(kNodeLockColumn,
                                        tr("Viewport selection lock"));
    outliner->setControlHandler(
        [this](QTreeWidgetItem *item, int column) {
            if (m_syncing || !item)
                return;
            const QVariant nodeIndexData =
                item->data(kNodeNameColumn, kNodeIndexRole);
            if (!nodeIndexData.isValid())
                return;
            const size_t nodeIndex =
                static_cast<size_t>(nodeIndexData.toInt());
            const auto &nodes = Scene::GetNodes();
            if (nodeIndex >= nodes.size())
                return;
            if (column == kNodeVisibilityColumn) {
                Scene::SetNodeBranchVisibility(nodeIndex,
                                               !nodes[nodeIndex].visible);
            } else if (column == kNodeLockColumn) {
                Scene::SetNodeSelectionLocked(
                    nodeIndex, !nodes[nodeIndex].selectionLocked);
            }
            refreshSceneList();
        });
    layout->addWidget(m_nodeList);

    m_volumeMaterialGroup = new QGroupBox(tr("Volume Material"), this);
    auto *volumeForm = new QFormLayout(m_volumeMaterialGroup);
    const auto makeFloat = [this, volumeForm](
                               const QString &label, double min, double max,
                               double step, int decimals) {
        auto *control = new QDoubleSpinBox(m_volumeMaterialGroup);
        control->setRange(min, max);
        control->setSingleStep(step);
        control->setDecimals(decimals);
        volumeForm->addRow(label, control);
        connect(control, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) {
                    if (!m_syncing) applyVolumeMaterialInspector();
                });
        return control;
    };
    m_volumeDensity =
        makeFloat(tr("Density"), 0.0, 100.0, 0.05, 3);
    m_volumeAbsorption =
        makeFloat(tr("Absorption"), 0.0, 20.0, 0.05, 3);
    m_volumeScattering =
        makeFloat(tr("Scattering g"), -0.95, 0.95, 0.01, 3);
    m_volumeAmbient =
        makeFloat(tr("Ambient"), 0.0, 10.0, 0.05, 3);
    m_volumeEmission =
        makeFloat(tr("Emission radiance"), 0.0, 10000000.0, 1000.0, 1);
    m_volumeLightingStrength =
        makeFloat(tr("Scene light"), 0.0, 100.0, 0.001, 4);
    m_volumeLightStats = new QLabel(m_volumeMaterialGroup);
    m_volumeLightStats->setWordWrap(true);
    volumeForm->addRow(tr("DXR light proxies"), m_volumeLightStats);
    m_volumeSequenceStats = new QLabel(m_volumeMaterialGroup);
    m_volumePlaybackMode = new QComboBox(m_volumeMaterialGroup);
    m_volumePlaybackMode->addItem(tr("Static"));
    m_volumePlaybackMode->addItem(tr("Animation Timeline"));
    m_volumePlaybackMode->addItem(tr("Scene Loop"));
    m_volumePlaybackFps = new QDoubleSpinBox(m_volumeMaterialGroup);
    m_volumePlaybackFps->setRange(0.1, 240.0);
    m_volumePlaybackFps->setSingleStep(1.0);
    m_volumePlaybackFps->setDecimals(2);
    volumeForm->addRow(tr("Playback FPS"), m_volumePlaybackFps);
    m_volumePlaybackLoop = new QCheckBox(tr("Loop sequence"),
                                        m_volumeMaterialGroup);
    m_volumeFrameOffset = new QSpinBox(m_volumeMaterialGroup);
    m_volumeFrameOffset->setRange(-100000, 100000);
    volumeForm->addRow(tr("VDB sequence"), m_volumeSequenceStats);
    volumeForm->addRow(tr("Playback"), m_volumePlaybackMode);
    volumeForm->addRow(QString(), m_volumePlaybackLoop);
    volumeForm->addRow(tr("Frame offset"), m_volumeFrameOffset);
    connect(m_volumePlaybackMode,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (!m_syncing) applyVolumePlaybackInspector();
            });
    connect(m_volumePlaybackFps,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                if (!m_syncing) applyVolumePlaybackInspector();
            });
    connect(m_volumePlaybackLoop, &QCheckBox::toggled, this, [this](bool) {
        if (!m_syncing) applyVolumePlaybackInspector();
    });
    connect(m_volumeFrameOffset, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) {
                if (!m_syncing) applyVolumePlaybackInspector();
            });
    m_volumeTemperatureLow =
        makeFloat(tr("Heat low"), 0.0, 0.999, 0.01, 3);
    m_volumeTemperatureHigh =
        makeFloat(tr("Heat high"), 0.001, 1.0, 0.01, 3);
    m_volumeTemperatureGamma =
        makeFloat(tr("Heat gamma"), 0.05, 8.0, 0.05, 3);
    m_volumeJitter =
        makeFloat(tr("Step jitter"), 0.0, 1.0, 0.05, 2);
    m_volumeColor = new QPushButton(tr("Choose"), m_volumeMaterialGroup);
    m_volumeEmissionColor =
        new QPushButton(tr("Choose"), m_volumeMaterialGroup);
    volumeForm->addRow(tr("Scatter color"), m_volumeColor);
    volumeForm->addRow(tr("Emission color"), m_volumeEmissionColor);
    m_volumeMarchSteps = new QSpinBox(m_volumeMaterialGroup);
    m_volumeMarchSteps->setRange(8, 1024);
    m_volumeLightSteps = new QSpinBox(m_volumeMaterialGroup);
    m_volumeLightSteps->setRange(1, 32);
    volumeForm->addRow(tr("March steps"), m_volumeMarchSteps);
    volumeForm->addRow(tr("Light steps"), m_volumeLightSteps);
    connect(m_volumeMarchSteps, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) {
                if (!m_syncing) applyVolumeMaterialInspector();
            });
    connect(m_volumeLightSteps, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) {
                if (!m_syncing) applyVolumeMaterialInspector();
            });
    const auto chooseColor = [this](QPushButton *button) {
        const QColor current =
            button->property("volumeColor").value<QColor>();
        const QColor selected =
            QColorDialog::getColor(current, this, tr("Volume Color"));
        if (!selected.isValid()) return;
        button->setProperty("volumeColor", selected);
        button->setStyleSheet(
            QStringLiteral("background-color: %1;").arg(selected.name()));
        applyVolumeMaterialInspector();
    };
    connect(m_volumeColor, &QPushButton::clicked, this,
            [this, chooseColor]() { chooseColor(m_volumeColor); });
    connect(m_volumeEmissionColor, &QPushButton::clicked, this,
            [this, chooseColor]() { chooseColor(m_volumeEmissionColor); });
    m_volumeMaterialGroup->hide();
    layout->addWidget(m_volumeMaterialGroup);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    connect(m_importButton, &QToolButton::clicked, this, []() {
        HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
        if (!owner) {
            owner = g_hwnd;
        }
        Scene::ImportModelWithDialog(owner);
    });
    connect(m_reimportButton, &QToolButton::clicked, this, [this]() {
        const int nodeIndex = selectedNodeIndex();
        if (nodeIndex >= 0) {
            Scene::ReimportNode(static_cast<size_t>(nodeIndex));
            refreshSceneList();
        }
    });
    connect(m_addPlaneButton, &QToolButton::clicked, this, []() {
        Scene::AddDefaultPlane(0.0f);
    });
    connect(m_frameSelectedButton, &QToolButton::clicked, this, []() {
        Scene::FrameSelected();
    });
    auto *frameShortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    frameShortcut->setContext(Qt::ApplicationShortcut);
    connect(frameShortcut, &QShortcut::activated, this, []() {
        if (QApplication::activeModalWidget() || IsTextEntryFocused()) {
            return;
        }

        Scene::FrameSelected();
    });
    connect(m_explodeButton, &QToolButton::clicked, this, [this]() {
        const int nodeIndex = selectedNodeIndex();
        if (nodeIndex >= 0 &&
            Scene::ExplodeNodeMeshes(static_cast<size_t>(nodeIndex)) > 0) {
            refreshSceneList();
        }
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        requestDeleteSelectedNode();
    });
    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    deleteShortcut->setContext(Qt::ApplicationShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this]() {
        requestDeleteSelectedNode();
    });
    connect(m_nodeList, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_syncing) {
            return;
        }
        std::vector<size_t> selectedNodeIndices;
        const QList<QTreeWidgetItem *> selectedItems = m_nodeList->selectedItems();
        selectedNodeIndices.reserve(static_cast<size_t>(selectedItems.size()));
        for (QTreeWidgetItem *item : selectedItems) {
            if (!item) {
                continue;
            }
            const QVariant nodeIndexData = item->data(kNodeNameColumn, kNodeIndexRole);
            if (nodeIndexData.isValid()) {
                selectedNodeIndices.push_back(
                    static_cast<size_t>(nodeIndexData.toInt()));
            }
        }
        Scene::SelectNodes(selectedNodeIndices);
        syncVolumeMaterialInspector();
    });
    connect(m_nodeList, &QTreeWidget::customContextMenuRequested,
            this, &ScenePanel::showNodeContextMenu);
    connect(m_sourceLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link != QStringLiteral("reimport")) {
            return;
        }
        const int nodeIndex = selectedNodeIndex();
        if (nodeIndex >= 0) {
            Scene::ReimportNode(static_cast<size_t>(nodeIndex));
            refreshSceneList();
        }
    });
}

void ScenePanel::syncVolumeMaterialInspector()
{
    if (!m_volumeMaterialGroup) return;
    const int nodeIndex = selectedNodeIndex();
    const auto &nodes = Scene::GetNodes();
    const bool valid = nodeIndex >= 0 &&
                       static_cast<size_t>(nodeIndex) < nodes.size() &&
                       !nodes[static_cast<size_t>(nodeIndex)].volumeAssetId.empty();
    m_volumeMaterialGroup->setVisible(valid);
    if (!valid) return;

    const bool wasSyncing = m_syncing;
    m_syncing = true;
    const Scene::VolumeMaterial &m =
        nodes[static_cast<size_t>(nodeIndex)].volumeMaterial;
    const Scene::Node &node = nodes[static_cast<size_t>(nodeIndex)];
    m_volumeDensity->setValue(m.densityScale);
    m_volumeAbsorption->setValue(m.absorption);
    m_volumeScattering->setValue(m.scattering);
    m_volumeAmbient->setValue(m.ambient);
    m_volumeEmission->setValue(m.emissionStrength);
    m_volumeLightingStrength->setValue(m.lightingStrength);
    const VolumetricRenderer::EmissionLightStats lightStats =
        VolumetricRenderer::GetEmissionLightStats();
    m_volumeLightStats->setText(
        tr("%1 volume(s), %2 lights, total %3, peak %4")
            .arg(lightStats.volumeCount)
            .arg(lightStats.lightCount)
            .arg(lightStats.totalIntensity, 0, 'g', 4)
            .arg(lightStats.maxIntensity, 0, 'g', 4));
    assetlib::AssetId volumeId;
    VolumetricRenderer::SequenceInfo sequence;
    if (assetlib::AssetId::FromString(node.volumeAssetId, volumeId))
        sequence = VolumetricRenderer::GetSequenceInfo(volumeId);
    m_volumeSequenceStats->setText(
        sequence.animated
            ? tr("%1 frames, resident %2%3")
                  .arg(sequence.frameCount)
                  .arg(sequence.currentFrame)
                  .arg(sequence.loading
                           ? tr(", loading %1").arg(sequence.pendingFrame)
                           : QString())
            : tr("Single frame"));
    m_volumePlaybackMode->setCurrentIndex(
        static_cast<int>(node.volumePlayback.mode));
    m_volumePlaybackFps->setValue(node.volumePlayback.fps);
    m_volumePlaybackLoop->setChecked(node.volumePlayback.loop);
    m_volumeFrameOffset->setValue(node.volumePlayback.frameOffset);
    m_volumePlaybackMode->setEnabled(sequence.animated);
    m_volumePlaybackFps->setEnabled(sequence.animated);
    m_volumePlaybackLoop->setEnabled(sequence.animated);
    m_volumeFrameOffset->setEnabled(sequence.animated);
    m_volumeTemperatureLow->setValue(m.temperatureLow);
    m_volumeTemperatureHigh->setValue(m.temperatureHigh);
    m_volumeTemperatureGamma->setValue(m.temperatureGamma);
    m_volumeJitter->setValue(m.stepJitter);
    m_volumeMarchSteps->setValue(m.marchSteps);
    m_volumeLightSteps->setValue(m.lightSteps);
    SetColorButton(m_volumeColor, m.color);
    SetColorButton(m_volumeEmissionColor, m.emissionColor);
    m_syncing = wasSyncing;
}

void ScenePanel::applyVolumeMaterialInspector()
{
    const int nodeIndex = selectedNodeIndex();
    const auto &nodes = Scene::GetNodes();
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.size() ||
        nodes[static_cast<size_t>(nodeIndex)].volumeAssetId.empty()) {
        return;
    }
    Scene::VolumeMaterial material =
        nodes[static_cast<size_t>(nodeIndex)].volumeMaterial;
    material.densityScale = static_cast<float>(m_volumeDensity->value());
    material.absorption = static_cast<float>(m_volumeAbsorption->value());
    material.scattering = static_cast<float>(m_volumeScattering->value());
    material.ambient = static_cast<float>(m_volumeAmbient->value());
    material.emissionStrength = static_cast<float>(m_volumeEmission->value());
    material.lightingStrength =
        static_cast<float>(m_volumeLightingStrength->value());
    material.temperatureLow =
        static_cast<float>(m_volumeTemperatureLow->value());
    material.temperatureHigh =
        static_cast<float>(m_volumeTemperatureHigh->value());
    material.temperatureGamma =
        static_cast<float>(m_volumeTemperatureGamma->value());
    if (material.temperatureHigh <= material.temperatureLow) {
        material.temperatureHigh =
            (std::min)(1.0f, material.temperatureLow + 0.001f);
    }
    material.stepJitter = static_cast<float>(m_volumeJitter->value());
    material.marchSteps = m_volumeMarchSteps->value();
    material.lightSteps = m_volumeLightSteps->value();
    const QColor scatter =
        m_volumeColor->property("volumeColor").value<QColor>();
    const QColor emission =
        m_volumeEmissionColor->property("volumeColor").value<QColor>();
    material.color[0] = static_cast<float>(scatter.redF());
    material.color[1] = static_cast<float>(scatter.greenF());
    material.color[2] = static_cast<float>(scatter.blueF());
    material.emissionColor[0] = static_cast<float>(emission.redF());
    material.emissionColor[1] = static_cast<float>(emission.greenF());
    material.emissionColor[2] = static_cast<float>(emission.blueF());
    Scene::SetVolumeNodeMaterial(static_cast<size_t>(nodeIndex), material);
}

void ScenePanel::applyVolumePlaybackInspector()
{
    const int nodeIndex = selectedNodeIndex();
    const auto &nodes = Scene::GetNodes();
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.size() ||
        nodes[static_cast<size_t>(nodeIndex)].volumeAssetId.empty()) {
        return;
    }
    Scene::VolumePlaybackSettings playback =
        nodes[static_cast<size_t>(nodeIndex)].volumePlayback;
    playback.mode = static_cast<Scene::VolumePlaybackMode>(
        m_volumePlaybackMode->currentIndex());
    playback.fps = static_cast<float>(m_volumePlaybackFps->value());
    playback.loop = m_volumePlaybackLoop->isChecked();
    playback.frameOffset = m_volumeFrameOffset->value();
    Scene::SetVolumeNodePlayback(static_cast<size_t>(nodeIndex), playback);
}

int ScenePanel::selectedNodeIndex() const
{
    if (!m_nodeList) {
        return -1;
    }

    QTreeWidgetItem *current = m_nodeList->currentItem();
    if (!current) {
        return -1;
    }

    const QVariant nodeIndexData = current->data(kNodeNameColumn, kNodeIndexRole);
    return nodeIndexData.isValid() ? nodeIndexData.toInt() : -1;
}

void ScenePanel::requestDeleteSelectedNode()
{
    const std::vector<size_t> selectedLightIndices =
        Scene::GetSelectedLightIndices();
    if (!selectedLightIndices.empty()) {
        Scene::RemoveLightInstances(selectedLightIndices);
        refreshSceneList();
        return;
    }

    const int nodeIndex = selectedNodeIndex();
    if (nodeIndex < 0) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Delete Scene Node"),
        tr("Are you sure you want to delete this node?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    Scene::DeleteNode(static_cast<size_t>(nodeIndex));
    refreshSceneList();
}

void ScenePanel::showNodeContextMenu(const QPoint &pos)
{
    if (!m_nodeList || IsSceneIoJobActive()) {
        return;
    }

    QMenu menu(this);
    auto *transformMenu = menu.addMenu(tr("Transform"));
    auto *mirrorMenu = transformMenu->addMenu(tr("Mirror"));

    const bool hasSelection = !Scene::GetSelectedNodeIndices().empty();
    const auto addMirrorAction = [&](const QString &label,
                                     const QString &toolTip,
                                     Scene::MirrorAxis axis) {
        QAction *action = mirrorMenu->addAction(label, this, [this, axis]() {
            Scene::MirrorSpace space =
                Scene::GetGizmoSpace() == Scene::GizmoSpace::Local
                    ? Scene::MirrorSpace::Local
                    : Scene::MirrorSpace::World;
            if (Scene::MirrorSelectedNodes(axis,
                                           Scene::MirrorPivot::SelectionCenter,
                                           space)) {
                refreshSceneList();
            }
        });
        action->setToolTip(toolTip);
        action->setEnabled(hasSelection);
    };

    addMirrorAction(tr("Flip X"), tr("Mirror across the YZ plane"),
                    Scene::MirrorAxis::X);
    addMirrorAction(tr("Flip Y"), tr("Mirror across the XZ plane"),
                    Scene::MirrorAxis::Y);
    addMirrorAction(tr("Flip Z"), tr("Mirror across the XY plane"),
                    Scene::MirrorAxis::Z);

    // Phase 6 scene-portability: per-node library link actions. Available when a
    // single importable model node is the context target.
    const int contextNodeIndex = selectedNodeIndex();
    if (contextNodeIndex >= 0) {
        for (const Scene::SceneAssetLink &link : Scene::GetSceneAssetLinks()) {
            if (static_cast<int>(link.nodeIndex) != contextNodeIndex) {
                continue;
            }
            menu.addSeparator();
            auto *linkMenu = menu.addMenu(tr("Library Link"));
            if (link.linkRecorded && !link.inLibrary) {
                QAction *header =
                    linkMenu->addAction(tr("Library asset is missing"));
                header->setEnabled(false);
                linkMenu->addSeparator();
            }
            QAction *save = linkMenu->addAction(
                tr("Save Embedded → Library"), this,
                [this, contextNodeIndex]() { saveNodeToLibrary(contextNodeIndex); });
            save->setToolTip(tr("Add this node's embedded geometry to the asset "
                                "library and link it."));
            QAction *relink = linkMenu->addAction(
                tr("Relink to Library Asset…"), this,
                [this, contextNodeIndex]() {
                    relinkNodeToLibrary(contextNodeIndex);
                });
            relink->setToolTip(tr("Point this node at an existing library Model "
                                  "asset (metadata only)."));
            if (link.linkRecorded) {
                QAction *clear = linkMenu->addAction(
                    tr("Clear Library Link"), this, [this, contextNodeIndex]() {
                        Scene::RelinkNodeToLibraryAsset(
                            static_cast<size_t>(contextNodeIndex),
                            assetlib::AssetId{});
                        refreshSceneList();
                    });
                clear->setToolTip(tr("Forget the recorded library AssetId."));
            }
            break;
        }
    }

    menu.exec(m_nodeList->viewport()->mapToGlobal(pos));
}

void ScenePanel::scheduleRefresh()
{
    m_treeDirty = true;
}

void ScenePanel::refreshSceneList()
{
    m_syncing = true;
    const QSignalBlocker treeSignalBlocker(m_nodeList);
    const bool sceneIoActive = IsSceneIoJobActive();
    const bool importActive = Scene::IsImportInProgress();
    m_lastSceneIoActive = sceneIoActive;
    m_lastImportActive = importActive;
    m_treeDirty = false;

    if (sceneIoActive) {
        m_importProgress->hide();
        m_importStatusLabel->hide();
        m_importButton->setEnabled(false);
        m_addPlaneButton->setEnabled(false);
        m_frameSelectedButton->setEnabled(false);
        m_explodeButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
        m_nodeList->setEnabled(false);
        m_statusLabel->setText(tr("Scene I/O in progress..."));
        m_syncing = false;
        return;
    }

    Scene::ProcessPendingImport();

    m_importButton->setEnabled(true);
    m_addPlaneButton->setEnabled(true);
    m_frameSelectedButton->setEnabled(
        !Scene::GetSelectedNodeIndices().empty() ||
        !Scene::GetSelectedLightIndices().empty());
    m_nodeList->setEnabled(true);

    const bool importing = importActive;
    if (importing) {
        float progress = Scene::GetImportProgress();
        if (progress < 0.0f) {
            progress = 0.0f;
        } else if (progress > 1.0f) {
            progress = 1.0f;
        }
        m_importProgress->setValue(static_cast<int>(progress * 100.0f + 0.5f));
        m_importStatusLabel->setText(
            QString::fromStdString(Scene::GetImportStatus()));
        m_importProgress->show();
        m_importStatusLabel->show();
    } else {
        m_importProgress->hide();
        m_importStatusLabel->hide();
    }

    const auto &nodes = Scene::GetNodes();
    std::vector<int> selectedRows;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].selected) {
            selectedRows.push_back(static_cast<int>(index));
        }
    }
    const std::unordered_set<int> selectedSet(selectedRows.begin(), selectedRows.end());

    QTreeWidgetItem *currentSelectedItem = nullptr;

    const uint64_t structureSignature = BuildTreeStructureSignature(nodes);
    if (structureSignature != m_treeStructureSignature) {
        const TreeUiState treeState = CaptureTreeUiState(m_nodeList);
        m_treeStructureSignature = structureSignature;

        std::vector<std::vector<size_t>> children(nodes.size());
        std::vector<size_t> liveRoots;
        std::vector<size_t> regularRoots;
        for (size_t index = 0; index < nodes.size(); ++index) {
            const bool liveLinkGroup = nodes[index].liveLinkManaged;
            const size_t parentIndex = nodes[index].parentIndex;
            if (parentIndex < nodes.size() &&
                nodes[parentIndex].liveLinkManaged == liveLinkGroup) {
                children[parentIndex].push_back(index);
            } else if (liveLinkGroup) {
                liveRoots.push_back(index);
            } else {
                regularRoots.push_back(index);
            }
        }

        m_nodeList->clear();
        auto *liveSyncRoot = new QTreeWidgetItem(m_nodeList);
        liveSyncRoot->setText(0, tr("Live Sync"));
        liveSyncRoot->setIcon(0, MakeOutlinerIcon(OutlinerIcon::LiveSync));
        liveSyncRoot->setFlags(liveSyncRoot->flags() & ~Qt::ItemIsSelectable);
        QFont liveSyncFont = liveSyncRoot->font(0);
        liveSyncFont.setBold(true);
        liveSyncRoot->setFont(0, liveSyncFont);
        liveSyncRoot->setForeground(0, QBrush(QColor(132, 140, 144)));
        liveSyncRoot->setExpanded(treeState.liveSyncExpanded);

        std::function<void(size_t, QTreeWidgetItem *, bool)> addNodeRecursive;
        addNodeRecursive = [&](size_t index, QTreeWidgetItem *parentItem, bool liveLinkGroup) {
            const Scene::Node &node = nodes[index];
            QTreeWidgetItem *item = nullptr;
            if (parentItem) {
                item = new QTreeWidgetItem(parentItem);
            } else if (liveLinkGroup) {
                item = new QTreeWidgetItem(liveSyncRoot);
            } else {
                item = new QTreeWidgetItem(m_nodeList);
            }

            item->setText(kNodeNameColumn, BuildNodeLabel(node));
            item->setIcon(kNodeNameColumn, NodeOutlinerIcon(node));
            item->setIcon(
                kNodeVisibilityColumn,
                MakeOutlinerIcon(node.visible ? OutlinerIcon::Visible
                                              : OutlinerIcon::Hidden));
            item->setIcon(
                kNodeLockColumn,
                MakeOutlinerIcon(node.selectionLocked ? OutlinerIcon::Locked
                                                      : OutlinerIcon::Unlocked));
            item->setTextAlignment(kNodeVisibilityColumn, Qt::AlignCenter);
            item->setTextAlignment(kNodeLockColumn, Qt::AlignCenter);
            const QString details =
                !node.volumeAssetId.empty()
                    ? tr("Volume node")
                    : node.importGroupRoot
                          ? tr("Import group - %1 material slot(s)")
                                .arg(static_cast<int>(
                                    node.linkedMaterialIndices.size()))
                          : tr("%1 mesh(es)")
                                .arg(static_cast<int>(node.meshIndices.size()));
            item->setToolTip(kNodeNameColumn, details);
            item->setData(kNodeNameColumn, kNodeBaseTooltipRole, details);
            item->setToolTip(
                kNodeVisibilityColumn,
                node.visible
                    ? tr("Visible in viewport and render. Click to hide this branch.")
                    : tr("Hidden in viewport and render. Click to show this branch."));
            item->setToolTip(kNodeLockColumn,
                             node.selectionLocked
                                 ? tr("Selection locked. Click to allow direct child selection.")
                                 : tr("Selection unlocked. Click to select the group when descendants are hit."));
            item->setData(kNodeNameColumn, kNodeIndexRole, static_cast<int>(index));
            if (!node.visible) {
                item->setForeground(kNodeNameColumn,
                                    QBrush(QColor(112, 120, 124)));
            }
            if (treeState.expandedNodeIndices.find(static_cast<int>(index)) !=
                treeState.expandedNodeIndices.end()) {
                item->setExpanded(true);
            }
            const bool nodeSelected =
                selectedSet.find(static_cast<int>(index)) != selectedSet.end();
            if (nodeSelected) {
                item->setSelected(true);
                if (!currentSelectedItem) {
                    currentSelectedItem = item;
                }
            }

            for (size_t childIndex : children[index]) {
                addNodeRecursive(childIndex, item, liveLinkGroup);
            }
        };

        for (size_t index : liveRoots) {
            addNodeRecursive(index, nullptr, true);
        }
        for (size_t index : regularRoots) {
            addNodeRecursive(index, nullptr, false);
        }
    } else {
        for (int topLevelIndex = 0; topLevelIndex < m_nodeList->topLevelItemCount(); ++topLevelIndex) {
            SyncTreeSelection(m_nodeList->topLevelItem(topLevelIndex),
                              selectedSet,
                              &currentSelectedItem);
        }
    }

    if (currentSelectedItem) {
        for (QTreeWidgetItem *ancestor = currentSelectedItem->parent(); ancestor;
             ancestor = ancestor->parent()) {
            ancestor->setExpanded(true);
        }
        m_nodeList->setCurrentItem(currentSelectedItem, 0,
                                   QItemSelectionModel::NoUpdate);
    } else {
        m_nodeList->clearSelection();
    }

    const int primarySelectedRow =
        selectedRows.empty() ? -1 : selectedRows.front();
    if (primarySelectedRow >= 0 &&
        primarySelectedRow < static_cast<int>(nodes.size()) &&
        Scene::CanReimportNode(static_cast<size_t>(primarySelectedRow))) {
        const QString sourcePath = QString::fromStdString(nodes[static_cast<size_t>(primarySelectedRow)].sourcePath);
        const QString fileName = QFileInfo(sourcePath).fileName().toHtmlEscaped();
        m_sourceLabel->setText(tr("Linked source: <a href=\"reimport\">%1</a>").arg(fileName));
        m_sourceLabel->setToolTip(sourcePath);
        m_sourceLabel->show();
        m_reimportButton->setEnabled(!importing);
    } else {
        m_sourceLabel->hide();
        m_sourceLabel->clear();
        m_reimportButton->setEnabled(false);
    }

    m_statusLabel->setText(
        tr("Nodes: %1\nLights: %2\nStatus: %3")
            .arg(static_cast<int>(nodes.size()))
            .arg(static_cast<int>(Scene::GetLightInstances().size()))
            .arg(QString::fromStdString(Scene::LastStatus())));

    m_deleteButton->setEnabled(!selectedRows.empty());
    const bool canExplode =
        selectedRows.size() == 1 &&
        selectedRows.front() >= 0 &&
        selectedRows.front() < static_cast<int>(nodes.size()) &&
        Scene::CanExplodeNodeMeshes(
            static_cast<size_t>(selectedRows.front()));
    m_explodeButton->setEnabled(canExplode && !importing);
    applyLinkDecorations();
    m_syncing = false;
    syncVolumeMaterialInspector();
}

void ScenePanel::applyLinkDecorations()
{
    if (!m_nodeList) {
        return;
    }
    // Map node index -> library link status for this scene. A node whose
    // recorded library AssetId no longer resolves is flagged so the user can
    // Save it back to the library or relink it (the geometry itself is always
    // embedded and renders regardless).
    std::unordered_map<int, Scene::SceneAssetLink> linkByNode;
    for (const Scene::SceneAssetLink &link : Scene::GetSceneAssetLinks()) {
        linkByNode.emplace(static_cast<int>(link.nodeIndex), link);
    }

    const QColor missingColor(0xE0, 0x8A, 0x3C); // warm amber
    std::function<void(QTreeWidgetItem *)> decorate = [&](QTreeWidgetItem *item) {
        if (!item) {
            return;
        }
        const QVariant nodeIndexData = item->data(kNodeNameColumn, kNodeIndexRole);
        if (nodeIndexData.isValid()) {
            const int nodeIndex = nodeIndexData.toInt();
            auto it = linkByNode.find(nodeIndex);
            const bool missing =
                it != linkByNode.end() && it->second.linkRecorded &&
                !it->second.inLibrary;
            if (missing) {
                item->setForeground(kNodeNameColumn, QBrush(missingColor));
                item->setToolTip(
                    kNodeNameColumn,
                    tr("Library asset missing — geometry is embedded and "
                       "renders, but the library link is broken. Right-click to "
                       "Save to Library or Relink."));
            } else {
                const auto &nodes = Scene::GetNodes();
                const bool hidden =
                    nodeIndex >= 0 &&
                    nodeIndex < static_cast<int>(nodes.size()) &&
                    !nodes[static_cast<size_t>(nodeIndex)].visible;
                if (hidden) {
                    item->setForeground(kNodeNameColumn,
                                        QBrush(QColor(112, 120, 124)));
                } else {
                    item->setData(kNodeNameColumn, Qt::ForegroundRole,
                                  QVariant());
                }
                item->setToolTip(
                    kNodeNameColumn,
                    item->data(kNodeNameColumn, kNodeBaseTooltipRole)
                        .toString());
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            decorate(item->child(i));
        }
    };
    for (int i = 0; i < m_nodeList->topLevelItemCount(); ++i) {
        decorate(m_nodeList->topLevelItem(i));
    }
}

void ScenePanel::relinkNodeToLibrary(int nodeIndex)
{
    assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
    if (!reg) {
        QMessageBox::warning(this, tr("Relink"),
                             tr("The asset library is unavailable."));
        return;
    }
    assetlib::AssetQuery query;
    query.type = assetlib::AssetType::Model;
    const std::vector<assetlib::AssetId> ids = reg->SearchAssets(query);
    if (ids.empty()) {
        QMessageBox::information(
            this, tr("Relink"),
            tr("The library has no Model assets to relink to. Use "
               "\"Save to Library\" to add this node's geometry instead."));
        return;
    }
    QStringList labels;
    for (const assetlib::AssetId &id : ids) {
        const assetlib::AssetMetadata *meta = reg->Get(id);
        const QString name = meta ? QString::fromStdString(meta->displayName)
                                  : QString::fromStdString(id.ToString());
        labels << name;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Relink to Library Asset"),
        tr("Choose the library Model asset to link this node to:"), labels, 0,
        false, &ok);
    if (!ok) {
        return;
    }
    const int chosenRow = labels.indexOf(chosen);
    if (chosenRow < 0 || chosenRow >= static_cast<int>(ids.size())) {
        return;
    }
    if (Scene::RelinkNodeToLibraryAsset(static_cast<size_t>(nodeIndex),
                                        ids[static_cast<size_t>(chosenRow)])) {
        refreshSceneList();
    }
}

void ScenePanel::saveNodeToLibrary(int nodeIndex)
{
    const assetlib::AssetId id =
        Scene::SaveNodeAsLibraryAsset(static_cast<size_t>(nodeIndex));
    if (id.valid()) {
        QMessageBox::information(
            this, tr("Save to Library"),
            tr("Added this node's geometry to the asset library and linked it."));
        refreshSceneList();
    } else {
        QMessageBox::warning(
            this, tr("Save to Library"),
            tr("Could not save this node to the asset library."));
    }
}
