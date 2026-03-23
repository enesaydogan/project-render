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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
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

bool MaterialAffectsRtStructure(const Asset::Material &material)
{
    return material.alphaMode != "OPAQUE" ||
           material.diffuseColor[3] < 0.999f ||
           material.transmissionWeight > 0.01f ||
           material.thinWalled > 0.5f;
}

void ApplyPreset(Asset::Material &m, int presetIdx)
{
    auto SetRoughness = [&](float r) {
        r = (r < 0.0f) ? 0.0f : (r > 1.0f ? 1.0f : r);
        m.roughness = r;
    };

    m.coatWeight = 0.0f;
    m.coatRoughness = 0.1f;
    m.thinWalled = 0.0f;
    m.translucency = 0.0f;
    m.uvScale[0] = 1.0f;
    m.uvScale[1] = 1.0f;
    m.uvOffset[0] = 0.0f;
    m.uvOffset[1] = 0.0f;

    m.triPlanarEnabled = 0.0f;
    m.triPlanarScale = 1.0f;
    m.triPlanarSharpness = 4.0f;
    m.triPlanarNormalStrength = 1.0f;

    m.transmissionColor[0] = 1.0f;
    m.transmissionColor[1] = 1.0f;
    m.transmissionColor[2] = 1.0f;
    m.transmissionWeight = 0.0f;

    switch (presetIdx) {
    default:
    case 0: // Dielectric Generic
        m.metalness = 0.0f;
        m.ior = 1.5f;
        SetRoughness(0.5f);
        break;
    case 1: // Paint / Plaster
        m.metalness = 0.0f;
        m.ior = 1.45f;
        SetRoughness(0.75f);
        break;
    case 2: // Concrete
        m.metalness = 0.0f;
        m.ior = 1.5f;
        SetRoughness(0.85f);
        break;
    case 3: // Wood (Raw)
        m.metalness = 0.0f;
        m.ior = 1.5f;
        SetRoughness(0.6f);
        break;
    case 4: // Wood (Varnished)
        m.metalness = 0.0f;
        m.ior = 1.5f;
        SetRoughness(0.35f);
        m.coatWeight = 0.8f;
        m.coatRoughness = 0.08f;
        break;
    case 5: // Tile (Ceramic)
        m.metalness = 0.0f;
        m.ior = 1.52f;
        SetRoughness(0.25f);
        m.coatWeight = 0.6f;
        m.coatRoughness = 0.12f;
        break;
    case 6: // Metal (Brushed)
        m.metalness = 1.0f;
        m.ior = 1.0f;
        SetRoughness(0.35f);
        break;
    case 7: // Metal (Polished)
        m.metalness = 1.0f;
        m.ior = 1.0f;
        SetRoughness(0.08f);
        break;
    case 8: // Plastic
        m.metalness = 0.0f;
        m.ior = 1.45f;
        SetRoughness(0.35f);
        m.coatWeight = 0.25f;
        m.coatRoughness = 0.15f;
        break;
    case 9: // Glass (Clear Window, Thin)
        m.metalness = 0.0f;
        m.ior = 1.52f;
        SetRoughness(0.02f);
        m.transmissionColor[0] = 1.0f;
        m.transmissionColor[1] = 1.0f;
        m.transmissionColor[2] = 1.0f;
        m.transmissionWeight = 1.0f;
        m.thinWalled = 1.0f;
        break;
    case 10: // Glass (Frosted, Thin)
        m.metalness = 0.0f;
        m.ior = 1.52f;
        SetRoughness(0.35f);
        m.transmissionColor[0] = 1.0f;
        m.transmissionColor[1] = 1.0f;
        m.transmissionColor[2] = 1.0f;
        m.transmissionWeight = 1.0f;
        m.thinWalled = 1.0f;
        break;
    case 11: // Glass (Tinted, Thin)
        m.metalness = 0.0f;
        m.ior = 1.52f;
        SetRoughness(0.05f);
        m.transmissionColor[0] = 0.85f;
        m.transmissionColor[1] = 0.95f;
        m.transmissionColor[2] = 1.0f;
        m.transmissionWeight = 1.0f;
        m.thinWalled = 1.0f;
        break;
    case 12: // Fabric (Approx)
        m.metalness = 0.0f;
        m.ior = 1.4f;
        SetRoughness(0.8f);
        m.translucency = 0.15f;
        break;
    case 13: // Vegetation Leaf (Approx)
        m.metalness = 0.0f;
        m.ior = 1.4f;
        SetRoughness(0.65f);
        m.translucency = 0.6f;
        m.thinWalled = 1.0f;
        break;
    }
}

} // namespace
MaterialEditorPanel::MaterialEditorPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshMaterials();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        refreshMaterials();
    });
    m_refreshTimer->start(250);
}

