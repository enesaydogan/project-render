#include "LightsPanel.h"

#include "ArchColorDialog.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kProtoItemType = QTreeWidgetItem::UserType;
constexpr int kInstanceItemType = QTreeWidgetItem::UserType + 1;

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

void LightsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_loadNotice = new QLabel(tr("Scene load in progress..."), this);
    m_loadNotice->setWordWrap(true);
    m_loadNotice->hide();
    layout->addWidget(m_loadNotice);

    auto *buttonGrid = new QGridLayout();
    buttonGrid->setContentsMargins(0, 0, 0, 0);
    buttonGrid->setSpacing(4);
    m_addPointButton = new QPushButton(tr("Point"), this);
    m_addSpotButton = new QPushButton(tr("Spot"), this);
    m_addRectButton = new QPushButton(tr("Rect"), this);
    m_addDiskButton = new QPushButton(tr("Disk"), this);
    m_addIESButton = new QPushButton(tr("IES"), this);

    m_addPointButton->setToolTip(tr("Add Point Light"));
    m_addSpotButton->setToolTip(tr("Add Spot Light"));
    m_addRectButton->setToolTip(tr("Add Rect Area Light"));
    m_addDiskButton->setToolTip(tr("Add Disk Area Light"));
    m_addIESButton->setToolTip(tr("Add IES Light (opens file dialog)"));

    buttonGrid->addWidget(m_addPointButton, 0, 0);
    buttonGrid->addWidget(m_addSpotButton, 0, 1);
    buttonGrid->addWidget(m_addRectButton, 0, 2);
    buttonGrid->addWidget(m_addDiskButton, 0, 3);
    buttonGrid->addWidget(m_addIESButton, 1, 0);
    layout->addLayout(buttonGrid);

    m_listGroup = new QGroupBox(tr("Lights"), this);
    auto *listLayout = new QVBoxLayout(m_listGroup);
    listLayout->setContentsMargins(8, 16, 8, 8);
    listLayout->setSpacing(4);
    m_lightTree = new QTreeWidget(m_listGroup);
    m_lightTree->setHeaderHidden(true);
    m_lightTree->setMinimumHeight(100);
    m_lightTree->setRootIsDecorated(true);
    listLayout->addWidget(m_lightTree);

    m_propertiesGroup = new QGroupBox(tr("Properties"), this);
    auto *propertiesLayout = new QVBoxLayout(m_propertiesGroup);
    propertiesLayout->setContentsMargins(8, 16, 8, 8);
    propertiesLayout->setSpacing(8);

    // --- Prototype shared properties ---
    m_protoPropsWidget = new QWidget(m_propertiesGroup);
    auto *protoLayout = new QVBoxLayout(m_protoPropsWidget);
    protoLayout->setContentsMargins(0, 0, 0, 0);
    protoLayout->setSpacing(6);

    auto *protoForm = new QFormLayout();
    protoForm->setContentsMargins(0, 0, 0, 0);
    protoForm->setSpacing(6);

    m_typeLabel = new QLabel(tr("None"), m_protoPropsWidget);
    m_typeLabel->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    protoForm->addRow(tr("Type"), m_typeLabel);

    m_instCountLabel = new QLabel(tr("1 instance"), m_protoPropsWidget);
    protoForm->addRow(tr("Instances"), m_instCountLabel);

    m_colorButton = new QPushButton(tr("Pick Color"), m_protoPropsWidget);
    protoForm->addRow(tr("Color"), m_colorButton);

    m_intensity = CreateDoubleSpinBox(0.0, 1000000.0, 10.0, 2);
    protoForm->addRow(tr("Intensity"), m_intensity);

    m_radius = CreateDoubleSpinBox(0.0, 10.0, 0.01, 3);
    protoForm->addRow(tr("Radius"), m_radius);

    protoLayout->addLayout(protoForm);

    m_spotGroup = new QGroupBox(tr("Spot Light"), m_protoPropsWidget);
    auto *spotForm = new QFormLayout(m_spotGroup);
    spotForm->setContentsMargins(8, 16, 8, 8);
    spotForm->setSpacing(6);
    m_innerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    m_outerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    spotForm->addRow(tr("Inner Angle"), m_innerAngle);
    spotForm->addRow(tr("Outer Angle"), m_outerAngle);
    protoLayout->addWidget(m_spotGroup);

    m_areaGroup = new QGroupBox(tr("Area Light"), m_protoPropsWidget);
    auto *areaForm = new QFormLayout(m_areaGroup);
    areaForm->setContentsMargins(8, 16, 8, 8);
    areaForm->setSpacing(6);
    m_areaWidth = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    m_areaHeight = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    areaForm->addRow(tr("Width"), m_areaWidth);
    areaForm->addRow(tr("Height"), m_areaHeight);
    protoLayout->addWidget(m_areaGroup);

    m_iesGroup = new QGroupBox(tr("IES Profile"), m_protoPropsWidget);
    auto *iesLayout = new QVBoxLayout(m_iesGroup);
    iesLayout->setContentsMargins(8, 16, 8, 8);
    iesLayout->setSpacing(6);
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
    instLayout->setSpacing(6);

    m_instanceLabel = new QLabel(tr("Instance Transform"), m_instancePropsWidget);
    m_instanceLabel->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    instLayout->addWidget(m_instanceLabel);

    auto *posRow = CreateVec3Editor(m_posX, m_posY, m_posZ, -10000.0, 10000.0, 0.1, 2);
    auto *posLabel = new QLabel(tr("Position"), m_instancePropsWidget);
    instLayout->addWidget(posLabel);
    instLayout->addWidget(posRow);

    m_directionLabel = new QLabel(tr("Direction"), m_instancePropsWidget);
    m_directionRow = CreateVec3Editor(m_dirX, m_dirY, m_dirZ, -1000.0, 1000.0, 0.01, 3);
    instLayout->addWidget(m_directionLabel);
    instLayout->addWidget(m_directionRow);

    propertiesLayout->addWidget(m_instancePropsWidget);

    // --- Actions ---
    m_addInstanceButton = new QPushButton(tr("Add Instance"), m_propertiesGroup);
    propertiesLayout->addWidget(m_addInstanceButton);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(4);
    m_selectButton = new QPushButton(tr("Select Gizmo"), m_propertiesGroup);
    m_removeButton = new QPushButton(tr("Remove"), m_propertiesGroup);
    actionRow->addWidget(m_selectButton);
    actionRow->addWidget(m_removeButton);
    propertiesLayout->addLayout(actionRow);

    auto *splitLayout = new QVBoxLayout();
    splitLayout->setSpacing(8);
    splitLayout->addWidget(m_listGroup, 1);
    splitLayout->addWidget(m_propertiesGroup, 2);
    layout->addLayout(splitLayout);

    // --- Connections ---

    connect(m_addPointButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLightPrototype(LightType::Omni);
        Scene::SelectLight(static_cast<int>(Scene::GetLightInstances().size()) - 1);
        refreshLights();
    });
    connect(m_addSpotButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLightPrototype(LightType::Spot);
        Scene::SelectLight(static_cast<int>(Scene::GetLightInstances().size()) - 1);
        refreshLights();
    });
    connect(m_addRectButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLightPrototype(LightType::AreaRect);
        Scene::SelectLight(static_cast<int>(Scene::GetLightInstances().size()) - 1);
        refreshLights();
    });
    connect(m_addDiskButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLightPrototype(LightType::AreaDisk);
        Scene::SelectLight(static_cast<int>(Scene::GetLightInstances().size()) - 1);
        refreshLights();
    });
    connect(m_addIESButton, &QPushButton::clicked, this, [this]() {
        const size_t protoIdx = Scene::AddLightPrototype(LightType::IES);
        const int instIdx = static_cast<int>(Scene::GetLightInstances().size()) - 1;
        Scene::SelectLight(instIdx);
        refreshLights();
        // Automatically open file dialog for IES profile
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load IES Profile"), QString(),
            tr("IES Files (*.ies);;All Files (*)"));
        if (!path.isEmpty()) {
            const int profIdx = Scene::LoadIESProfile(path.toStdString());
            if (profIdx >= 0) {
                auto protos = Scene::GetLightPrototypes();
                if (protoIdx < protos.size()) {
                    LightPrototype proto = protos[protoIdx];
                    proto.iesProfileIndex = profIdx;
                    Scene::UpdateLightPrototype(protoIdx, proto);
                }
                refreshLights();
            } else {
                QMessageBox::warning(this, tr("IES Load Failed"),
                                     tr("Could not parse IES file:\n%1").arg(path));
            }
        }
    });

    connect(m_lightTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (m_syncing) return;
        if (!current) {
            Scene::SelectLight(-1);
            refreshLights();
            return;
        }
        if (current->type() == kInstanceItemType) {
            const int instIdx = current->data(0, Qt::UserRole).toInt();
            Scene::SelectLight(instIdx);
        } else {
            Scene::SelectLight(-1);
        }
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

    // Instance transform connections
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

    connect(m_selectButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int instIdx = selectedInstanceIndex();
        if (instIdx >= 0) Scene::SelectLight(instIdx);
        refreshLights();
    });

    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        QTreeWidgetItem *item = m_lightTree->currentItem();
        if (!item) return;
        if (item->type() == kInstanceItemType) {
            const int instIdx = item->data(0, Qt::UserRole).toInt();
            Scene::RemoveLightInstance(static_cast<size_t>(instIdx));
        } else {
            const int protoIdx = item->data(0, Qt::UserRole).toInt();
            Scene::RemoveLightPrototype(static_cast<size_t>(protoIdx));
        }
        refreshLights();
    });

    connect(m_addInstanceButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) return;
        const int protoIdx = selectedPrototypeIndex();
        if (protoIdx < 0) return;
        const size_t newIdx = Scene::AddLightInstance(static_cast<size_t>(protoIdx));
        Scene::SelectLight(static_cast<int>(newIdx));
        refreshLights();
    });
}

