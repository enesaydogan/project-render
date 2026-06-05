#include "MaterialEditorPanel.h"

#include "ArchColorDialog.h"
#include "SliderControl.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../assets/asset_loader.h"
#include "../dx12_context.h"
#include "../dxr_renderer.h"
#include "../editor_ui.h"
#include "../file_import.h"
#include "../material/material_system.h"
#include "../material_editor.h"
#include "../scene.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QApplication>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QMetaObject>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <QTabBar>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>

extern HWND g_hwnd;
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;

namespace {

constexpr int kTextureToolButtonSize = 30;

enum class TextureToolIcon {
    Load,
    Clear
};

enum class MaterialTabIcon {
    Surface,
    Advanced,
    Opacity,
    Maps,
    Grass,
    Mapping,
    Parallax,
    Qa
};

class MaterialIconTabBar final : public QTabBar
{
public:
    explicit MaterialIconTabBar(QWidget *parent = nullptr)
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
        const int availableWidth =
            parentWidget() ? parentWidget()->contentsRect().width() : width();
        const int tabWidth =
            count() > 0 ? std::max(32, availableWidth / count()) : 36;
        return QSize(tabWidth, 42);
    }

    QSize minimumTabSizeHint(int index) const override
    {
        return tabSizeHint(index);
    }

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            const QRect tabRect = option.rect.adjusted(2, 3, -2, -3);
            const bool selected = index == currentIndex();

            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(45, 54, 58));
                painter.drawRoundedRect(tabRect, 7.0, 7.0);
            }

            const QIcon::Mode mode = selected ? QIcon::Selected : QIcon::Normal;
            const QPixmap pixmap =
                tabIcon(index).pixmap(iconSize(), mode, QIcon::Off);
            const QPoint topLeft(tabRect.center().x() - pixmap.width() / 2,
                                 tabRect.center().y() - pixmap.height() / 2);
            painter.drawPixmap(topLeft, pixmap);
        }
    }
};

class MaterialTabWidget final : public QTabWidget
{
public:
    explicit MaterialTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new MaterialIconTabBar(this));
    }
};

QPixmap DrawMaterialTabIcon(MaterialTabIcon icon, const QColor &color)
{
    constexpr int kIconSize = 20;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case MaterialTabIcon::Surface:
        painter.drawEllipse(QRectF(3.0, 3.0, 14.0, 14.0));
        painter.drawArc(QRectF(6.0, 6.0, 8.0, 8.0), 25 * 16, 130 * 16);
        painter.drawLine(QPointF(3.0, 10.0), QPointF(17.0, 10.0));
        break;
    case MaterialTabIcon::Advanced:
        painter.drawLine(QPointF(3.0, 5.0), QPointF(17.0, 5.0));
        painter.drawLine(QPointF(3.0, 10.0), QPointF(17.0, 10.0));
        painter.drawLine(QPointF(3.0, 15.0), QPointF(17.0, 15.0));
        painter.setBrush(color);
        painter.drawEllipse(QRectF(6.0, 3.0, 4.0, 4.0));
        painter.drawEllipse(QRectF(12.0, 8.0, 4.0, 4.0));
        painter.drawEllipse(QRectF(4.0, 13.0, 4.0, 4.0));
        break;
    case MaterialTabIcon::Opacity: {
        QPainterPath drop;
        drop.moveTo(10.0, 2.5);
        drop.cubicTo(8.2, 5.7, 4.8, 9.0, 4.8, 12.2);
        drop.cubicTo(4.8, 15.3, 7.1, 17.5, 10.0, 17.5);
        drop.cubicTo(12.9, 17.5, 15.2, 15.3, 15.2, 12.2);
        drop.cubicTo(15.2, 9.0, 11.8, 5.7, 10.0, 2.5);
        painter.drawPath(drop);
        painter.drawLine(QPointF(7.2, 13.8), QPointF(12.8, 8.2));
        break;
    }
    case MaterialTabIcon::Maps:
        painter.drawRect(QRectF(3.0, 3.0, 14.0, 14.0));
        painter.drawLine(QPointF(10.0, 3.0), QPointF(10.0, 17.0));
        painter.drawLine(QPointF(3.0, 10.0), QPointF(17.0, 10.0));
        painter.fillRect(QRectF(3.8, 3.8, 5.4, 5.4), color);
        painter.fillRect(QRectF(10.8, 10.8, 5.4, 5.4), color);
        break;
    case MaterialTabIcon::Grass:
        painter.drawLine(QPointF(10.0, 17.0), QPointF(10.0, 6.0));
        painter.drawLine(QPointF(9.5, 16.5), QPointF(4.0, 9.0));
        painter.drawLine(QPointF(10.5, 16.5), QPointF(16.0, 8.0));
        painter.drawLine(QPointF(7.5, 17.0), QPointF(6.0, 12.0));
        painter.drawLine(QPointF(12.5, 17.0), QPointF(14.5, 12.0));
        break;
    case MaterialTabIcon::Mapping:
        painter.drawRect(QRectF(4.0, 4.0, 12.0, 12.0));
        painter.drawLine(QPointF(4.0, 10.0), QPointF(16.0, 10.0));
        painter.drawLine(QPointF(10.0, 4.0), QPointF(10.0, 16.0));
        painter.drawLine(QPointF(10.0, 10.0), QPointF(18.0, 10.0));
        painter.drawLine(QPointF(10.0, 10.0), QPointF(10.0, 2.0));
        break;
    case MaterialTabIcon::Parallax:
        painter.drawRect(QRectF(3.0, 5.0, 11.0, 11.0));
        painter.drawRect(QRectF(6.0, 2.0, 11.0, 11.0));
        painter.drawLine(QPointF(9.0, 10.0), QPointF(14.5, 4.5));
        painter.drawLine(QPointF(11.5, 4.5), QPointF(14.5, 4.5));
        painter.drawLine(QPointF(14.5, 4.5), QPointF(14.5, 7.5));
        break;
    case MaterialTabIcon::Qa:
        painter.drawEllipse(QRectF(3.0, 3.0, 14.0, 14.0));
        painter.drawLine(QPointF(6.5, 10.0), QPointF(9.0, 12.7));
        painter.drawLine(QPointF(9.0, 12.7), QPointF(14.2, 7.2));
        break;
    }

    return pixmap;
}

QIcon MakeMaterialTabIcon(MaterialTabIcon icon)
{
    QIcon result;
    result.addPixmap(DrawMaterialTabIcon(icon, QColor(174, 181, 185)),
                     QIcon::Normal, QIcon::Off);
    result.addPixmap(DrawMaterialTabIcon(icon, QColor(100, 214, 248)),
                     QIcon::Selected, QIcon::Off);
    result.addPixmap(DrawMaterialTabIcon(icon, QColor(100, 214, 248)),
                     QIcon::Active, QIcon::Off);
    return result;
}

int AddMaterialTab(QTabWidget *tabs,
                   QWidget *page,
                   MaterialTabIcon icon,
                   const QString &name)
{
    const int index = tabs->addTab(page, MakeMaterialTabIcon(icon), QString());
    tabs->setTabToolTip(index, name);
    tabs->setTabWhatsThis(index, name);
    return index;
}

Asset::TextureUsageSemantic TextureCompressionSemantic(int slot)
{
    using Semantic = Asset::TextureUsageSemantic;
    switch (slot) {
    case 0:
        return Semantic::Color;
    case 1:
    case 3:
    case 4:
    case 7:
    case 10:
    case 11:
        return Semantic::Scalar;
    case 2:
        return Semantic::PackedSurface;
    case 5:
    case 6:
        return Semantic::Normal;
    case 8:
        return Semantic::Emissive;
    case 9:
        return Semantic::Color;
    default:
        return Semantic::Unknown;
    }
}

SliderControl *CreateSliderControl(double minValue,
                                   double maxValue,
                                   double step,
                                   int decimals)
{
    return new SliderControl(minValue, maxValue, step, decimals);
}

QWidget *CreateVec2Row(SliderControl **outX,
                       SliderControl **outY,
                       double minValue,
                       double maxValue,
                       double step,
                       int decimals)
{
    auto *widget = new QWidget();
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    *outX = CreateSliderControl(minValue, maxValue, step, decimals);
    *outY = CreateSliderControl(minValue, maxValue, step, decimals);
    layout->addWidget(*outX);
    layout->addWidget(*outY);
    return widget;
}

QString MaterialLabel(const Asset::Material &mat, int index)
{
    QString name = QString::fromUtf8(mat.name).trimmed();
    if (name.isEmpty()) {
        name = QObject::tr("Material");
    }
    return QObject::tr("%1  (#%2)").arg(name).arg(index);
}

QString TextureLabel(const Asset::Texture &tex, int index)
{
    QString label = QObject::tr("#%1").arg(index);
    if (tex.width > 0 && tex.height > 0) {
        label += QObject::tr(" (%1x%2)").arg(tex.width).arg(tex.height);
    }
    if (!tex.resource) {
        label += QObject::tr(" [missing]");
    }
    return label;
}

bool FilterPass(const QString &filter, const char *name)
{
    if (filter.isEmpty()) {
        return true;
    }
    const QString matName = QString::fromUtf8(name);
    return matName.contains(filter, Qt::CaseInsensitive);
}

bool IsHDRTexturePath(const std::wstring &path)
{
    if (path.size() < 4) {
        return false;
    }
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring ext = path.substr(dot);
    for (auto &c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return (ext == L".hdr" || ext == L".exr");
}

QIcon MakeTextureToolIcon(TextureToolIcon icon)
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
    case TextureToolIcon::Load: {
        QPainterPath folder;
        folder.moveTo(2.5, 6.0);
        folder.lineTo(6.6, 6.0);
        folder.lineTo(8.2, 4.2);
        folder.lineTo(15.5, 4.2);
        folder.lineTo(15.5, 14.0);
        folder.lineTo(2.5, 14.0);
        folder.closeSubpath();
        painter.drawPath(folder);
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(9.0, 7.2), QPointF(9.0, 12.0));
        painter.drawLine(QPointF(6.8, 9.8), QPointF(9.0, 12.0));
        painter.drawLine(QPointF(11.2, 9.8), QPointF(9.0, 12.0));
        break;
    }
    case TextureToolIcon::Clear:
        painter.drawRect(QRectF(4.8, 4.8, 8.6, 8.6));
        painter.setPen(QPen(muted, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(6.8, 6.8), QPointF(11.4, 11.4));
        painter.drawLine(QPointF(11.4, 6.8), QPointF(6.8, 11.4));
        painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(13.5, 3.8), QPointF(15.2, 2.1));
        painter.drawLine(QPointF(15.2, 2.1), QPointF(16.2, 3.1));
        break;
    }

    return QIcon(pixmap);
}

QToolButton *CreateTextureToolButton(QWidget *parent,
                                     const QString &toolTip,
                                     TextureToolIcon icon)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(toolTip);
    button->setStatusTip(toolTip);
    button->setIcon(MakeTextureToolIcon(icon));
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(kTextureToolButtonSize, kTextureToolButtonSize);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

int AlphaModeIndex(const std::string &mode)
{
    if (mode == "MASK") {
        return 1;
    }
    if (mode == "BLEND") {
        return 2;
    }
    return 0;
}

const char *AlphaModeFromIndex(int index)
{
    switch (index) {
    case 1: return "MASK";
    case 2: return "BLEND";
    default: return "OPAQUE";
    }
}

bool IsReflectionGlossinessWorkflow(const Asset::Material &material)
{
    return MaterialSystem::UsesReflectionGlossiness(material);
}

QString RoughnessLabelForMaterial(const Asset::Material &material)
{
    return IsReflectionGlossinessWorkflow(material)
               ? QObject::tr("Glossiness")
               : QObject::tr("Roughness");
}

QString SecondaryLabelForMaterial(const Asset::Material &material)
{
    return IsReflectionGlossinessWorkflow(material)
               ? QObject::tr("Reflection Weight")
               : QObject::tr("Metalness");
}

QString RoughnessTextureTitleForMaterial(const Asset::Material &material)
{
    return IsReflectionGlossinessWorkflow(material)
               ? QObject::tr("Glossiness")
               : QObject::tr("Roughness");
}

