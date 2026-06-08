#include "ScatterPanel.h"

#include "../scene.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

// =============================================================================
// Design language matches MaterialEditorPanel.cpp / LightsPanel.cpp:
//   - Vector tab icons via a custom QTabBar
//   - QToolButton header strip with 22px icons
//   - QGroupBox sections, 4-6 px spacing
//   - #58d0f4 accent color for headers + active icons
//   - List widget with subtle stripe (#1a1d1f / #1f2225)
//   - Fixed-height scroll area for the inspector so the list doesn't jump
// =============================================================================

namespace {

constexpr int kToolButtonSize = 30;
constexpr int kToolIconPx = 18;
constexpr int kTabIconPx = 20;
constexpr int kInspectorHeight = 460;

QColor StrokeColor() { return QColor(206, 214, 218); }
QColor AccentColor() { return QColor(88, 208, 244); }
QColor MutedColor()  { return QColor(132, 140, 144); }

// -- Tool icons ---------------------------------------------------------------
enum class ScatterToolIcon {
    AddModel,
    DeleteModel,
    Pick,
    AddTargets,
    AddObjects,
    Clean,
    Bake,
    Reseed,
    HideSource,
    RemoveListItem
};

QIcon MakeScatterToolIcon(ScatterToolIcon icon)
{
    QPixmap pixmap(kToolIconPx, kToolIconPx);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor stroke = StrokeColor();
    const QColor accent = AccentColor();
    const QColor muted  = MutedColor();
    QPen strokePen(stroke, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen accentPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen mutedPen(muted, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(strokePen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case ScatterToolIcon::AddModel: {
        // Sparkle / scatter pattern with a plus.
        painter.setBrush(stroke);
        painter.drawEllipse(QPointF(5.5, 6.0),  1.3, 1.3);
        painter.drawEllipse(QPointF(13.0, 6.5), 1.0, 1.0);
        painter.drawEllipse(QPointF(7.0, 13.0), 1.0, 1.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(13.0, 11.0), QPointF(13.0, 16.0));
        painter.drawLine(QPointF(10.5, 13.5), QPointF(15.5, 13.5));
        break;
    }
    case ScatterToolIcon::DeleteModel: {
        // Trash can.
        painter.drawLine(QPointF(3.5, 5.0),  QPointF(14.5, 5.0));
        painter.drawLine(QPointF(7.0, 3.5),  QPointF(11.0, 3.5));
        painter.drawLine(QPointF(5.0, 6.5),  QPointF(6.0, 15.0));
        painter.drawLine(QPointF(13.0, 6.5), QPointF(12.0, 15.0));
        painter.drawLine(QPointF(5.0, 15.0), QPointF(13.0, 15.0));
        painter.setPen(mutedPen);
        painter.drawLine(QPointF(9.0, 7.5), QPointF(9.0, 13.5));
        break;
    }
    case ScatterToolIcon::Pick: {
        // Crosshair with center dot.
        painter.drawEllipse(QPointF(9.0, 9.0), 4.5, 4.5);
        painter.drawLine(QPointF(9.0, 1.5), QPointF(9.0, 4.0));
        painter.drawLine(QPointF(9.0, 14.0), QPointF(9.0, 16.5));
        painter.drawLine(QPointF(1.5, 9.0), QPointF(4.0, 9.0));
        painter.drawLine(QPointF(14.0, 9.0), QPointF(16.5, 9.0));
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(9.0, 9.0), 1.4, 1.4);
        break;
    }
    case ScatterToolIcon::AddTargets: {
        // Surface plane with a check.
        painter.drawLine(QPointF(2.0, 14.0),  QPointF(11.0, 14.0));
        painter.drawLine(QPointF(2.0, 14.0),  QPointF(6.0, 9.0));
        painter.drawLine(QPointF(6.0, 9.0),   QPointF(15.0, 9.0));
        painter.drawLine(QPointF(11.0, 14.0), QPointF(15.0, 9.0));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(9.5, 5.5),  QPointF(11.0, 7.0));
        painter.drawLine(QPointF(11.0, 7.0), QPointF(14.5, 2.5));
        break;
    }
    case ScatterToolIcon::AddObjects: {
        // Stylised tree / asset glyph with a plus.
        QPainterPath canopy;
        canopy.moveTo(7.0, 11.5);
        canopy.lineTo(3.0, 11.5);
        canopy.lineTo(7.0, 6.5);
        canopy.lineTo(5.0, 6.5);
        canopy.lineTo(8.5, 2.5);
        canopy.lineTo(12.0, 6.5);
        canopy.lineTo(10.0, 6.5);
        canopy.lineTo(14.0, 11.5);
        canopy.lineTo(10.0, 11.5);
        canopy.lineTo(10.0, 15.0);
        canopy.lineTo(7.0, 15.0);
        canopy.closeSubpath();
        painter.drawPath(canopy);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(13.0, 14.0), QPointF(16.5, 14.0));
        painter.drawLine(QPointF(14.75, 12.25), QPointF(14.75, 15.75));
        break;
    }
    case ScatterToolIcon::Clean: {
        // Broom: angled head + handle.
        painter.drawLine(QPointF(11.5, 3.0), QPointF(6.0, 9.5));
        painter.drawLine(QPointF(3.5, 13.5), QPointF(7.5, 9.0));
        painter.drawLine(QPointF(6.5, 12.5), QPointF(11.5, 13.5));
        painter.drawLine(QPointF(7.5, 11.0), QPointF(13.5, 11.5));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(11.5, 3.0), QPointF(15.0, 6.5));
        break;
    }
    case ScatterToolIcon::Bake: {
        // Frozen instances become solid mesh: cube + arrow.
        painter.drawRect(QRectF(3.0, 5.0, 7.0, 7.0));
        painter.drawLine(QPointF(3.0, 5.0), QPointF(5.0, 3.0));
        painter.drawLine(QPointF(10.0, 5.0), QPointF(12.0, 3.0));
        painter.drawLine(QPointF(10.0, 12.0), QPointF(12.0, 10.0));
        painter.drawLine(QPointF(5.0, 3.0), QPointF(12.0, 3.0));
        painter.drawLine(QPointF(12.0, 3.0), QPointF(12.0, 10.0));
        painter.setPen(accentPen);
        painter.drawLine(QPointF(12.5, 14.0), QPointF(16.5, 14.0));
        painter.drawLine(QPointF(15.0, 12.5), QPointF(16.5, 14.0));
        painter.drawLine(QPointF(15.0, 15.5), QPointF(16.5, 14.0));
        break;
    }
    case ScatterToolIcon::Reseed: {
        // Die / pip face.
        painter.drawRoundedRect(QRectF(3.0, 3.0, 12.0, 12.0), 2.5, 2.5);
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(6.0, 6.0), 1.2, 1.2);
        painter.drawEllipse(QPointF(12.0, 6.0), 1.2, 1.2);
        painter.drawEllipse(QPointF(9.0, 9.0), 1.2, 1.2);
        painter.drawEllipse(QPointF(6.0, 12.0), 1.2, 1.2);
        painter.drawEllipse(QPointF(12.0, 12.0), 1.2, 1.2);
        break;
    }
    case ScatterToolIcon::HideSource: {
        // Eye with a slash.
        QPainterPath eye;
        eye.moveTo(2.5, 9.0);
        eye.quadTo(9.0, 3.5, 15.5, 9.0);
        eye.quadTo(9.0, 14.5, 2.5, 9.0);
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(9.0, 9.0), 2.2, 2.2);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(3.5, 15.0), QPointF(15.0, 3.5));
        break;
    }
    case ScatterToolIcon::RemoveListItem: {
        painter.drawLine(QPointF(4.5, 9.0), QPointF(13.5, 9.0));
        break;
    }
    }
    return QIcon(pixmap);
}

