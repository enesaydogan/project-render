#include "ScenePanel.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
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
constexpr int kSceneToolButtonSize = 30;

enum class SceneToolIcon {
    ImportModel,
    Reimport,
    AddGroundPlane,
    ExplodeSelected,
    DeleteSelected
};

struct TreeUiState {
    bool liveSyncExpanded = true;
    std::unordered_set<int> expandedNodeIndices;
};

QString BuildNodeLabel(const Scene::Node &node)
{
    QString label = QString::fromStdString(node.name);
    if (!node.volumeAssetId.empty()) {
        label += QObject::tr(" (volume)");
    } else {
        label += QObject::tr(" (%1 meshes)").arg(static_cast<int>(node.meshIndices.size()));
    }
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

    m_nodeList = new QTreeWidget(this);
    m_nodeList->setColumnCount(2);
    m_nodeList->setHeaderHidden(true);
    m_nodeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nodeList->header()->setSectionResizeMode(kNodeNameColumn, QHeaderView::Stretch);
    m_nodeList->header()->setSectionResizeMode(kNodeLockColumn, QHeaderView::ResizeToContents);
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
    m_volumeDensity->setValue(m.densityScale);
    m_volumeAbsorption->setValue(m.absorption);
    m_volumeScattering->setValue(m.scattering);
    m_volumeAmbient->setValue(m.ambient);
    m_volumeEmission->setValue(m.emissionStrength);
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
    m_syncing = false;
    syncVolumeMaterialInspector();
}
