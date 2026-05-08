#include "ScenePanel.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>
#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

extern HWND g_hwnd;

namespace {

constexpr int kNodeIndexRole = Qt::UserRole;
constexpr int kNodeNameColumn = 0;
constexpr int kNodeLockColumn = 1;

struct TreeUiState {
    bool liveSyncExpanded = true;
    std::unordered_set<int> expandedNodeIndices;
};

QString BuildNodeLabel(const Scene::Node &node)
{
    QString label = QString::fromStdString(node.name);
    label += QObject::tr(" (%1 meshes)").arg(static_cast<int>(node.meshIndices.size()));
    if (!node.visible) {
        label += QObject::tr(" [hidden]");
    }
    return label;
}

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

    auto *buttonGrid = new QGridLayout();
    buttonGrid->setContentsMargins(0, 0, 0, 0);
    buttonGrid->setSpacing(4);
    
    m_importButton = new QPushButton(tr("Import Model"), this);
    m_reimportButton = new QPushButton(tr("Reimport Selected"), this);
    m_addPlaneButton = new QPushButton(tr("Add Ground Plane"), this);
    m_deleteButton = new QPushButton(tr("Delete Selected"), this);
    
    buttonGrid->addWidget(m_importButton, 0, 0);
    buttonGrid->addWidget(m_reimportButton, 0, 1);
    buttonGrid->addWidget(m_addPlaneButton, 1, 0);
    buttonGrid->addWidget(m_deleteButton, 1, 1);
    layout->addLayout(buttonGrid);

    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextFormat(Qt::RichText);
    m_sourceLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_sourceLabel->setOpenExternalLinks(false);
    layout->addWidget(m_sourceLabel);

    m_nodeList = new QTreeWidget(this);
    m_nodeList->setColumnCount(2);
    m_nodeList->setHeaderHidden(true);
    m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeList->header()->setSectionResizeMode(kNodeNameColumn, QHeaderView::Stretch);
    m_nodeList->header()->setSectionResizeMode(kNodeLockColumn, QHeaderView::ResizeToContents);
    layout->addWidget(m_nodeList);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    connect(m_importButton, &QPushButton::clicked, this, []() {
        HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
        if (!owner) {
            owner = g_hwnd;
        }
        Scene::ImportModelWithDialog(owner);
    });
    connect(m_reimportButton, &QPushButton::clicked, this, [this]() {
        const int nodeIndex = selectedNodeIndex();
        if (nodeIndex >= 0) {
            Scene::ReimportNode(static_cast<size_t>(nodeIndex));
            refreshSceneList();
        }
    });
    connect(m_addPlaneButton, &QPushButton::clicked, this, []() {
        Scene::AddDefaultPlane(0.0f);
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const int nodeIndex = selectedNodeIndex();
        if (nodeIndex >= 0) {
            Scene::DeleteNode(static_cast<size_t>(nodeIndex));
            refreshSceneList();
        }
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
    });
    connect(m_nodeList, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int column) {
        if (m_syncing || !item || column != kNodeLockColumn) {
            return;
        }
        const QVariant nodeIndexData = item->data(kNodeNameColumn, kNodeIndexRole);
        if (!nodeIndexData.isValid()) {
            return;
        }
        const size_t nodeIndex = static_cast<size_t>(nodeIndexData.toInt());
        const auto &nodes = Scene::GetNodes();
        if (nodeIndex >= nodes.size()) {
            return;
        }
        Scene::SetNodeSelectionLocked(nodeIndex,
                                      !nodes[nodeIndex].selectionLocked);
        refreshSceneList();
    });
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
        m_deleteButton->setEnabled(false);
        m_nodeList->setEnabled(false);
        m_statusLabel->setText(tr("Scene I/O in progress..."));
        m_syncing = false;
        return;
    }

    Scene::ProcessPendingImport();

    m_importButton->setEnabled(true);
    m_addPlaneButton->setEnabled(true);
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
        liveSyncRoot->setText(kNodeLockColumn, QString());
        liveSyncRoot->setFlags(liveSyncRoot->flags() & ~Qt::ItemIsSelectable);
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
            item->setText(kNodeLockColumn, node.selectionLocked ? tr("Lock") : tr("Free"));
            item->setToolTip(kNodeLockColumn,
                             node.selectionLocked
                                 ? tr("Click to allow selecting child meshes")
                                 : tr("Click to select this node when descendants are hit"));
            item->setData(kNodeNameColumn, kNodeIndexRole, static_cast<int>(index));
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
            .arg(static_cast<int>(Scene::GetLights().size()))
            .arg(QString::fromStdString(Scene::LastStatus())));

    m_deleteButton->setEnabled(!selectedRows.empty());
    m_syncing = false;
}
