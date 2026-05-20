#include "LightsPanel.h"

#include "ArchColorDialog.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

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
    x->setPrefix((QStringLiteral("X: ")));
    y->setPrefix((QStringLiteral("Y: ")));
    z->setPrefix((QStringLiteral("Z: ")));
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
    
    // Icon fonts or symbols can be added here if desired.
    m_addPointButton->setToolTip(tr("Add Point Light"));
    m_addSpotButton->setToolTip(tr("Add Spot Light"));
    m_addRectButton->setToolTip(tr("Add Rect Area Light"));
    m_addDiskButton->setToolTip(tr("Add Disk Area Light"));
    
    buttonGrid->addWidget(m_addPointButton, 0, 0);
    buttonGrid->addWidget(m_addSpotButton, 0, 1);
    buttonGrid->addWidget(m_addRectButton, 0, 2);
    buttonGrid->addWidget(m_addDiskButton, 0, 3);
    layout->addLayout(buttonGrid);

    m_listGroup = new QGroupBox(tr("Lights"), this);
    auto *listLayout = new QVBoxLayout(m_listGroup);
    listLayout->setContentsMargins(8, 16, 8, 8);
    listLayout->setSpacing(6);
    listLayout->setSpacing(4);
    m_lightList = new QListWidget(m_listGroup);
    m_lightList->setMinimumHeight(100);
    listLayout->addWidget(m_lightList);

    m_propertiesGroup = new QGroupBox(tr("Properties"), this);
    auto *propertiesLayout = new QVBoxLayout(m_propertiesGroup);
    propertiesLayout->setContentsMargins(8, 16, 8, 8);
    propertiesLayout->setSpacing(6);
    propertiesLayout->setSpacing(8);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    m_typeLabel = new QLabel(tr("None"), m_propertiesGroup);
    m_typeLabel->setStyleSheet(QStringLiteral("color: #58d0f4; font-weight: bold;"));
    form->addRow(tr("Type"), m_typeLabel);

    auto *posRow = CreateVec3Editor(m_posX, m_posY, m_posZ, -10000.0, 10000.0, 0.1, 2);
    // Put vec3 editors on a new row to save horizontal space
    auto *posLabel = new QLabel(tr("Position"), m_propertiesGroup);
    propertiesLayout->addLayout(form);
    propertiesLayout->addWidget(posLabel);
    propertiesLayout->addWidget(posRow);

    auto *form2 = new QFormLayout();
    form2->setContentsMargins(0, 0, 0, 0);
    form2->setSpacing(6);
    m_colorButton = new QPushButton(tr("Pick Color"), m_propertiesGroup);
    form2->addRow(tr("Color"), m_colorButton);

    m_intensity = CreateDoubleSpinBox(0.0, 1000000.0, 10.0, 2);
    form2->addRow(tr("Intensity"), m_intensity);
    
    m_radius = CreateDoubleSpinBox(0.0, 10.0, 0.01, 3);
    form2->addRow(tr("Radius"), m_radius);
    propertiesLayout->addLayout(form2);

    m_directionLabel = new QLabel(tr("Direction"), m_propertiesGroup);
    m_directionRow = CreateVec3Editor(m_dirX, m_dirY, m_dirZ, -1000.0, 1000.0, 0.01, 3);
    propertiesLayout->addWidget(m_directionLabel);
    propertiesLayout->addWidget(m_directionRow);

    m_spotGroup = new QGroupBox(tr("Spot Light"), m_propertiesGroup);
    auto *spotForm = new QFormLayout(m_spotGroup);
    spotForm->setContentsMargins(8, 16, 8, 8);
    spotForm->setVerticalSpacing(6);
    spotForm->setSpacing(6);
    m_innerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    m_outerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    spotForm->addRow(tr("Inner Angle"), m_innerAngle);
    spotForm->addRow(tr("Outer Angle"), m_outerAngle);
    propertiesLayout->addWidget(m_spotGroup);

    m_areaGroup = new QGroupBox(tr("Area Light"), m_propertiesGroup);
    auto *areaForm = new QFormLayout(m_areaGroup);
    areaForm->setContentsMargins(8, 16, 8, 8);
    areaForm->setVerticalSpacing(6);
    areaForm->setSpacing(6);
    m_areaWidth = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    m_areaHeight = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    areaForm->addRow(tr("Width"), m_areaWidth);
    areaForm->addRow(tr("Height"), m_areaHeight);
    propertiesLayout->addWidget(m_areaGroup);

    m_iesGroup = new QGroupBox(tr("IES"), m_propertiesGroup);
    auto *iesLayout = new QVBoxLayout(m_iesGroup);
    iesLayout->setContentsMargins(8, 16, 8, 8);
    iesLayout->setSpacing(6);
    m_iesLabel = new QLabel(tr("IES Atlas Index: -1"), m_iesGroup);
    iesLayout->addWidget(m_iesLabel);
    propertiesLayout->addWidget(m_iesGroup);

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
    
    // Since properties can take up space, no generic stretch at the bottom 
    // helps keep the UI anchored properly without floating loosely.

    connect(m_addPointButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLight(LightType::Omni);
        Scene::SelectLight(static_cast<int>(Scene::GetLights().size()) - 1);
        refreshLights();
    });
    connect(m_addSpotButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLight(LightType::Spot);
        Scene::SelectLight(static_cast<int>(Scene::GetLights().size()) - 1);
        refreshLights();
    });
    connect(m_addRectButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLight(LightType::AreaRect);
        Scene::SelectLight(static_cast<int>(Scene::GetLights().size()) - 1);
        refreshLights();
    });
    connect(m_addDiskButton, &QPushButton::clicked, this, [this]() {
        Scene::AddLight(LightType::AreaDisk);
        Scene::SelectLight(static_cast<int>(Scene::GetLights().size()) - 1);
        refreshLights();
    });

    connect(m_lightList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_syncing) {
            return;
        }
        Scene::SelectLight(row);
        refreshLights();
    });

    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        const int row = m_lightList->currentRow();
        auto &lights = Scene::GetLights();
        if (row < 0 || row >= static_cast<int>(lights.size())) {
            return;
        }

        const Light originalLight = lights[static_cast<size_t>(row)];
        const QColor originalColor = m_currentColor;
        auto applyPreview = [this, row, originalLight](const QColor &color) {
            if (!color.isValid()) {
                return;
            }
            Light preview = originalLight;
            const float intensity = static_cast<float>(m_intensity->value());
            preview.emission[0] = static_cast<float>(color.redF()) * intensity;
            preview.emission[1] = static_cast<float>(color.greenF()) * intensity;
            preview.emission[2] = static_cast<float>(color.blueF()) * intensity;
            Scene::UpdateLight(static_cast<size_t>(row), preview);
            m_currentColor = color;
            updateColorButton(m_currentColor);
        };

        auto restoreOriginal = [this, row, originalLight, originalColor]() {
            Scene::UpdateLight(static_cast<size_t>(row), originalLight);
            m_currentColor = originalColor;
            updateColorButton(m_currentColor);
        };
        ArchColorDialog::showColor(m_currentColor, this, tr("Select Light Color"),
                                   applyPreview, applyPreview, restoreOriginal);
    });

    connect(m_intensity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLight([this](Light &l) {
            const float intensity = static_cast<float>(m_intensity->value());
            const float r = static_cast<float>(m_currentColor.redF());
            const float g = static_cast<float>(m_currentColor.greenF());
            const float b = static_cast<float>(m_currentColor.blueF());
            l.emission[0] = r * intensity;
            l.emission[1] = g * intensity;
            l.emission[2] = b * intensity;
        });
    });

    auto applyPosition = [this]() {
        applyLight([this](Light &l) {
            l.position[0] = static_cast<float>(m_posX->value());
            l.position[1] = static_cast<float>(m_posY->value());
            l.position[2] = static_cast<float>(m_posZ->value());
        });
    };
    connect(m_posX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });
    connect(m_posY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });
    connect(m_posZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyPosition](double) { applyPosition(); });

    auto applyDirection = [this]() {
        applyLight([this](Light &l) {
            l.direction[0] = static_cast<float>(m_dirX->value());
            l.direction[1] = static_cast<float>(m_dirY->value());
            l.direction[2] = static_cast<float>(m_dirZ->value());
        });
    };
    connect(m_dirX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });
    connect(m_dirY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });
    connect(m_dirZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyDirection](double) { applyDirection(); });

    connect(m_radius, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        applyLight([this](Light &l) {
            l.radius = static_cast<float>(m_radius->value());
        });
    });

    connect(m_innerAngle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        if (m_outerAngle->value() < value) {
            m_outerAngle->setValue(value);
        }
        applyLight([this](Light &l) {
            const double innerRad = m_innerAngle->value() * (kPi / 180.0);
            const double outerRad = m_outerAngle->value() * (kPi / 180.0);
            l.innerConeAngle = static_cast<float>(std::cos(innerRad));
            l.outerConeAngle = static_cast<float>(std::cos(outerRad));
        });
    });
    connect(m_outerAngle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) {
            return;
        }
        if (value < m_innerAngle->value()) {
            m_outerAngle->setValue(m_innerAngle->value());
        }
        applyLight([this](Light &l) {
            const double innerRad = m_innerAngle->value() * (kPi / 180.0);
            const double outerRad = m_outerAngle->value() * (kPi / 180.0);
            l.innerConeAngle = static_cast<float>(std::cos(innerRad));
            l.outerConeAngle = static_cast<float>(std::cos(outerRad));
        });
    });

    auto applyArea = [this]() {
        applyLight([this](Light &l) {
            l.areaExtents[0] = static_cast<float>(m_areaWidth->value());
            l.areaExtents[1] = static_cast<float>(m_areaHeight->value());
        });
    };
    connect(m_areaWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyArea](double) { applyArea(); });
    connect(m_areaHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyArea](double) { applyArea(); });

    connect(m_selectButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        Scene::SelectLight(m_lightList->currentRow());
        refreshLights();
    });

    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        if (m_syncing) {
            return;
        }
        const int row = m_lightList->currentRow();
        if (row < 0) {
            return;
        }
        const int selected = Scene::GetSelectedLightIndex();
        Scene::RemoveLight(static_cast<size_t>(row));
        if (selected == row) {
            Scene::SelectLight(-1);
        } else if (selected > row) {
            Scene::SelectLight(selected - 1);
        }
        refreshLights();
    });
}