QToolButton *MakeToolButton(QWidget *parent, ScatterToolIcon icon,
                            const QString &tip, bool checkable = false)
{
    auto *btn = new QToolButton(parent);
    btn->setIcon(MakeScatterToolIcon(icon));
    btn->setIconSize(QSize(kToolIconPx, kToolIconPx));
    btn->setFixedSize(kToolButtonSize, kToolButtonSize);
    btn->setAutoRaise(false);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(tip);
    btn->setStatusTip(tip);
    btn->setCheckable(checkable);
    return btn;
}

// -- Tab icons ----------------------------------------------------------------
enum class ScatterTabIcon { Targets, Objects, Placement };

QPixmap DrawScatterTabIcon(ScatterTabIcon icon, const QColor &color)
{
    QPixmap pixmap(kTabIconPx, kTabIconPx);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case ScatterTabIcon::Targets: {
        // Surface plane with scatter dots on top.
        painter.drawLine(QPointF(2.5, 15.0), QPointF(11.0, 15.0));
        painter.drawLine(QPointF(2.5, 15.0), QPointF(7.0, 10.0));
        painter.drawLine(QPointF(7.0, 10.0), QPointF(15.5, 10.0));
        painter.drawLine(QPointF(11.0, 15.0), QPointF(15.5, 10.0));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(6.0, 7.0), 1.2, 1.2);
        painter.drawEllipse(QPointF(10.0, 4.5), 1.2, 1.2);
        painter.drawEllipse(QPointF(13.0, 8.0), 1.2, 1.2);
        break;
    }
    case ScatterTabIcon::Objects: {
        QPainterPath tree;
        tree.moveTo(10.0, 2.5);
        tree.lineTo(15.0, 9.0);
        tree.lineTo(12.5, 9.0);
        tree.lineTo(16.0, 13.5);
        tree.lineTo(11.5, 13.5);
        tree.lineTo(11.5, 16.5);
        tree.lineTo(8.5, 16.5);
        tree.lineTo(8.5, 13.5);
        tree.lineTo(4.0, 13.5);
        tree.lineTo(7.5, 9.0);
        tree.lineTo(5.0, 9.0);
        tree.closeSubpath();
        painter.drawPath(tree);
        break;
    }
    case ScatterTabIcon::Placement: {
        // Gear / settings.
        painter.drawEllipse(QPointF(10.0, 10.0), 3.5, 3.5);
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < 8; ++i) {
            const double theta = i * (kPi / 4.0);
            const double cx = 10.0 + std::cos(theta) * 5.0;
            const double cy = 10.0 + std::sin(theta) * 5.0;
            const double ex = 10.0 + std::cos(theta) * 7.5;
            const double ey = 10.0 + std::sin(theta) * 7.5;
            painter.drawLine(QPointF(cx, cy), QPointF(ex, ey));
        }
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(10.0, 10.0), 1.4, 1.4);
        break;
    }
    }
    return pixmap;
}

QIcon MakeScatterTabIcon(ScatterTabIcon icon)
{
    QIcon result;
    result.addPixmap(DrawScatterTabIcon(icon, QColor(174, 181, 185)),
                     QIcon::Normal, QIcon::Off);
    result.addPixmap(DrawScatterTabIcon(icon, AccentColor()),
                     QIcon::Selected, QIcon::Off);
    result.addPixmap(DrawScatterTabIcon(icon, AccentColor()),
                     QIcon::Active, QIcon::Off);
    return result;
}

class ScatterIconTabBar final : public QTabBar
{
public:
    explicit ScatterIconTabBar(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        setDrawBase(false);
        setExpanding(false);
        setUsesScrollButtons(false);
    }
protected:
    QSize tabSizeHint(int index) const override
    {
        Q_UNUSED(index);
        return QSize(44, 38);
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab option;
            initStyleOption(&option, i);
            const QRect r = option.rect.adjusted(2, 3, -2, -3);
            const bool selected = i == currentIndex();
            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(45, 54, 58));
                painter.drawRoundedRect(r, 7.0, 7.0);
            }
            const QIcon::Mode mode = selected ? QIcon::Selected : QIcon::Normal;
            const QPixmap pix = tabIcon(i).pixmap(iconSize(), mode, QIcon::Off);
            painter.drawPixmap(r.center().x() - pix.width() / 2,
                               r.center().y() - pix.height() / 2, pix);
        }
    }
};

// QTabWidget::setTabBar is protected; subclass to plug in our custom bar.
class ScatterTabWidget final : public QTabWidget
{
public:
    explicit ScatterTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new ScatterIconTabBar(this));
    }
};

// -- Spinbox factory ----------------------------------------------------------
QDoubleSpinBox *MakeDoubleSpin(double minV, double maxV, double step,
                               int decimals, bool adaptive = false)
{
    auto *s = new QDoubleSpinBox();
    s->setRange(minV, maxV);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setAccelerated(true);
    if (adaptive) {
        s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    }
    return s;
}

// -- Section header (cyan bold label, used inside group boxes) ---------------
QLabel *MakeSectionHeader(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    return label;
}

// -- Misc helpers -------------------------------------------------------------
QString NodeLabel(size_t nodeIndex)
{
    const auto &nodes = Scene::GetNodes();
    if (nodeIndex >= nodes.size()) {
        return QObject::tr("<missing node>");
    }
    return QString::fromStdString(nodes[nodeIndex].name);
}

QString MeshLabel(size_t meshIndex)
{
    if (meshIndex == static_cast<size_t>(-1)) {
        return QObject::tr("<missing mesh>");
    }
    return QObject::tr("mesh %1").arg(static_cast<qulonglong>(meshIndex));
}