bool MaterialAffectsRtStructure(const Asset::Material &material)
{
    return MaterialSystem::MaterialAffectsRtStructure(material);
}

bool IsWidgetBeingEdited(QWidget *widget)
{
    if (!widget) {
        return false;
    }

    QWidget *focus = QApplication::focusWidget();
    return widget->hasFocus() ||
           (focus && (focus == widget || widget->isAncestorOf(focus)));
}

void SyncLineEditText(QLineEdit *lineEdit, const QString &text)
{
    if (!lineEdit || IsWidgetBeingEdited(lineEdit) || lineEdit->text() == text) {
        return;
    }

    const QSignalBlocker blocker(lineEdit);
    lineEdit->setText(text);
}

void SyncComboBoxIndex(QComboBox *comboBox, int index)
{
    if (!comboBox || IsWidgetBeingEdited(comboBox) || comboBox->currentIndex() == index) {
        return;
    }

    const QSignalBlocker blocker(comboBox);
    comboBox->setCurrentIndex(index);
}

void SyncCheckBoxState(QCheckBox *checkBox, bool checked)
{
    if (!checkBox || IsWidgetBeingEdited(checkBox) || checkBox->isChecked() == checked) {
        return;
    }

    const QSignalBlocker blocker(checkBox);
    checkBox->setChecked(checked);
}

void SyncSliderControlValue(SliderControl *control, double value)
{
    if (!control || control->isInteracting()) {
        return;
    }

    control->setValue(value);
}

void ApplyPreset(Asset::Material &m, int presetIdx)
{
    MaterialSystem::ApplyPreset(m, presetIdx);
}

} // namespace
MaterialEditorPanel::MaterialEditorPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshMaterials();

    m_sceneChangeListenerId = Scene::RegisterChangeListener([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            scheduleRefresh();
        }, Qt::QueuedConnection);
    });
    m_editorStateListenerId = MaterialEditor::RegisterStateListener([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            scheduleRefresh();
        }, Qt::QueuedConnection);
    });
}

MaterialEditorPanel::~MaterialEditorPanel()
{
    if (m_sceneChangeListenerId != 0) {
        Scene::UnregisterChangeListener(m_sceneChangeListenerId);
    }
    if (m_editorStateListenerId != 0) {
        MaterialEditor::UnregisterStateListener(m_editorStateListenerId);
    }
}

void MaterialEditorPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    scheduleRefresh();
}

void MaterialEditorPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout();
    m_pickButton = new QPushButton(tr("Pick Material"), this);
    m_pickStatusLabel = new QLabel(tr("Click on object surface..."), this);
    m_pickStatusLabel->setStyleSheet(QStringLiteral("color: #2f7d4a;"));
    m_pickStatusLabel->setVisible(false);
    m_countsLabel = new QLabel(this);
    m_countsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toolbar->addWidget(m_pickButton);
    toolbar->addWidget(m_pickStatusLabel);
    toolbar->addStretch(1);
    toolbar->addWidget(m_countsLabel);
    layout->addLayout(toolbar);

    auto *filterRow = new QHBoxLayout();
    m_showAllCheck = new QCheckBox(tr("Show all materials"), this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search material name..."));
    m_filterEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_showAllCheck);
    filterRow->addStretch(1);
    filterRow->addWidget(m_filterEdit, 1);
    layout->addLayout(filterRow);

    m_nodeLabel = new QLabel(tr("No node selected"), this);
    m_nodeLabel->setWordWrap(true);
    layout->addWidget(m_nodeLabel);

    m_materialList = new QListWidget(this);
    m_materialList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_materialList->setAlternatingRowColors(true);
    m_materialList->setMinimumHeight(180);
    layout->addWidget(m_materialList);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    m_inspectorGroup = new QGroupBox(tr("Inspector"), this);
    auto *inspectorLayout = new QVBoxLayout(m_inspectorGroup);

    auto *infoForm = new QFormLayout();
    infoForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    infoForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    infoForm->setContentsMargins(0, 0, 0, 0);
    infoForm->setHorizontalSpacing(6);
    infoForm->setVerticalSpacing(4);
    m_materialNameEdit = new QLineEdit(m_inspectorGroup);
    m_materialNameEdit->setMaxLength(63);
    m_materialIdLabel = new QLabel(m_inspectorGroup);
    infoForm->addRow(tr("Name"), m_materialNameEdit);
    infoForm->addRow(tr("ID"), m_materialIdLabel);
    inspectorLayout->addLayout(infoForm);

    auto *actionRow = new QHBoxLayout();
    m_copyButton = new QPushButton(tr("Copy"), m_inspectorGroup);
    m_pasteButton = new QPushButton(tr("Paste"), m_inspectorGroup);
    m_resetButton = new QPushButton(tr("Reset"), m_inspectorGroup);
    m_resetNoTexButton = new QPushButton(tr("Reset (No Tex)"), m_inspectorGroup);
    actionRow->addWidget(m_copyButton);
    actionRow->addWidget(m_pasteButton);
    actionRow->addWidget(m_resetButton);
    actionRow->addWidget(m_resetNoTexButton);
    inspectorLayout->addLayout(actionRow);

    auto *presetRow = new QHBoxLayout();
    m_presetCombo = new QComboBox(m_inspectorGroup);
    m_presetCombo->addItems({
        tr("Dielectric Generic"),
        tr("Paint / Plaster"),
        tr("Concrete"),
        tr("Wood (Raw)"),
        tr("Wood (Varnished)"),
        tr("Tile (Ceramic)"),
        tr("Metal (Brushed)"),
        tr("Metal (Polished)"),
        tr("Plastic"),
        tr("Glass (Clear Window, Thin)"),
        tr("Glass (Frosted, Thin)"),
        tr("Glass (Tinted, Thin)"),
        tr("Fabric (Approx)"),
        tr("Vegetation Leaf (Approx)"),
    });
    m_applyPresetButton = new QPushButton(tr("Apply"), m_inspectorGroup);
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(m_applyPresetButton);
    inspectorLayout->addLayout(presetRow);

    m_tabs = new MaterialTabWidget(m_inspectorGroup);
    m_textureOptionsModel = new QStandardItemModel(this);

    auto createTextureSlot = [this](TextureSlot slot, const QString &title) {
        auto *group = new QWidget(this);
        group->setStyleSheet(QStringLiteral("QWidget { background-color: rgba(255, 255, 255, 0.05); border-radius: 4px; }"));
        
        auto *mainLayout = new QHBoxLayout(group);
        mainLayout->setContentsMargins(4, 4, 4, 4);
        mainLayout->setSpacing(8);

        auto *thumbLabel = new QLabel(group);
        thumbLabel->setFixedSize(48, 48);
        thumbLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        thumbLabel->setAlignment(Qt::AlignCenter);
        thumbLabel->setStyleSheet(QStringLiteral("background-color: #222; border-radius: 2px;"));
        thumbLabel->setText(tr("None"));
        mainLayout->addWidget(thumbLabel);

        auto *controlsLayout = new QVBoxLayout();
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        controlsLayout->setSpacing(2);

        auto *loadButton =
            CreateTextureToolButton(group, tr("Load texture"), TextureToolIcon::Load);
        auto *clearButton =
            CreateTextureToolButton(group, tr("Clear texture"), TextureToolIcon::Clear);

        auto *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(4);

        if (!title.isEmpty()) {
            auto *titleLabel = new QLabel(title, group);
            titleLabel->setWordWrap(true);
            titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            titleLabel->setStyleSheet(QStringLiteral("color: #999; font-weight: bold; background: transparent;"));
            titleRow->addWidget(titleLabel, 1);
        } else {
            titleRow->addStretch(1);
        }
        titleRow->addWidget(loadButton);
        titleRow->addWidget(clearButton);
        controlsLayout->addLayout(titleRow);

        auto *bottomRow = new QHBoxLayout();
        bottomRow->setContentsMargins(0, 0, 0, 0);
        bottomRow->setSpacing(3);

        auto *combo = new QComboBox(group);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(4);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        combo->setModel(m_textureOptionsModel);

        auto *amount = CreateSliderControl(0.0, 1.0, 0.01, 3);
        amount->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        amount->setMinimumWidth(0);
        amount->spinBox()->setMinimumWidth(52);
        amount->spinBox()->setMaximumWidth(60);
        if (auto *slider = amount->findChild<QSlider *>()) {
            slider->setMinimumWidth(40);
        }

        bottomRow->addWidget(combo, 1);
        bottomRow->addWidget(amount, 1);

        controlsLayout->addLayout(bottomRow);
        mainLayout->addLayout(controlsLayout, 1);

        m_textureSlots[slot].group = group;
        m_textureSlots[slot].combo = combo;
        m_textureSlots[slot].amount = amount;
        m_textureSlots[slot].loadButton = loadButton;
        m_textureSlots[slot].clearButton = clearButton;
        m_textureSlots[slot].thumbLabel = thumbLabel;
    };

    createTextureSlot(Albedo, tr("Albedo Texture"));
    createTextureSlot(Opacity, tr("Opacity Texture"));
    createTextureSlot(PackedMetalRough, tr("Packed PBR Texture"));
    createTextureSlot(Metalness, tr("Metalness Texture"));
    createTextureSlot(RoughnessGlossiness, tr("Roughness / Glossiness Texture"));
    createTextureSlot(Normal, tr("Normal Map Texture"));
    createTextureSlot(CoatNormal, tr("Coat Normal Map"));
    createTextureSlot(Occlusion, tr("Ambient Occlusion Texture"));
    createTextureSlot(Emissive, tr("Emissive Texture"));
    createTextureSlot(SpecularColor, tr("Specular Color Texture"));
    createTextureSlot(Thickness, tr("Thickness Texture"));
    createTextureSlot(Parallax, tr("Parallax Texture"));
    if (m_textureSlots[Parallax].amount) {
        m_textureSlots[Parallax].amount->setVisible(false);
    }

    auto configureMaterialForm = [](QFormLayout *form) {
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);
        form->setContentsMargins(6, 10, 6, 10);
    };
    auto addDivider = [](QFormLayout *form, QWidget *parent) {
        auto *divider = new QFrame(parent);
        divider->setFrameShape(QFrame::HLine);
        divider->setObjectName(QStringLiteral("MaterialDivider"));
        form->addRow(divider);
    };
    auto createPlaceholderCheck = [this](const QString &text,
                                         bool checked,
                                         QWidget *parent,
                                         const QString &reason = QString()) {
        auto *check = new QCheckBox(text, parent);
        check->setChecked(checked);
        check->setEnabled(false);
        check->setToolTip(
            reason.isEmpty()
                ? tr("Placeholder: this material option is not implemented by the renderer yet.")
                : reason);
        return check;
    };
    auto createPlaceholderCombo = [this](const QStringList &items,
                                         int index,
                                         QWidget *parent,
                                         const QString &reason = QString()) {
        auto *combo = new QComboBox(parent);
        combo->addItems(items);
        combo->setCurrentIndex(index);
        combo->setEnabled(false);
        combo->setToolTip(
            reason.isEmpty()
                ? tr("Placeholder: this material option is not implemented by the renderer yet.")
                : reason);
        return combo;
    };

    auto *textureTab = new QWidget(m_tabs);
    auto *textureLayout = new QVBoxLayout(textureTab);
    textureLayout->setContentsMargins(6, 10, 6, 10);
    textureLayout->setSpacing(7);

    auto createMapShortcut = [this, textureTab](TextureSlot slot,
                                                QWidget *parent) {
        auto *button = new QToolButton(parent);
        button->setText(tr("Map"));
        button->setToolTip(tr("Open the texture slot editor."));
        button->setProperty("materialMapShortcut", true);
        button->setFixedWidth(48);
        connect(button, &QToolButton::clicked, this, [this, textureTab, slot]() {
            m_tabs->setCurrentWidget(textureTab);
            QWidget *group = m_textureSlots[slot].group;
            if (group) {
                group->setFocus(Qt::OtherFocusReason);
            }
        });
        return button;
    };
    auto addMappedRow = [&createMapShortcut](QFormLayout *form,
                                             const QString &label,
                                             QWidget *control,
                                             TextureSlot slot,
                                             QWidget *parent) {
        auto *rowWidget = new QWidget(parent);
        auto *row = new QGridLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setHorizontalSpacing(10);
        auto *caption = new QLabel(label, rowWidget);
        row->addWidget(caption, 0, 0);
        row->addWidget(control, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(createMapShortcut(slot, rowWidget), 0, 2,
                       Qt::AlignCenter);
        row->setColumnStretch(0, 1);
        form->addRow(rowWidget);
    };
    auto addSliderRow = [&createMapShortcut](QFormLayout *form,
                                             const QString &label,
                                             SliderControl *control,
                                             int textureSlot,
                                             QWidget *parent) {
        control->setStackedLabel(label);
        auto *rowWidget = new QWidget(parent);
        auto *row = new QGridLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setHorizontalSpacing(10);
        row->addWidget(control, 0, 0);
        if (textureSlot >= 0) {
            row->addWidget(
                createMapShortcut(static_cast<TextureSlot>(textureSlot),
                                  rowWidget),
                0, 1, Qt::AlignVCenter);
        } else {
            auto *mapSpacer = new QWidget(rowWidget);
            mapSpacer->setFixedWidth(48);
            row->addWidget(mapSpacer, 0, 1);
        }
        row->setColumnStretch(0, 1);
        form->addRow(rowWidget);
    };

    auto *surfaceTab = new QWidget(m_tabs);
    auto *surfaceLayout = new QVBoxLayout(surfaceTab);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    auto *surfaceTitle = new QLabel(tr("Surface"), surfaceTab);
    surfaceTitle->setObjectName(QStringLiteral("MaterialPageTitle"));
    surfaceLayout->addWidget(surfaceTitle);
    auto *surfaceForm = new QFormLayout();
    configureMaterialForm(surfaceForm);
    surfaceLayout->addLayout(surfaceForm);

    m_baseColorButton = new QPushButton(surfaceTab);
    addMappedRow(surfaceForm, tr("Diffuse color"), m_baseColorButton, Albedo,
                 surfaceTab);

    auto *bumpType = new QComboBox(surfaceTab);
    bumpType->addItems({tr("Normal Map"), tr("Bump Map (placeholder)")});
    bumpType->setToolTip(
        tr("Normal maps are supported. Height/bump conversion is a placeholder."));
    if (auto *model = qobject_cast<QStandardItemModel *>(bumpType->model())) {
        if (QStandardItem *item = model->item(1)) {
            item->setEnabled(false);
        }
    }
    surfaceForm->addRow(tr("Bump type"), bumpType);
    surfaceForm->addRow(tr("Bump map"),
                        createMapShortcut(Normal, surfaceTab));
    m_bumpAmount = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Bump amount"), m_bumpAmount, Normal,
                 surfaceTab);

    addDivider(surfaceForm, surfaceTab);

    m_materialClassCombo = new QComboBox(surfaceTab);
    m_materialClassCombo->addItems({
        tr("Generic"),
        tr("Metal"),
        tr("Glass"),
        tr("Fabric"),
        tr("Leaf"),
        tr("Emissive"),
    });
    surfaceForm->addRow(tr("Material class"), m_materialClassCombo);

    m_workflowCombo = new QComboBox(surfaceTab);
    m_workflowCombo->addItems({
        tr("Metalness / Roughness"),
        tr("Reflection / Glossiness"),
    });
    m_workflowCombo->setVisible(false);

    m_metalness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Metalness"), m_metalness, Metalness,
                 surfaceTab);
    m_secondarySurfaceLabel = m_metalness->captionLabel();

    m_specularWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Specular weight"), m_specularWeight, -1,
                 surfaceTab);
    m_specularWeightLabel = m_specularWeight->captionLabel();
    m_specularColorButton = new QPushButton(surfaceTab);
    addMappedRow(surfaceForm, tr("Reflection color"), m_specularColorButton,
                 SpecularColor, surfaceTab);

    m_roughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Roughness"), m_roughness,
                 RoughnessGlossiness, surfaceTab);
    m_roughnessSurfaceLabel = m_roughness->captionLabel();

    addDivider(surfaceForm, surfaceTab);

    m_coatWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Coat amount"), m_coatWeight, -1,
                 surfaceTab);
    m_coatRoughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Coat glossiness"), m_coatRoughness, -1,
                 surfaceTab);

    addDivider(surfaceForm, surfaceTab);

    m_transmission = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(surfaceForm, tr("Refraction amount"), m_transmission, -1,
                 surfaceTab);
    m_transmissionColorButton = new QPushButton(surfaceTab);
    surfaceForm->addRow(tr("Refraction color"), m_transmissionColorButton);
    m_ior = CreateSliderControl(MaterialSystem::kMinMaterialIor,
                                MaterialSystem::kMaxMaterialIor, 0.001, 3);
    addSliderRow(surfaceForm, tr("Refraction IOR"), m_ior, -1, surfaceTab);

    addDivider(surfaceForm, surfaceTab);

    m_emissiveColorButton = new QPushButton(surfaceTab);
    addMappedRow(surfaceForm, tr("Self-illumination color"),
                 m_emissiveColorButton, Emissive, surfaceTab);
    m_emissiveIntensity = CreateSliderControl(0.0, 1000000.0, 1.0, 1);
    addSliderRow(surfaceForm, tr("Self-illumination intensity"),
                 m_emissiveIntensity, -1, surfaceTab);
    surfaceLayout->addStretch(1);
    AddMaterialTab(m_tabs, surfaceTab, MaterialTabIcon::Surface,
                   tr("Surface"));

    auto *advancedTab = new QWidget(m_tabs);
    auto *advancedLayout = new QVBoxLayout(advancedTab);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    auto *advancedTitle = new QLabel(tr("Surface Details"), advancedTab);
    advancedTitle->setObjectName(QStringLiteral("MaterialPageTitle"));
    advancedLayout->addWidget(advancedTitle);
    auto *advancedForm = new QFormLayout();
    configureMaterialForm(advancedForm);
    advancedLayout->addLayout(advancedForm);
    advancedForm->addRow(createPlaceholderCheck(
        tr("Physically based Fresnel"), true, advancedTab,
        tr("Always enabled by the renderer's physically based GGX model.")));
    advancedForm->addRow(createPlaceholderCheck(
        tr("Shared surface IOR"), true, advancedTab,
        tr("The renderer currently uses the shared material IOR.")));
    addDivider(advancedForm, advancedTab);
    advancedForm->addRow(
        tr("Reflection model"),
        createPlaceholderCombo({tr("GGX")}, 0, advancedTab,
                               tr("The renderer currently uses GGX.")));
    auto *ggxTail = CreateSliderControl(0.0, 5.0, 0.01, 3);
    ggxTail->setValue(2.0);
    ggxTail->setEnabled(false);
    ggxTail->setToolTip(
        tr("Placeholder: configurable reflection-tail shaping is not implemented."));
    addSliderRow(advancedForm, tr("Highlight tail"), ggxTail, -1,
                 advancedTab);

    m_useRoughness = new QCheckBox(tr("Use roughness"), advancedTab);
    m_useRoughness->setToolTip(
        tr("Switches between the metalness/roughness and reflection/glossiness authoring workflows."));
    advancedForm->addRow(m_useRoughness);
    m_normalMapFlipY =
        new QCheckBox(tr("DirectX normal map (-Y)"), advancedTab);
    m_normalMapFlipY->setToolTip(
        tr("Flips the tangent-space green channel for DirectX-style -Y maps. "
           "Leave this off for OpenGL/glTF maps, including filenames containing _nor_gl_."));
    advancedForm->addRow(m_normalMapFlipY);
    m_doubleSided = new QCheckBox(tr("Double-sided"), advancedTab);
    advancedForm->addRow(m_doubleSided);
    advancedForm->addRow(
        createPlaceholderCheck(tr("Back-face reflections"), false, advancedTab));

    addDivider(advancedForm, advancedTab);
    auto *coatColor = new QPushButton(advancedTab);
    coatColor->setText(tr("White"));
    coatColor->setEnabled(false);
    coatColor->setToolTip(
        tr("Placeholder: the current coat layer has no independent color."));
    advancedForm->addRow(tr("Coat color"), coatColor);
    m_coatIor = CreateSliderControl(MaterialSystem::kMinMaterialIor,
                                    MaterialSystem::kMaxMaterialIor, 0.01, 3);
    addSliderRow(advancedForm, tr("Coat IOR"), m_coatIor, -1,
                 advancedTab);
    auto *refractionGloss = CreateSliderControl(0.0, 1.0, 0.01, 3);
    refractionGloss->setValue(1.0);
    refractionGloss->setEnabled(false);
    refractionGloss->setToolTip(
        tr("Placeholder: refraction currently shares the surface roughness."));
    addSliderRow(advancedForm, tr("Refraction glossiness"),
                 refractionGloss, -1, advancedTab);
    advancedForm->addRow(createPlaceholderCheck(
        tr("Transmission shadows"), true, advancedTab,
        tr("Transmission is included in visibility by the renderer.")));
    m_thinWalled = new QCheckBox(tr("Thin-walled transmission"), advancedTab);
    advancedForm->addRow(m_thinWalled);

    addDivider(advancedForm, advancedTab);
    advancedForm->addRow(createPlaceholderCheck(
        tr("Emission contributes to lighting"), true, advancedTab,
        tr("Emissive materials already participate in renderer lighting.")));
    advancedForm->addRow(createPlaceholderCheck(
        tr("Emission exposure compensation"), false, advancedTab));

    addDivider(advancedForm, advancedTab);
    m_anisotropy = CreateSliderControl(-1.0, 1.0, 0.01, 3);
    addSliderRow(advancedForm, tr("Anisotropy"), m_anisotropy, -1,
                 advancedTab);
    m_anisotropyRotation = CreateSliderControl(0.0, 360.0, 1.0, 1);
    addSliderRow(advancedForm, tr("Anisotropy rotation"),
                 m_anisotropyRotation, -1, advancedTab);
    m_sheenWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(advancedForm, tr("Sheen amount"), m_sheenWeight, -1,
                 advancedTab);
    m_sheenColorButton = new QPushButton(advancedTab);
    advancedForm->addRow(tr("Sheen color"), m_sheenColorButton);
    advancedLayout->addStretch(1);
    AddMaterialTab(m_tabs, advancedTab, MaterialTabIcon::Advanced,
                   tr("Surface Details"));

    auto *opacityTab = new QWidget(m_tabs);
    auto *opacityLayout = new QVBoxLayout(opacityTab);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    auto *opacityTitle =
        new QLabel(tr("Opacity and Volume"), opacityTab);
    opacityTitle->setObjectName(QStringLiteral("MaterialPageTitle"));
    opacityLayout->addWidget(opacityTitle);
    auto *opacityForm = new QFormLayout();
    configureMaterialForm(opacityForm);
    opacityLayout->addLayout(opacityForm);
    auto *opacitySource = new QComboBox(opacityTab);
    opacitySource->addItem(tr("Material alpha x opacity map"));
    opacitySource->setToolTip(
        tr("The renderer multiplies material alpha by the optional opacity texture."));
    opacityForm->addRow(tr("Opacity input"), opacitySource);
    m_opacity = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(opacityForm, tr("Opacity"), m_opacity, Opacity, opacityTab);
    m_alphaMode = new QComboBox(opacityTab);
    m_alphaMode->addItems({tr("Opaque"), tr("Clip"), tr("Stochastic")});
    opacityForm->addRow(tr("Opacity mode"), m_alphaMode);
    m_alphaCutoff = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(opacityForm, tr("Clip threshold"), m_alphaCutoff, -1,
                 opacityTab);

    addDivider(opacityForm, opacityTab);
    m_translucencyType = new QComboBox(opacityTab);
    m_translucencyType->addItems(
        {tr("None"), tr("Thin translucency"), tr("SSS (placeholder)")});
    m_translucencyType->setToolTip(
        tr("Thin translucency is supported. Full subsurface scattering is a placeholder."));
    if (auto *model =
            qobject_cast<QStandardItemModel *>(m_translucencyType->model())) {
        if (QStandardItem *item = model->item(2)) {
            item->setEnabled(false);
        }
    }
    opacityForm->addRow(tr("Translucency type"), m_translucencyType);
    m_translucency = CreateSliderControl(0.0, 1.0, 0.01, 3);
    addSliderRow(opacityForm, tr("Translucency amount"), m_translucency, -1,
                 opacityTab);
    m_thickness = CreateSliderControl(0.0, 1.0, 0.001, 4);
    addSliderRow(opacityForm, tr("Depth"), m_thickness, Thickness,
                 opacityTab);
    m_attenuationDistance = CreateSliderControl(0.0, 100.0, 0.01, 3);
    addSliderRow(opacityForm, tr("Attenuation distance"),
                 m_attenuationDistance, -1, opacityTab);
    opacityLayout->addStretch(1);
    AddMaterialTab(m_tabs, opacityTab, MaterialTabIcon::Opacity,
                   tr("Opacity and Volume"));

    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        if (m_textureSlots[slot].group) {
            textureLayout->addWidget(m_textureSlots[slot].group);
        }
    }
    textureLayout->addStretch(1);
    AddMaterialTab(m_tabs, textureTab, MaterialTabIcon::Maps, tr("Maps"));

    auto *grassTab = new QWidget(m_tabs);
    auto *grassLayout = new QVBoxLayout(grassTab);
    m_grassEnabled = new QCheckBox(tr("Enable Grass"), grassTab);
    grassLayout->addWidget(m_grassEnabled);
    m_grassHint = new QLabel(
        tr("Enable Grass to use this material as a grass emitter."), grassTab);
    m_grassHint->setWordWrap(true);
    m_grassHint->setStyleSheet(QStringLiteral("color: #777;"));
    grassLayout->addWidget(m_grassHint);

    auto *grassForm = new QFormLayout();
    grassForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    grassForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    grassForm->setContentsMargins(0, 0, 0, 0);
    grassForm->setHorizontalSpacing(6);
    grassForm->setVerticalSpacing(4);
    m_grassColorButton = new QPushButton(tr("Pick"), grassTab);
    grassForm->addRow(tr("Grass Color"), m_grassColorButton);
    m_grassBladeSize = CreateSliderControl(0.05, 5.0, 0.05, 2);
    grassForm->addRow(tr("Blade Size"), m_grassBladeSize);
    m_grassBladeCount = CreateSliderControl(0.0, 1024.0, 0.5, 1);
    grassForm->addRow(tr("Patch Density / m2"), m_grassBladeCount);
    m_grassBladeVariation = CreateSliderControl(0.0, 1.0, 0.01, 2);
    grassForm->addRow(tr("Blade Variation"), m_grassBladeVariation);
    grassLayout->addLayout(grassForm);
    grassLayout->addStretch(1);
    AddMaterialTab(m_tabs, grassTab, MaterialTabIcon::Grass, tr("Grass"));

    auto *mappingTab = new QWidget(m_tabs);
    auto *mappingForm = new QFormLayout(mappingTab);
    mappingForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    mappingForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mappingForm->setHorizontalSpacing(6);
    mappingForm->setVerticalSpacing(4);
    QWidget *uvScaleWidget = CreateVec2Row(&m_uvScaleX, &m_uvScaleY, 0.0, 1000.0, 0.1, 3);
    mappingForm->addRow(tr("UV Scale"), uvScaleWidget);
    QWidget *uvOffsetWidget = CreateVec2Row(&m_uvOffsetX, &m_uvOffsetY, -10000.0, 10000.0, 0.1, 3);
    mappingForm->addRow(tr("UV Offset"), uvOffsetWidget);
    m_uvRotation = CreateSliderControl(-360.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("UV Rotation (deg)"), m_uvRotation);

    auto *triPlanarDivider = new QFrame(mappingTab);
    triPlanarDivider->setFrameShape(QFrame::HLine);
    triPlanarDivider->setFrameShadow(QFrame::Sunken);
    mappingForm->addRow(triPlanarDivider);

    m_triPlanarEnabled = new QCheckBox(tr("Enable Tri-Planar"), mappingTab);
    mappingForm->addRow(m_triPlanarEnabled);
    m_triPlanarScale = CreateSliderControl(0.001, 50.0, 0.05, 3);
    m_triPlanarScale->setLogarithmic(true);
    mappingForm->addRow(tr("Scale"), m_triPlanarScale);
    m_triPlanarSharpness = CreateSliderControl(0.25, 16.0, 0.1, 2);
    mappingForm->addRow(tr("Sharpness"), m_triPlanarSharpness);
    m_triPlanarNormalStrength = CreateSliderControl(0.0, 4.0, 0.05, 2);
    mappingForm->addRow(tr("Normal Strength"), m_triPlanarNormalStrength);
    m_triPlanarRotationX = CreateSliderControl(0.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("Rotation X (deg)"), m_triPlanarRotationX);
    m_triPlanarRotationY = CreateSliderControl(0.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("Rotation Y (deg)"), m_triPlanarRotationY);
    m_triPlanarRotationZ = CreateSliderControl(0.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("Rotation Z (deg)"), m_triPlanarRotationZ);
    m_triPlanarVariationMode = new QComboBox(mappingTab);
    m_triPlanarVariationMode->addItems({tr("Off"), tr("Per Mesh"), tr("Per Surface")});
    mappingForm->addRow(tr("Stochastic Tiling"), m_triPlanarVariationMode);
    m_triPlanarVariationOffset = CreateSliderControl(0.0, 1.0, 0.01, 2);
    mappingForm->addRow(tr("Offset Jitter"), m_triPlanarVariationOffset);
    m_stochasticTilingRotation = CreateSliderControl(0.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("Random Rotation"), m_stochasticTilingRotation);
    m_stochasticTilingMirror = new QCheckBox(tr("Mirror Tiles"), mappingTab);
    mappingForm->addRow(m_stochasticTilingMirror);
    m_stochasticTilingColorVariation = CreateSliderControl(0.0, 1.0, 0.01, 2);
    mappingForm->addRow(tr("Color Variation"), m_stochasticTilingColorVariation);

    AddMaterialTab(m_tabs, mappingTab, MaterialTabIcon::Mapping,
                   tr("Mapping"));

    auto *parallaxTab = new QWidget(m_tabs);
    auto *parallaxForm = new QFormLayout(parallaxTab);
    parallaxForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    parallaxForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    parallaxForm->setHorizontalSpacing(6);
    parallaxForm->setVerticalSpacing(4);
    m_parallaxMode = new QComboBox(parallaxTab);
    m_parallaxMode->addItems({tr("Off"), tr("Height Map"), tr("Window Box")});
    parallaxForm->addRow(tr("Mode"), m_parallaxMode);
    parallaxForm->addRow(m_textureSlots[Parallax].group);
    m_parallaxDepthScale = CreateSliderControl(0.0, 0.25, 0.001, 4);
    parallaxForm->addRow(tr("Height Depth"), m_parallaxDepthScale);
    m_parallaxRoomDepth = CreateSliderControl(0.1, 100.0, 0.05, 3);
    m_parallaxRoomDepth->setLogarithmic(true);
    parallaxForm->addRow(tr("Room Depth"), m_parallaxRoomDepth);
    m_parallaxWindowAspect = CreateSliderControl(0.05, 20.0, 0.01, 3);
    parallaxForm->addRow(tr("Window Aspect"), m_parallaxWindowAspect);
    m_parallaxWindowBrightness = CreateSliderControl(0.0, 1000000.0, 1.0, 1);
    parallaxForm->addRow(tr("Window Brightness"), m_parallaxWindowBrightness);
    QWidget *parallaxScaleWidget =
        CreateVec2Row(&m_parallaxScaleX, &m_parallaxScaleY,
                      0.05, 20.0, 0.01, 3);
    m_parallaxScaleX->setLogarithmic(true);
    m_parallaxScaleY->setLogarithmic(true);
    parallaxForm->addRow(tr("Window Scale"), parallaxScaleWidget);
    QWidget *parallaxOffsetWidget =
        CreateVec2Row(&m_parallaxOffsetX, &m_parallaxOffsetY,
                      -2.0, 2.0, 0.001, 4);
    parallaxForm->addRow(tr("Window Offset"), parallaxOffsetWidget);
    m_parallaxBackFace = new QCheckBox(tr("Render On Back Face"), parallaxTab);
    m_parallaxBackFace->setToolTip(
        tr("Uses the opposite surface side for Window Box parallax on reversed window planes."));
    parallaxForm->addRow(m_parallaxBackFace);
    AddMaterialTab(m_tabs, parallaxTab, MaterialTabIcon::Parallax,
                   tr("Parallax"));

    auto *qaTab = new QWidget(m_tabs);
    auto *qaLayout = new QVBoxLayout(qaTab);
    m_qaLabel = new QLabel(qaTab);
    m_qaLabel->setWordWrap(true);
    m_qaLabel->setTextFormat(Qt::RichText);
    qaLayout->addWidget(m_qaLabel);
    qaLayout->addStretch(1);
    AddMaterialTab(m_tabs, qaTab, MaterialTabIcon::Qa,
                   tr("Material QA"));

    m_tabs->setObjectName(QStringLiteral("ProjectMaterialTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setIconSize(QSize(20, 20));
    m_tabs->tabBar()->setAccessibleName(tr("Material editor sections"));
    m_tabs->setStyleSheet(QStringLiteral(R"(
        QTabWidget#ProjectMaterialTabs::pane {
            background: #242424;
            border: none;
        }
        QTabWidget#ProjectMaterialTabs QTabBar {
            background: #242424;
            border: none;
        }
        QLabel#MaterialPageTitle {
            color: white;
            font-size: 16px;
            font-weight: 700;
            padding: 12px 6px 2px 6px;
        }
        QLabel#SliderControlCaption {
            color: #d7d7d7;
            font-size: 13px;
        }
        QDoubleSpinBox#SliderControlValue {
            min-height: 22px;
            padding: 1px 4px;
            background: transparent;
            color: #ffffff;
            border: none;
            font-size: 13px;
        }
        QDoubleSpinBox#SliderControlValue:focus {
            background: #303030;
            border: 1px solid #58d0f4;
        }
        QFrame#MaterialDivider {
            color: #505050;
            background: #505050;
            max-height: 1px;
            margin: 7px 0px;
        }
        QToolButton[materialMapShortcut="true"] {
            color: #b8b8b8;
            background: transparent;
            border: 1px solid #686868;
            min-height: 30px;
            padding: 2px 5px;
        }
        QToolButton[materialMapShortcut="true"]:hover {
            color: #72d8fa;
            border-color: #72d8fa;
        }
    )"));

    inspectorLayout->addWidget(m_tabs);
    layout->addWidget(m_inspectorGroup);
    layout->addStretch(1);

    connect(m_pickButton, &QPushButton::clicked, this, [this]() {
        const bool enable = !MaterialEditor::IsPickingEnabled();
        MaterialEditor::SetPickingEnabled(enable);
        updatePickUi();
    });
    connect(m_showAllCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_syncing) {
            return;
        }
        rebuildMaterialList();
    });
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_syncing) {
            return;
        }
        rebuildMaterialList();
    });
    connect(m_materialList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_syncing) {
            return;
        }
        if (row < 0 || row >= static_cast<int>(m_materialIndices.size())) {
            return;
        }
        m_selectedMaterial = m_materialIndices[row];
        syncInspector();
    });

    connect(m_materialNameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_syncing) {
            return;
        }
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        const QByteArray text = m_materialNameEdit->text().toUtf8();
        strncpy_s(mat.name, text.constData(), _TRUNCATE);
        DxrRenderer::ResetAccumulation();
        rebuildMaterialList();
        syncInspector();
    });

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        m_clipboard = std::make_unique<Asset::Material>(g_loadedMaterials[idx]);
        m_pasteButton->setEnabled(true);
    });
    connect(m_pasteButton, &QPushButton::clicked, this, [this]() {
        if (!m_clipboard) {
            return;
        }
        applyMaterialChange([this](Asset::Material &mat) {
            mat = *m_clipboard;
        }, true, false, true);
    });
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        applyMaterialChange([](Asset::Material &mat) {
            Asset::Material def;
            char nameBuf[64];
            strncpy_s(nameBuf, mat.name, _TRUNCATE);
            const int d = mat.diffuseTexture;
            const int a = mat.opacityTexture;
            const int n = mat.normalTexture;
            const int cn = mat.coatNormalTexture;
            const int e = mat.emissiveTexture;
            const int o = mat.occlusionTexture;
            const int mr = mat.metalRoughTexture;
            const int mt = mat.metalnessTexture;
            const int rg = mat.roughnessGlossTexture;
            const int sc = mat.specularColorTexture;
            const int tk = mat.thicknessTexture;
            const int px = mat.parallaxTexture;
            const float da = mat.diffuseTextureAmount;
            const float aa = mat.opacityTextureAmount;
            const float na = mat.normalTextureAmount;
            const bool normalMapFlipY = mat.normalMapFlipY;
            const float cna = mat.coatNormalTextureAmount;
            const float ea = mat.emissiveTextureAmount;
            const float oa = mat.occlusionTextureAmount;
            const float mra = mat.metalRoughTextureAmount;
            const float mta = mat.metalnessTextureAmount;
            const float rga = mat.roughnessGlossTextureAmount;
            const float sca = mat.specularColorTextureAmount;
            const float tka = mat.thicknessTextureAmount;
            const uint32_t pm = mat.parallaxMode;
            const float pds = mat.parallaxDepthScale;
            const float prd = mat.parallaxRoomDepth;
            const float pwa = mat.parallaxWindowAspect;
            const float psx = mat.parallaxUvScale[0];
            const float psy = mat.parallaxUvScale[1];
            const float pox = mat.parallaxUvOffset[0];
            const float poy = mat.parallaxUvOffset[1];
            const bool pbf = mat.parallaxBackFace;
            mat = def;
            strncpy_s(mat.name, nameBuf, _TRUNCATE);
            mat.diffuseTexture = d;
            mat.opacityTexture = a;
            mat.normalTexture = n;
            mat.coatNormalTexture = cn;
            mat.emissiveTexture = e;
            mat.occlusionTexture = o;
            mat.metalRoughTexture = mr;
            mat.metalnessTexture = mt;
            mat.roughnessGlossTexture = rg;
            mat.specularColorTexture = sc;
            mat.thicknessTexture = tk;
            mat.parallaxTexture = px;
            mat.diffuseTextureAmount = da;
            mat.opacityTextureAmount = aa;
            mat.normalTextureAmount = na;
            mat.normalMapFlipY = normalMapFlipY;
            mat.coatNormalTextureAmount = cna;
            mat.emissiveTextureAmount = ea;
            mat.occlusionTextureAmount = oa;
            mat.metalRoughTextureAmount = mra;
            mat.metalnessTextureAmount = mta;
            mat.roughnessGlossTextureAmount = rga;
            mat.specularColorTextureAmount = sca;
            mat.thicknessTextureAmount = tka;
            mat.parallaxMode = pm;
            mat.parallaxDepthScale = pds;
            mat.parallaxRoomDepth = prd;
            mat.parallaxWindowAspect = pwa;
            mat.parallaxUvScale[0] = psx;
            mat.parallaxUvScale[1] = psy;
            mat.parallaxUvOffset[0] = pox;
            mat.parallaxUvOffset[1] = poy;
            mat.parallaxBackFace = pbf;
        }, true, false, true);
    });
    connect(m_resetNoTexButton, &QPushButton::clicked, this, [this]() {
        applyMaterialChange([](Asset::Material &mat) {
            Asset::Material def;
            char nameBuf[64];
            strncpy_s(nameBuf, mat.name, _TRUNCATE);
            mat = def;
            strncpy_s(mat.name, nameBuf, _TRUNCATE);
        }, true, false, true);
    });

    connect(m_applyPresetButton, &QPushButton::clicked, this, [this]() {
        const int presetIdx = m_presetCombo->currentIndex();
        applyMaterialChange([presetIdx](Asset::Material &mat) {
            ApplyPreset(mat, presetIdx);
        }, true, false, true);
    });

    connect(m_baseColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Base Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.diffuseColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.diffuseColor[0] = static_cast<float>(picked.redF());
                m.diffuseColor[1] = static_cast<float>(picked.greenF());
                m.diffuseColor[2] = static_cast<float>(picked.blueF());
            });
    });
    connect(m_specularColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Specular Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.specularColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.specularColor[0] = static_cast<float>(picked.redF());
                m.specularColor[1] = static_cast<float>(picked.greenF());
                m.specularColor[2] = static_cast<float>(picked.blueF());
            });
    });

    connect(m_transmissionColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Transmission Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.transmissionColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.transmissionColor[0] = static_cast<float>(picked.redF());
                m.transmissionColor[1] = static_cast<float>(picked.greenF());
                m.transmissionColor[2] = static_cast<float>(picked.blueF());
            });
    });
    connect(m_sheenColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Sheen Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.sheenColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.sheenColor[0] = static_cast<float>(picked.redF());
                m.sheenColor[1] = static_cast<float>(picked.greenF());
                m.sheenColor[2] = static_cast<float>(picked.blueF());
            });
    });

    connect(m_grassColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Grass Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.grassColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.grassColor[0] = static_cast<float>(picked.redF());
                m.grassColor[1] = static_cast<float>(picked.greenF());
                m.grassColor[2] = static_cast<float>(picked.blueF());
                m.diffuseColor[0] = m.grassColor[0];
                m.diffuseColor[1] = m.grassColor[1];
                m.diffuseColor[2] = m.grassColor[2];
            });
    });

    connect(m_emissiveColorButton, &QPushButton::clicked, this, [this]() {
        pickMaterialColor(
            tr("Pick Emissive Color"),
            [this](const Asset::Material &m) {
                return getColorFromMaterial(m.emissiveColor);
            },
            [](Asset::Material &m, const QColor &picked) {
                m.emissiveColor[0] = static_cast<float>(picked.redF());
                m.emissiveColor[1] = static_cast<float>(picked.greenF());
                m.emissiveColor[2] = static_cast<float>(picked.blueF());
            });
    });

    connect(m_workflowCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            m.workflow = static_cast<uint32_t>(std::clamp(index, 0, 1));
            if (m.workflow == Asset::Material::kWorkflowReflectionGlossiness) {
                m.metalness = 0.0f;
            }
        });
    });
    connect(m_useRoughness, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        const int workflow =
            enabled ? Asset::Material::kWorkflowMetalRoughness
                    : Asset::Material::kWorkflowReflectionGlossiness;
        applyMaterialChange([workflow](Asset::Material &m) {
            m.workflow = static_cast<uint32_t>(workflow);
            if (m.workflow == Asset::Material::kWorkflowReflectionGlossiness) {
                m.metalness = 0.0f;
            }
        });
    });
    connect(m_normalMapFlipY, &QCheckBox::toggled, this,
            [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.normalMapFlipY = enabled;
        });
    });

    connect(m_materialClassCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            MaterialSystem::ApplyMaterialClassAuthoringDefaults(
                m, static_cast<uint32_t>(std::clamp(index, 0, 5)));
        }, true);
    });

    connect(m_roughness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            float v = static_cast<float>(value);
            if (v < 0.0f) {
                v = 0.0f;
            } else if (v > 1.0f) {
                v = 1.0f;
            }
            m.roughness = IsReflectionGlossinessWorkflow(m) ? (1.0f - v) : v;
        });
    });
    connect(m_bumpAmount->spinBox(),
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            MaterialSystem::SetTextureAmount(
                m, MaterialSystem::TextureSlot::Normal,
                static_cast<float>(value));
        });
    });
    connect(m_metalness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            const float v = static_cast<float>(value);
            if (IsReflectionGlossinessWorkflow(m)) {
                m.specularWeight = v;
                m.metalness = 0.0f;
            } else {
                m.metalness = v;
            }
        });
    });
    connect(m_specularWeight->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.specularWeight = static_cast<float>(value);
        });
    });
    connect(m_ior->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.ior = static_cast<float>(value);
        });
    });
    connect(m_transmission->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.transmissionWeight = static_cast<float>(value);
        }, true);
    });
    connect(m_thickness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.thickness = static_cast<float>(value);
        });
    });
    connect(m_attenuationDistance->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.attenuationDistance = static_cast<float>(value);
        });
    });
    connect(m_coatWeight->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.coatWeight = static_cast<float>(value);
        });
    });
    connect(m_coatRoughness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.coatRoughness =
                1.0f - std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        });
    });
    connect(m_coatIor->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.coatIor = static_cast<float>(value);
        });
    });
    connect(m_translucency->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.translucency = static_cast<float>(value);
        }, true);
    });
    connect(m_translucencyType,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        if (m_syncing || index > 1) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            if (index == 0) {
                m.translucency = 0.0f;
            } else if (m.translucency <= 1.0e-5f) {
                m.translucency = 0.5f;
            }
        }, true);
    });
    connect(m_anisotropy->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.anisotropy = static_cast<float>(value);
        });
    });
    connect(m_anisotropyRotation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.anisotropyRotation = static_cast<float>(value);
        });
    });
    connect(m_sheenWeight->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.sheenWeight = static_cast<float>(value);
        });
    });
    connect(m_thinWalled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.thinWalled = enabled ? 1.0f : 0.0f;
        }, true);
    });

    connect(m_grassEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            if (enabled) {
                Asset::ApplyDefaultGrassLook(m);
            } else {
                m.isGrass = false;
            }
        }, false, true);
    });
    connect(m_grassBladeSize->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.grassBladeSize = static_cast<float>(value);
        }, false, true);
    });
    connect(m_grassBladeCount->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.grassBladeCount = static_cast<float>(value);
        }, false, true);
    });
    connect(m_grassBladeVariation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.grassBladeVariation = static_cast<float>(value);
        }, false, true);
    });

    connect(m_uvScaleX->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.uvScale[0] = static_cast<float>(value);
        });
    });
    connect(m_uvScaleY->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.uvScale[1] = static_cast<float>(value);
        });
    });
    connect(m_uvOffsetX->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.uvOffset[0] = static_cast<float>(value);
        });
    });
    connect(m_uvOffsetY->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.uvOffset[1] = static_cast<float>(value);
        });
    });
    connect(m_uvRotation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.uvRotationDegrees = static_cast<float>(value);
        });
    });
    connect(m_triPlanarEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.triPlanarEnabled = enabled ? 1.0f : 0.0f;
        });
    });
    connect(m_triPlanarScale->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarScale = static_cast<float>(value);
        });
    });
    connect(m_triPlanarSharpness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarSharpness = static_cast<float>(value);
        });
    });
    connect(m_triPlanarNormalStrength->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarNormalStrength = static_cast<float>(value);
        });
    });
    connect(m_triPlanarRotationX->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarRotationDegrees[0] = static_cast<float>(value);
        });
    });
    connect(m_triPlanarRotationY->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarRotationDegrees[1] = static_cast<float>(value);
        });
    });
    connect(m_triPlanarRotationZ->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarRotationDegrees[2] = static_cast<float>(value);
        });
    });
    connect(m_triPlanarVariationMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            m.triPlanarVariationMode = static_cast<uint32_t>(std::clamp(index, 0, 2));
        });
    });
    connect(m_triPlanarVariationOffset->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarVariationOffset = static_cast<float>(value);
        });
    });
    connect(m_stochasticTilingRotation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.stochasticTilingRotationDegrees = static_cast<float>(value);
        });
    });
    connect(m_stochasticTilingMirror, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.stochasticTilingMirror = enabled;
        });
    });
    connect(m_stochasticTilingColorVariation->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.stochasticTilingColorVariation = static_cast<float>(value);
        });
    });

    connect(m_parallaxMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            m.parallaxMode = static_cast<uint32_t>(std::clamp(index, 0, 2));
        }, false, false, true);
    });
    connect(m_parallaxDepthScale->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxDepthScale = static_cast<float>(value);
        });
    });
    connect(m_parallaxRoomDepth->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxRoomDepth = static_cast<float>(value);
        });
    });
    connect(m_parallaxWindowAspect->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxWindowAspect = static_cast<float>(value);
        });
    });
    connect(m_parallaxWindowBrightness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.emissiveIntensity = static_cast<float>(value);
        });
    });
    connect(m_parallaxScaleX->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxUvScale[0] = static_cast<float>(value);
        });
    });
    connect(m_parallaxScaleY->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxUvScale[1] = static_cast<float>(value);
        });
    });
    connect(m_parallaxOffsetX->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxUvOffset[0] = static_cast<float>(value);
        });
    });
    connect(m_parallaxOffsetY->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.parallaxUvOffset[1] = static_cast<float>(value);
        });
    });
    connect(m_parallaxBackFace, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.parallaxBackFace = enabled;
        });
    });

    connect(m_emissiveIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.emissiveIntensity = static_cast<float>(value);
        });
    });
    connect(m_opacity->spinBox(),
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.diffuseColor[3] =
                std::clamp(static_cast<float>(value), 0.0f, 1.0f);
        }, true);
    });

    connect(m_doubleSided, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([enabled](Asset::Material &m) {
            m.doubleSided = enabled;
        });
    });
    connect(m_alphaMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([index](Asset::Material &m) {
            m.alphaMode = AlphaModeFromIndex(index);
        }, true);
        m_alphaCutoff->setEnabled(index == 1);
    });
    connect(m_alphaCutoff->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.alphaCutoff = static_cast<float>(value);
        }, true);
    });

    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        auto &widgets = m_textureSlots[slot];
        if (!widgets.combo) {
            continue;
        }
        connect(widgets.combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, slot](int index) {
            if (m_syncing) {
                return;
            }
            applyMaterialChange([this, index, slot](Asset::Material &m) {
                int newIdx = textureIndexFromVisibleCombo(index);
                setTextureIndexForSlot(m, static_cast<TextureSlot>(slot), newIdx);
            }, false, false, true);
        });
        connect(widgets.amount->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, slot](double value) {
            if (m_syncing) {
                return;
            }
            applyMaterialChange([slot, value](Asset::Material &m) {
                MaterialSystem::SetTextureAmount(
                    m,
                    static_cast<MaterialSystem::TextureSlot>(slot),
                    static_cast<float>(value));
            });
        });
        connect(widgets.clearButton, &QPushButton::clicked, this, [this, slot]() {
            applyMaterialChange([this, slot](Asset::Material &m) {
                setTextureIndexForSlot(m, static_cast<TextureSlot>(slot), -1);
            }, false, false, true);
        });
        connect(widgets.loadButton, &QPushButton::clicked, this, [this, slot]() {
            std::wstring chosen;
            HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
            if (!owner) {
                owner = g_hwnd;
            }
            if (OpenTextureFileDialog(owner, chosen) && !chosen.empty()) {
                const bool isHdr = IsHDRTexturePath(chosen);
                const int newTex = Scene::AddTextureFromFile(
                    WStringToUtf8(chosen),
                    isHdr,
                    isHdr ? Asset::TextureUsageSemantic::Hdr
                          : TextureCompressionSemantic(slot));
                if (newTex >= 0) {
                    applyMaterialChange([this, newTex, slot](Asset::Material &m) {
                        setTextureIndexForSlot(m, static_cast<TextureSlot>(slot), newTex);
                    }, false, false, true);
                }
            }
        });
    }
}

