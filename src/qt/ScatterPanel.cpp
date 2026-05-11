#include "ScatterPanel.h"

#include "../scene.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QDoubleSpinBox *CreateDoubleSpin(double minValue, double maxValue,
                                 double step, int decimals)
{
    auto *spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setAccelerated(true);
    return spin;
}

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

} // namespace

ScatterPanel::ScatterPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshUi();

    m_sceneChangeListenerId = Scene::RegisterChangeListener([this]() {
        QMetaObject::invokeMethod(this, [this]() { refreshUi(); },
                                  Qt::QueuedConnection);
    });

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        const uint64_t rev = Scene::GetScatterRuntimeRevision();
        if (rev != m_lastScatterRevision) {
            refreshUi();
        }
    });
    m_refreshTimer->start(250);
}

ScatterPanel::~ScatterPanel()
{
    if (m_sceneChangeListenerId != 0) {
        Scene::UnregisterChangeListener(m_sceneChangeListenerId);
    }
}

void ScatterPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *modelButtons = new QHBoxLayout();
    m_addModelButton = new QPushButton(tr("Add"), this);
    m_deleteModelButton = new QPushButton(tr("Delete"), this);
    modelButtons->addWidget(m_addModelButton);
    modelButtons->addWidget(m_deleteModelButton);
    layout->addLayout(modelButtons);

    m_modelList = new QListWidget(this);
    m_modelList->setMinimumHeight(90);
    layout->addWidget(m_modelList);

    auto *modelGroup = new QGroupBox(tr("Model"), this);
    auto *modelForm = new QFormLayout(modelGroup);
    m_modelName = new QLineEdit(modelGroup);
    m_modelEnabled = new QCheckBox(tr("Enabled"), modelGroup);
    m_modelSeed = new QSpinBox(modelGroup);
    m_modelSeed->setRange(1, 2147483647);
    modelForm->addRow(tr("Name"), m_modelName);
    modelForm->addRow(tr("State"), m_modelEnabled);
    modelForm->addRow(tr("Seed"), m_modelSeed);
    layout->addWidget(modelGroup);

    auto *assignmentButtons = new QHBoxLayout();
    m_addTargetsButton = new QPushButton(tr("Add Targets"), this);
    m_addTargetsButton->setToolTip(tr("Add meshes from the current scene selection as scatter surfaces"));
    m_addObjectsButton = new QPushButton(tr("Add Objects"), this);
    m_addObjectsButton->setToolTip(tr("Use meshes from the current scene selection as scatter prototypes"));
    assignmentButtons->addWidget(m_addTargetsButton);
    assignmentButtons->addWidget(m_addObjectsButton);
    layout->addLayout(assignmentButtons);

    auto *targetGroup = new QGroupBox(tr("Targets"), this);
    auto *targetLayout = new QVBoxLayout(targetGroup);
    m_targetList = new QListWidget(targetGroup);
    m_targetList->setMinimumHeight(80);
    m_removeTargetButton = new QPushButton(tr("Remove Target"), targetGroup);
    targetLayout->addWidget(m_targetList);
    targetLayout->addWidget(m_removeTargetButton);
    layout->addWidget(targetGroup);

    auto *objectGroup = new QGroupBox(tr("Objects"), this);
    auto *objectLayout = new QVBoxLayout(objectGroup);
    m_objectList = new QListWidget(objectGroup);
    m_objectList->setMinimumHeight(80);
    m_removeObjectButton = new QPushButton(tr("Remove Object"), objectGroup);
    objectLayout->addWidget(m_objectList);
    objectLayout->addWidget(m_removeObjectButton);
    layout->addWidget(objectGroup);

    auto *objectEditGroup = new QGroupBox(tr("Object Settings"), this);
    auto *objectForm = new QFormLayout(objectEditGroup);
    m_objectName = new QLineEdit(objectEditGroup);
    m_objectEnabled = new QCheckBox(tr("Enabled"), objectEditGroup);
    m_density = CreateDoubleSpin(0.0, 10000.0, 0.5, 2);
    m_weight = CreateDoubleSpin(0.0, 100.0, 0.1, 2);
    m_maxInstances = new QSpinBox(objectEditGroup);
    m_maxInstances->setRange(0, 2000000);
    m_minScale = CreateDoubleSpin(0.001, 1000.0, 0.05, 3);
    m_maxScale = CreateDoubleSpin(0.001, 1000.0, 0.05, 3);
    m_yaw = CreateDoubleSpin(0.0, 360.0, 5.0, 1);
    m_pitch = CreateDoubleSpin(0.0, 180.0, 1.0, 1);
    m_roll = CreateDoubleSpin(0.0, 180.0, 1.0, 1);
    m_normalAlign = CreateDoubleSpin(0.0, 1.0, 0.05, 2);
    m_slopeMin = CreateDoubleSpin(0.0, 89.0, 1.0, 1);
    m_slopeMax = CreateDoubleSpin(0.0, 89.0, 1.0, 1);
    m_jitter = CreateDoubleSpin(0.0, 1000.0, 0.01, 3);

    objectForm->addRow(tr("Name"), m_objectName);
    objectForm->addRow(tr("State"), m_objectEnabled);
    objectForm->addRow(tr("Density / m2"), m_density);
    objectForm->addRow(tr("Weight"), m_weight);
    objectForm->addRow(tr("Max Instances"), m_maxInstances);
    objectForm->addRow(tr("Min Scale"), m_minScale);
    objectForm->addRow(tr("Max Scale"), m_maxScale);
    objectForm->addRow(tr("Yaw Random"), m_yaw);
    objectForm->addRow(tr("Pitch Random"), m_pitch);
    objectForm->addRow(tr("Roll Random"), m_roll);
    objectForm->addRow(tr("Normal Align"), m_normalAlign);
    objectForm->addRow(tr("Slope Min"), m_slopeMin);
    objectForm->addRow(tr("Slope Max"), m_slopeMax);
    objectForm->addRow(tr("Jitter"), m_jitter);
    layout->addWidget(objectEditGroup);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    layout->addWidget(m_statsLabel);
    layout->addStretch(1);

    connect(m_addModelButton, &QPushButton::clicked, this, [this]() {
        const size_t row = Scene::AddScatterModel(tr("Scatter").toStdString());
        setSelectedModel(static_cast<int>(row));
    });
    connect(m_deleteModelButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::RemoveScatterModel(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) {
            syncInspector();
        }
    });
    connect(m_objectList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) {
            syncInspector();
        }
    });
    connect(m_targetList, &QListWidget::currentRowChanged, this, [this](int) {
        if (!m_syncing) {
            syncInspector();
        }
    });
    connect(m_addTargetsButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterTargets(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_addObjectsButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterObjects(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_removeTargetButton, &QPushButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            targetRow < 0 ||
            targetRow >= static_cast<int>(models[static_cast<size_t>(modelRow)].targets.size())) {
            return;
        }
        Scene::ScatterModel model = models[static_cast<size_t>(modelRow)];
        model.targets.erase(model.targets.begin() + targetRow);
        Scene::UpdateScatterModel(static_cast<size_t>(modelRow), model);
        refreshUi();
    });
    connect(m_removeObjectButton, &QPushButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int objectRow = selectedObjectIndex();
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            objectRow < 0 ||
            objectRow >= static_cast<int>(models[static_cast<size_t>(modelRow)].objects.size())) {
            return;
        }
        Scene::ScatterModel model = models[static_cast<size_t>(modelRow)];
        model.objects.erase(model.objects.begin() + objectRow);
        Scene::UpdateScatterModel(static_cast<size_t>(modelRow), model);
        refreshUi();
    });

    connect(m_modelName, &QLineEdit::editingFinished, this, [this]() { applyModelEdit(); });
    connect(m_modelEnabled, &QCheckBox::toggled, this, [this](bool) { applyModelEdit(); });
    connect(m_modelSeed, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applyModelEdit(); });

    auto objectEdit = [this]() { applyObjectEdit(); };
    connect(m_objectName, &QLineEdit::editingFinished, this, objectEdit);
    connect(m_objectEnabled, &QCheckBox::toggled, this, [objectEdit](bool) { objectEdit(); });
    connect(m_density, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_weight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_maxInstances, qOverload<int>(&QSpinBox::valueChanged), this, [objectEdit](int) { objectEdit(); });
    connect(m_minScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_maxScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_yaw, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_pitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_roll, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_normalAlign, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_slopeMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_slopeMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_jitter, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
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
        m_modelList->addItem(tr("%1%2  (%3 targets, %4 objects)")
                                 .arg(model.enabled ? QString() : tr("[off] "))
                                 .arg(QString::fromStdString(model.name))
                                 .arg(static_cast<int>(model.targets.size()))
                                 .arg(static_cast<int>(model.objects.size())));
    }
    if (!models.empty()) {
        const int row = std::clamp(previousModel, 0, static_cast<int>(models.size()) - 1);
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
    m_addObjectsButton->setEnabled(hasModel);
    m_modelName->setEnabled(hasModel);
    m_modelEnabled->setEnabled(hasModel);
    m_modelSeed->setEnabled(hasModel);

    m_targetList->clear();
    m_objectList->clear();

    if (!hasModel) {
        m_modelName->clear();
        m_modelEnabled->setChecked(false);
        m_statsLabel->setText(tr("Add a scatter model, then select surface nodes and prototype object nodes."));
    } else {
        const Scene::ScatterModel &model = models[static_cast<size_t>(modelRow)];
        m_modelName->setText(QString::fromStdString(model.name));
        m_modelEnabled->setChecked(model.enabled);
        m_modelSeed->setValue(static_cast<int>(std::max(1u, model.seed)));

        for (const Scene::ScatterTarget &target : model.targets) {
            m_targetList->addItem(tr("%1%2 / %3")
                                      .arg(target.enabled ? QString() : tr("[off] "))
                                      .arg(NodeLabel(target.nodeIndex))
                                      .arg(MeshLabel(target.meshIndex)));
        }
        for (const Scene::ScatterObject &object : model.objects) {
            m_objectList->addItem(tr("%1%2  (%3 meshes)")
                                      .arg(object.enabled ? QString() : tr("[off] "))
                                      .arg(QString::fromStdString(object.name))
                                      .arg(static_cast<int>(object.meshIndices.size())));
        }
        m_statsLabel->setText(tr("Virtual instances are generated at render time; prototype nodes can be hidden from the Scene panel if you only want their scattered copies."));
    }

    const int objectRow = selectedObjectIndex();
    const bool hasObject =
        hasModel && objectRow >= 0 &&
        objectRow < static_cast<int>(models[static_cast<size_t>(modelRow)].objects.size());
    m_removeTargetButton->setEnabled(hasModel && m_targetList->currentRow() >= 0);
    m_removeObjectButton->setEnabled(hasObject);
    m_objectName->setEnabled(hasObject);
    m_objectEnabled->setEnabled(hasObject);
    m_density->setEnabled(hasObject);
    m_weight->setEnabled(hasObject);
    m_maxInstances->setEnabled(hasObject);
    m_minScale->setEnabled(hasObject);
    m_maxScale->setEnabled(hasObject);
    m_yaw->setEnabled(hasObject);
    m_pitch->setEnabled(hasObject);
    m_roll->setEnabled(hasObject);
    m_normalAlign->setEnabled(hasObject);
    m_slopeMin->setEnabled(hasObject);
    m_slopeMax->setEnabled(hasObject);
    m_jitter->setEnabled(hasObject);

    if (hasObject) {
        const Scene::ScatterObject &object =
            models[static_cast<size_t>(modelRow)].objects[static_cast<size_t>(objectRow)];
        m_objectName->setText(QString::fromStdString(object.name));
        m_objectEnabled->setChecked(object.enabled);
        m_density->setValue(object.densityPerSquareMeter);
        m_weight->setValue(object.weight);
        m_maxInstances->setValue(static_cast<int>(std::min<uint32_t>(object.maxInstances, 2000000u)));
        m_minScale->setValue(object.minScale);
        m_maxScale->setValue(object.maxScale);
        m_yaw->setValue(object.randomYawDegrees);
        m_pitch->setValue(object.randomPitchDegrees);
        m_roll->setValue(object.randomRollDegrees);
        m_normalAlign->setValue(object.normalAlign);
        m_slopeMin->setValue(object.slopeMinDegrees);
        m_slopeMax->setValue(object.slopeMaxDegrees);
        m_jitter->setValue(object.jitterMeters);
    } else {
        m_objectName->clear();
        m_objectEnabled->setChecked(false);
    }
    m_syncing = false;
}