int LightsPanel::selectedInstanceIndex() const
{
    QTreeWidgetItem *item = m_lightTree->currentItem();
    if (item) {
        if (item->type() == kInstanceItemType)
            return item->data(0, Qt::UserRole).toInt();
        return -1;
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
        // Go up to parent prototype
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
    }

    return hash;
}

void LightsPanel::updateColorButton(const QColor &color)
{
    const QString style =
        QStringLiteral("background-color: %1;").arg(color.name(QColor::HexRgb));
    m_colorButton->setStyleSheet(style);
}

void LightsPanel::updatePropertyVisibility(uint32_t type)
{
    const bool isSpot = (type == static_cast<uint32_t>(LightType::Spot));
    const bool isArea = (type == static_cast<uint32_t>(LightType::AreaRect) ||
                         type == static_cast<uint32_t>(LightType::AreaDisk));
    // IES is a profile on point/spot/IES-type lights
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
    m_listGroup->setEnabled(!sceneIoActive);
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

            // Count instances
            int instCount = 0;
            for (const auto &inst : insts) {
                if (inst.prototypeIndex == p) ++instCount;
            }

            auto *protoItem = new QTreeWidgetItem(kProtoItemType);
            protoItem->setText(0, tr("Proto %1 (%2) [%3]")
                .arg(static_cast<int>(p)).arg(typeStr).arg(instCount));
            protoItem->setData(0, Qt::UserRole, static_cast<int>(p));
            if (!proto.enabled) {
                QFont f = protoItem->font(0);
                f.setItalic(true);
                protoItem->setFont(0, f);
            }
            m_lightTree->addTopLevelItem(protoItem);

            // Instances
            for (size_t i = 0; i < insts.size(); ++i) {
                const LightInstance &inst = insts[i];
                if (inst.prototypeIndex != p) continue;

                auto *instItem = new QTreeWidgetItem(kInstanceItemType);
                instItem->setText(0, tr("Instance %1  [%2, %3, %4]")
                    .arg(static_cast<int>(i))
                    .arg(inst.position[0], 0, 'f', 2)
                    .arg(inst.position[1], 0, 'f', 2)
                    .arg(inst.position[2], 0, 'f', 2));
                instItem->setData(0, Qt::UserRole, static_cast<int>(i));
                if (!inst.enabled) {
                    QFont f = instItem->font(0);
                    f.setItalic(true);
                    instItem->setFont(0, f);
                }
                protoItem->addChild(instItem);
            }

            protoItem->setExpanded(true);
        }

        m_lastLightListSignature = signature;
    }

    // Select the appropriate tree item
    {
        const QSignalBlocker treeBlocker(m_lightTree);
        if (selectedInst >= 0 && selectedInst < static_cast<int>(insts.size())) {
            // Find and select the instance item
            for (int topIdx = 0; topIdx < m_lightTree->topLevelItemCount(); ++topIdx) {
                QTreeWidgetItem *protoItem = m_lightTree->topLevelItem(topIdx);
                for (int childIdx = 0; childIdx < protoItem->childCount(); ++childIdx) {
                    QTreeWidgetItem *instItem = protoItem->child(childIdx);
                    if (instItem->data(0, Qt::UserRole).toInt() == selectedInst) {
                        m_lightTree->setCurrentItem(instItem);
                        break;
                    }
                }
            }
        }
    }

    const int curProtoIdx = selectedPrototypeIndex();
    const int curInstIdx = selectedInstanceIndex();
    const bool hasProto = (curProtoIdx >= 0 && curProtoIdx < static_cast<int>(protos.size()));
    const bool hasInst = (curInstIdx >= 0 && curInstIdx < static_cast<int>(insts.size()));

    m_propertiesGroup->setEnabled(hasProto || hasInst);
    m_addInstanceButton->setEnabled(hasProto);
    m_selectButton->setEnabled(hasInst);
    m_removeButton->setEnabled(hasProto || hasInst);

    // Update remove button text
    QTreeWidgetItem *curItem = m_lightTree->currentItem();
    if (curItem && curItem->type() == kProtoItemType) {
        m_removeButton->setText(tr("Remove Proto"));
    } else {
        m_removeButton->setText(tr("Remove Instance"));
    }

    // Update prototype property inspector
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

        // Count instances
        int instCount = 0;
        for (const auto &inst : insts) {
            if (inst.prototypeIndex == static_cast<size_t>(curProtoIdx)) ++instCount;
        }
        m_instCountLabel->setText(tr("%1 instance(s)").arg(instCount));

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

        // Update IES profile info
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

    // Update instance property inspector
    const bool updateInstInspector =
        hasInst && (!hasPropertyEditorFocus() || curInstIdx != m_lastInspectorInstIndex);

    if (updateInstInspector) {
        const LightInstance &inst = insts[static_cast<size_t>(curInstIdx)];

        m_posX->setValue(inst.position[0]);
        m_posY->setValue(inst.position[1]);
        m_posZ->setValue(inst.position[2]);

        m_dirX->setValue(inst.direction[0]);
        m_dirY->setValue(inst.direction[1]);
        m_dirZ->setValue(inst.direction[2]);

        m_instanceLabel->setText(tr("Instance %1 Transform").arg(curInstIdx));

        // Show direction for non-point lights
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
