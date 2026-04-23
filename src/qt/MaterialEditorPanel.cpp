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
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QMetaObject>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
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
    infoForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
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

    auto createTextureSlot = [this](TextureSlot slot, const QString &title) {
        auto *group = new QWidget(this);
        auto *mainLayout = new QVBoxLayout(group);
        mainLayout->setContentsMargins(0, 4, 0, 4);
        mainLayout->setSpacing(4);

        auto *loadButton = new QPushButton(group);
        auto *clearButton = new QPushButton(group);
        loadButton->setIcon(group->style()->standardIcon(QStyle::SP_DialogOpenButton));
        clearButton->setIcon(group->style()->standardIcon(QStyle::SP_DialogResetButton));
        loadButton->setToolTip(tr("Load texture"));
        clearButton->setToolTip(tr("Clear texture"));
        loadButton->setFixedSize(22, 22);
        clearButton->setFixedSize(22, 22);

        if (!title.isEmpty()) {
            auto *titleRow = new QHBoxLayout();
            titleRow->setContentsMargins(0, 0, 0, 0);
            titleRow->setSpacing(4);

            auto *titleLabel = new QLabel(title, group);
            titleLabel->setWordWrap(true);
            titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            titleLabel->setStyleSheet(QStringLiteral("color: #999; font-weight: bold;"));
            titleRow->addWidget(titleLabel, 1);
            titleRow->addWidget(loadButton);
            titleRow->addWidget(clearButton);
            mainLayout->addLayout(titleRow);
        } else {
            auto *buttonRow = new QHBoxLayout();
            buttonRow->setContentsMargins(0, 0, 0, 0);
            buttonRow->addStretch(1);
            buttonRow->addWidget(loadButton);
            buttonRow->addWidget(clearButton);
            mainLayout->addLayout(buttonRow);
        }

        auto *controlsLayout = new QHBoxLayout();
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        controlsLayout->setSpacing(3);

        auto *combo = new QComboBox(group);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(1);
        combo->setFixedWidth(76);
        combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto *amount = CreateSliderControl(0.0, 1.0, 0.01, 3);
        amount->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        amount->setMinimumWidth(0);
        amount->spinBox()->setMinimumWidth(52);
        amount->spinBox()->setMaximumWidth(60);
        if (auto *slider = amount->findChild<QSlider *>()) {
            slider->setMinimumWidth(40);
        }

        controlsLayout->addWidget(combo);
        controlsLayout->addWidget(amount, 1);

        mainLayout->addLayout(controlsLayout);

        m_textureSlots[slot].group = group;
        m_textureSlots[slot].combo = combo;
        m_textureSlots[slot].amount = amount;
        m_textureSlots[slot].loadButton = loadButton;
        m_textureSlots[slot].clearButton = clearButton;
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

    auto *surfaceTab = new QWidget(m_tabs);
    auto *surfaceForm = new QFormLayout(surfaceTab);
    surfaceForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    surfaceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    surfaceForm->setHorizontalSpacing(6);
    surfaceForm->setVerticalSpacing(4);
    
    m_baseColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Base Color"), m_baseColorButton);
    surfaceForm->addRow(m_textureSlots[Albedo].group);

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

    m_specularWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Specular Weight"), m_specularWeight);
    m_specularColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Specular Color"), m_specularColorButton);
    surfaceForm->addRow(m_textureSlots[SpecularColor].group);

    m_ior = CreateSliderControl(1.0, 3.0, 0.001, 3);
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
    m_coatIor = CreateSliderControl(1.0, 3.0, 0.01, 3);
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
    grassForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
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
    mappingForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
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
    m_triPlanarRotationDegrees = CreateSliderControl(0.0, 360.0, 1.0, 1);
    mappingForm->addRow(tr("Rotation (deg)"), m_triPlanarRotationDegrees);
    m_triPlanarVariationMode = new QComboBox(mappingTab);
    m_triPlanarVariationMode->addItems({tr("Off"), tr("Per Mesh"), tr("Per Surface")});
    mappingForm->addRow(tr("Variation"), m_triPlanarVariationMode);
    m_triPlanarVariationOffset = CreateSliderControl(0.0, 1.0, 0.01, 2);
    mappingForm->addRow(tr("Variation Offset"), m_triPlanarVariationOffset);

    m_tabs->addTab(mappingTab, tr("Mapping"));

    auto *emissionTab = new QWidget(m_tabs);
    auto *emissionForm = new QFormLayout(emissionTab);
    emissionForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
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
    flagsForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
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
        }, true);
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
        }, true);
    });
    connect(m_resetNoTexButton, &QPushButton::clicked, this, [this]() {
        applyMaterialChange([](Asset::Material &mat) {
            Asset::Material def;
            char nameBuf[64];
            strncpy_s(nameBuf, mat.name, _TRUNCATE);
            mat = def;
            strncpy_s(mat.name, nameBuf, _TRUNCATE);
        }, true);
    });

    connect(m_applyPresetButton, &QPushButton::clicked, this, [this]() {
        const int presetIdx = m_presetCombo->currentIndex();
        applyMaterialChange([presetIdx](Asset::Material &mat) {
            ApplyPreset(mat, presetIdx);
        }, true);
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
    connect(m_triPlanarRotationDegrees->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.triPlanarRotationDegrees = static_cast<float>(value);
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
            });
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
            });
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
                    });
                    updateTextureOptions();
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

    m_materialNameEdit->setText(QString::fromUtf8(mat.name));
    m_materialIdLabel->setText(tr("#%1").arg(idx));
    m_pasteButton->setEnabled(static_cast<bool>(m_clipboard));

    setColorButton(m_baseColorButton, getColorFromMaterial(mat.diffuseColor));
    m_workflowCombo->setCurrentIndex(static_cast<int>(std::clamp(mat.workflow, 0u, 1u)));
    updateWorkflowUi(mat);
    m_roughness->setValue(IsReflectionGlossinessWorkflow(mat)
                              ? (1.0f - mat.roughness)
                              : mat.roughness);
    m_metalness->setValue(IsReflectionGlossinessWorkflow(mat)
                              ? mat.specularWeight
                              : mat.metalness);
    m_specularWeight->setValue(mat.specularWeight);
    setColorButton(m_specularColorButton, getColorFromMaterial(mat.specularColor));
    m_ior->setValue(mat.ior);
    m_transmission->setValue(mat.transmissionWeight);
    setColorButton(m_transmissionColorButton, getColorFromMaterial(mat.transmissionColor));
    m_thickness->setValue(mat.thickness);
    m_attenuationDistance->setValue(mat.attenuationDistance);
    m_coatWeight->setValue(mat.coatWeight);
    m_coatRoughness->setValue(mat.coatRoughness);
    m_coatIor->setValue(mat.coatIor);
    m_translucency->setValue(mat.translucency);
    m_anisotropy->setValue(mat.anisotropy);
    m_anisotropyRotation->setValue(mat.anisotropyRotation);
    m_sheenWeight->setValue(mat.sheenWeight);
    setColorButton(m_sheenColorButton, getColorFromMaterial(mat.sheenColor));
    m_thinWalled->setChecked(mat.thinWalled > 0.5f);

    m_grassEnabled->setChecked(mat.isGrass);
    setColorButton(m_grassColorButton, getColorFromMaterial(mat.grassColor));
    m_grassBladeSize->setValue(mat.grassBladeSize);
    m_grassBladeCount->setValue(mat.grassBladeCount);
    m_grassBladeVariation->setValue(mat.grassBladeVariation);
    m_grassHint->setVisible(!mat.isGrass);
    m_grassColorButton->setEnabled(mat.isGrass);
    m_grassBladeSize->setEnabled(mat.isGrass);
    m_grassBladeCount->setEnabled(mat.isGrass);
    m_grassBladeVariation->setEnabled(mat.isGrass);

    m_uvScaleX->setValue(mat.uvScale[0]);
    m_uvScaleY->setValue(mat.uvScale[1]);
    m_uvOffsetX->setValue(mat.uvOffset[0]);
    m_uvOffsetY->setValue(mat.uvOffset[1]);
    m_triPlanarEnabled->setChecked(mat.triPlanarEnabled > 0.5f);
    m_triPlanarScale->setValue(mat.triPlanarScale);
    m_triPlanarSharpness->setValue(mat.triPlanarSharpness);
    m_triPlanarNormalStrength->setValue(mat.triPlanarNormalStrength);
    m_triPlanarRotationDegrees->setValue(mat.triPlanarRotationDegrees);
    m_triPlanarVariationMode->setCurrentIndex(
        static_cast<int>(std::clamp(mat.triPlanarVariationMode, 0u, 2u)));
    m_triPlanarVariationOffset->setValue(mat.triPlanarVariationOffset);
    const bool triPlanarActive = mat.triPlanarEnabled > 0.5f;
    m_triPlanarRotationDegrees->setEnabled(triPlanarActive);
    m_triPlanarVariationMode->setEnabled(triPlanarActive);
    m_triPlanarVariationOffset->setEnabled(triPlanarActive);

    setColorButton(m_emissiveColorButton, getColorFromMaterial(mat.emissiveColor));
    m_emissiveIntensity->setValue(mat.emissiveIntensity);

    m_doubleSided->setChecked(mat.doubleSided);
    m_alphaMode->setCurrentIndex(AlphaModeIndex(mat.alphaMode));
    m_alphaCutoff->setValue(mat.alphaCutoff);
    m_alphaCutoff->setEnabled(mat.alphaMode == "MASK");

    updateTextureOptions();
    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        updateTextureSlotUi(static_cast<TextureSlot>(slot), mat);
    }
    updateQa();

    m_syncing = false;
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
    if (idx < 0) {
        return;
    }

    m_visibleTextureIndices.clear();
    for (int i = 0; i < static_cast<int>(g_loadedTextures.size()); ++i) {
        if (g_loadedTextures[i].hiddenInEditor) {
            continue;
        }
        m_visibleTextureIndices.push_back(i);
    }

    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        auto &widgets = m_textureSlots[slot];
        if (!widgets.combo) {
            continue;
        }
        QSignalBlocker blocker(widgets.combo);
        widgets.combo->clear();
        widgets.combo->addItem(tr("None"));
        for (int i = 0; i < static_cast<int>(m_visibleTextureIndices.size()); ++i) {
            int texIdx = m_visibleTextureIndices[i];
            const auto &tex = g_loadedTextures[texIdx];
            QPixmap preview = createTexturePreview(tex, QSize(24, 24));
            if (preview.isNull()) {
                widgets.combo->addItem(TextureLabel(tex, texIdx));
            } else {
                widgets.combo->addItem(QIcon(preview), TextureLabel(tex, texIdx));
            }
        }
        const int texIdx = textureIndexForSlot(g_loadedMaterials[idx], static_cast<TextureSlot>(slot));
        int comboIndex = visibleComboIndexForTexture(texIdx);
        if (comboIndex < 0 || comboIndex >= widgets.combo->count()) {
            comboIndex = 0;
        }
        widgets.combo->setCurrentIndex(comboIndex);
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
}