QFrame *MakeHLine(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

// =============================================================================
// ScatterPanel
// =============================================================================

ScatterPanel::ScatterPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshUi();

    // Authoring edits hit the scene change listener; render-time stats
    // catch up on the next edit. (See Round 2 C1 notes — no polling timer.)
    m_sceneChangeListenerId = Scene::RegisterChangeListener([this]() {
        QMetaObject::invokeMethod(this, [this]() { refreshUi(); },
                                  Qt::QueuedConnection);
    });
}

ScatterPanel::~ScatterPanel()
{
    if (m_sceneChangeListenerId != 0) {
        Scene::UnregisterChangeListener(m_sceneChangeListenerId);
    }
}

void ScatterPanel::createUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // --- Header toolbar: model / target / object / bake actions --------------
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(2);

    m_addModelButton = MakeToolButton(toolbar, ScatterToolIcon::AddModel,
        tr("New scatter model"));
    m_deleteModelButton = MakeToolButton(toolbar, ScatterToolIcon::DeleteModel,
        tr("Delete the selected scatter model"));

    auto *sep1 = new QFrame(toolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);

    m_addTargetsButton = MakeToolButton(toolbar, ScatterToolIcon::AddTargets,
        tr("Add scatter targets from the current scene selection"));
    m_pickTargetButton = MakeToolButton(toolbar, ScatterToolIcon::Pick,
        tr("Pick target — click a viewport surface to add it as a scatter target"),
        /*checkable*/ true);
    m_addObjectsButton = MakeToolButton(toolbar, ScatterToolIcon::AddObjects,
        tr("Use the current scene selection as scatter prototype objects"));

    auto *sep2 = new QFrame(toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);

    m_cleanupObjectsButton = MakeToolButton(toolbar, ScatterToolIcon::Clean,
        tr("Drop scatter prototypes whose meshes are gone and targets whose "
           "source nodes were deleted"));
    m_bakeToNodesButton = MakeToolButton(toolbar, ScatterToolIcon::Bake,
        tr("Bake to nodes — flatten current scatter into real Scene::Nodes "
           "and disable the source model so output doesn't double up"));

    m_pickStatusLabel = new QLabel(QString(), toolbar);
    m_pickStatusLabel->setStyleSheet(QStringLiteral("color: #58d0f4;"));

    toolbarLayout->addWidget(m_addModelButton);
    toolbarLayout->addWidget(m_deleteModelButton);
    toolbarLayout->addWidget(sep1);
    toolbarLayout->addWidget(m_addTargetsButton);
    toolbarLayout->addWidget(m_pickTargetButton);
    toolbarLayout->addWidget(m_addObjectsButton);
    toolbarLayout->addWidget(sep2);
    toolbarLayout->addWidget(m_cleanupObjectsButton);
    toolbarLayout->addWidget(m_bakeToNodesButton);
    toolbarLayout->addWidget(m_pickStatusLabel, 1);
    root->addWidget(toolbar);

    // --- Model list (compact, striped) --------------------------------------
    m_modelList = new QListWidget(this);
    m_modelList->setMinimumHeight(96);
    m_modelList->setAlternatingRowColors(true);
    m_modelList->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #1a1d1f; }"
        "QListWidget::item:alternate { background-color: #1f2225; }"));
    root->addWidget(m_modelList);

    auto *modelDelShortcut =
        new QShortcut(QKeySequence(Qt::Key_Delete), m_modelList);
    modelDelShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(modelDelShortcut, &QShortcut::activated, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::RemoveScatterModel(static_cast<size_t>(row));
            refreshUi();
        }
    });

    // --- Model header group: name, on, seed + reseed, preview density / budget
    auto *modelGroup = new QGroupBox(tr("Model"), this);
    auto *modelForm = new QFormLayout(modelGroup);
    modelForm->setContentsMargins(8, 6, 8, 6);
    modelForm->setSpacing(4);

    m_modelName = new QLineEdit(modelGroup);
    m_modelEnabled = new QCheckBox(tr("Enabled"), modelGroup);
    m_modelEnabled->setToolTip(
        tr("Disable to stop generating instances without losing settings."));

    auto *seedRow = new QWidget(modelGroup);
    auto *seedRowLayout = new QHBoxLayout(seedRow);
    seedRowLayout->setContentsMargins(0, 0, 0, 0);
    seedRowLayout->setSpacing(4);
    m_modelSeed = new QSpinBox(seedRow);
    m_modelSeed->setRange(1, 2147483647);
    m_modelSeed->setToolTip(
        tr("Random seed for instance placement. Same seed = same layout."));
    m_reseedButton = MakeToolButton(seedRow, ScatterToolIcon::Reseed,
        tr("Reseed with a random value"));
    m_reseedButton->setFixedSize(26, 26);
    m_reseedButton->setIconSize(QSize(16, 16));
    seedRowLayout->addWidget(m_modelSeed, 1);
    seedRowLayout->addWidget(m_reseedButton, 0);

    m_previewDensityScale = MakeDoubleSpin(0.0, 1.0, 0.05, 2);
    m_previewDensityScale->setToolTip(
        tr("0..1 scaler on density for editor preview. Does not affect the "
           "authored density value."));
    m_previewBudget = new QSpinBox(modelGroup);
    m_previewBudget->setRange(0, 2000000);
    m_previewBudget->setToolTip(
        tr("Model-wide hard cap across all objects. If reached, remaining "
           "objects produce no instances (see Skipped in stats)."));

    modelForm->addRow(tr("Name"), m_modelName);
    modelForm->addRow(tr("State"), m_modelEnabled);
    modelForm->addRow(tr("Seed"), seedRow);
    modelForm->addRow(tr("Preview Density"), m_previewDensityScale);
    modelForm->addRow(tr("Preview Budget"), m_previewBudget);
    root->addWidget(modelGroup);

    // --- Tabbed inspector (Targets / Objects / Placement) -------------------
    m_tabs = new ScatterTabWidget(this);
    m_tabs->setIconSize(QSize(kTabIconPx, kTabIconPx));
    m_tabs->setDocumentMode(true);

    // Wrap inspector in a fixed-height scroll area so the list above doesn't
    // jump every time the user switches tabs.
    auto *inspectorScroll = new QScrollArea(this);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspectorScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    inspectorScroll->setFixedHeight(kInspectorHeight);

    auto *inspectorHost = new QWidget(inspectorScroll);
    auto *inspectorLayout = new QVBoxLayout(inspectorHost);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(4);
    inspectorLayout->addWidget(m_tabs);
    inspectorLayout->addStretch(1);
    inspectorScroll->setWidget(inspectorHost);

    // Targets tab ------------------------------------------------------------
    {
        auto *page = new QWidget();
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(6, 6, 6, 6);
        col->setSpacing(4);

        m_targetList = new QListWidget(page);
        m_targetList->setMinimumHeight(110);
        m_targetList->setAlternatingRowColors(true);
        m_targetList->setStyleSheet(QStringLiteral(
            "QListWidget { background-color: #1a1d1f; }"
            "QListWidget::item:alternate { background-color: #1f2225; }"));
        col->addWidget(m_targetList);

        auto *actions = new QHBoxLayout();
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(2);
        m_removeTargetButton = MakeToolButton(page,
            ScatterToolIcon::RemoveListItem,
            tr("Remove the selected scatter target from this model"));
        actions->addWidget(m_removeTargetButton);
        actions->addStretch(1);
        col->addLayout(actions);

        col->addWidget(MakeHLine(page));
        col->addWidget(MakeSectionHeader(tr("Target Settings"), page));

        auto *form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(4);
        m_targetEnabled = new QCheckBox(tr("Enabled"), page);
        m_targetWeight = MakeDoubleSpin(0.0, 100.0, 0.1, 2);
        m_targetWeight->setToolTip(
            tr("Multiplier on this target's triangle areas when distributing "
               "instances across the model's targets."));
        form->addRow(tr("State"), m_targetEnabled);
        form->addRow(tr("Weight"), m_targetWeight);
        col->addLayout(form);
        col->addStretch(1);

        m_tabs->addTab(page, MakeScatterTabIcon(ScatterTabIcon::Targets),
                       QString());
        m_tabs->setTabToolTip(0, tr("Targets — surface meshes scatter spawns on"));
    }

    // Objects tab ------------------------------------------------------------
    {
        auto *page = new QWidget();
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(6, 6, 6, 6);
        col->setSpacing(4);

        m_objectList = new QListWidget(page);
        m_objectList->setMinimumHeight(110);
        m_objectList->setAlternatingRowColors(true);
        m_objectList->setStyleSheet(QStringLiteral(
            "QListWidget { background-color: #1a1d1f; }"
            "QListWidget::item:alternate { background-color: #1f2225; }"));
        col->addWidget(m_objectList);

        auto *actions = new QHBoxLayout();
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(2);
        m_removeObjectButton = MakeToolButton(page,
            ScatterToolIcon::RemoveListItem,
            tr("Remove the selected prototype from this model"));
        m_hideSourceButton = MakeToolButton(page, ScatterToolIcon::HideSource,
            tr("Hide / show the original scene nodes used as this prototype's "
               "library source"));
        actions->addWidget(m_removeObjectButton);
        actions->addWidget(m_hideSourceButton);
        actions->addStretch(1);
        col->addLayout(actions);

        col->addWidget(MakeHLine(page));
        col->addWidget(MakeSectionHeader(tr("Object Identity"), page));

        auto *form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(4);
        m_objectName = new QLineEdit(page);
        m_objectEnabled = new QCheckBox(tr("Enabled"), page);
        m_objectEnabled->setToolTip(
            tr("Disable to skip this prototype without losing settings."));
        form->addRow(tr("Name"), m_objectName);
        form->addRow(tr("State"), m_objectEnabled);
        col->addLayout(form);
        col->addStretch(1);

        m_tabs->addTab(page, MakeScatterTabIcon(ScatterTabIcon::Objects),
                       QString());
        m_tabs->setTabToolTip(1, tr("Objects — prototype meshes scatter spawns"));
    }

    // Placement tab (grouped Round 2 sections, refreshed look) ---------------
    {
        auto *page = new QWidget();
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(6, 6, 6, 6);
        col->setSpacing(6);

        // Density & Caps
        auto *densityGroup = new QGroupBox(tr("Density && Caps"), page);
        auto *densityForm = new QFormLayout(densityGroup);
        densityForm->setContentsMargins(8, 6, 8, 6);
        densityForm->setSpacing(4);
        m_density = MakeDoubleSpin(0.0, 10000.0, 0.5, 2, true);
        m_density->setToolTip(tr("Target instances per square meter of surface."));
        m_weight = MakeDoubleSpin(0.0, 100.0, 0.1, 2);
        m_weight->setToolTip(
            tr("Multiplier on this prototype's share of the model density."));
        m_maxInstances = new QSpinBox(densityGroup);
        m_maxInstances->setRange(0, 2000000);
        m_maxInstances->setToolTip(
            tr("Hard cap for this prototype regardless of computed density."));
        m_previewMaxInstances = new QSpinBox(densityGroup);
        m_previewMaxInstances->setRange(0, 2000000);
        m_previewMaxInstances->setToolTip(
            tr("Editor-only soft cap. 0 = ignore (use Max Instances)."));
        densityForm->addRow(tr("Density / m\xc2\xb2"), m_density);
        densityForm->addRow(tr("Weight"), m_weight);
        densityForm->addRow(tr("Max Instances"), m_maxInstances);
        densityForm->addRow(tr("Preview Max"), m_previewMaxInstances);
        col->addWidget(densityGroup);

        // Scale
        auto *scaleGroup = new QGroupBox(tr("Scale"), page);
        auto *scaleForm = new QFormLayout(scaleGroup);
        scaleForm->setContentsMargins(8, 6, 8, 6);
        scaleForm->setSpacing(4);
        m_minScale = MakeDoubleSpin(0.001, 1000.0, 0.05, 3, true);
        m_maxScale = MakeDoubleSpin(0.001, 1000.0, 0.05, 3, true);
        scaleForm->addRow(tr("Min Scale"), m_minScale);
        scaleForm->addRow(tr("Max Scale"), m_maxScale);
        col->addWidget(scaleGroup);

        // Rotation
        auto *rotationGroup = new QGroupBox(tr("Rotation"), page);
        auto *rotationForm = new QFormLayout(rotationGroup);
        rotationForm->setContentsMargins(8, 6, 8, 6);
        rotationForm->setSpacing(4);
        m_yaw = MakeDoubleSpin(0.0, 360.0, 5.0, 1);
        m_pitch = MakeDoubleSpin(0.0, 180.0, 1.0, 1);
        m_roll = MakeDoubleSpin(0.0, 180.0, 1.0, 1);
        m_normalAlign = MakeDoubleSpin(0.0, 1.0, 0.05, 2);
        m_normalAlign->setToolTip(
            tr("0 = align to world up. 1 = align to surface normal."));
        rotationForm->addRow(tr("Yaw Random"), m_yaw);
        rotationForm->addRow(tr("Pitch Random"), m_pitch);
        rotationForm->addRow(tr("Roll Random"), m_roll);
        rotationForm->addRow(tr("Normal Align"), m_normalAlign);
        col->addWidget(rotationGroup);

        // Surface Filter
        auto *filterGroup = new QGroupBox(tr("Surface Filter"), page);
        auto *filterForm = new QFormLayout(filterGroup);
        filterForm->setContentsMargins(8, 6, 8, 6);
        filterForm->setSpacing(4);
        m_slopeMin = MakeDoubleSpin(0.0, 89.0, 1.0, 1);
        m_slopeMax = MakeDoubleSpin(0.0, 89.0, 1.0, 1);
        m_heightMin = MakeDoubleSpin(-100000.0, 100000.0, 1.0, 3, true);
        m_heightMin->setToolTip(
            tr("Minimum world-Y at which placements are kept."));
        m_heightMax = MakeDoubleSpin(-100000.0, 100000.0, 1.0, 3, true);
        m_heightMax->setToolTip(
            tr("Maximum world-Y at which placements are kept."));
        filterForm->addRow(tr("Slope Min (deg)"), m_slopeMin);
        filterForm->addRow(tr("Slope Max (deg)"), m_slopeMax);
        filterForm->addRow(tr("Y Min"), m_heightMin);
        filterForm->addRow(tr("Y Max"), m_heightMax);
        col->addWidget(filterGroup);

        // Distribution
        auto *distGroup = new QGroupBox(tr("Distribution"), page);
        auto *distForm = new QFormLayout(distGroup);
        distForm->setContentsMargins(8, 6, 8, 6);
        distForm->setSpacing(4);
        m_jitter = MakeDoubleSpin(0.0, 1000.0, 0.05, 3, true);
        m_edgeAvoidance = MakeDoubleSpin(0.0, 0.33, 0.01, 2);
        m_edgeAvoidance->setToolTip(
            tr("Skip placements within this fractional barycentric distance "
               "of a triangle edge."));
        m_collisionAvoidance = MakeDoubleSpin(0.0, 10000.0, 0.05, 3, true);
        m_collisionAvoidance->setToolTip(
            tr("Minimum world-space distance between accepted instances "
               "within this prototype."));
        m_avoidLightRadius = MakeDoubleSpin(0.0, 10000.0, 0.1, 3, true);
        m_avoidLightRadius->setToolTip(
            tr("Skip placements within this radius of any enabled scene "
               "light."));
        distForm->addRow(tr("Jitter (m)"), m_jitter);
        distForm->addRow(tr("Edge Avoid"), m_edgeAvoidance);
        distForm->addRow(tr("Avoid Collision (m)"), m_collisionAvoidance);
        distForm->addRow(tr("Avoid Lights (m)"), m_avoidLightRadius);
        col->addWidget(distGroup);

        // Clumping
        auto *clumpGroup = new QGroupBox(tr("Clumping"), page);
        auto *clumpForm = new QFormLayout(clumpGroup);
        clumpForm->setContentsMargins(8, 6, 8, 6);
        clumpForm->setSpacing(4);
        m_clumpScale = MakeDoubleSpin(0.0, 10000.0, 0.25, 2, true);
        m_clumpScale->setToolTip(
            tr("World-space spatial scale of clump noise. 0 = uniform."));
        m_clumpStrength = MakeDoubleSpin(0.0, 1.0, 0.05, 2);
        m_clumpStrength->setToolTip(
            tr("How aggressively to thin out non-clump regions. 0 = uniform, "
               "1 = full noise mask."));
        clumpForm->addRow(tr("Clump Scale"), m_clumpScale);
        clumpForm->addRow(tr("Clump Strength"), m_clumpStrength);
        col->addWidget(clumpGroup);

        // Camera Fade
        auto *cameraGroup = new QGroupBox(tr("Camera Fade"), page);
        auto *cameraForm = new QFormLayout(cameraGroup);
        cameraForm->setContentsMargins(8, 6, 8, 6);
        cameraForm->setSpacing(4);
        m_minDistance = MakeDoubleSpin(0.0, 100000.0, 1.0, 2, true);
        m_minDistance->setToolTip(
            tr("Skip placements closer than this distance from the camera."));
        m_maxDistance = MakeDoubleSpin(0.0, 100000.0, 1.0, 2, true);
        m_maxDistance->setToolTip(
            tr("Skip placements further than this distance from the camera. "
               "0 = no max."));
        m_distanceFade = MakeDoubleSpin(0.0, 100000.0, 1.0, 2, true);
        m_distanceFade->setToolTip(
            tr("Soft falloff width inside Max Distance."));
        cameraForm->addRow(tr("Min Distance"), m_minDistance);
        cameraForm->addRow(tr("Max Distance"), m_maxDistance);
        cameraForm->addRow(tr("Fade Width"), m_distanceFade);
        col->addWidget(cameraGroup);

        col->addStretch(1);
        m_tabs->addTab(page, MakeScatterTabIcon(ScatterTabIcon::Placement),
                       QString());
        m_tabs->setTabToolTip(2, tr("Placement — per-object scatter parameters"));
    }

    root->addWidget(inspectorScroll);

    // --- Stats footer -------------------------------------------------------
    auto *statsFrame = new QFrame(this);
    statsFrame->setFrameShape(QFrame::StyledPanel);
    statsFrame->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #1a1d1f; border-radius: 4px; }"));
    auto *statsLayout = new QVBoxLayout(statsFrame);
    statsLayout->setContentsMargins(8, 6, 8, 6);
    statsLayout->setSpacing(2);
    m_statsLabel = new QLabel(statsFrame);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statsLayout->addWidget(m_statsLabel);
    root->addWidget(statsFrame);

    // =========================================================================
    // Signal wiring
    // =========================================================================

    connect(m_addModelButton, &QToolButton::clicked, this, [this]() {
        const size_t row = Scene::AddScatterModel(tr("Scatter").toStdString());
        setSelectedModel(static_cast<int>(row));
    });
    connect(m_deleteModelButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::RemoveScatterModel(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) syncInspector();
    });
    connect(m_targetList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) syncInspector();
    });
    connect(m_objectList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) syncInspector();
    });

    connect(m_targetEnabled, &QCheckBox::toggled, this,
            [this](bool) { applyTargetEdit(); });
    connect(m_targetWeight, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { applyTargetEdit(); });

    connect(m_addTargetsButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterTargets(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_pickTargetButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row < 0) return;
        if (Scene::IsScatterPickingTarget()) {
            // Re-clicking cancels — feels natural for a checkable toggle.
            Scene::CancelScatterPick();
        } else {
            Scene::SetScatterPickTarget(static_cast<size_t>(row));
        }
        syncInspector();
    });
    connect(m_addObjectsButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterObjects(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_cleanupObjectsButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::RemoveUnusedScatterObjects(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_bakeToNodesButton, &QToolButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row < 0) return;
        const size_t created =
            Scene::BakeScatterModelToNodes(static_cast<size_t>(row));
        if (created > 0) {
            const auto &models = Scene::GetScatterModels();
            if (static_cast<size_t>(row) < models.size()) {
                Scene::ScatterModel header = models[static_cast<size_t>(row)];
                header.enabled = false;
                Scene::UpdateScatterModelHeader(static_cast<size_t>(row),
                                                header);
            }
        }
        refreshUi();
    });

    connect(m_removeTargetButton, &QToolButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            targetRow < 0 ||
            targetRow >= static_cast<int>(
                             models[static_cast<size_t>(modelRow)].targets.size())) {
            return;
        }
        Scene::ScatterModel model = models[static_cast<size_t>(modelRow)];
        model.targets.erase(model.targets.begin() + targetRow);
        Scene::UpdateScatterModel(static_cast<size_t>(modelRow), model);
        refreshUi();
    });
    connect(m_removeObjectButton, &QToolButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int objectRow = selectedObjectIndex();
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            objectRow < 0 ||
            objectRow >= static_cast<int>(
                             models[static_cast<size_t>(modelRow)].objects.size())) {
            return;
        }
        Scene::ScatterModel model = models[static_cast<size_t>(modelRow)];
        model.objects.erase(model.objects.begin() + objectRow);
        Scene::UpdateScatterModel(static_cast<size_t>(modelRow), model);
        refreshUi();
    });
    connect(m_hideSourceButton, &QToolButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int objectRow = selectedObjectIndex();
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            objectRow < 0 ||
            objectRow >= static_cast<int>(
                             models[static_cast<size_t>(modelRow)].objects.size())) {
            return;
        }
        const Scene::ScatterObject &object =
            models[static_cast<size_t>(modelRow)].objects[static_cast<size_t>(objectRow)];
        Scene::SetScatterObjectSourcesHidden(static_cast<size_t>(modelRow),
                                             static_cast<size_t>(objectRow),
                                             !object.librarySourceHidden);
        refreshUi();
    });

    // Model header signals ---------------------------------------------------
    connect(m_modelName, &QLineEdit::editingFinished, this,
            [this]() { applyModelEdit(); });
    connect(m_modelEnabled, &QCheckBox::toggled, this,
            [this](bool) { applyModelEdit(); });
    connect(m_modelSeed, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { applyModelEdit(); });
    connect(m_previewDensityScale,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { applyModelEdit(); });
    connect(m_previewBudget, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { applyModelEdit(); });
    connect(m_reseedButton, &QToolButton::clicked, this, [this]() {
        const int randomSeed = static_cast<int>(
            QRandomGenerator::global()->bounded(1,
                                                std::numeric_limits<int>::max()));
        m_modelSeed->setValue(randomSeed);
    });

    // Object placement signals -----------------------------------------------
    auto objectEdit = [this]() { applyObjectEdit(); };
    connect(m_objectName, &QLineEdit::editingFinished, this, objectEdit);
    connect(m_objectEnabled, &QCheckBox::toggled, this,
            [objectEdit](bool) { objectEdit(); });
    connect(m_density, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_weight, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_maxInstances, qOverload<int>(&QSpinBox::valueChanged), this,
            [objectEdit](int) { objectEdit(); });
    connect(m_previewMaxInstances, qOverload<int>(&QSpinBox::valueChanged),
            this, [objectEdit](int) { objectEdit(); });
    connect(m_minScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_maxScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_yaw, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_pitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_roll, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_normalAlign, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_slopeMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_slopeMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_jitter, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_heightMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_heightMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_minDistance, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_maxDistance, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_distanceFade, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_avoidLightRadius,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_clumpScale, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_clumpStrength, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [objectEdit](double) { objectEdit(); });
    connect(m_edgeAvoidance,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
    connect(m_collisionAvoidance,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [objectEdit](double) { objectEdit(); });
}

