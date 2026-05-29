#include "LightsPanel.h"

#include "ArchColorDialog.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QKeySequence>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kProtoItemType = QTreeWidgetItem::UserType;
constexpr int kInstanceItemType = QTreeWidgetItem::UserType + 1;
constexpr int kLightToolButtonSize = 30;
constexpr int kLightToolIconPx = 18;

enum class LightToolIcon {
    Point,
    Spot,
    Rect,
    Disk,
    IES,
    AddInstance,
    Copy,
    MergeCopies,
    SelectGizmo,
    Remove,
    MoveToSurface,
};

QIcon MakeLightToolIcon(LightToolIcon icon)
{
    QPixmap pixmap(kLightToolIconPx, kLightToolIconPx);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor stroke(206, 214, 218);
    const QColor accent(88, 208, 244);
    const QColor muted(132, 140, 144);

    QPen strokePen(stroke, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen accentPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen mutedPen(muted, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    painter.setPen(strokePen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case LightToolIcon::Point: {
        painter.setPen(accentPen);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(9.0, 9.0), 2.3, 2.3);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(strokePen);
        for (int i = 0; i < 8; ++i) {
            const double theta = i * (kPi / 4.0);
            const double cx = 9.0 + std::cos(theta) * 5.0;
            const double cy = 9.0 + std::sin(theta) * 5.0;
            const double ex = 9.0 + std::cos(theta) * 7.5;
            const double ey = 9.0 + std::sin(theta) * 7.5;
            painter.drawLine(QPointF(cx, cy), QPointF(ex, ey));
        }
        break;
    }
    case LightToolIcon::Spot: {
        QPainterPath cone;
        cone.moveTo(9.0, 3.0);
        cone.lineTo(3.5, 14.5);
        cone.lineTo(14.5, 14.5);
        cone.closeSubpath();
        painter.drawPath(cone);
        painter.setPen(accentPen);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(9.0, 3.5), 1.7, 1.7);
        break;
    }
    case LightToolIcon::Rect: {
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        painter.drawRect(QRectF(3.0, 5.0, 12.0, 8.0));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(3.0, 14.5), QPointF(15.0, 14.5));
        painter.drawLine(QPointF(5.5, 14.5), QPointF(5.5, 16.5));
        painter.drawLine(QPointF(9.0, 14.5), QPointF(9.0, 16.5));
        painter.drawLine(QPointF(12.5, 14.5), QPointF(12.5, 16.5));
        break;
    }
    case LightToolIcon::Disk: {
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        painter.drawEllipse(QPointF(9.0, 8.0), 6.0, 3.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(3.0, 14.5), QPointF(15.0, 14.5));
        painter.drawLine(QPointF(5.5, 14.5), QPointF(5.5, 16.5));
        painter.drawLine(QPointF(9.0, 14.5), QPointF(9.0, 16.5));
        painter.drawLine(QPointF(12.5, 14.5), QPointF(12.5, 16.5));
        break;
    }
    case LightToolIcon::IES: {
        painter.drawEllipse(QPointF(9.0, 5.5), 3.5, 3.0);
        painter.drawEllipse(QPointF(9.0, 12.5), 5.5, 3.5);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(9.0, 2.5), QPointF(9.0, 16.0));
        break;
    }
    case LightToolIcon::AddInstance: {
        painter.drawEllipse(QPointF(6.5, 9.0), 3.2, 3.2);
        painter.drawEllipse(QPointF(11.5, 9.0), 3.2, 3.2);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(8.2, 9.0), QPointF(9.8, 9.0));
        break;
    }
    case LightToolIcon::Copy: {
        painter.drawRect(QRectF(3.5, 3.5, 8.5, 8.5));
        painter.setPen(accentPen);
        painter.drawRect(QRectF(6.0, 6.0, 8.5, 8.5));
        break;
    }
    case LightToolIcon::MergeCopies: {
        painter.drawLine(QPointF(3.0, 4.0), QPointF(9.0, 9.0));
        painter.drawLine(QPointF(15.0, 4.0), QPointF(9.0, 9.0));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(9.0, 9.0), QPointF(9.0, 15.0));
        painter.drawLine(QPointF(7.0, 13.0), QPointF(9.0, 15.0));
        painter.drawLine(QPointF(11.0, 13.0), QPointF(9.0, 15.0));
        break;
    }
    case LightToolIcon::SelectGizmo: {
        painter.drawEllipse(QPointF(9.0, 9.0), 5.0, 5.0);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(9.0, 1.5), QPointF(9.0, 5.0));
        painter.drawLine(QPointF(9.0, 13.0), QPointF(9.0, 16.5));
        painter.drawLine(QPointF(1.5, 9.0), QPointF(5.0, 9.0));
        painter.drawLine(QPointF(13.0, 9.0), QPointF(16.5, 9.0));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(9.0, 9.0), 1.4, 1.4);
        break;
    }
    case LightToolIcon::Remove: {
        painter.drawLine(QPointF(4.0, 5.5), QPointF(14.0, 5.5));
        painter.drawLine(QPointF(7.0, 3.5), QPointF(11.0, 3.5));
        painter.drawLine(QPointF(5.5, 6.5), QPointF(6.5, 15.0));
        painter.drawLine(QPointF(12.5, 6.5), QPointF(11.5, 15.0));
        painter.drawLine(QPointF(5.5, 15.0), QPointF(12.5, 15.0));
        painter.setPen(mutedPen);
        painter.drawLine(QPointF(9.0, 7.5), QPointF(9.0, 13.5));
        break;
    }
    case LightToolIcon::MoveToSurface: {
        painter.drawLine(QPointF(3.0, 14.5), QPointF(15.0, 14.5));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(9.0, 3.0), QPointF(9.0, 11.5));
        painter.drawLine(QPointF(6.0, 8.5), QPointF(9.0, 11.5));
        painter.drawLine(QPointF(12.0, 8.5), QPointF(9.0, 11.5));
        break;
    }
    }

    return QIcon(pixmap);
}

QToolButton *CreateLightToolButton(QWidget *parent,
                                   const QString &toolTip,
                                   LightToolIcon icon)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(toolTip);
    button->setStatusTip(toolTip);
    button->setIcon(MakeLightToolIcon(icon));
    button->setIconSize(QSize(kLightToolIconPx, kLightToolIconPx));
    button->setFixedSize(kLightToolButtonSize, kLightToolButtonSize);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QDoubleSpinBox *CreateDoubleSpinBox(double minValue,
                                    double maxValue,
                                    double step,
                                    int decimals)
{
    auto *spinBox = new QDoubleSpinBox();
    spinBox->setRange(minValue, maxValue);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(decimals);
    spinBox->setAccelerated(true);
    return spinBox;
}