QPixmap MaterialEditorPanel::createTexturePreview(const Asset::Texture &tex, const QSize &size) const
{
    if (tex.width == 0 || tex.height == 0 || tex.cpuData.empty()) {
        return QPixmap();
    }

    if ((tex.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         tex.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
        tex.cpuData.size() >= static_cast<size_t>(tex.width) * tex.height * 4) {
        QImage image(tex.cpuData.data(),
                     static_cast<int>(tex.width),
                     static_cast<int>(tex.height),
                     QImage::Format_RGBA8888);
        return QPixmap::fromImage(image.copy()).scaled(
            size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (tex.format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
        tex.cpuData.size() >= static_cast<size_t>(tex.width) * tex.height * 16) {
        QImage image(static_cast<int>(tex.width),
                     static_cast<int>(tex.height),
                     QImage::Format_RGBA8888);
        const float *pixels = reinterpret_cast<const float *>(tex.cpuData.data());
        for (int y = 0; y < static_cast<int>(tex.height); ++y) {
            for (int x = 0; x < static_cast<int>(tex.width); ++x) {
                const size_t base = (static_cast<size_t>(y) * tex.width + x) * 4;
                const float r = std::clamp(pixels[base + 0], 0.0f, 1.0f);
                const float g = std::clamp(pixels[base + 1], 0.0f, 1.0f);
                const float b = std::clamp(pixels[base + 2], 0.0f, 1.0f);
                image.setPixelColor(x, y, QColor::fromRgbF(r, g, b, 1.0f));
            }
        }
        return QPixmap::fromImage(image).scaled(
            size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return QPixmap();
}

void MaterialEditorPanel::applyMaterialChange(const std::function<void(Asset::Material &)> &fn,
                                              bool markOpacityDirty,
                                              bool requestAsRebuild)
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
    syncInspector();
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
    widgets.amount->setValue(MaterialSystem::GetTextureAmount(
        mat, static_cast<MaterialSystem::TextureSlot>(slot)));
    widgets.amount->setEnabled(texIdx >= 0);
}