void ScatterPanel::refreshUi()
{
    m_syncing = true;
    m_lastScatterRevision = Scene::GetScatterRuntimeRevision();
    const int previousModel = selectedModelIndex();
    const int previousObject = selectedObjectIndex();
    const auto &models = Scene::GetScatterModels();

    m_modelList->clear();
    for (size_t i = 0; i < models.size(); ++i) {
        const Scene::ScatterModel &model = models[i];
        m_modelList->addItem(tr("%1%2  \xe2\x80\x94  %3 targets, %4 objects")
                                 .arg(model.enabled ? QString() : tr("[off] "))
                                 .arg(QString::fromStdString(model.name))
                                 .arg(static_cast<int>(model.targets.size()))
                                 .arg(static_cast<int>(model.objects.size())));
    }
    if (!models.empty()) {
        const int row =
            std::clamp(previousModel, 0, static_cast<int>(models.size()) - 1);
        m_modelList->setCurrentRow(row);
    }
    if (m_objectList && previousObject >= 0) {
        m_objectList->setCurrentRow(previousObject);
    }
    m_syncing = false;
    syncInspector();
}

void ScatterPanel::syncInspector()
{
    m_syncing = true;
    const auto &models = Scene::GetScatterModels();
    const int modelRow = selectedModelIndex();
    const bool hasModel =
        modelRow >= 0 && modelRow < static_cast<int>(models.size());

    m_deleteModelButton->setEnabled(hasModel);
    m_addTargetsButton->setEnabled(hasModel);
    m_pickTargetButton->setEnabled(hasModel);
    m_addObjectsButton->setEnabled(hasModel);
    m_cleanupObjectsButton->setEnabled(hasModel);
    m_bakeToNodesButton->setEnabled(hasModel);

    const bool picking = Scene::IsScatterPickingTarget();
    {
        QSignalBlocker block(m_pickTargetButton);
        m_pickTargetButton->setChecked(picking);
    }
    m_pickStatusLabel->setText(
        picking ? tr("Picking… click a viewport surface") : QString());

    m_modelName->setEnabled(hasModel);
    m_modelEnabled->setEnabled(hasModel);
    m_modelSeed->setEnabled(hasModel);
    m_reseedButton->setEnabled(hasModel);
    m_previewDensityScale->setEnabled(hasModel);
    m_previewBudget->setEnabled(hasModel);

    const int previousTargetRow = m_targetList ? m_targetList->currentRow() : -1;
    const int previousObjectRow = m_objectList ? m_objectList->currentRow() : -1;
    m_targetList->clear();
    m_objectList->clear();

    if (!hasModel) {
        m_modelName->clear();
        m_modelEnabled->setChecked(false);
        m_modelSeed->setValue(1);
        m_previewDensityScale->setValue(1.0);
        m_previewBudget->setValue(0);
        m_statsLabel->setText(
            tr("Add a scatter model with the ✚ button, then add targets "
               "(surfaces) and objects (prototypes) from the scene selection."));
    } else {
        const Scene::ScatterModel &model = models[static_cast<size_t>(modelRow)];
        m_modelName->setText(QString::fromStdString(model.name));
        m_modelEnabled->setChecked(model.enabled);
        m_modelSeed->setValue(static_cast<int>(std::max(1u, model.seed)));
        m_previewDensityScale->setValue(
            std::clamp(model.previewDensityScale, 0.0f, 1.0f));
        m_previewBudget->setValue(static_cast<int>(
            std::min<uint32_t>(model.previewInstanceBudget, 2000000u)));

        for (const Scene::ScatterTarget &target : model.targets) {
            m_targetList->addItem(tr("%1%2 / %3")
                                      .arg(target.enabled ? QString()
                                                          : tr("[off] "))
                                      .arg(NodeLabel(target.nodeIndex))
                                      .arg(MeshLabel(target.meshIndex)));
        }
        for (const Scene::ScatterObject &object : model.objects) {
            m_objectList->addItem(tr("%1%2  \xe2\x80\x94  %3 meshes")
                                      .arg(object.enabled ? QString()
                                                          : tr("[off] "))
                                      .arg(QString::fromStdString(object.name))
                                      .arg(static_cast<int>(
                                          object.meshIndices.size())));
        }
        if (!model.targets.empty()) {
            m_targetList->setCurrentRow(std::clamp(
                previousTargetRow, 0,
                static_cast<int>(model.targets.size()) - 1));
        }
        if (!model.objects.empty()) {
            m_objectList->setCurrentRow(std::clamp(
                previousObjectRow, 0,
                static_cast<int>(model.objects.size()) - 1));
        }
        const Scene::ScatterRuntimeStats stats = Scene::GetScatterRuntimeStats();
        QString headline =
            tr("<b>%1</b> instances  \xc2\xb7  %2 targets active  \xc2\xb7  "
               "%3 objects active")
                .arg(static_cast<qulonglong>(stats.generatedInstances))
                .arg(static_cast<unsigned>(stats.activeTargets))
                .arg(static_cast<unsigned>(stats.activeObjects));
        if (stats.skippedByBudget > 0 || stats.skippedByObjectCap > 0) {
            QStringList skipped;
            if (stats.skippedByBudget > 0) {
                skipped << tr("budget %1")
                                .arg(static_cast<unsigned>(stats.skippedByBudget));
            }
            if (stats.skippedByObjectCap > 0) {
                skipped << tr("per-object cap %1")
                                .arg(static_cast<unsigned>(stats.skippedByObjectCap));
            }
            headline += tr("  \xc2\xb7  Skipped: %1").arg(skipped.join(", "));
        }
        if (!stats.perObject.empty()) {
            std::vector<Scene::ScatterObjectStats> sorted = stats.perObject;
            std::sort(sorted.begin(), sorted.end(),
                      [](const Scene::ScatterObjectStats &a,
                         const Scene::ScatterObjectStats &b) {
                          return a.instancesGenerated > b.instancesGenerated;
                      });
            QStringList rows;
            const size_t limit = std::min<size_t>(sorted.size(), 6);
            for (size_t i = 0; i < limit; ++i) {
                const auto &e = sorted[i];
                QString name = QString::fromStdString(e.objectName);
                if (e.modelIndex != static_cast<size_t>(modelRow)) {
                    name = QString("[%1] %2")
                               .arg(QString::fromStdString(e.modelName))
                               .arg(name);
                }
                if (e.skippedByObjectCap > 0) {
                    rows << tr("%1: %2 (+%3 capped)")
                                .arg(name)
                                .arg(static_cast<unsigned>(e.instancesGenerated))
                                .arg(static_cast<unsigned>(e.skippedByObjectCap));
                } else {
                    rows << tr("%1: %2")
                                .arg(name)
                                .arg(static_cast<unsigned>(e.instancesGenerated));
                }
            }
            if (sorted.size() > limit) {
                rows << tr("\xe2\x80\xa6 +%1 more")
                            .arg(static_cast<int>(sorted.size() - limit));
            }
            headline += "<br><span style='color:#aaaaaa;'>" +
                        rows.join("  \xc2\xb7  ") + "</span>";
        }
        m_statsLabel->setText(headline);
    }

    const int objectRow = selectedObjectIndex();
    const bool hasObject =
        hasModel && objectRow >= 0 &&
        objectRow < static_cast<int>(
                        models[static_cast<size_t>(modelRow)].objects.size());
    m_removeTargetButton->setEnabled(hasModel &&
                                     m_targetList->currentRow() >= 0);
    const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
    const bool hasTarget =
        hasModel && targetRow >= 0 &&
        targetRow < static_cast<int>(
                        models[static_cast<size_t>(modelRow)].targets.size());
    m_targetEnabled->setEnabled(hasTarget);
    m_targetWeight->setEnabled(hasTarget);
    if (hasTarget) {
        const Scene::ScatterTarget &target =
            models[static_cast<size_t>(modelRow)]
                .targets[static_cast<size_t>(targetRow)];
        m_targetEnabled->setChecked(target.enabled);
        m_targetWeight->setValue(target.weight);
    } else {
        m_targetEnabled->setChecked(false);
        m_targetWeight->setValue(1.0);
    }

    m_removeObjectButton->setEnabled(hasObject);
    m_hideSourceButton->setEnabled(hasObject);
    m_objectName->setEnabled(hasObject);
    m_objectEnabled->setEnabled(hasObject);
    m_density->setEnabled(hasObject);
    m_weight->setEnabled(hasObject);
    m_maxInstances->setEnabled(hasObject);
    m_previewMaxInstances->setEnabled(hasObject);
    m_minScale->setEnabled(hasObject);
    m_maxScale->setEnabled(hasObject);
    m_yaw->setEnabled(hasObject);
    m_pitch->setEnabled(hasObject);
    m_roll->setEnabled(hasObject);
    m_normalAlign->setEnabled(hasObject);
    m_slopeMin->setEnabled(hasObject);
    m_slopeMax->setEnabled(hasObject);
    m_jitter->setEnabled(hasObject);
    m_heightMin->setEnabled(hasObject);
    m_heightMax->setEnabled(hasObject);
    m_minDistance->setEnabled(hasObject);
    m_maxDistance->setEnabled(hasObject);
    m_distanceFade->setEnabled(hasObject);
    m_avoidLightRadius->setEnabled(hasObject);
    m_clumpScale->setEnabled(hasObject);
    m_clumpStrength->setEnabled(hasObject);
    m_edgeAvoidance->setEnabled(hasObject);
    m_collisionAvoidance->setEnabled(hasObject);

    if (hasObject) {
        const Scene::ScatterObject &object =
            models[static_cast<size_t>(modelRow)]
                .objects[static_cast<size_t>(objectRow)];
        m_hideSourceButton->setToolTip(
            object.librarySourceHidden
                ? tr("Show this prototype's source nodes")
                : tr("Hide this prototype's source nodes"));
        m_objectName->setText(QString::fromStdString(object.name));
        m_objectEnabled->setChecked(object.enabled);
        m_density->setValue(object.densityPerSquareMeter);
        m_weight->setValue(object.weight);
        m_maxInstances->setValue(static_cast<int>(
            std::min<uint32_t>(object.maxInstances, 2000000u)));
        m_previewMaxInstances->setValue(static_cast<int>(
            std::min<uint32_t>(object.previewMaxInstances, 2000000u)));
        m_minScale->setValue(object.minScale);
        m_maxScale->setValue(object.maxScale);
        m_yaw->setValue(object.randomYawDegrees);
        m_pitch->setValue(object.randomPitchDegrees);
        m_roll->setValue(object.randomRollDegrees);
        m_normalAlign->setValue(object.normalAlign);
        m_slopeMin->setValue(object.slopeMinDegrees);
        m_slopeMax->setValue(object.slopeMaxDegrees);
        m_jitter->setValue(object.jitterMeters);
        m_heightMin->setValue(object.heightMin);
        m_heightMax->setValue(object.heightMax);
        m_minDistance->setValue(object.minDistance);
        m_maxDistance->setValue(object.maxDistance);
        m_distanceFade->setValue(object.distanceFadeMeters);
        m_avoidLightRadius->setValue(object.avoidLightRadius);
        m_clumpScale->setValue(object.clumpScale);
        m_clumpStrength->setValue(object.clumpStrength);
        m_edgeAvoidance->setValue(object.edgeAvoidance);
        m_collisionAvoidance->setValue(object.collisionAvoidanceRadius);
    } else {
        m_objectName->clear();
        m_objectEnabled->setChecked(false);
        m_density->setValue(0.0);
        m_weight->setValue(0.0);
        m_maxInstances->setValue(0);
        m_previewMaxInstances->setValue(0);
        m_heightMin->setValue(0.0);
        m_heightMax->setValue(0.0);
        m_minDistance->setValue(0.0);
        m_maxDistance->setValue(0.0);
        m_distanceFade->setValue(0.0);
        m_avoidLightRadius->setValue(0.0);
        m_clumpScale->setValue(0.0);
        m_clumpStrength->setValue(0.0);
        m_edgeAvoidance->setValue(0.0);
        m_collisionAvoidance->setValue(0.0);
    }
    m_syncing = false;
}