QWidget *CreateVec3Editor(QDoubleSpinBox *&x,
                          QDoubleSpinBox *&y,
                          QDoubleSpinBox *&z,
                          double minValue,
                          double maxValue,
                          double step,
                          int decimals)
{
    auto *row = new QWidget();
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    x = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
    y = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
    z = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
    x->setPrefix(QStringLiteral("X: "));
    y->setPrefix(QStringLiteral("Y: "));
    z->setPrefix(QStringLiteral("Z: "));
    layout->addWidget(x);
    layout->addWidget(y);
    layout->addWidget(z);
    return row;
}

} // namespace

LightsPanel::LightsPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshLights();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!hasPropertyEditorFocus()) {
            refreshLights();
        }
    });
    m_refreshTimer->start(250);
}

void LightsPanel::beginCreateForType(uint32_t lightType)
{
    // If the same type is already armed, treat the click as a toggle/cancel.
    if (Scene::IsLightPlacementActive() &&
        Scene::GetLightPlacementMode() == Scene::LightPlacementMode::Create &&
        m_armedCreateType == static_cast<int>(lightType)) {
        Scene::CancelLightPlacement();
        m_armedCreateType = -1;
        refreshLights();
        return;
    }
    m_armedCreateType = static_cast<int>(lightType);
    Scene::BeginCreateLightAtClick(static_cast<LightType>(lightType));
    refreshLights();
}

void LightsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_loadNotice = new QLabel(tr("Scene load in progress..."), this);
    m_loadNotice->setWordWrap(true);
    m_loadNotice->hide();
    layout->addWidget(m_loadNotice);

    // --- Toolbar with icon-based light creation buttons + placement status ---
    m_toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(m_toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(2);

    m_addPointButton = CreateLightToolButton(m_toolbar,
        tr("Add Point Light — click an icon, then click in the viewport to place"),
        LightToolIcon::Point);
    m_addSpotButton = CreateLightToolButton(m_toolbar,
        tr("Add Spot Light — click an icon, then click in the viewport to place"),
        LightToolIcon::Spot);
    m_addRectButton = CreateLightToolButton(m_toolbar,
        tr("Add Rect Area Light — click an icon, then click in the viewport to place"),
        LightToolIcon::Rect);
    m_addDiskButton = CreateLightToolButton(m_toolbar,
        tr("Add Disk Area Light — click an icon, then click in the viewport to place"),
        LightToolIcon::Disk);
    m_addIESButton = CreateLightToolButton(m_toolbar,
        tr("Add IES Light — pick an .ies profile, then click in the viewport to place"),
        LightToolIcon::IES);
    m_addPointButton->setCheckable(true);
    m_addSpotButton->setCheckable(true);
    m_addRectButton->setCheckable(true);
    m_addDiskButton->setCheckable(true);
    m_addIESButton->setCheckable(true);

    toolbarLayout->addWidget(m_addPointButton);
    toolbarLayout->addWidget(m_addSpotButton);
    toolbarLayout->addWidget(m_addRectButton);
    toolbarLayout->addWidget(m_addDiskButton);
    toolbarLayout->addWidget(m_addIESButton);

    auto *toolbarSep = new QFrame(m_toolbar);
    toolbarSep->setFrameShape(QFrame::VLine);
    toolbarSep->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(toolbarSep);

    m_placementStatusLabel = new QLabel(QString(), m_toolbar);
    m_placementStatusLabel->setStyleSheet(QStringLiteral("color: #58d0f4;"));
    m_placementStatusLabel->setWordWrap(true);
    toolbarLayout->addWidget(m_placementStatusLabel, 1);
    layout->addWidget(m_toolbar);

    // --- Lights tree (no group box wrapper, the tab is already labeled Lights) ---
    m_lightTree = new QTreeWidget(this);
    m_lightTree->setColumnCount(4);
    m_lightTree->setHeaderLabels({tr("Group / Instance"), tr("On"),
                                  tr("Count / Position"), tr("Profile")});
    m_lightTree->setRootIsDecorated(true);
    m_lightTree->setAlternatingRowColors(true);
    m_lightTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_lightTree->setMinimumHeight(120);
    layout->addWidget(m_lightTree, 1);

    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this,
            [this]() { removeSelectedItems(); });

    // --- Properties section ---
    m_propertiesGroup = new QWidget(this);
    auto *propertiesLayout = new QVBoxLayout(m_propertiesGroup);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    propertiesLayout->setSpacing(6);

    auto *propsHeader = new QLabel(tr("Properties"), m_propertiesGroup);
    propsHeader->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    propertiesLayout->addWidget(propsHeader);

    // --- Action row (icon buttons): Add Instance, Copy, Merge, Select, Remove ---
    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(2);
    m_addInstanceButton = CreateLightToolButton(m_propertiesGroup,
        tr("Add Instance — duplicate selected light sharing this group"),
        LightToolIcon::AddInstance);
    m_duplicateCopyButton = CreateLightToolButton(m_propertiesGroup,
        tr("Copy — duplicate selected light as an independent editable copy"),
        LightToolIcon::Copy);
    m_mergeCopiesButton = CreateLightToolButton(m_propertiesGroup,
        tr("Merge Copies — merge compatible copied groups back into this prototype"),
        LightToolIcon::MergeCopies);
    m_selectButton = CreateLightToolButton(m_propertiesGroup,
        tr("Select Gizmo for the selected instance"),
        LightToolIcon::SelectGizmo);
    m_removeButton = CreateLightToolButton(m_propertiesGroup,
        tr("Remove selected (Del)"),
        LightToolIcon::Remove);
    actionRow->addWidget(m_addInstanceButton);
    actionRow->addWidget(m_duplicateCopyButton);
    actionRow->addWidget(m_mergeCopiesButton);
    actionRow->addWidget(m_selectButton);
    actionRow->addWidget(m_removeButton);
    actionRow->addStretch(1);
    propertiesLayout->addLayout(actionRow);

    // --- Prototype shared properties ---
    m_protoPropsWidget = new QWidget(m_propertiesGroup);
    auto *protoLayout = new QVBoxLayout(m_protoPropsWidget);
    protoLayout->setContentsMargins(0, 0, 0, 0);
    protoLayout->setSpacing(4);

    auto *protoForm = new QFormLayout();
    protoForm->setContentsMargins(0, 0, 0, 0);
    protoForm->setSpacing(4);

    m_typeLabel = new QLabel(tr("None"), m_protoPropsWidget);
    m_typeLabel->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    protoForm->addRow(tr("Type"), m_typeLabel);

    m_nameEdit = new QLineEdit(m_protoPropsWidget);
    m_nameEdit->setMaxLength(63);
    protoForm->addRow(tr("Name"), m_nameEdit);

    m_protoEnabled = new QCheckBox(tr("Enabled"), m_protoPropsWidget);
    protoForm->addRow(m_protoEnabled);

    m_instCountLabel = new QLabel(tr("1 instance"), m_protoPropsWidget);
    protoForm->addRow(tr("Instances"), m_instCountLabel);

    m_colorButton = new QPushButton(QString(), m_protoPropsWidget);
    m_colorButton->setFixedSize(60, 22);
    m_colorButton->setToolTip(tr("Pick light color"));
    protoForm->addRow(tr("Color"), m_colorButton);

    m_intensity = CreateDoubleSpinBox(0.0, 1000000.0, 10.0, 2);
    protoForm->addRow(tr("Intensity"), m_intensity);

    m_radius = CreateDoubleSpinBox(0.0, 10.0, 0.01, 3);
    protoForm->addRow(tr("Radius"), m_radius);

    protoLayout->addLayout(protoForm);

    m_spotGroup = new QGroupBox(tr("Spot"), m_protoPropsWidget);
    auto *spotForm = new QFormLayout(m_spotGroup);
    spotForm->setContentsMargins(6, 12, 6, 6);
    spotForm->setSpacing(4);
    m_innerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    m_outerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    spotForm->addRow(tr("Inner Angle"), m_innerAngle);
    spotForm->addRow(tr("Outer Angle"), m_outerAngle);
    protoLayout->addWidget(m_spotGroup);

    m_areaGroup = new QGroupBox(tr("Area"), m_protoPropsWidget);
    auto *areaForm = new QFormLayout(m_areaGroup);
    areaForm->setContentsMargins(6, 12, 6, 6);
    areaForm->setSpacing(4);
    m_areaWidth = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    m_areaHeight = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    areaForm->addRow(tr("Width"), m_areaWidth);
    areaForm->addRow(tr("Height"), m_areaHeight);
    protoLayout->addWidget(m_areaGroup);

    m_iesGroup = new QGroupBox(tr("IES Profile"), m_protoPropsWidget);
    auto *iesLayout = new QVBoxLayout(m_iesGroup);
    iesLayout->setContentsMargins(6, 12, 6, 6);
    iesLayout->setSpacing(4);
    m_iesLabel = new QLabel(tr("No profile assigned"), m_iesGroup);
    m_iesLabel->setWordWrap(true);
    iesLayout->addWidget(m_iesLabel);

    auto *iesBtnRow = new QHBoxLayout();
    iesBtnRow->setSpacing(4);
    m_loadIESButton = new QPushButton(tr("Load IES..."), m_iesGroup);
    m_clearIESButton = new QPushButton(tr("Clear"), m_iesGroup);
    m_loadIESButton->setToolTip(tr("Load an .ies photometric profile file"));
    m_clearIESButton->setToolTip(tr("Remove the assigned IES profile"));
    iesBtnRow->addWidget(m_loadIESButton);
    iesBtnRow->addWidget(m_clearIESButton);
    iesLayout->addLayout(iesBtnRow);
    protoLayout->addWidget(m_iesGroup);

    propertiesLayout->addWidget(m_protoPropsWidget);

    // --- Instance transform properties ---
    m_instancePropsWidget = new QWidget(m_propertiesGroup);
    auto *instLayout = new QVBoxLayout(m_instancePropsWidget);
    instLayout->setContentsMargins(0, 0, 0, 0);
    instLayout->setSpacing(4);

    m_instanceLabel = new QLabel(tr("Instance Transform"), m_instancePropsWidget);
    m_instanceLabel->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    instLayout->addWidget(m_instanceLabel);

    m_instanceEnabled = new QCheckBox(tr("Enabled"), m_instancePropsWidget);
    instLayout->addWidget(m_instanceEnabled);

    // Position row with inline "click to move to surface" icon button.
    auto *posHeaderRow = new QHBoxLayout();
    posHeaderRow->setContentsMargins(0, 0, 0, 0);
    posHeaderRow->setSpacing(4);
    auto *posLabel = new QLabel(tr("Position"), m_instancePropsWidget);
    m_moveToSurfaceButton = CreateLightToolButton(m_instancePropsWidget,
        tr("Click-move to surface — click then pick a point in the viewport"),
        LightToolIcon::MoveToSurface);
    m_moveToSurfaceButton->setFixedSize(22, 22);
    m_moveToSurfaceButton->setIconSize(QSize(16, 16));
    m_moveToSurfaceButton->setCheckable(true);
    posHeaderRow->addWidget(posLabel);
    posHeaderRow->addStretch(1);
    posHeaderRow->addWidget(m_moveToSurfaceButton);
    instLayout->addLayout(posHeaderRow);

    auto *posRow = CreateVec3Editor(m_posX, m_posY, m_posZ, -10000.0, 10000.0, 0.1, 2);
    instLayout->addWidget(posRow);

    m_directionLabel = new QLabel(tr("Direction"), m_instancePropsWidget);
    m_directionRow = CreateVec3Editor(m_dirX, m_dirY, m_dirZ, -1000.0, 1000.0, 0.01, 3);
    instLayout->addWidget(m_directionLabel);
    instLayout->addWidget(m_directionRow);

    propertiesLayout->addWidget(m_instancePropsWidget);

    layout->addWidget(m_propertiesGroup, 2);

    // --- Connections ---

    connect(m_addPointButton, &QToolButton::clicked, this, [this]() {
        beginCreateForType(static_cast<uint32_t>(LightType::Omni));
    });
    connect(m_addSpotButton, &QToolButton::clicked, this, [this]() {
        beginCreateForType(static_cast<uint32_t>(LightType::Spot));
    });
    connect(m_addRectButton, &QToolButton::clicked, this, [this]() {
        beginCreateForType(static_cast<uint32_t>(LightType::AreaRect));
    });
    connect(m_addDiskButton, &QToolButton::clicked, this, [this]() {
        beginCreateForType(static_cast<uint32_t>(LightType::AreaDisk));
    });
    connect(m_addIESButton, &QToolButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load IES Profile"), QString(),
            tr("IES Files (*.ies);;All Files (*)"));
        if (path.isEmpty()) {
            refreshLights();
            return;
        }
        const int profIdx = Scene::LoadIESProfile(path.toStdString());
        if (profIdx < 0) {
            QMessageBox::warning(this, tr("IES Load Failed"),
                                 tr("Could not parse IES file:\n%1").arg(path));
            refreshLights();
            return;
        }
        m_armedCreateType = static_cast<int>(LightType::IES);
        Scene::BeginCreateLightAtClick(LightType::IES, profIdx);
        refreshLights();
    });

    connect(m_lightTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (m_syncing) return;
        if (!current) {
            Scene::SelectLight(-1);
            refreshLights();
            return;
        }
        if (current->type() == kInstanceItemType) {
            std::vector<size_t> selectedIndices;
            const int currentInstIdx = current->data(0, Qt::UserRole).toInt();
            const auto &insts = Scene::GetLightInstances();
            for (QTreeWidgetItem *item : m_lightTree->selectedItems()) {
                if (!item || item->type() != kInstanceItemType) {
                    continue;
                }
                const int instIdx = item->data(0, Qt::UserRole).toInt();
                if (instIdx >= 0 && instIdx < static_cast<int>(insts.size())) {
                    selectedIndices.push_back(static_cast<size_t>(instIdx));
                }
            }
            if (currentInstIdx >= 0 &&
                currentInstIdx < static_cast<int>(insts.size())) {
                const size_t currentIndex = static_cast<size_t>(currentInstIdx);
                selectedIndices.erase(
                    std::remove(selectedIndices.begin(), selectedIndices.end(),
                                currentIndex),
                    selectedIndices.end());
                selectedIndices.push_back(currentIndex);
            }
            Scene::SelectLights(selectedIndices);
        } else if (m_lightTree->selectedItems().empty()) {
            Scene::SelectLight(-1);
        }
        refreshLights();
    });

    connect(m_lightTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (m_syncing) return;
        std::vector<size_t> selectedIndices;
        const auto &insts = Scene::GetLightInstances();
        for (QTreeWidgetItem *item : m_lightTree->selectedItems()) {
            if (!item || item->type() != kInstanceItemType) {
                continue;
            }
            const int instIdx = item->data(0, Qt::UserRole).toInt();
            if (instIdx >= 0 && instIdx < static_cast<int>(insts.size())) {
                selectedIndices.push_back(static_cast<size_t>(instIdx));
            }
        }
        QTreeWidgetItem *current = m_lightTree->currentItem();
        if (current && current->type() == kInstanceItemType) {
            const int instIdx = current->data(0, Qt::UserRole).toInt();
            if (instIdx >= 0 && instIdx < static_cast<int>(insts.size())) {
                const size_t currentIndex = static_cast<size_t>(instIdx);
                selectedIndices.erase(
                    std::remove(selectedIndices.begin(), selectedIndices.end(),
                                currentIndex),
                    selectedIndices.end());
                selectedIndices.push_back(currentIndex);
            }
        }
        if (selectedIndices.empty()) {
            if (!m_lightTree->currentItem() ||
                m_lightTree->currentItem()->type() == kInstanceItemType) {
                Scene::SelectLight(-1);
                refreshLights();
            }
            return;
        }
        Scene::SelectLights(selectedIndices);
        refreshLights();
    });

    connect(m_lightTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_syncing || !item || column != 1) return;
        const bool enabled = (item->checkState(1) == Qt::Checked);
        if (item->type() == kProtoItemType) {
            const int protoIdx = item->data(0, Qt::UserRole).toInt();
            const auto &protos = Scene::GetLightPrototypes();
            if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
            LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
            proto.enabled = enabled;
            Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
        } else {
            const int instIdx = item->data(0, Qt::UserRole).toInt();
            const auto &insts = Scene::GetLightInstances();
            if (instIdx < 0 || instIdx >= static_cast<int>(insts.size())) return;
            LightInstance inst = insts[static_cast<size_t>(instIdx)];
            inst.enabled = enabled;
            Scene::UpdateLightInstance(static_cast<size_t>(instIdx), inst);
        }
        refreshLights();
    });

    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        std::snprintf(proto.name, sizeof(proto.name), "%s",
                      m_nameEdit->text().trimmed().toUtf8().constData());
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
        refreshLights();
    });

    connect(m_protoEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.enabled = enabled;
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
        refreshLights();
    });

    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;

        const LightPrototype originalProto = protos[static_cast<size_t>(protoIdx)];
        const QColor originalColor = m_currentColor;
        auto applyPreview = [this, protoIdx, originalProto](const QColor &color) {
            if (!color.isValid()) return;
            LightPrototype preview = originalProto;
            preview.color[0] = static_cast<float>(color.redF());
            preview.color[1] = static_cast<float>(color.greenF());
            preview.color[2] = static_cast<float>(color.blueF());
            Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), preview);
            m_currentColor = color;
            updateColorButton(m_currentColor);
        };
        auto restoreOriginal = [this, protoIdx, originalProto, originalColor]() {
            Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), originalProto);
            m_currentColor = originalColor;
            updateColorButton(m_currentColor);
        };
        ArchColorDialog::showColor(m_currentColor, this, tr("Select Light Color"),
                                   applyPreview, applyPreview, restoreOriginal);
    });

    connect(m_intensity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.intensity = static_cast<float>(m_intensity->value());
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
    });

    connect(m_radius, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.radius = static_cast<float>(m_radius->value());
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
    });

    connect(m_innerAngle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) return;
        if (m_outerAngle->value() < value) m_outerAngle->setValue(value);
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        const double innerRad = m_innerAngle->value() * (kPi / 180.0);
        const double outerRad = m_outerAngle->value() * (kPi / 180.0);
        proto.innerConeAngle = static_cast<float>(std::cos(innerRad));
        proto.outerConeAngle = static_cast<float>(std::cos(outerRad));
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
    });
    connect(m_outerAngle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) return;
        if (value < m_innerAngle->value()) m_outerAngle->setValue(m_innerAngle->value());
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        const double innerRad = m_innerAngle->value() * (kPi / 180.0);
        const double outerRad = m_outerAngle->value() * (kPi / 180.0);
        proto.innerConeAngle = static_cast<float>(std::cos(innerRad));
        proto.outerConeAngle = static_cast<float>(std::cos(outerRad));
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
    });

    auto applyArea = [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.areaExtents[0] = static_cast<float>(m_areaWidth->value());
        proto.areaExtents[1] = static_cast<float>(m_areaHeight->value());
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
    };
    connect(m_areaWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyArea](double) { applyArea(); });
    connect(m_areaHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyArea](double) { applyArea(); });

    connect(m_loadIESButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load IES Profile"), QString(),
            tr("IES Files (*.ies);;All Files (*)"));
        if (path.isEmpty()) return;
        const int profIdx = Scene::LoadIESProfile(path.toStdString());
        if (profIdx < 0) {
            QMessageBox::warning(this, tr("IES Load Failed"),
                                 tr("Could not parse IES file:\n%1").arg(path));
            return;
        }
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.iesProfileIndex = profIdx;
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
        refreshLights();
    });

    connect(m_clearIESButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        const auto &protos = Scene::GetLightPrototypes();
        if (protoIdx < 0 || protoIdx >= static_cast<int>(protos.size())) return;
        LightPrototype proto = protos[static_cast<size_t>(protoIdx)];
        proto.iesProfileIndex = -1;
        proto.iesAtlasIndex = -1;
        Scene::UpdateLightPrototype(static_cast<size_t>(protoIdx), proto);
        refreshLights();
    });

    auto applyPosition = [this]() {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        const auto &insts = Scene::GetLightInstances();
        if (instIdx < 0 || instIdx >= static_cast<int>(insts.size())) return;
        LightInstance inst = insts[static_cast<size_t>(instIdx)];
        inst.position[0] = static_cast<float>(m_posX->value());
        inst.position[1] = static_cast<float>(m_posY->value());
        inst.position[2] = static_cast<float>(m_posZ->value());
        Scene::UpdateLightInstance(static_cast<size_t>(instIdx), inst);
    };
    connect(m_posX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });
    connect(m_posY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });
    connect(m_posZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });

    auto applyDirection = [this]() {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        const auto &insts = Scene::GetLightInstances();
        if (instIdx < 0 || instIdx >= static_cast<int>(insts.size())) return;
        LightInstance inst = insts[static_cast<size_t>(instIdx)];
        inst.direction[0] = static_cast<float>(m_dirX->value());
        inst.direction[1] = static_cast<float>(m_dirY->value());
        inst.direction[2] = static_cast<float>(m_dirZ->value());
        Scene::UpdateLightInstance(static_cast<size_t>(instIdx), inst);
    };
    connect(m_dirX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });
    connect(m_dirY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });
    connect(m_dirZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });

    connect(m_instanceEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        const auto &insts = Scene::GetLightInstances();
        if (instIdx < 0 || instIdx >= static_cast<int>(insts.size())) return;
        LightInstance inst = insts[static_cast<size_t>(instIdx)];
        inst.enabled = enabled;
        Scene::UpdateLightInstance(static_cast<size_t>(instIdx), inst);
        refreshLights();
    });

    connect(m_selectButton, &QToolButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        if (instIdx >= 0) Scene::SelectLight(instIdx);
        refreshLights();
    });

    connect(m_removeButton, &QToolButton::clicked, this, [this]() {
        removeSelectedItems();
    });

    connect(m_addInstanceButton, &QToolButton::clicked, this, [this]() {
        if (m_syncing) return;
        int instIdx = selectedInstanceIndex();
        size_t newIdx = static_cast<size_t>(-1);
        if (instIdx >= 0) {
            newIdx = Scene::DuplicateLightInstanceAsInstance(static_cast<size_t>(instIdx));
        } else {
            const int protoIdx = selectedPrototypeIndex();
            if (protoIdx < 0) return;
            newIdx = Scene::AddLightInstance(static_cast<size_t>(protoIdx));
        }
        Scene::SelectLight(static_cast<int>(newIdx));
        refreshLights();
    });

    connect(m_duplicateCopyButton, &QToolButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        if (instIdx < 0) return;
        Scene::DuplicateLightInstanceAsCopy(static_cast<size_t>(instIdx));
        refreshLights();
    });

    connect(m_mergeCopiesButton, &QToolButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        if (protoIdx < 0) return;
        const int moved = Scene::MergeCompatibleLightPrototypes(static_cast<size_t>(protoIdx));
        if (moved == 0) {
            QMessageBox::information(this, tr("Merge Copies"),
                                     tr("No compatible copied light groups were found."));
        }
        refreshLights();
    });

    connect(m_moveToSurfaceButton, &QToolButton::clicked, this, [this]() {
        if (m_syncing) return;
        if (Scene::IsLightPlacementActive()) {
            Scene::CancelLightPlacement();
            refreshLights();
            return;
        }
        const int instIdx = selectedInstanceIndex();
        if (instIdx < 0) return;
        Scene::BeginMoveLightToSurface(instIdx);
        refreshLights();
    });
}