void MaterialEditorPanel::refreshMaterials()
{
    m_refreshQueued = false;
    if (IsSceneIoJobActive()) {
        return;
    }
    Scene::ProcessPendingImport();
    updateCounts();
    updatePickUi();

    const int pending = MaterialEditor::ConsumePendingMaterialSelect();
    if (pending >= 0 && pending < static_cast<int>(g_loadedMaterials.size())) {
        m_selectedMaterial = pending;
        if (!m_showAllCheck->isChecked()) {
            const auto &nodes = Scene::GetNodes();
            int selectedNodeIndex = -1;
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].selected) {
                    selectedNodeIndex = static_cast<int>(i);
                    break;
                }
            }
            if (selectedNodeIndex >= 0) {
                const Scene::Node &node = nodes[static_cast<size_t>(selectedNodeIndex)];
                bool inNode = false;
                for (size_t mi = 0; mi < node.meshIndices.size(); ++mi) {
                    const size_t meshIndex = node.meshIndices[mi];
                    if (meshIndex >= g_loadedMeshes.size()) {
                        continue;
                    }
                    const int matIdx = g_loadedMeshes[meshIndex].materialIndex;
                    if (matIdx == pending) {
                        inNode = true;
                        break;
                    }
                }
                if (!inNode) {
                    m_syncing = true;
                    m_showAllCheck->setChecked(true);
                    m_syncing = false;
                }
            }
        }
    }

    rebuildMaterialList();
    syncInspector();
}