MaterialEditorPanel::~MaterialEditorPanel() = default;

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

    auto *surfaceTab = new QWidget(m_tabs);
    auto *surfaceForm = new QFormLayout(surfaceTab);
    m_baseColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Base Color"), m_baseColorButton);
    m_roughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Roughness"), m_roughness);
    m_metalness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Metalness"), m_metalness);
    m_specularWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Specular Weight"), m_specularWeight);
    m_ior = CreateSliderControl(0.01, 10.0, 0.01, 3);
    m_ior->setLogarithmic(true);
    surfaceForm->addRow(tr("IOR"), m_ior);

    auto *transmissionDivider = new QFrame(surfaceTab);
    transmissionDivider->setFrameShape(QFrame::HLine);
    transmissionDivider->setFrameShadow(QFrame::Sunken);
    surfaceForm->addRow(transmissionDivider);

    m_transmission = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Transmission"), m_transmission);
    m_transmissionColorButton = new QPushButton(tr("Pick"), surfaceTab);
    surfaceForm->addRow(tr("Transmission Color"), m_transmissionColorButton);
    m_coatWeight = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Coat"), m_coatWeight);
    m_coatRoughness = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Coat Roughness"), m_coatRoughness);
    m_translucency = CreateSliderControl(0.0, 1.0, 0.01, 3);
    surfaceForm->addRow(tr("Translucency"), m_translucency);
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
    m_grassColorButton = new QPushButton(tr("Pick"), grassTab);
    grassForm->addRow(tr("Grass Color"), m_grassColorButton);
    m_grassBladeSize = CreateSliderControl(0.05, 5.0, 0.05, 2);
    grassForm->addRow(tr("Blade Size"), m_grassBladeSize);
    m_grassBladeCount = CreateSliderControl(0.0, 1024.0, 0.5, 1);
    grassForm->addRow(tr("Blade Count / m2"), m_grassBladeCount);
    m_grassBladeVariation = CreateSliderControl(0.0, 1.0, 0.01, 2);
    grassForm->addRow(tr("Blade Variation"), m_grassBladeVariation);
    grassLayout->addLayout(grassForm);
    grassLayout->addStretch(1);
    m_tabs->addTab(grassTab, tr("Grass"));

    auto *texturesTab = new QWidget(m_tabs);
    auto *texturesLayout = new QVBoxLayout(texturesTab);

    auto createTextureSlot = [this, texturesLayout](TextureSlot slot, const QString &title) {
        auto *group = new QGroupBox(title, this);
        auto *groupLayout = new QVBoxLayout(group);
        auto *combo = new QComboBox(group);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
        groupLayout->addWidget(combo);

        auto *buttonRow = new QHBoxLayout();
        auto *clearButton = new QPushButton(tr("Clear"), group);
        auto *loadButton = new QPushButton(tr("Load..."), group);
        auto *editButton = new QPushButton(tr("Edit Index"), group);
        buttonRow->addWidget(clearButton);
        buttonRow->addWidget(loadButton);
        buttonRow->addWidget(editButton);
        buttonRow->addStretch(1);
        groupLayout->addLayout(buttonRow);

        auto *infoLabel = new QLabel(tr("No texture bound"), group);
        infoLabel->setWordWrap(true);
        infoLabel->setStyleSheet(QStringLiteral("color: #777;"));
        groupLayout->addWidget(infoLabel);

        texturesLayout->addWidget(group);

        m_textureSlots[slot].combo = combo;
        m_textureSlots[slot].clearButton = clearButton;
        m_textureSlots[slot].loadButton = loadButton;
        m_textureSlots[slot].editButton = editButton;
        m_textureSlots[slot].infoLabel = infoLabel;
    };

    createTextureSlot(Albedo, tr("Albedo"));
    createTextureSlot(MetalRough, tr("Metal / Roughness"));
    createTextureSlot(Normal, tr("Normal"));
    createTextureSlot(Occlusion, tr("Occlusion"));
    createTextureSlot(Emissive, tr("Emissive"));
    texturesLayout->addStretch(1);

    m_tabs->addTab(texturesTab, tr("Textures"));
    auto *mappingTab = new QWidget(m_tabs);
    auto *mappingForm = new QFormLayout(mappingTab);
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

    m_tabs->addTab(mappingTab, tr("Mapping"));

    auto *emissionTab = new QWidget(m_tabs);
    auto *emissionForm = new QFormLayout(emissionTab);
    m_emissiveColorButton = new QPushButton(tr("Pick"), emissionTab);
    emissionForm->addRow(tr("Emissive Color"), m_emissiveColorButton);
    m_emissiveIntensity = CreateSliderControl(0.0, 1000000.0, 1.0, 1);
    emissionForm->addRow(tr("Emissive Intensity"), m_emissiveIntensity);
    m_tabs->addTab(emissionTab, tr("Emission"));

    auto *flagsTab = new QWidget(m_tabs);
    auto *flagsForm = new QFormLayout(flagsTab);
    m_doubleSided = new QCheckBox(tr("Double Sided"), flagsTab);
    flagsForm->addRow(m_doubleSided);
    m_alphaMode = new QComboBox(flagsTab);
    m_alphaMode->addItems({tr("OPAQUE"), tr("MASK"), tr("BLEND")});
    flagsForm->addRow(tr("Alpha Mode"), m_alphaMode);
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
            const int n = mat.normalTexture;
            const int e = mat.emissiveTexture;
            const int o = mat.occlusionTexture;
            const int mr = mat.metalRoughTexture;
            mat = def;
            strncpy_s(mat.name, nameBuf, _TRUNCATE);
            mat.diffuseTexture = d;
            mat.normalTexture = n;
            mat.emissiveTexture = e;
            mat.occlusionTexture = o;
            mat.metalRoughTexture = mr;
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
            m.roughness = v;
        });
    });
    connect(m_metalness->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.metalness = static_cast<float>(value);
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
    connect(m_translucency->spinBox(), qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        applyMaterialChange([value](Asset::Material &m) {
            m.translucency = static_cast<float>(value);
        }, true);
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
                int newIdx = index - 1;
                if (newIdx >= 0 && newIdx < static_cast<int>(g_loadedTextures.size()) &&
                    !g_loadedTextures[newIdx].resource) {
                    newIdx = -1;
                }
                setTextureIndexForSlot(m, static_cast<TextureSlot>(slot), newIdx);
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
        connect(widgets.editButton, &QPushButton::clicked, this, [this, slot]() {
            const int idx = currentMaterialIndex();
            if (idx < 0) {
                return;
            }
            const Asset::Material &mat = g_loadedMaterials[idx];
            const int current = textureIndexForSlot(mat, static_cast<TextureSlot>(slot));
            const int maxIndex = static_cast<int>(g_loadedTextures.size()) - 1;
            bool ok = false;
            int value = QInputDialog::getInt(this, tr("Texture Index"),
                                             tr("Set texture index (-1 for None)"),
                                             current, -1, std::max(-1, maxIndex), 1, &ok);
            if (!ok) {
                return;
            }
            applyMaterialChange([this, value, slot](Asset::Material &m) {
                int newIdx = value;
                if (newIdx >= 0 && newIdx < static_cast<int>(g_loadedTextures.size()) &&
                    !g_loadedTextures[newIdx].resource) {
                    newIdx = -1;
                }
                if (newIdx < -1 || newIdx >= static_cast<int>(g_loadedTextures.size())) {
                    newIdx = -1;
                }
                setTextureIndexForSlot(m, static_cast<TextureSlot>(slot), newIdx);
            });
        });
    }
}
void MaterialEditorPanel::refreshMaterials()
{
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
    if (showAll || !selectedNode) {
        scopeIndices.reserve(g_loadedMaterials.size());
        for (int i = 0; i < static_cast<int>(g_loadedMaterials.size()); ++i) {
            scopeIndices.push_back(i);
        }
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
    m_roughness->setValue(mat.roughness);
    m_metalness->setValue(mat.metalness);
    m_specularWeight->setValue(mat.specularWeight);
    m_ior->setValue(mat.ior);
    m_transmission->setValue(mat.transmissionWeight);
    setColorButton(m_transmissionColorButton, getColorFromMaterial(mat.transmissionColor));
    m_coatWeight->setValue(mat.coatWeight);
    m_coatRoughness->setValue(mat.coatRoughness);
    m_translucency->setValue(mat.translucency);
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

    setColorButton(m_emissiveColorButton, getColorFromMaterial(mat.emissiveColor));
    m_emissiveIntensity->setValue(mat.emissiveIntensity);

    m_doubleSided->setChecked(mat.doubleSided);
    m_alphaMode->setCurrentIndex(AlphaModeIndex(mat.alphaMode));

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
    const bool isMetal = mat.metalness > 0.5f;
    const bool isGlass = mat.transmissionWeight > 0.01f;
    const float aMin = std::min({mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2]});
    const float aMax = std::max({mat.diffuseColor[0], mat.diffuseColor[1], mat.diffuseColor[2]});

    QStringList warnings;
    if (!isMetal && !isGlass && (aMin < 0.02f || aMax > 0.90f)) {
        warnings << tr("Dielectric albedo is outside typical range (avoid near-black/white).");
    }
    if (rough < 0.02f) {
        warnings << tr("Roughness < 0.02 can cause fireflies (shader clamps to 0.02).");
    }
    if (mat.coatWeight > 0.01f && mat.coatRoughness < 0.02f) {
        warnings << tr("Coat roughness very low; may sparkle.");
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
    m_countsLabel->setText(
        tr("Loaded: %1 materials, %2 textures")
            .arg(static_cast<int>(g_loadedMaterials.size()))
            .arg(static_cast<int>(g_loadedTextures.size())));
}

void MaterialEditorPanel::updateTextureOptions()
{
    const int idx = currentMaterialIndex();
    if (idx < 0) {
        return;
    }

    QStringList options;
    options << tr("None");
    for (int i = 0; i < static_cast<int>(g_loadedTextures.size()); ++i) {
        options << TextureLabel(g_loadedTextures[i], i);
    }

    for (int slot = 0; slot < TextureSlotCount; ++slot) {
        auto &widgets = m_textureSlots[slot];
        if (!widgets.combo) {
            continue;
        }
        QSignalBlocker blocker(widgets.combo);
        widgets.combo->clear();
        widgets.combo->addItems(options);
        const int texIdx = textureIndexForSlot(g_loadedMaterials[idx], static_cast<TextureSlot>(slot));
        int comboIndex = texIdx < 0 ? 0 : (texIdx + 1);
        if (comboIndex < 0 || comboIndex >= widgets.combo->count()) {
            comboIndex = 0;
        }
        widgets.combo->setCurrentIndex(comboIndex);
    }
}

int MaterialEditorPanel::textureIndexForSlot(const Asset::Material &mat, TextureSlot slot) const
{
    switch (slot) {
    case Albedo: return mat.diffuseTexture;
    case MetalRough: return mat.metalRoughTexture;
    case Normal: return mat.normalTexture;
    case Occlusion: return mat.occlusionTexture;
    case Emissive: return mat.emissiveTexture;
    default: return -1;
    }
}

void MaterialEditorPanel::setTextureIndexForSlot(Asset::Material &mat, TextureSlot slot, int index)
{
    switch (slot) {
    case Albedo: mat.diffuseTexture = index; break;
    case MetalRough: mat.metalRoughTexture = index; break;
    case Normal: mat.normalTexture = index; break;
    case Occlusion: mat.occlusionTexture = index; break;
    case Emissive: mat.emissiveTexture = index; break;
    default: break;
    }
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
    if (!widgets.infoLabel) {
        return;
    }
    const int texIdx = textureIndexForSlot(mat, slot);
    if (texIdx < 0) {
        widgets.infoLabel->setText(tr("No texture bound"));
        return;
    }
    if (texIdx >= static_cast<int>(g_loadedTextures.size())) {
        widgets.infoLabel->setText(tr("Texture #%1 (missing)").arg(texIdx));
        return;
    }

    const auto &tex = g_loadedTextures[texIdx];
    QString info = tr("Texture #%1").arg(texIdx);
    if (tex.width > 0 && tex.height > 0) {
        info += tr(" (%1x%2)").arg(tex.width).arg(tex.height);
    }
    if (tex.mipLevels > 0) {
        info += tr(" mips=%1").arg(tex.mipLevels);
    }
    if (!tex.resource) {
        info += tr(" [missing]");
    }
    widgets.infoLabel->setText(info);
}
