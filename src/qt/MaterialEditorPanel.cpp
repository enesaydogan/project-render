#include "MaterialEditorPanel.h"

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
#include <QColorDialog>
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
#include <QPixmap>
#include <QPushButton>
#include <QApplication>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QMetaObject>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>

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

    m_tabs = new QTabWidget(m_inspectorGroup);
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

        auto *loadButton = new QPushButton(group);
        auto *clearButton = new QPushButton(group);
        loadButton->setIcon(group->style()->standardIcon(QStyle::SP_DialogOpenButton));
        clearButton->setIcon(group->style()->standardIcon(QStyle::SP_DialogResetButton));
        loadButton->setToolTip(tr("Load texture"));
        clearButton->setToolTip(tr("Clear texture"));
        loadButton->setFixedSize(22, 22);
        clearButton->setFixedSize(22, 22);

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

    auto *surfaceTab = new QWidget(m_tabs);
    auto *surfaceForm = new QFormLayout(surfaceTab);
    surfaceForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    surfaceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    surfaceForm->setHorizontalSpacing(6);
    surfaceForm->setVerticalSpacing(4);
    
    m_baseColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Base Color"), m_baseColorButton);
    surfaceForm->addRow(m_textureSlots[Albedo].group);

    m_materialClassCombo = new QComboBox(surfaceTab);
    m_materialClassCombo->addItems({
        tr("Generic"),
        tr("Metal"),
        tr("Glass"),
        tr("Fabric"),
        tr("Leaf"),
        tr("Emissive"),
    });
    surfaceForm->addRow(tr("Material Class"), m_materialClassCombo);

    m_workflowCombo = new QComboBox(surfaceTab);
    m_workflowCombo->addItems({
        tr("Metalness / Roughness"),
        tr("Reflection / Glossiness"),
    });
    surfaceForm->addRow(tr("Workflow"), m_workflowCombo);

    m_roughnessSurfaceLabel = new QLabel(tr("Roughness"), surfaceTab);
    m_roughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(m_roughnessSurfaceLabel, m_roughness);
    surfaceForm->addRow(m_textureSlots[RoughnessGlossiness].group);

    m_secondarySurfaceLabel = new QLabel(tr("Metalness"), surfaceTab);
    m_metalness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(m_secondarySurfaceLabel, m_metalness);
    surfaceForm->addRow(m_textureSlots[Metalness].group);
    surfaceForm->addRow(m_textureSlots[PackedMetalRough].group);

    surfaceForm->addRow(m_textureSlots[Normal].group);
    surfaceForm->addRow(m_textureSlots[Occlusion].group);

    m_specularWeightLabel = new QLabel(tr("Specular Weight"), surfaceTab);
    m_specularWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(m_specularWeightLabel, m_specularWeight);
    m_specularColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Specular Color"), m_specularColorButton);
    surfaceForm->addRow(m_textureSlots[SpecularColor].group);

    m_ior = CreateSliderControl(MaterialSystem::kMinMaterialIor,
                                MaterialSystem::kMaxMaterialIor, 0.001, 3);
    surfaceForm->addRow(tr("IOR"), m_ior);

    auto *transmissionDivider = new QFrame(surfaceTab);
    transmissionDivider->setFrameShape(QFrame::HLine);
    transmissionDivider->setFrameShadow(QFrame::Sunken);
    surfaceForm->addRow(transmissionDivider);

    m_transmission = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Transmission"), m_transmission);
    m_transmissionColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Transmission Color"), m_transmissionColorButton);
    
    m_thickness = CreateSliderControl(0.0, 1.0, 0.001, 4);
    surfaceForm->addRow(tr("Thickness"), m_thickness);
    surfaceForm->addRow(m_textureSlots[Thickness].group);

    m_attenuationDistance = CreateSliderControl(0.0, 100.0, 0.01, 3);
    surfaceForm->addRow(tr("Attenuation Dist"), m_attenuationDistance);
    
    m_coatWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Coat"), m_coatWeight);
    m_coatRoughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Coat Roughness"), m_coatRoughness);
    m_coatIor = CreateSliderControl(MaterialSystem::kMinMaterialIor,
                                    MaterialSystem::kMaxMaterialIor, 0.01, 3);
    surfaceForm->addRow(tr("Coat IOR"), m_coatIor);
    surfaceForm->addRow(m_textureSlots[CoatNormal].group);

    m_translucency = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Translucency"), m_translucency);
    
    m_anisotropy = CreateSliderControl(-1.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Anisotropy"), m_anisotropy);
    m_anisotropyRotation = CreateSliderControl(0.0, 360.0, 1.0, 1);
    surfaceForm->addRow(tr("Aniso Rotation"), m_anisotropyRotation);
    
    m_sheenWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Sheen"), m_sheenWeight);
    m_sheenColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Sheen Color"), m_sheenColorButton);
    
    m_thinWalled = new QCheckBox(tr("Thin Walled"), surfaceTab);
    surfaceForm->addRow(m_thinWalled);

    m_tabs->addTab(surfaceTab, tr("Surface"));

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
    m_tabs->addTab(grassTab, tr("Grass"));

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

    m_tabs->addTab(mappingTab, tr("Mapping"));

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
    m_tabs->addTab(parallaxTab, tr("Parallax"));

    auto *emissionTab = new QWidget(m_tabs);
    auto *emissionForm = new QFormLayout(emissionTab);
    emissionForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    emissionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    emissionForm->setHorizontalSpacing(6);
    emissionForm->setVerticalSpacing(4);
    m_emissiveColorButton = new QPushButton(tr("Pick"), emissionTab);
    emissionForm->addRow(tr("Emissive Color"), m_emissiveColorButton);
    m_emissiveIntensity = CreateSliderControl(0.0, 1000000.0, 1.0, 1);
    emissionForm->addRow(tr("Emissive Intensity"), m_emissiveIntensity);
    emissionForm->addRow(m_textureSlots[Emissive].group);
    m_tabs->addTab(emissionTab, tr("Emission"));

    auto *flagsTab = new QWidget(m_tabs);
    auto *flagsForm = new QFormLayout(flagsTab);
    flagsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    flagsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    flagsForm->setHorizontalSpacing(6);
    flagsForm->setVerticalSpacing(4);
    m_doubleSided = new QCheckBox(tr("Double Sided"), flagsTab);
    flagsForm->addRow(m_doubleSided);
    m_alphaMode = new QComboBox(flagsTab);
    m_alphaMode->addItems({tr("OPAQUE"), tr("MASK"), tr("BLEND")});
    flagsForm->addRow(tr("Alpha Mode"), m_alphaMode);
    m_alphaCutoff = CreateSliderControl(0.0, 1.0, 0.01, 3);
    flagsForm->addRow(tr("Alpha Cutoff"), m_alphaCutoff);
    flagsForm->addRow(m_textureSlots[Opacity].group);
    m_tabs->addTab(flagsTab, tr("Flags"));

    auto *qaTab = new QWidget(m_tabs);
    auto *qaLayout = new QVBoxLayout(qaTab);
    m_qaLabel = new QLabel(qaTab);
    m_qaLabel->setWordWrap(true);
    m_qaLabel->setTextFormat(Qt::RichText);
    qaLayout->addWidget(m_qaLabel);
    qaLayout->addStretch(1);
    m_tabs->addTab(qaTab, tr("QA"));

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
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.diffuseColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Base Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
            m.diffuseColor[0] = static_cast<float>(picked.redF());
            m.diffuseColor[1] = static_cast<float>(picked.greenF());
            m.diffuseColor[2] = static_cast<float>(picked.blueF());
        });
    });
    connect(m_specularColorButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.specularColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Specular Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
            m.specularColor[0] = static_cast<float>(picked.redF());
            m.specularColor[1] = static_cast<float>(picked.greenF());
            m.specularColor[2] = static_cast<float>(picked.blueF());
        });
    });

    connect(m_transmissionColorButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.transmissionColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Transmission Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
            m.transmissionColor[0] = static_cast<float>(picked.redF());
            m.transmissionColor[1] = static_cast<float>(picked.greenF());
            m.transmissionColor[2] = static_cast<float>(picked.blueF());
        });
    });
    connect(m_sheenColorButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.sheenColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Sheen Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
            m.sheenColor[0] = static_cast<float>(picked.redF());
            m.sheenColor[1] = static_cast<float>(picked.greenF());
            m.sheenColor[2] = static_cast<float>(picked.blueF());
        });
    });

    connect(m_grassColorButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.grassColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Grass Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
            m.grassColor[0] = static_cast<float>(picked.redF());
            m.grassColor[1] = static_cast<float>(picked.greenF());
            m.grassColor[2] = static_cast<float>(picked.blueF());
            m.diffuseColor[0] = m.grassColor[0];
            m.diffuseColor[1] = m.grassColor[1];
            m.diffuseColor[2] = m.grassColor[2];
        });
    });

    connect(m_emissiveColorButton, &QPushButton::clicked, this, [this]() {
        const int idx = currentMaterialIndex();
        if (idx < 0) {
            return;
        }
        Asset::Material &mat = g_loadedMaterials[idx];
        QColor current = getColorFromMaterial(mat.emissiveColor);
        QColor picked = QColorDialog::getColor(current, this, tr("Pick Emissive Color"));
        if (!picked.isValid()) {
            return;
        }
        applyMaterialChange([picked](Asset::Material &m) {
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
            m.coatRoughness = static_cast<float>(value);
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

    connect(m_emissiveIntensity->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.emissiveIntensity = static_cast<float>(value);
        });
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
                const int newTex = Scene::AddTextureFromFile(WStringToUtf8(chosen), isHdr);
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

    if (!showAll && selectedNodeIndex != m_lastSelectedNodeIndex) {
        m_lastSelectedNodeIndex = selectedNodeIndex;
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
    updateWorkflowUi(mat);

    SyncSliderControlValue(m_roughness,
                           IsReflectionGlossinessWorkflow(mat)
                               ? (1.0f - mat.roughness)
                               : mat.roughness);
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
    SyncSliderControlValue(m_coatRoughness, mat.coatRoughness);
    SyncSliderControlValue(m_coatIor, mat.coatIor);
    SyncSliderControlValue(m_translucency, mat.translucency);
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

    setColorButton(m_emissiveColorButton, getColorFromMaterial(mat.emissiveColor));
    SyncSliderControlValue(m_emissiveIntensity, mat.emissiveIntensity);

    SyncCheckBoxState(m_doubleSided, mat.doubleSided);
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

    if ((tex.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         tex.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
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

    if (tex.format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
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

void MaterialEditorPanel::setColorButton(QPushButton *button, const QColor &color)
{
    if (!button) {
        return;
    }
    const QString bg = color.name(QColor::HexRgb);
    const QString fg = (color.lightness() < 128) ? QStringLiteral("#ffffff")
                                                 : QStringLiteral("#000000");
    button->setText(tr("Pick"));
    button->setStyleSheet(QStringLiteral("background-color:%1;color:%2;").arg(bg, fg));
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