void MaterialEditorPanel::scheduleRefresh()
{
    if (m_refreshQueued) {
        return;
    }
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        refreshMaterials();
    }, Qt::QueuedConnection);
}

void MaterialEditorPanel::rebuildMaterialList()
{
    m_syncing = true;

    const auto &nodes = Scene::GetNodes();
    int selectedNodeIndex = -1;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].selected) {
            selectedNodeIndex = static_cast<int>(i);
            break;
        }
    }
    const Scene::Node *selectedNode =
        (selectedNodeIndex >= 0) ? &nodes[static_cast<size_t>(selectedNodeIndex)] : nullptr;

    if (selectedNode) {
        m_nodeLabel->setText(tr("Node: %1").arg(QString::fromStdString(selectedNode->name)));
    } else {
        m_nodeLabel->setText(tr("No node selected"));
    }

    const bool showAll = m_showAllCheck->isChecked();
    std::vector<int> scopeIndices;
    auto gatherSceneMaterialIndices = [&]() {
        std::vector<int> indices;
        for (const Scene::Node &node : nodes) {
            for (size_t meshIndex : node.meshIndices) {
                if (meshIndex >= g_loadedMeshes.size()) {
                    continue;
                }
                const int matIdx = g_loadedMeshes[meshIndex].materialIndex;
                if (matIdx < 0 || matIdx >= static_cast<int>(g_loadedMaterials.size())) {
                    continue;
                }
                if (std::find(indices.begin(), indices.end(), matIdx) == indices.end()) {
                    indices.push_back(matIdx);
                }
            }
        }
        return indices;
    };
    if (showAll || !selectedNode) {
        scopeIndices = gatherSceneMaterialIndices();
    } else {
        for (size_t i = 0; i < selectedNode->meshIndices.size(); ++i) {
            const size_t meshIndex = selectedNode->meshIndices[i];
            if (meshIndex >= g_loadedMeshes.size()) {
                continue;
            }
            const int matIdx = g_loadedMeshes[meshIndex].materialIndex;
            if (matIdx < 0 || matIdx >= static_cast<int>(g_loadedMaterials.size())) {
                continue;
            }
            if (std::find(scopeIndices.begin(), scopeIndices.end(), matIdx) == scopeIndices.end()) {
                scopeIndices.push_back(matIdx);
            }
        }
    }

    if (selectedNodeIndex != m_lastSelectedNodeIndex) {
        m_lastSelectedNodeIndex = selectedNodeIndex;
        if (showAll && selectedNode) {
            m_syncing = true;
            m_showAllCheck->setChecked(false);
            m_syncing = false;
            scopeIndices.clear();
            for (size_t i = 0; i < selectedNode->meshIndices.size(); ++i) {
                const size_t meshIndex = selectedNode->meshIndices[i];
                if (meshIndex >= g_loadedMeshes.size()) {
                    continue;
                }
                const int matIdx = g_loadedMeshes[meshIndex].materialIndex;
                if (matIdx < 0 || matIdx >= static_cast<int>(g_loadedMaterials.size())) {
                    continue;
                }
                if (std::find(scopeIndices.begin(), scopeIndices.end(), matIdx) == scopeIndices.end()) {
                    scopeIndices.push_back(matIdx);
                }
            }
        }
        bool ok = false;
        for (int idx : scopeIndices) {
            if (idx == m_selectedMaterial) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            m_selectedMaterial = scopeIndices.empty() ? -1 : scopeIndices.front();
        }
    }

    if (m_selectedMaterial < 0 && !scopeIndices.empty()) {
        m_selectedMaterial = scopeIndices.front();
    }

    m_materialList->clear();
    m_materialIndices.clear();

    if (!showAll && !selectedNode) {
        auto *item = new QListWidgetItem(tr("Select an object, or enable 'Show all materials'."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        m_materialList->addItem(item);
        m_materialList->setEnabled(false);
        m_syncing = false;
        return;
    }

    m_materialList->setEnabled(true);
    if (scopeIndices.empty()) {
        auto *item = new QListWidgetItem(tr("No materials in this scope."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        m_materialList->addItem(item);
        m_syncing = false;
        return;
    }

    const QString filter = m_filterEdit->text().trimmed();
    for (int matIdx : scopeIndices) {
        if (matIdx < 0 || matIdx >= static_cast<int>(g_loadedMaterials.size())) {
            continue;
        }
        if (!FilterPass(filter, g_loadedMaterials[matIdx].name)) {
            continue;
        }
        auto *item = new QListWidgetItem(MaterialLabel(g_loadedMaterials[matIdx], matIdx));
        if (matIdx == m_selectedMaterial) {
            item->setSelected(true);
        }
        m_materialList->addItem(item);
        m_materialIndices.push_back(matIdx);
    }

    if (m_materialIndices.empty()) {
        auto *item = new QListWidgetItem(tr("No materials match the filter."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        m_materialList->addItem(item);
    } else {
        setSelectedMaterial(m_selectedMaterial, false);
    }

    m_syncing = false;
}

void MaterialEditorPanel::syncInspector()
{
    m_syncing = true;

    const int idx = currentMaterialIndex();
    if (idx < 0) {
        m_inspectorGroup->setEnabled(false);
        m_materialIdLabel->setText(tr("No material selected"));
        m_syncing = false;
        return;
    }

    Asset::Material &mat = g_loadedMaterials[idx];
    m_inspectorGroup->setEnabled(true);

    SyncLineEditText(m_materialNameEdit, QString::fromUtf8(mat.name));
    m_materialIdLabel->setText(tr("#%1").arg(idx));
    m_pasteButton->setEnabled(static_cast<bool>(m_clipboard));

    syncInspectorMaterialState(mat, true);

    m_syncing = false;
}

void MaterialEditorPanel::syncInspectorMaterialState(const Asset::Material &mat,
                                                     bool refreshTextureUi)
{
    setColorButton(m_baseColorButton, getColorFromMaterial(mat.diffuseColor));
    SyncComboBoxIndex(m_materialClassCombo,
                      static_cast<int>(MaterialSystem::ClampMaterialClass(
                          mat.materialClass)));
    SyncComboBoxIndex(m_workflowCombo,
                      static_cast<int>(std::clamp(mat.workflow, 0u, 1u)));
    SyncCheckBoxState(m_useRoughness,
                      !IsReflectionGlossinessWorkflow(mat));
    SyncCheckBoxState(m_normalMapFlipY, mat.normalMapFlipY);
    updateWorkflowUi(mat);

    SyncSliderControlValue(m_roughness,
                           IsReflectionGlossinessWorkflow(mat)
                               ? (1.0f - mat.roughness)
                               : mat.roughness);
    SyncSliderControlValue(
        m_bumpAmount,
        MaterialSystem::GetTextureAmount(
            mat, MaterialSystem::TextureSlot::Normal));
    SyncSliderControlValue(m_metalness,
                           IsReflectionGlossinessWorkflow(mat)
                               ? mat.specularWeight
                               : mat.metalness);
    SyncSliderControlValue(m_specularWeight, mat.specularWeight);
    setColorButton(m_specularColorButton, getColorFromMaterial(mat.specularColor));
    SyncSliderControlValue(m_ior, mat.ior);
    SyncSliderControlValue(m_transmission, mat.transmissionWeight);
    setColorButton(m_transmissionColorButton, getColorFromMaterial(mat.transmissionColor));
    SyncSliderControlValue(m_thickness, mat.thickness);
    SyncSliderControlValue(m_attenuationDistance, mat.attenuationDistance);
    SyncSliderControlValue(m_coatWeight, mat.coatWeight);
    SyncSliderControlValue(m_coatRoughness, 1.0f - mat.coatRoughness);
    SyncSliderControlValue(m_coatIor, mat.coatIor);
    SyncSliderControlValue(m_translucency, mat.translucency);
    SyncComboBoxIndex(m_translucencyType,
                      mat.translucency > 1.0e-5f ? 1 : 0);
    SyncSliderControlValue(m_anisotropy, mat.anisotropy);
    SyncSliderControlValue(m_anisotropyRotation, mat.anisotropyRotation);
    SyncSliderControlValue(m_sheenWeight, mat.sheenWeight);
    setColorButton(m_sheenColorButton, getColorFromMaterial(mat.sheenColor));
    SyncCheckBoxState(m_thinWalled, mat.thinWalled > 0.5f);

    SyncCheckBoxState(m_grassEnabled, mat.isGrass);
    setColorButton(m_grassColorButton, getColorFromMaterial(mat.grassColor));
    SyncSliderControlValue(m_grassBladeSize, mat.grassBladeSize);
    SyncSliderControlValue(m_grassBladeCount, mat.grassBladeCount);
    SyncSliderControlValue(m_grassBladeVariation, mat.grassBladeVariation);
    m_grassHint->setVisible(!mat.isGrass);
    m_grassColorButton->setEnabled(mat.isGrass);
    m_grassBladeSize->setEnabled(mat.isGrass);
    m_grassBladeCount->setEnabled(mat.isGrass);
    m_grassBladeVariation->setEnabled(mat.isGrass);

    SyncSliderControlValue(m_uvScaleX, mat.uvScale[0]);
    SyncSliderControlValue(m_uvScaleY, mat.uvScale[1]);
    SyncSliderControlValue(m_uvOffsetX, mat.uvOffset[0]);
    SyncSliderControlValue(m_uvOffsetY, mat.uvOffset[1]);
    SyncSliderControlValue(m_uvRotation, mat.uvRotationDegrees);
    SyncCheckBoxState(m_triPlanarEnabled, mat.triPlanarEnabled > 0.5f);
    SyncSliderControlValue(m_triPlanarScale, mat.triPlanarScale);
    SyncSliderControlValue(m_triPlanarSharpness, mat.triPlanarSharpness);
    SyncSliderControlValue(m_triPlanarNormalStrength, mat.triPlanarNormalStrength);
    SyncSliderControlValue(m_triPlanarRotationX, mat.triPlanarRotationDegrees[0]);
    SyncSliderControlValue(m_triPlanarRotationY, mat.triPlanarRotationDegrees[1]);
    SyncSliderControlValue(m_triPlanarRotationZ, mat.triPlanarRotationDegrees[2]);
    SyncComboBoxIndex(m_triPlanarVariationMode,
        static_cast<int>(std::clamp(mat.triPlanarVariationMode, 0u, 2u)));
    SyncSliderControlValue(m_triPlanarVariationOffset, mat.triPlanarVariationOffset);
    SyncSliderControlValue(m_stochasticTilingRotation, mat.stochasticTilingRotationDegrees);
    SyncCheckBoxState(m_stochasticTilingMirror, mat.stochasticTilingMirror);
    SyncSliderControlValue(m_stochasticTilingColorVariation,
                           mat.stochasticTilingColorVariation);
    const bool triPlanarActive = mat.triPlanarEnabled > 0.5f;
    const bool stochasticActive =
        mat.triPlanarVariationMode != Asset::Material::kTriPlanarVariationOff;
    m_triPlanarRotationX->setEnabled(triPlanarActive);
    m_triPlanarRotationY->setEnabled(triPlanarActive);
    m_triPlanarRotationZ->setEnabled(triPlanarActive);
    m_triPlanarVariationOffset->setEnabled(stochasticActive);
    m_stochasticTilingRotation->setEnabled(stochasticActive);
    m_stochasticTilingMirror->setEnabled(stochasticActive);
    m_stochasticTilingColorVariation->setEnabled(stochasticActive);

    const int parallaxMode =
        static_cast<int>(std::clamp(mat.parallaxMode, 0u, 2u));
    SyncComboBoxIndex(m_parallaxMode, parallaxMode);
    SyncSliderControlValue(m_parallaxDepthScale, mat.parallaxDepthScale);
    SyncSliderControlValue(m_parallaxRoomDepth, mat.parallaxRoomDepth);
    SyncSliderControlValue(m_parallaxWindowAspect, mat.parallaxWindowAspect);
    SyncSliderControlValue(m_parallaxWindowBrightness, mat.emissiveIntensity);
    SyncSliderControlValue(m_parallaxScaleX, mat.parallaxUvScale[0]);
    SyncSliderControlValue(m_parallaxScaleY, mat.parallaxUvScale[1]);
    SyncSliderControlValue(m_parallaxOffsetX, mat.parallaxUvOffset[0]);
    SyncSliderControlValue(m_parallaxOffsetY, mat.parallaxUvOffset[1]);
    SyncCheckBoxState(m_parallaxBackFace, mat.parallaxBackFace);
    const bool parallaxActive =
        parallaxMode != static_cast<int>(Asset::Material::kParallaxModeOff);
    const bool heightMapMode =
        parallaxMode == static_cast<int>(Asset::Material::kParallaxModeHeightMap);
    const bool windowBoxMode =
        parallaxMode == static_cast<int>(Asset::Material::kParallaxModeWindowBox);
    if (m_textureSlots[Parallax].group) {
        m_textureSlots[Parallax].group->setEnabled(parallaxActive);
    }
    m_parallaxDepthScale->setEnabled(heightMapMode);
    m_parallaxRoomDepth->setEnabled(windowBoxMode);
    m_parallaxWindowAspect->setEnabled(windowBoxMode);
    m_parallaxWindowBrightness->setEnabled(windowBoxMode);
    m_parallaxScaleX->setEnabled(windowBoxMode);
    m_parallaxScaleY->setEnabled(windowBoxMode);
    m_parallaxOffsetX->setEnabled(windowBoxMode);
    m_parallaxOffsetY->setEnabled(windowBoxMode);
    m_parallaxBackFace->setEnabled(windowBoxMode);

    setColorButton(m_emissiveColorButton, getColorFromMaterial(mat.emissiveColor));
    SyncSliderControlValue(m_emissiveIntensity, mat.emissiveIntensity);

    SyncCheckBoxState(m_doubleSided, mat.doubleSided);
    SyncSliderControlValue(m_opacity, mat.diffuseColor[3]);
    SyncComboBoxIndex(m_alphaMode, AlphaModeIndex(mat.alphaMode));
    SyncSliderControlValue(m_alphaCutoff, mat.alphaCutoff);
    m_alphaCutoff->setEnabled(mat.alphaMode == "MASK");

    if (refreshTextureUi) {
        updateTextureOptions();
        for (int slot = 0; slot < TextureSlotCount; ++slot) {
            updateTextureSlotUi(static_cast<TextureSlot>(slot), mat);
        }
    }
    updateQa();
}

void MaterialEditorPanel::updateQa()
{
    const int idx = currentMaterialIndex();
    if (idx < 0) {
        m_qaLabel->clear();
        return;
    }
    const Asset::Material &mat = g_loadedMaterials[idx];

    const float rough = std::clamp(mat.roughness, 0.0f, 1.0f);
    const bool isMetal = mat.metalness > 1.0e-5f;
    const bool isGlass = mat.transmissionWeight > 1.0e-5f;
    const float aMin = std::min({mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2]});
    const float aMax = std::max({mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2]});

    QStringList warnings;
    if (!isMetal && !isGlass && (aMin < 0.02f || aMax > 0.90f)) {
        warnings << tr("Dielectric albedo is outside typical range (avoid near-black/white).");
    }
    if (rough < 0.001f) {
        warnings << (IsReflectionGlossinessWorkflow(mat)
                         ? tr("Glossiness near 1.0 can need more samples for stable sharp highlights.")
                         : tr("Near-zero roughness can need more samples for stable sharp highlights."));
    }
    if (mat.coatWeight > 1.0e-5f && mat.coatRoughness < 0.001f) {
        warnings << tr("Coat roughness very low; may sparkle.");
    }
    if (mat.transmissionWeight > 1.0e-5f && mat.thinWalled <= 0.5f &&
        mat.thickness <= 1.0e-5f && mat.thicknessTexture < 0) {
        warnings << tr("Solid transmissive material has no thickness; volume tint will be weak.");
    }
    if (mat.opacityTexture >= 0 && mat.alphaMode == "OPAQUE") {
        warnings << tr("Opacity texture is assigned while alpha mode is OPAQUE.");
    }

    if (warnings.isEmpty()) {
        m_qaLabel->setText(tr("No warnings."));
        return;
    }

    QString html;
    for (const QString &warning : warnings) {
        html += QStringLiteral("<div style=\"color:#c77f00;\">%1</div>")
                    .arg(warning.toHtmlEscaped());
    }
    m_qaLabel->setText(html);
}

void MaterialEditorPanel::updatePickUi()
{
    const bool picking = MaterialEditor::IsPickingEnabled();
    if (picking) {
        m_pickButton->setText(tr("Cancel Pick"));
        m_pickButton->setStyleSheet(QStringLiteral("background-color:#2f7d4a;color:white;"));
        m_pickStatusLabel->setVisible(true);
    } else {
        m_pickButton->setText(tr("Pick Material"));
        m_pickButton->setStyleSheet(QString());
        m_pickStatusLabel->setVisible(false);
    }
}

void MaterialEditorPanel::updateCounts()
{
    int visibleTextureCount = 0;
    for (const auto &texture : g_loadedTextures) {
        if (!texture.hiddenInEditor) {
            ++visibleTextureCount;
        }
    }
    m_countsLabel->setText(
        tr("Loaded: %1 materials, %2 textures")
            .arg(static_cast<int>(g_loadedMaterials.size()))
            .arg(visibleTextureCount));
}

void MaterialEditorPanel::updateTextureOptions()
{
    const int idx = currentMaterialIndex();
    if (idx < 0 || !m_textureOptionsModel) {
        return;
    }

    const uint64_t signature = textureOptionsSignature();
    if (signature != m_textureOptionsSignature) {
        m_textureOptionsSignature = signature;
        m_visibleTextureIndices.clear();
        m_textureOptionsModel->clear();

        m_textureOptionsModel->appendRow(new QStandardItem(tr("None")));
        for (int texIdx = 0; texIdx < static_cast<int>(g_loadedTextures.size()); ++texIdx) {
            const auto &tex = g_loadedTextures[texIdx];
            if (tex.hiddenInEditor) {
                continue;
            }
            m_visibleTextureIndices.push_back(texIdx);

            auto *item = new QStandardItem(TextureLabel(tex, texIdx));
            const QPixmap preview = createTexturePreview(tex, QSize(24, 24));
            if (!preview.isNull()) {
                item->setIcon(QIcon(preview));
            }
            m_textureOptionsModel->appendRow(item);
        }
    }

    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        auto &widgets = m_textureSlots[slot];
        if (!widgets.combo) {
            continue;
        }
        QSignalBlocker blocker(widgets.combo);
        if (widgets.combo->model() != m_textureOptionsModel) {
            widgets.combo->setModel(m_textureOptionsModel);
        }
        const int texIdx = textureIndexForSlot(g_loadedMaterials[idx], static_cast<TextureSlot>(slot));
        int comboIndex = visibleComboIndexForTexture(texIdx);
        if (comboIndex < 0 || comboIndex >= widgets.combo->count()) {
            comboIndex = 0;
        }
        SyncComboBoxIndex(widgets.combo, comboIndex);
    }
}

int MaterialEditorPanel::textureIndexForSlot(const Asset::Material &mat, TextureSlot slot) const
{
    switch (slot) {
    case Albedo:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::BaseColor);
    case Opacity:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Opacity);
    case PackedMetalRough:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::PackedSurface);
    case Metalness:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Metalness);
    case RoughnessGlossiness:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::RoughnessOrGlossiness);
    case Normal:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Normal);
    case CoatNormal:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::CoatNormal);
    case Occlusion:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Occlusion);
    case Emissive:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Emissive);
    case SpecularColor:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::SpecularColor);
    case Thickness:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Thickness);
    case Parallax:
        return MaterialSystem::GetTextureIndex(mat, MaterialSystem::TextureSlot::Parallax);
    default:
        return -1;
    }
}