void LightsPanel::removeSelectedItems()
{
    if (m_syncing || !m_lightTree) {
        return;
    }

    const auto &insts = Scene::GetLightInstances();
    const auto &protos = Scene::GetLightPrototypes();
    std::vector<size_t> instanceIndices;
    std::vector<size_t> prototypeIndices;

    auto collectItem = [&](QTreeWidgetItem *item) {
        if (!item) {
            return;
        }
        const int rawIndex = item->data(0, Qt::UserRole).toInt();
        if (rawIndex < 0) {
            return;
        }
        if (item->type() == kInstanceItemType) {
            if (rawIndex < static_cast<int>(insts.size())) {
                instanceIndices.push_back(static_cast<size_t>(rawIndex));
            }
        } else if (item->type() == kProtoItemType) {
            if (rawIndex < static_cast<int>(protos.size())) {
                prototypeIndices.push_back(static_cast<size_t>(rawIndex));
            }
        }
    };

    for (QTreeWidgetItem *item : m_lightTree->selectedItems()) {
        collectItem(item);
    }
    if (instanceIndices.empty() && prototypeIndices.empty()) {
        collectItem(m_lightTree->currentItem());
    }

    if (!instanceIndices.empty()) {
        Scene::RemoveLightInstances(instanceIndices);
        refreshLights();
        return;
    }

    std::sort(prototypeIndices.begin(), prototypeIndices.end());
    prototypeIndices.erase(
        std::unique(prototypeIndices.begin(), prototypeIndices.end()),
        prototypeIndices.end());
    for (auto it = prototypeIndices.rbegin(); it != prototypeIndices.rend();
         ++it) {
        Scene::RemoveLightPrototype(*it);
    }
    if (!prototypeIndices.empty()) {
        refreshLights();
    }
}