void ScatterPanel::applyModelEdit()
{
    if (m_syncing) return;
    const int row = selectedModelIndex();
    const auto &models = Scene::GetScatterModels();
    if (row < 0 || row >= static_cast<int>(models.size())) return;
    Scene::ScatterModel header = models[static_cast<size_t>(row)];
    header.name = m_modelName->text().toStdString();
    header.enabled = m_modelEnabled->isChecked();
    header.seed = static_cast<uint32_t>(std::max(1, m_modelSeed->value()));
    header.previewDensityScale = std::clamp(
        static_cast<float>(m_previewDensityScale->value()), 0.0f, 1.0f);
    header.previewInstanceBudget =
        static_cast<uint32_t>(std::max(0, m_previewBudget->value()));
    Scene::UpdateScatterModelHeader(static_cast<size_t>(row), header);
}

void ScatterPanel::applyTargetEdit()
{
    if (m_syncing) return;
    const int modelRow = selectedModelIndex();
    const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
    const auto &models = Scene::GetScatterModels();
    if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
        targetRow < 0 ||
        targetRow >= static_cast<int>(
                         models[static_cast<size_t>(modelRow)].targets.size())) {
        return;
    }
    Scene::ScatterTarget target =
        models[static_cast<size_t>(modelRow)]
            .targets[static_cast<size_t>(targetRow)];
    target.enabled = m_targetEnabled->isChecked();
    target.weight = static_cast<float>(m_targetWeight->value());
    Scene::UpdateScatterTarget(static_cast<size_t>(modelRow),
                               static_cast<size_t>(targetRow), target);
}