void MaterialEditorPanel::setTextureIndexForSlot(Asset::Material &mat, TextureSlot slot, int index)
{
    switch (slot) {
    case Albedo:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::BaseColor, index);
        break;
    case Opacity:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Opacity, index);
        break;
    case PackedMetalRough:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::PackedSurface, index);
        break;
    case Metalness:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Metalness, index);
        break;
    case RoughnessGlossiness:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::RoughnessOrGlossiness, index);
        break;
    case Normal:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Normal, index);
        break;
    case CoatNormal:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::CoatNormal, index);
        break;
    case Occlusion:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Occlusion, index);
        break;
    case Emissive:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Emissive, index);
        break;
    case SpecularColor:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::SpecularColor, index);
        break;
    case Thickness:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Thickness, index);
        break;
    case Parallax:
        MaterialSystem::SetTextureIndex(mat, MaterialSystem::TextureSlot::Parallax, index);
        break;
    default: break;
    }
}

int MaterialEditorPanel::textureIndexFromVisibleCombo(int comboIndex) const
{
    if (comboIndex <= 0) {
        return -1;
    }
    const int visibleIndex = comboIndex - 1;
    if (visibleIndex < 0 || visibleIndex >= static_cast<int>(m_visibleTextureIndices.size())) {
        return -1;
    }
    return m_visibleTextureIndices[visibleIndex];
}

