#include "LightsPanel.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
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
    x = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
    y = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
    z = CreateDoubleSpinBox(minValue, maxValue, step, decimals);
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
        refreshLights();
    });
    m_refreshTimer->start(250);
}

void LightsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_loadNotice = new QLabel(tr("Scene load in progress..."), this);
    m_loadNotice->setWordWrap(true);
    m_loadNotice->hide();
    layout->addWidget(m_loadNotice);

    auto *buttonGrid = new QGridLayout();
    m_addPointButton = new QPushButton(tr("Add Point Light"), this);
    m_addSpotButton = new QPushButton(tr("Add Spot Light"), this);
    m_addRectButton = new QPushButton(tr("Add Rect Area"), this);
    m_addDiskButton = new QPushButton(tr("Add Disk Area"), this);
    buttonGrid->addWidget(m_addPointButton, 0, 0);
    buttonGrid->addWidget(m_addSpotButton, 0, 1);
    buttonGrid->addWidget(m_addRectButton, 1, 0);
    buttonGrid->addWidget(m_addDiskButton, 1, 1);
    layout->addLayout(buttonGrid);

    m_listGroup = new QGroupBox(tr("Lights"), this);
    auto *listLayout = new QVBoxLayout(m_listGroup);
    m_lightList = new QListWidget(m_listGroup);
    listLayout->addWidget(m_lightList);

    m_propertiesGroup = new QGroupBox(tr("Selected Light"), this);
    auto *propertiesLayout = new QVBoxLayout(m_propertiesGroup);
    auto *form = new QFormLayout();

    m_typeLabel = new QLabel(tr("None"), m_propertiesGroup);
    form->addRow(tr("Type"), m_typeLabel);

    auto *posRow = CreateVec3Editor(m_posX, m_posY, m_posZ, -10000.0, 10000.0, 0.1, 2);
    form->addRow(tr("Position"), posRow);

    m_colorButton = new QPushButton(tr("Pick Color"), m_propertiesGroup);
    form->addRow(tr("Color"), m_colorButton);

    m_intensity = CreateDoubleSpinBox(0.0, 1000000.0, 10.0, 2);
    form->addRow(tr("Intensity"), m_intensity);

    m_directionLabel = new QLabel(tr("Direction"), m_propertiesGroup);
    m_directionRow = CreateVec3Editor(m_dirX, m_dirY, m_dirZ, -1000.0, 1000.0, 0.01, 3);
    form->addRow(m_directionLabel, m_directionRow);

    m_radius = CreateDoubleSpinBox(0.0, 10.0, 0.01, 3);
    form->addRow(tr("Radius"), m_radius);

    propertiesLayout->addLayout(form);

    m_spotGroup = new QGroupBox(tr("Spot Light"), m_propertiesGroup);
    auto *spotForm = new QFormLayout(m_spotGroup);
    m_innerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    m_outerAngle = CreateDoubleSpinBox(0.0, 90.0, 0.1, 1);
    spotForm->addRow(tr("Inner Angle"), m_innerAngle);
    spotForm->addRow(tr("Outer Angle"), m_outerAngle);
    propertiesLayout->addWidget(m_spotGroup);

    m_areaGroup = new QGroupBox(tr("Area Light"), m_propertiesGroup);
    auto *areaForm = new QFormLayout(m_areaGroup);
    m_areaWidth = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    m_areaHeight = CreateDoubleSpinBox(0.01, 50.0, 0.1, 2);
    areaForm->addRow(tr("Width"), m_areaWidth);
    areaForm->addRow(tr("Height"), m_areaHeight);
    propertiesLayout->addWidget(m_areaGroup);

    m_iesGroup = new QGroupBox(tr("IES"), m_propertiesGroup);
    auto *iesLayout = new QVBoxLayout(m_iesGroup);
    m_iesLabel = new QLabel(tr("IES Atlas Index: -1"), m_iesGroup);
    iesLayout->addWidget(m_iesLabel);
    propertiesLayout->addWidget(m_iesGroup);

    auto *actionRow = new QHBoxLayout();
    m_selectButton = new QPushButton(tr("Select for Gizmo"), m_propertiesGroup);
    m_removeButton = new QPushButton(tr("Remove"), m_propertiesGroup);
    actionRow->addWidget(m_selectButton);
    actionRow->addWidget(m_removeButton);
    propertiesLayout->addLayout(actionRow);

    auto *splitLayout = new QVBoxLayout();
    splitLayout->addWidget(m_listGroup);
    splitLayout->addWidget(m_propertiesGroup);
    splitLayout->setStretch(0, 1);
    splitLayout->setStretch(1, 2);
    layout->addLayout(splitLayout);
    layout->addStretch(1);

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
        QColor chosen = QColorDialog::getColor(m_currentColor, this, tr("Select Light Color"));
        if (!chosen.isValid()) {
            return;
        }
        m_currentColor = chosen;
        updateColorButton(m_currentColor);
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

    const bool loading = IsSceneLoadInProgress();
    m_loadNotice->setVisible(loading);
    m_listGroup->setEnabled(!loading);
    m_propertiesGroup->setEnabled(!loading);
    m_addPointButton->setEnabled(!loading);
    m_addSpotButton->setEnabled(!loading);
    m_addRectButton->setEnabled(!loading);
    m_addDiskButton->setEnabled(!loading);
    if (loading) {
        m_syncing = false;
        return;
    }

    const auto &lights = Scene::GetLights();
    int selected = Scene::GetSelectedLightIndex();
    if (selected >= static_cast<int>(lights.size())) {
        Scene::SelectLight(-1);
        selected = -1;
    }

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

    if (selected >= 0 && selected < m_lightList->count()) {
        m_lightList->setCurrentRow(selected);
    } else {
        m_lightList->setCurrentRow(-1);
    }

    const bool hasSelection = (selected >= 0 && selected < (int)lights.size());
    m_propertiesGroup->setEnabled(hasSelection);
    m_selectButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);

    if (hasSelection) {
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
    }

    m_syncing = false;
}