void ScatterPanel::applyObjectEdit()
{
    if (m_syncing) return;
    const int modelRow = selectedModelIndex();
    const int objectRow = selectedObjectIndex();
    const auto &models = Scene::GetScatterModels();
    if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
        objectRow < 0 ||
        objectRow >= static_cast<int>(
                         models[static_cast<size_t>(modelRow)].objects.size())) {
        return;
    }
    Scene::ScatterObject object =
        models[static_cast<size_t>(modelRow)]
            .objects[static_cast<size_t>(objectRow)];
    object.name = m_objectName->text().toStdString();
    object.enabled = m_objectEnabled->isChecked();
    object.densityPerSquareMeter = static_cast<float>(m_density->value());
    object.weight = static_cast<float>(m_weight->value());
    object.maxInstances =
        static_cast<uint32_t>(std::max(0, m_maxInstances->value()));
    object.previewMaxInstances =
        static_cast<uint32_t>(std::max(0, m_previewMaxInstances->value()));
    object.minScale = static_cast<float>(m_minScale->value());
    object.maxScale = static_cast<float>(m_maxScale->value());
    object.randomYawDegrees = static_cast<float>(m_yaw->value());
    object.randomPitchDegrees = static_cast<float>(m_pitch->value());
    object.randomRollDegrees = static_cast<float>(m_roll->value());
    object.normalAlign = static_cast<float>(m_normalAlign->value());
    object.slopeMinDegrees = static_cast<float>(m_slopeMin->value());
    object.slopeMaxDegrees = static_cast<float>(m_slopeMax->value());
    object.jitterMeters = static_cast<float>(m_jitter->value());
    object.heightMin = static_cast<float>(m_heightMin->value());
    object.heightMax = static_cast<float>(m_heightMax->value());
    object.minDistance = static_cast<float>(m_minDistance->value());
    object.maxDistance = static_cast<float>(m_maxDistance->value());
    object.distanceFadeMeters = static_cast<float>(m_distanceFade->value());
    object.avoidLightRadius = static_cast<float>(m_avoidLightRadius->value());
    object.clumpScale = static_cast<float>(m_clumpScale->value());
    object.clumpStrength = static_cast<float>(m_clumpStrength->value());
    object.edgeAvoidance = static_cast<float>(m_edgeAvoidance->value());
    object.collisionAvoidanceRadius =
        static_cast<float>(m_collisionAvoidance->value());
    Scene::UpdateScatterObject(static_cast<size_t>(modelRow),
                               static_cast<size_t>(objectRow), object);
}

int ScatterPanel::selectedModelIndex() const
{
    return m_modelList ? m_modelList->currentRow() : -1;
}

int ScatterPanel::selectedObjectIndex() const
{
    return m_objectList ? m_objectList->currentRow() : -1;
}

void ScatterPanel::setSelectedModel(int row)
{
    if (m_modelList) m_modelList->setCurrentRow(row);
    refreshUi();
}