int MaterialEditorPanel::visibleComboIndexForTexture(int textureIndex) const
{
    if (textureIndex < 0) {
        return 0;
    }
    for (size_t i = 0; i < m_visibleTextureIndices.size(); ++i) {
        if (m_visibleTextureIndices[i] == textureIndex) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

uint64_t MaterialEditorPanel::textureOptionsSignature() const
{
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(static_cast<uint64_t>(g_loadedTextures.size()));
    for (const auto &tex : g_loadedTextures) {
        mix(tex.hiddenInEditor ? 1ull : 0ull);
        mix(static_cast<uint64_t>(tex.width));
        mix(static_cast<uint64_t>(tex.height));
        mix(static_cast<uint64_t>(tex.mipLevels));
        mix(static_cast<uint64_t>(tex.format));
        mix(static_cast<uint64_t>(tex.cpuMipLevels));
        mix(static_cast<uint64_t>(tex.cpuFormat));
        mix(static_cast<uint64_t>(tex.cpuData.size()));
        mix(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tex.cpuData.data())));
        mix(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tex.resource.Get())));
    }

    return hash;
}

void MaterialEditorPanel::updateWorkflowUi(const Asset::Material &mat)
{
    if (m_roughnessSurfaceLabel) {
        m_roughnessSurfaceLabel->setText(RoughnessLabelForMaterial(mat));
    }
    if (m_secondarySurfaceLabel) {
        m_secondarySurfaceLabel->setText(SecondaryLabelForMaterial(mat));
    }

    const bool reflectionWorkflow = IsReflectionGlossinessWorkflow(mat);
    if (m_textureSlots[Metalness].group) {
        m_textureSlots[Metalness].group->setVisible(!reflectionWorkflow);
    }
    if (m_specularWeightLabel) {
        m_specularWeightLabel->setVisible(!reflectionWorkflow);
    }
    if (m_specularWeight) {
        m_specularWeight->setVisible(!reflectionWorkflow);
    }
}