void ScatterPanel::applyModelEdit()
{
    if (m_syncing) {
        return;
    }
    const int row = selectedModelIndex();
    const auto &models = Scene::GetScatterModels();
    if (row < 0 || row >= static_cast<int>(models.size())) {
        return;
    }
    Scene::ScatterModel model = models[static_cast<size_t>(row)];
    model.name = m_modelName->text().toStdString();
    model.enabled = m_modelEnabled->isChecked();
    model.seed = static_cast<uint32_t>(std::max(1, m_modelSeed->value()));
    Scene::UpdateScatterModel(static_cast<size_t>(row), model);
}

void ScatterPanel::applyObjectEdit()
{
    if (m_syncing) {
        return;
    }
    const int modelRow = selectedModelIndex();
    const int objectRow = selectedObjectIndex();
    const auto &models = Scene::GetScatterModels();
    if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
        objectRow < 0 ||
        objectRow >= static_cast<int>(models[static_cast<size_t>(modelRow)].objects.size())) {
        return;
    }
    Scene::ScatterModel model = models[static_cast<size_t>(modelRow)];
    Scene::ScatterObject &object = model.objects[static_cast<size_t>(objectRow)];
    object.name = m_objectName->text().toStdString();
    object.enabled = m_objectEnabled->isChecked();
    object.densityPerSquareMeter = static_cast<float>(m_density->value());
    object.weight = static_cast<float>(m_weight->value());
    object.maxInstances = static_cast<uint32_t>(std::max(0, m_maxInstances->value()));
    object.minScale = static_cast<float>(m_minScale->value());
    object.maxScale = static_cast<float>(m_maxScale->value());
    object.randomYawDegrees = static_cast<float>(m_yaw->value());
    object.randomPitchDegrees = static_cast<float>(m_pitch->value());
    object.randomRollDegrees = static_cast<float>(m_roll->value());
    object.normalAlign = static_cast<float>(m_normalAlign->value());
    object.slopeMinDegrees = static_cast<float>(m_slopeMin->value());
    object.slopeMaxDegrees = static_cast<float>(m_slopeMax->value());
    object.jitterMeters = static_cast<float>(m_jitter->value());
    Scene::UpdateScatterModel(static_cast<size_t>(modelRow), model);
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
    if (m_modelList) {
        m_modelList->setCurrentRow(row);
    }
    refreshUi();
}