void LightsPanel::applyLight(const std::function<void(Light &)> &fn)
{
    if (m_syncing) {
        return;
    }
    auto &lights = Scene::GetLights();
    const int idx = Scene::GetSelectedLightIndex();
    if (idx < 0 || idx >= static_cast<int>(lights.size())) {
        return;
    }
    fn(lights[idx]);
    Scene::UpdateLights();
}

bool LightsPanel::hasPropertyEditorFocus() const
{
    QWidget *focus = QApplication::focusWidget();
    if (!focus || !isAncestorOf(focus)) {
        return false;
    }

    auto editing = [focus](QWidget *widget) {
        return widget && (focus == widget || widget->isAncestorOf(focus));
    };

    return editing(m_intensity) ||
           editing(m_posX) ||
           editing(m_posY) ||
           editing(m_posZ) ||
           editing(m_dirX) ||
           editing(m_dirY) ||
           editing(m_dirZ) ||
           editing(m_radius) ||
           editing(m_innerAngle) ||
           editing(m_outerAngle) ||
           editing(m_areaWidth) ||
           editing(m_areaHeight);
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

    const auto &lights = Scene::GetLights();
    mix(static_cast<uint64_t>(lights.size()));
    for (const Light &light : lights) {
        mix(static_cast<uint64_t>(light.type));
        for (float v : light.position) {
            mixFloat(v);
        }
        for (float v : light.emission) {
            mixFloat(v);
        }
        for (float v : light.direction) {
            mixFloat(v);
        }
        mixFloat(light.radius);
        mixFloat(light.innerConeAngle);
        mixFloat(light.outerConeAngle);
        for (float v : light.areaExtents) {
            mixFloat(v);
        }
        mix(static_cast<uint64_t>(static_cast<int64_t>(light.iesAtlasIndex) + 1));
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
    const bool isOmni = (type == static_cast<uint32_t>(LightType::Omni));
    const bool isSpot = (type == static_cast<uint32_t>(LightType::Spot));
    const bool isArea = (type == static_cast<uint32_t>(LightType::AreaRect) ||
                         type == static_cast<uint32_t>(LightType::AreaDisk));
    const bool isIes = (type == static_cast<uint32_t>(LightType::IES));

    m_directionLabel->setVisible(!isOmni);
    m_directionRow->setVisible(!isOmni);
    m_spotGroup->setVisible(isSpot);
    m_areaGroup->setVisible(isArea);
    m_iesGroup->setVisible(isIes);
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
    if (sceneIoActive) {
        m_syncing = false;
        return;
    }

    const auto &lights = Scene::GetLights();
    int selected = Scene::GetSelectedLightIndex();
    if (selected >= static_cast<int>(lights.size())) {
        Scene::SelectLight(-1);
        selected = -1;
    }

    const uint64_t signature = lightListSignature();
    if (signature != m_lastLightListSignature) {
        const QSignalBlocker listBlocker(m_lightList);
        m_lightList->clear();
        for (size_t i = 0; i < lights.size(); ++i) {
            const Light &l = lights[i];
            const char *typeStr = "Unknown";
            switch (static_cast<LightType>(l.type)) {
            case LightType::Directional: typeStr = "Sun"; break;
            case LightType::Omni: typeStr = "Omni"; break;
            case LightType::Spot: typeStr = "Spot"; break;
            case LightType::AreaRect: typeStr = "Rect"; break;
            case LightType::AreaDisk: typeStr = "Disk"; break;
            case LightType::IES: typeStr = "IES"; break;
            }
            m_lightList->addItem(
                tr("Light %1 (%2)  [%3, %4, %5]")
                    .arg(static_cast<int>(i))
                    .arg(typeStr)
                    .arg(l.position[0], 0, 'f', 2)
                    .arg(l.position[1], 0, 'f', 2)
                    .arg(l.position[2], 0, 'f', 2));
        }
        m_lastLightListSignature = signature;
    }

    if (selected >= 0 && selected < m_lightList->count()) {
        m_lightList->setCurrentRow(selected);
    } else {
        m_lightList->setCurrentRow(-1);
    }

    const bool hasSelection = (selected >= 0 && selected < (int)lights.size());
    m_propertiesGroup->setEnabled(hasSelection);
    m_selectButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);

    const bool updateInspector =
        hasSelection &&
        (!hasPropertyEditorFocus() || selected != m_lastInspectorLightIndex);

    if (updateInspector) {
        const Light &l = lights[(size_t)selected];
        const char *typeStr = "Unknown";
        switch (static_cast<LightType>(l.type)) {
        case LightType::Directional: typeStr = "Sun"; break;
        case LightType::Omni: typeStr = "Omni"; break;
        case LightType::Spot: typeStr = "Spot"; break;
        case LightType::AreaRect: typeStr = "Rect"; break;
        case LightType::AreaDisk: typeStr = "Disk"; break;
        case LightType::IES: typeStr = "IES"; break;
        }
        m_typeLabel->setText(tr("%1").arg(typeStr));

        m_posX->setValue(l.position[0]);
        m_posY->setValue(l.position[1]);
        m_posZ->setValue(l.position[2]);

        const float maxComp =
            std::max({l.emission[0], l.emission[1], l.emission[2], 0.001f});
        const float r = l.emission[0] / maxComp;
        const float g = l.emission[1] / maxComp;
        const float b = l.emission[2] / maxComp;
        m_currentColor = QColor::fromRgbF(r, g, b);
        updateColorButton(m_currentColor);
        m_intensity->setValue(maxComp);

        m_dirX->setValue(l.direction[0]);
        m_dirY->setValue(l.direction[1]);
        m_dirZ->setValue(l.direction[2]);

        m_radius->setValue(l.radius);

        const double innerDeg =
            std::acos(std::clamp<double>(l.innerConeAngle, -1.0, 1.0)) * (180.0 / kPi);
        const double outerDeg =
            std::acos(std::clamp<double>(l.outerConeAngle, -1.0, 1.0)) * (180.0 / kPi);
        m_innerAngle->setValue(innerDeg);
        m_outerAngle->setValue(outerDeg);
        m_outerAngle->setMinimum(m_innerAngle->value());

        m_areaWidth->setValue(l.areaExtents[0]);
        m_areaHeight->setValue(l.areaExtents[1]);

        m_iesLabel->setText(tr("IES Atlas Index: %1").arg(l.iesAtlasIndex));

        updatePropertyVisibility(l.type);
        m_lastInspectorLightIndex = selected;
    } else if (!hasSelection) {
        m_lastInspectorLightIndex = -1;
    }

    m_syncing = false;
}