int LightsPanel::selectedInstanceIndex() const
{
    QTreeWidgetItem *item = m_lightTree->currentItem();
    if (item) {
        if (item->type() == kInstanceItemType)
            return item->data(0, Qt::UserRole).toInt();
    }

    const int sel = Scene::GetSelectedLightIndex();
    if (sel >= 0 && sel < static_cast<int>(Scene::GetLightInstances().size()))
        return sel;
    return -1;
}

int LightsPanel::selectedPrototypeIndex() const
{
    QTreeWidgetItem *item = m_lightTree->currentItem();
    if (!item) return -1;
    if (item->type() == kInstanceItemType) {
        QTreeWidgetItem *parent = item->parent();
        if (parent) return parent->data(0, Qt::UserRole).toInt();
        return -1;
    }
    return item->data(0, Qt::UserRole).toInt();
}

bool LightsPanel::hasPropertyEditorFocus() const
{
    QWidget *focus = QApplication::focusWidget();
    if (!focus || !isAncestorOf(focus)) return false;

    auto editing = [focus](QWidget *widget) {
        return widget && (focus == widget || widget->isAncestorOf(focus));
    };

    return editing(m_intensity) ||
           editing(m_nameEdit) ||
           editing(m_posX) || editing(m_posY) || editing(m_posZ) ||
           editing(m_dirX) || editing(m_dirY) || editing(m_dirZ) ||
           editing(m_radius) ||
           editing(m_innerAngle) || editing(m_outerAngle) ||
           editing(m_areaWidth) || editing(m_areaHeight);
}