QPixmap MaterialEditorPanel::createTexturePreview(const Asset::Texture &tex, const QSize &size) const
{
    if (tex.width == 0 || tex.height == 0 || tex.cpuData.empty()) {
        return QPixmap();
    }

    const int previewWidth = std::max(1, size.width());
    const int previewHeight = std::max(1, size.height());
    QImage image(previewWidth, previewHeight, QImage::Format_RGBA8888);

    if ((tex.cpuFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
         tex.cpuFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
        tex.cpuMipLevels >= 1 &&
        tex.cpuData.size() >= static_cast<size_t>(tex.width) * tex.height * 4) {
        const uint8_t *pixels = tex.cpuData.data();
        for (int y = 0; y < previewHeight; ++y) {
            const int srcY = std::min<int>(
                static_cast<int>(tex.height) - 1,
                static_cast<int>((static_cast<double>(y) + 0.5) *
                                 static_cast<double>(tex.height) /
                                 static_cast<double>(previewHeight)));
            for (int x = 0; x < previewWidth; ++x) {
                const int srcX = std::min<int>(
                    static_cast<int>(tex.width) - 1,
                    static_cast<int>((static_cast<double>(x) + 0.5) *
                                     static_cast<double>(tex.width) /
                                     static_cast<double>(previewWidth)));
                const size_t base =
                    (static_cast<size_t>(srcY) * tex.width + static_cast<size_t>(srcX)) * 4;
                image.setPixelColor(
                    x, y,
                    QColor(pixels[base + 0],
                           pixels[base + 1],
                           pixels[base + 2],
                           pixels[base + 3]));
            }
        }
        return QPixmap::fromImage(image);
    }

    if (tex.cpuFormat == DXGI_FORMAT_R32G32B32A32_FLOAT &&
        tex.cpuMipLevels >= 1 &&
        tex.cpuData.size() >= static_cast<size_t>(tex.width) * tex.height * 16) {
        const float *pixels = reinterpret_cast<const float *>(tex.cpuData.data());
        for (int y = 0; y < previewHeight; ++y) {
            const int srcY = std::min<int>(
                static_cast<int>(tex.height) - 1,
                static_cast<int>((static_cast<double>(y) + 0.5) *
                                 static_cast<double>(tex.height) /
                                 static_cast<double>(previewHeight)));
            for (int x = 0; x < previewWidth; ++x) {
                const int srcX = std::min<int>(
                    static_cast<int>(tex.width) - 1,
                    static_cast<int>((static_cast<double>(x) + 0.5) *
                                     static_cast<double>(tex.width) /
                                     static_cast<double>(previewWidth)));
                const size_t base =
                    (static_cast<size_t>(srcY) * tex.width + static_cast<size_t>(srcX)) * 4;
                const float r = std::clamp(pixels[base + 0], 0.0f, 1.0f);
                const float g = std::clamp(pixels[base + 1], 0.0f, 1.0f);
                const float b = std::clamp(pixels[base + 2], 0.0f, 1.0f);
                image.setPixelColor(x, y, QColor::fromRgbF(r, g, b, 1.0f));
            }
        }
        return QPixmap::fromImage(image);
    }

    return QPixmap();
}

void MaterialEditorPanel::applyMaterialChange(const std::function<void(Asset::Material &)> &fn,
                                              bool markOpacityDirty,
                                              bool requestAsRebuild,
                                              bool refreshTextureUi)
{
    const int idx = currentMaterialIndex();
    if (idx < 0) {
        return;
    }
    Asset::Material &mat = g_loadedMaterials[idx];
    const Asset::Material before = mat;
    fn(mat);

    const bool structureChanged =
        MaterialAffectsRtStructure(before) != MaterialAffectsRtStructure(mat);

    if (markOpacityDirty || structureChanged || requestAsRebuild) {
        DxrRenderer::MarkMaterialDirty(idx);
    }
    DxrRenderer::ResetAccumulation();
    m_syncing = true;
    syncInspectorMaterialState(mat, refreshTextureUi);
    m_syncing = false;
}

void MaterialEditorPanel::pickMaterialColor(
    const QString &title,
    const std::function<QColor(const Asset::Material &)> &readColor,
    const std::function<void(Asset::Material &, const QColor &)> &writeColor)
{
    const int idx = currentMaterialIndex();
    if (idx < 0 || idx >= static_cast<int>(g_loadedMaterials.size())) {
        return;
    }

    const Asset::Material originalMaterial = g_loadedMaterials[idx];
    const QColor originalColor = readColor(originalMaterial);
    auto applyPreview = [this, idx, writeColor](const QColor &color) {
        if (!color.isValid() || idx >= static_cast<int>(g_loadedMaterials.size())) {
            return;
        }
        writeColor(g_loadedMaterials[idx], color);
        DxrRenderer::MarkMaterialDirty(idx);
        m_syncing = true;
        syncInspectorMaterialState(g_loadedMaterials[idx], false);
        m_syncing = false;
        DxrRenderer::ResetAccumulation();
    };

    auto restoreOriginal = [this, idx, originalMaterial]() {
        if (idx < static_cast<int>(g_loadedMaterials.size())) {
            g_loadedMaterials[idx] = originalMaterial;
            DxrRenderer::MarkMaterialDirty(idx);
            m_syncing = true;
            syncInspectorMaterialState(g_loadedMaterials[idx], false);
            m_syncing = false;
            DxrRenderer::ResetAccumulation();
        }
    };

    ArchColorDialog::showColor(originalColor, this, title, applyPreview,
                               applyPreview, restoreOriginal);
}

void MaterialEditorPanel::setColorButton(QPushButton *button, const QColor &color)
{
    if (!button) {
        return;
    }
    const QString bg = color.name(QColor::HexRgb);
    button->setText(QString());
    button->setFixedSize(58, 32);
    button->setStyleSheet(
        QStringLiteral(
            "background-color:%1;border:2px solid #666;border-radius:7px;")
            .arg(bg));
    button->setToolTip(tr("RGB %1 %2 %3")
                           .arg(color.red())
                           .arg(color.green())
                           .arg(color.blue()));
}

QColor MaterialEditorPanel::getColorFromMaterial(const float color[3]) const
{
    const float r = std::clamp(color[0], 0.0f, 1.0f);
    const float g = std::clamp(color[1], 0.0f, 1.0f);
    const float b = std::clamp(color[2], 0.0f, 1.0f);
    return QColor::fromRgbF(r, g, b);
}

int MaterialEditorPanel::currentMaterialIndex() const
{
    if (m_selectedMaterial < 0 || m_selectedMaterial >= static_cast<int>(g_loadedMaterials.size())) {
        return -1;
    }
    return m_selectedMaterial;
}

void MaterialEditorPanel::setSelectedMaterial(int materialIndex, bool ensureVisible)
{
    if (materialIndex < 0) {
        return;
    }
    int row = -1;
    for (size_t i = 0; i < m_materialIndices.size(); ++i) {
        if (m_materialIndices[i] == materialIndex) {
            row = static_cast<int>(i);
            break;
        }
    }
    if (row >= 0 && row < m_materialList->count()) {
        m_materialList->setCurrentRow(row);
        if (ensureVisible) {
            auto *item = m_materialList->item(row);
            if (item) {
                m_materialList->scrollToItem(item);
            }
        }
    }
}

void MaterialEditorPanel::updateTextureSlotUi(TextureSlot slot, const Asset::Material &mat)
{
    auto &widgets = m_textureSlots[slot];
    if (!widgets.amount) {
        return;
    }
    const int texIdx = textureIndexForSlot(mat, slot);
    SyncSliderControlValue(
        widgets.amount,
        MaterialSystem::GetTextureAmount(
            mat, static_cast<MaterialSystem::TextureSlot>(slot)));
    widgets.amount->setEnabled(texIdx >= 0);

    if (widgets.thumbLabel) {
        if (texIdx >= 0 && texIdx < static_cast<int>(g_loadedTextures.size())) {
            const QPixmap preview = createTexturePreview(g_loadedTextures[texIdx], QSize(48, 48));
            if (!preview.isNull()) {
                widgets.thumbLabel->setPixmap(preview);
                widgets.thumbLabel->setText(QString());
            } else {
                widgets.thumbLabel->setPixmap(QPixmap());
                widgets.thumbLabel->setText(tr("None"));
            }
        } else {
            widgets.thumbLabel->setPixmap(QPixmap());
            widgets.thumbLabel->setText(tr("None"));
        }
    }
}
