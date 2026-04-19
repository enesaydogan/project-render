#include "ScenePanel.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <functional>

extern HWND g_hwnd;

namespace {

constexpr int kNodeIndexRole = Qt::UserRole;

QString BuildNodeLabel(const Scene::Node &node)
{
    QString label = QString::fromStdString(node.name);
    label += QObject::tr(" (%1 meshes)").arg(static_cast<int>(node.meshIndices.size()));
    if (!node.visible) {
        label += QObject::tr(" [hidden]");
    }
    return label;
}

}

ScenePanel::ScenePanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshSceneList();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        refreshSceneList();
    });
    m_refreshTimer->start(500);
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
    m_nodeList->setColumnCount(1);
    m_nodeList->setHeaderHidden(true);
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
    connect(m_nodeList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (m_syncing || !current) {
            return;
        }
        const QVariant nodeIndexData = current->data(0, kNodeIndexRole);
        if (!nodeIndexData.isValid()) {
            return;
        }
        Scene::SelectNode(static_cast<size_t>(nodeIndexData.toInt()));
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

    const QVariant nodeIndexData = current->data(0, kNodeIndexRole);
    return nodeIndexData.isValid() ? nodeIndexData.toInt() : -1;
}

void ScenePanel::refreshSceneList()
{
    m_syncing = true;

    if (IsSceneIoJobActive()) {
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

    const bool importing = Scene::IsImportInProgress();
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
    int selectedRow = -1;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].selected) {
            selectedRow = static_cast<int>(index);
            break;
        }
    }

    m_nodeList->clear();
    auto *liveSyncRoot = new QTreeWidgetItem(m_nodeList);
    liveSyncRoot->setText(0, tr("Live Sync"));
    liveSyncRoot->setFlags(liveSyncRoot->flags() & ~Qt::ItemIsSelectable);
    liveSyncRoot->setExpanded(true);

    QTreeWidgetItem *selectedItem = nullptr;
    auto isGroupRoot = [&](size_t index, bool liveLinkGroup) {
        if (index >= nodes.size() || nodes[index].liveLinkManaged != liveLinkGroup) {
            return false;
        }
        const size_t parentIndex = nodes[index].parentIndex;
        return parentIndex == static_cast<size_t>(-1) ||
               parentIndex >= nodes.size() ||
               nodes[parentIndex].liveLinkManaged != liveLinkGroup;
    };

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

        item->setText(0, BuildNodeLabel(node));
        item->setData(0, kNodeIndexRole, static_cast<int>(index));
        if (static_cast<int>(index) == selectedRow) {
            selectedItem = item;
        }

        for (size_t childIndex = 0; childIndex < nodes.size(); ++childIndex) {
            if (nodes[childIndex].parentIndex != index ||
                nodes[childIndex].liveLinkManaged != liveLinkGroup) {
                continue;
            }
            addNodeRecursive(childIndex, item, liveLinkGroup);
        }
    };

    for (size_t index = 0; index < nodes.size(); ++index) {
        if (isGroupRoot(index, true)) {
            addNodeRecursive(index, nullptr, true);
        }
    }
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (isGroupRoot(index, false)) {
            addNodeRecursive(index, nullptr, false);
        }
    }

    if (selectedItem) {
        m_nodeList->setCurrentItem(selectedItem);
    } else {
        m_nodeList->clearSelection();
    }

    if (selectedRow >= 0 && selectedRow < static_cast<int>(nodes.size()) &&
        Scene::CanReimportNode(static_cast<size_t>(selectedRow))) {
        const QString sourcePath = QString::fromStdString(nodes[static_cast<size_t>(selectedRow)].sourcePath);
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

    m_deleteButton->setEnabled(selectedNodeIndex() >= 0);
    m_syncing = false;
}