uint64_t LightsPanel::lightListSignature() const
{
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto mixFloat = [&mix](float value) {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "float hash size mismatch");
        std::memcpy(&bits, &value, sizeof(bits));
        mix(bits);
    };

    const auto &protos = Scene::GetLightPrototypes();
    const auto &insts = Scene::GetLightInstances();
    mix(static_cast<uint64_t>(protos.size()));
    mix(static_cast<uint64_t>(insts.size()));
    for (const LightPrototype &p : protos) {
        for (char ch : p.name) mix(static_cast<unsigned char>(ch));
        mix(static_cast<uint64_t>(p.type));
        mix(p.enabled ? 1ull : 0ull);
        for (float v : p.color) mixFloat(v);
        mixFloat(p.intensity);
        mixFloat(p.radius);
        mixFloat(p.innerConeAngle);
        mixFloat(p.outerConeAngle);
        for (float v : p.areaExtents) mixFloat(v);
        mix(static_cast<uint64_t>(static_cast<int64_t>(p.iesAtlasIndex) + 1));
        mix(static_cast<uint64_t>(static_cast<int64_t>(p.iesProfileIndex) + 1));
    }
    for (const LightInstance &inst : insts) {
        mix(static_cast<uint64_t>(inst.prototypeIndex));
        for (float v : inst.position) mixFloat(v);
        for (float v : inst.direction) mixFloat(v);
        mix(inst.enabled ? 1ull : 0ull);
        mix(inst.selected ? 1ull : 0ull);
    }

    return hash;
}

void LightsPanel::updateColorButton(const QColor &color)
{
    const QString style =
        QStringLiteral("background-color: %1; border: 1px solid #333;")
            .arg(color.name(QColor::HexRgb));
    m_colorButton->setStyleSheet(style);
}

void LightsPanel::updatePropertyVisibility(uint32_t type)
{
    const bool isSpot = (type == static_cast<uint32_t>(LightType::Spot));
    const bool isArea = (type == static_cast<uint32_t>(LightType::AreaRect) ||
                         type == static_cast<uint32_t>(LightType::AreaDisk));
    const bool isIesCompatible = (type == static_cast<uint32_t>(LightType::Omni) ||
                                   type == static_cast<uint32_t>(LightType::Spot) ||
                                   type == static_cast<uint32_t>(LightType::IES));

    m_spotGroup->setVisible(isSpot);
    m_areaGroup->setVisible(isArea);
    m_iesGroup->setVisible(isIesCompatible);
}

void LightsPanel::refreshLights()
{
    m_syncing = true;

    const bool sceneIoActive = IsSceneIoJobActive();
    m_loadNotice->setVisible(sceneIoActive);
    m_lightTree->setEnabled(!sceneIoActive);
    m_propertiesGroup->setEnabled(!sceneIoActive);
    m_addPointButton->setEnabled(!sceneIoActive);
    m_addSpotButton->setEnabled(!sceneIoActive);
    m_addRectButton->setEnabled(!sceneIoActive);
    m_addDiskButton->setEnabled(!sceneIoActive);
    m_addIESButton->setEnabled(!sceneIoActive);
    if (sceneIoActive) {
        m_syncing = false;
        return;
    }

    const auto &protos = Scene::GetLightPrototypes();
    const auto &insts = Scene::GetLightInstances();
    const int selectedInst = Scene::GetSelectedLightIndex();
    const std::vector<size_t> selectedInsts = Scene::GetSelectedLightIndices();

    const uint64_t signature = lightListSignature();
    if (signature != m_lastLightListSignature) {
        const QSignalBlocker treeBlocker(m_lightTree);
        m_lightTree->clear();

        for (size_t p = 0; p < protos.size(); ++p) {
            const LightPrototype &proto = protos[p];

            const char *typeStr = "Unknown";
            switch (static_cast<LightType>(proto.type)) {
            case LightType::Directional: typeStr = "Sun"; break;
            case LightType::Omni: typeStr = "Point"; break;
            case LightType::Spot: typeStr = "Spot"; break;
            case LightType::AreaRect: typeStr = "Rect"; break;
            case LightType::AreaDisk: typeStr = "Disk"; break;
            case LightType::IES: typeStr = "IES"; break;
            }

            int instCount = 0;
            int emittedCount = 0;
            for (const auto &inst : insts) {
                if (inst.prototypeIndex == p) {
                    ++instCount;
                    if (proto.enabled && inst.enabled) ++emittedCount;
                }
            }

            auto *protoItem = new QTreeWidgetItem(kProtoItemType);
            const QString groupName = proto.name[0] != '\0'
                ? QString::fromUtf8(proto.name)
                : tr("%1 %2").arg(typeStr).arg(static_cast<int>(p) + 1);
            protoItem->setText(0, tr("%1  (%2)").arg(groupName).arg(typeStr));
            protoItem->setText(2, tr("%1 inst / %2 active").arg(instCount).arg(emittedCount));
            QString profileText = tr("-");
            if (proto.iesProfileIndex >= 0 &&
                proto.iesProfileIndex < static_cast<int>(Scene::GetIESProfiles().size())) {
                const auto &prof = Scene::GetIESProfiles()[proto.iesProfileIndex];
                profileText = QString::fromStdString(prof.displayName);
            }
            protoItem->setText(3, profileText);
            protoItem->setData(0, Qt::UserRole, static_cast<int>(p));
            protoItem->setFlags(protoItem->flags() | Qt::ItemIsUserCheckable);
            protoItem->setCheckState(1, proto.enabled ? Qt::Checked : Qt::Unchecked);
            if (!proto.enabled) {
                QFont f = protoItem->font(0);
                f.setItalic(true);
                protoItem->setFont(0, f);
            }
            m_lightTree->addTopLevelItem(protoItem);

            for (size_t i = 0; i < insts.size(); ++i) {
                const LightInstance &inst = insts[i];
                if (inst.prototypeIndex != p) continue;

                auto *instItem = new QTreeWidgetItem(kInstanceItemType);
                instItem->setText(0, tr("Instance %1").arg(static_cast<int>(i)));
                instItem->setText(2, tr("%1, %2, %3")
                    .arg(inst.position[0], 0, 'f', 2)
                    .arg(inst.position[1], 0, 'f', 2)
                    .arg(inst.position[2], 0, 'f', 2));
                instItem->setData(0, Qt::UserRole, static_cast<int>(i));
                instItem->setFlags(instItem->flags() | Qt::ItemIsUserCheckable);
                instItem->setCheckState(1, inst.enabled ? Qt::Checked : Qt::Unchecked);
                if (!inst.enabled) {
                    QFont f = instItem->font(0);
                    f.setItalic(true);
                    instItem->setFont(0, f);
                }
                protoItem->addChild(instItem);
            }

            protoItem->setExpanded(true);
        }

        for (int col = 0; col < m_lightTree->columnCount(); ++col) {
            m_lightTree->resizeColumnToContents(col);
        }

        m_lastLightListSignature = signature;
    }

    {
        const QSignalBlocker treeBlocker(m_lightTree);
        m_lightTree->clearSelection();
        QTreeWidgetItem *activeItem = nullptr;
        QTreeWidgetItem *firstSelectedItem = nullptr;
        if (selectedInst >= 0 && selectedInst < static_cast<int>(insts.size())) {
            for (int topIdx = 0; topIdx < m_lightTree->topLevelItemCount(); ++topIdx) {
                QTreeWidgetItem *protoItem = m_lightTree->topLevelItem(topIdx);
                for (int childIdx = 0; childIdx < protoItem->childCount(); ++childIdx) {
                    QTreeWidgetItem *instItem = protoItem->child(childIdx);
                    const int instIdx = instItem->data(0, Qt::UserRole).toInt();
                    const bool isSelected =
                        std::find(selectedInsts.begin(), selectedInsts.end(),
                                  static_cast<size_t>(instIdx)) != selectedInsts.end();
                    instItem->setSelected(isSelected);
                    if (isSelected && !firstSelectedItem) {
                        firstSelectedItem = instItem;
                    }
                    if (instIdx == selectedInst) {
                        activeItem = instItem;
                    }
                }
            }
        } else {
            for (int topIdx = 0; topIdx < m_lightTree->topLevelItemCount(); ++topIdx) {
                QTreeWidgetItem *protoItem = m_lightTree->topLevelItem(topIdx);
                for (int childIdx = 0; childIdx < protoItem->childCount(); ++childIdx) {
                    QTreeWidgetItem *instItem = protoItem->child(childIdx);
                    const int instIdx = instItem->data(0, Qt::UserRole).toInt();
                    const bool isSelected =
                        instIdx >= 0 &&
                        std::find(selectedInsts.begin(), selectedInsts.end(),
                                  static_cast<size_t>(instIdx)) != selectedInsts.end();
                    instItem->setSelected(isSelected);
                    if (isSelected && !firstSelectedItem) {
                        firstSelectedItem = instItem;
                    }
                }
            }
        }
        if (activeItem) {
            m_lightTree->setCurrentItem(activeItem);
        } else if (firstSelectedItem) {
            m_lightTree->setCurrentItem(firstSelectedItem);
        }
    }

    const int curProtoIdx = selectedPrototypeIndex();
    const int curInstIdx = selectedInstanceIndex();
    const bool hasProto = (curProtoIdx >= 0 && curProtoIdx < static_cast<int>(protos.size()));
    const bool hasInst = (curInstIdx >= 0 && curInstIdx < static_cast<int>(insts.size()));

    m_propertiesGroup->setEnabled(hasProto || hasInst);
    m_protoPropsWidget->setVisible(hasProto);
    m_instancePropsWidget->setVisible(hasInst);
    m_addInstanceButton->setEnabled(hasProto);
    m_duplicateCopyButton->setEnabled(hasInst);
    m_mergeCopiesButton->setEnabled(hasProto);
    m_selectButton->setEnabled(hasInst);
    m_removeButton->setEnabled(hasProto || hasInst);
    m_moveToSurfaceButton->setEnabled(hasInst);

    // Placement state — sync toolbar toggle states and status label.
    const bool placementActive = Scene::IsLightPlacementActive();
    const Scene::LightPlacementMode placementMode = Scene::GetLightPlacementMode();
    const bool creating = placementActive && placementMode == Scene::LightPlacementMode::Create;
    const bool moving = placementActive && placementMode == Scene::LightPlacementMode::MoveSelected;

    if (!creating) {
        m_armedCreateType = -1;
    }
    {
        const QSignalBlocker b1(m_addPointButton);
        const QSignalBlocker b2(m_addSpotButton);
        const QSignalBlocker b3(m_addRectButton);
        const QSignalBlocker b4(m_addDiskButton);
        const QSignalBlocker b5(m_addIESButton);
        const QSignalBlocker b6(m_moveToSurfaceButton);
        m_addPointButton->setChecked(creating && m_armedCreateType == static_cast<int>(LightType::Omni));
        m_addSpotButton->setChecked(creating && m_armedCreateType == static_cast<int>(LightType::Spot));
        m_addRectButton->setChecked(creating && m_armedCreateType == static_cast<int>(LightType::AreaRect));
        m_addDiskButton->setChecked(creating && m_armedCreateType == static_cast<int>(LightType::AreaDisk));
        m_addIESButton->setChecked(creating && m_armedCreateType == static_cast<int>(LightType::IES));
        m_moveToSurfaceButton->setChecked(moving);
    }
    if (creating) {
        const char *typeName = "light";
        switch (static_cast<LightType>(m_armedCreateType)) {
        case LightType::Omni: typeName = "Point"; break;
        case LightType::Spot: typeName = "Spot"; break;
        case LightType::AreaRect: typeName = "Rect"; break;
        case LightType::AreaDisk: typeName = "Disk"; break;
        case LightType::IES: typeName = "IES"; break;
        default: break;
        }
        m_placementStatusLabel->setText(tr("Click viewport to place %1. Esc cancels.").arg(typeName));
    } else if (moving) {
        m_placementStatusLabel->setText(tr("Click a surface to move the selected light. Esc cancels."));
    } else {
        m_placementStatusLabel->clear();
    }

    QTreeWidgetItem *curItem = m_lightTree->currentItem();
    if (selectedInsts.size() > 1) {
        m_removeButton->setToolTip(tr("Remove Instances (Del)"));
    } else if (curItem && curItem->type() == kProtoItemType) {
        m_removeButton->setToolTip(tr("Remove Group (Del)"));
    } else {
        m_removeButton->setToolTip(tr("Remove Instance (Del)"));
    }

    const bool updateProtoInspector =
        hasProto && (!hasPropertyEditorFocus() || curProtoIdx != m_lastInspectorProtoIndex);

    if (updateProtoInspector) {
        const LightPrototype &proto = protos[static_cast<size_t>(curProtoIdx)];

        const char *typeStr = "Unknown";
        switch (static_cast<LightType>(proto.type)) {
        case LightType::Directional: typeStr = "Sun"; break;
        case LightType::Omni: typeStr = "Point"; break;
        case LightType::Spot: typeStr = "Spot"; break;
        case LightType::AreaRect: typeStr = "Rect"; break;
        case LightType::AreaDisk: typeStr = "Disk"; break;
        case LightType::IES: typeStr = "IES"; break;
        }
        m_typeLabel->setText(tr("%1").arg(typeStr));
        m_nameEdit->setText(QString::fromUtf8(proto.name));
        m_protoEnabled->setChecked(proto.enabled);

        int instCount = 0;
        int emittedCount = 0;
        for (const auto &inst : insts) {
            if (inst.prototypeIndex == static_cast<size_t>(curProtoIdx)) {
                ++instCount;
                if (proto.enabled && inst.enabled) ++emittedCount;
            }
        }
        m_instCountLabel->setText(tr("%1 instance(s), %2 active").arg(instCount).arg(emittedCount));

        m_currentColor = QColor::fromRgbF(
            std::clamp(proto.color[0], 0.0f, 1.0f),
            std::clamp(proto.color[1], 0.0f, 1.0f),
            std::clamp(proto.color[2], 0.0f, 1.0f));
        updateColorButton(m_currentColor);
        m_intensity->setValue(proto.intensity);
        m_radius->setValue(proto.radius);

        const double innerDeg =
            std::acos(std::clamp<double>(proto.innerConeAngle, -1.0, 1.0)) * (180.0 / kPi);
        const double outerDeg =
            std::acos(std::clamp<double>(proto.outerConeAngle, -1.0, 1.0)) * (180.0 / kPi);
        m_innerAngle->setValue(innerDeg);
        m_outerAngle->setValue(outerDeg);
        m_outerAngle->setMinimum(m_innerAngle->value());

        m_areaWidth->setValue(proto.areaExtents[0]);
        m_areaHeight->setValue(proto.areaExtents[1]);

        if (proto.iesProfileIndex >= 0) {
            const auto &iesProfiles = Scene::GetIESProfiles();
            if (proto.iesProfileIndex < static_cast<int>(iesProfiles.size())) {
                const auto &prof = iesProfiles[proto.iesProfileIndex];
                m_iesLabel->setText(tr("Profile: %1\nSlice: %2 | Status: %3")
                    .arg(QString::fromStdString(prof.displayName))
                    .arg(prof.atlasSlice)
                    .arg(prof.gpuReady ? tr("Ready") : tr("Loading...")));
            } else {
                m_iesLabel->setText(tr("Profile index %1 not found").arg(proto.iesProfileIndex));
            }
        } else {
            m_iesLabel->setText(tr("No profile assigned"));
        }

        updatePropertyVisibility(proto.type);
        m_lastInspectorProtoIndex = curProtoIdx;
    } else if (!hasProto) {
        m_lastInspectorProtoIndex = -2;
    }

    const bool updateInstInspector =
        hasInst && (!hasPropertyEditorFocus() || curInstIdx != m_lastInspectorInstIndex);

    if (updateInstInspector) {
        const LightInstance &inst = insts[static_cast<size_t>(curInstIdx)];
        m_instanceEnabled->setChecked(inst.enabled);

        m_posX->setValue(inst.position[0]);
        m_posY->setValue(inst.position[1]);
        m_posZ->setValue(inst.position[2]);

        m_dirX->setValue(inst.direction[0]);
        m_dirY->setValue(inst.direction[1]);
        m_dirZ->setValue(inst.direction[2]);

        m_instanceLabel->setText(tr("Instance %1 Transform").arg(curInstIdx));

        bool showDir = true;
        if (hasProto) {
            const LightPrototype &proto = protos[static_cast<size_t>(curProtoIdx)];
            showDir = (proto.type != static_cast<uint32_t>(LightType::Omni));
        }
        m_directionLabel->setVisible(showDir);
        m_directionRow->setVisible(showDir);

        m_lastInspectorInstIndex = curInstIdx;
    } else if (!hasInst) {
        m_lastInspectorInstIndex = -2;
    }

    m_syncing = false;
}
