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
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {

QDoubleSpinBox *CreateDoubleSpin(double minValue, double maxValue,
                                 double step, int decimals,
                                 bool adaptiveStep = false)
{
    auto *spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setAccelerated(true);
    if (adaptiveStep) {
        // B11: large-range fields (jitter, collision avoidance, distance
        // bounds) get scale-aware steps so dragging from 0..10 000 isn't a
        // ten-million-click slog.
        spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    }
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

    // C1: previously polled the scatter revision at 4 Hz to catch stats
    // updates that happen during render (no scene-change event). Authoring
    // edits already trigger the scene change listener below, so the panel
    // refreshes immediately on user input; per-frame stat changes catch up
    // on the next edit. The 250 ms timer is gone.
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
    m_modelEnabled->setToolTip(
        tr("Disable to stop generating instances without losing settings."));
    // B3: seed + reseed button on one row.
    auto *seedRow = new QWidget(modelGroup);
    auto *seedRowLayout = new QHBoxLayout(seedRow);
    seedRowLayout->setContentsMargins(0, 0, 0, 0);
    seedRowLayout->setSpacing(4);
    m_modelSeed = new QSpinBox(seedRow);
    m_modelSeed->setRange(1, 2147483647);
    m_modelSeed->setToolTip(
        tr("Random seed for instance placement. Same seed = same layout."));
    m_reseedButton = new QPushButton(tr("\xf0\x9f\x8e\xb2"), seedRow); // 🎲
    m_reseedButton->setToolTip(tr("Reseed with a random value."));
    m_reseedButton->setFixedWidth(28);
    seedRowLayout->addWidget(m_modelSeed, 1);
    seedRowLayout->addWidget(m_reseedButton, 0);
    m_previewDensityScale = CreateDoubleSpin(0.0, 1.0, 0.05, 2);
    m_previewDensityScale->setToolTip(
        tr("0..1 scaler on density for editor preview. Does not affect "
           "authored density value."));
    m_previewBudget = new QSpinBox(modelGroup);
    m_previewBudget->setRange(0, 2000000);
    m_previewBudget->setToolTip(
        tr("Model-wide hard cap across all objects. If reached, remaining "
           "objects produce no instances (see 'Budget skipped' in stats)."));
    modelForm->addRow(tr("Name"), m_modelName);
    modelForm->addRow(tr("State"), m_modelEnabled);
    modelForm->addRow(tr("Seed"), seedRow);
    modelForm->addRow(tr("Preview Density"), m_previewDensityScale);
    modelForm->addRow(tr("Preview Budget"), m_previewBudget);
    layout->addWidget(modelGroup);

    auto *targetButtons = new QHBoxLayout();
    m_addTargetsButton = new QPushButton(tr("Add Targets"), this);
    m_addTargetsButton->setToolTip(tr("Add meshes from the current scene selection as scatter surfaces"));
    m_pickTargetButton = new QPushButton(tr("Pick Target"), this);
    m_pickTargetButton->setToolTip(tr("Click a viewport surface to add it to this scatter model"));
    m_cancelPickButton = new QPushButton(tr("Cancel Pick"), this);
    m_cancelPickButton->setToolTip(tr("Stop viewport scatter-target picking"));
    targetButtons->addWidget(m_addTargetsButton);
    targetButtons->addWidget(m_pickTargetButton);
    targetButtons->addWidget(m_cancelPickButton);
    layout->addLayout(targetButtons);

    auto *objectButtons = new QHBoxLayout();
    m_addObjectsButton = new QPushButton(tr("Add Objects"), this);
    m_addObjectsButton->setToolTip(tr("Use meshes from the current scene selection as scatter prototypes"));
    m_cleanupObjectsButton = new QPushButton(tr("Clean"), this);
    m_cleanupObjectsButton->setToolTip(
        tr("Drop scatter prototypes whose meshes are gone, and scatter "
           "targets whose source nodes were deleted."));
    m_bakeToNodesButton = new QPushButton(tr("Bake to Nodes"), this);
    m_bakeToNodesButton->setToolTip(
        tr("Flatten the current scatter into real scene nodes (one node per "
           "instance). Use after authoring is final; disables the source "
           "scatter model so render output doesn't double up."));
    objectButtons->addWidget(m_addObjectsButton);
    objectButtons->addWidget(m_cleanupObjectsButton);
    objectButtons->addWidget(m_bakeToNodesButton);
    layout->addLayout(objectButtons);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);

    auto *targetGroup = new QGroupBox(tr("Targets"), this);
    auto *targetLayout = new QVBoxLayout(targetGroup);
    m_targetList = new QListWidget(targetGroup);
    m_targetList->setMinimumHeight(80);
    m_removeTargetButton = new QPushButton(tr("Remove Target"), targetGroup);
    auto *targetForm = new QFormLayout();
    targetForm->setContentsMargins(0, 0, 0, 0);
    m_targetEnabled = new QCheckBox(tr("Enabled"), targetGroup);
    m_targetWeight = CreateDoubleSpin(0.0, 100.0, 0.1, 2);
    targetLayout->addWidget(m_targetList);
    targetForm->addRow(tr("State"), m_targetEnabled);
    targetForm->addRow(tr("Weight"), m_targetWeight);
    targetLayout->addLayout(targetForm);
    targetLayout->addWidget(m_removeTargetButton);
    m_tabs->addTab(targetGroup, tr("Targets"));

    auto *objectGroup = new QGroupBox(tr("Objects"), this);
    auto *objectLayout = new QVBoxLayout(objectGroup);
    m_objectList = new QListWidget(objectGroup);
    m_objectList->setMinimumHeight(80);
    m_removeObjectButton = new QPushButton(tr("Remove Object"), objectGroup);
    m_hideSourceButton = new QPushButton(tr("Hide Source"), objectGroup);
    m_hideSourceButton->setToolTip(tr("Hide or show the original scene nodes used as this prototype library source"));
    objectLayout->addWidget(m_objectList);
    objectLayout->addWidget(m_hideSourceButton);
    objectLayout->addWidget(m_removeObjectButton);
    m_tabs->addTab(objectGroup, tr("Objects"));

    // B6: Placement tab broken into labelled QGroupBox sections so the user
    // isn't staring at a wall of 20 spinboxes.
    auto *placementHost = new QWidget(this);
    auto *placementLayout = new QVBoxLayout(placementHost);
    placementLayout->setContentsMargins(0, 0, 0, 0);
    placementLayout->setSpacing(6);

    // Identity ----------------------------------------------------------------
    auto *identityGroup = new QGroupBox(tr("Identity"), placementHost);
    auto *identityForm = new QFormLayout(identityGroup);
    m_objectName = new QLineEdit(identityGroup);
    m_objectEnabled = new QCheckBox(tr("Enabled"), identityGroup);
    m_objectEnabled->setToolTip(
        tr("Disable to skip this prototype without losing settings."));
    identityForm->addRow(tr("Name"), m_objectName);
    identityForm->addRow(tr("State"), m_objectEnabled);
    placementLayout->addWidget(identityGroup);

    // Density / caps ----------------------------------------------------------
    auto *densityGroup = new QGroupBox(tr("Density && Caps"), placementHost);
    auto *densityForm = new QFormLayout(densityGroup);
    m_density = CreateDoubleSpin(0.0, 10000.0, 0.5, 2, true);
    m_density->setToolTip(tr("Target instances per square meter of surface."));
    m_weight = CreateDoubleSpin(0.0, 100.0, 0.1, 2);
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
    placementLayout->addWidget(densityGroup);

    // Scale -------------------------------------------------------------------
    auto *scaleGroup = new QGroupBox(tr("Scale"), placementHost);
    auto *scaleForm = new QFormLayout(scaleGroup);
    m_minScale = CreateDoubleSpin(0.001, 1000.0, 0.05, 3, true);
    m_maxScale = CreateDoubleSpin(0.001, 1000.0, 0.05, 3, true);
    scaleForm->addRow(tr("Min Scale"), m_minScale);
    scaleForm->addRow(tr("Max Scale"), m_maxScale);
    placementLayout->addWidget(scaleGroup);

    // Rotation ----------------------------------------------------------------
    auto *rotationGroup = new QGroupBox(tr("Rotation"), placementHost);
    auto *rotationForm = new QFormLayout(rotationGroup);
    m_yaw = CreateDoubleSpin(0.0, 360.0, 5.0, 1);
    m_pitch = CreateDoubleSpin(0.0, 180.0, 1.0, 1);
    m_roll = CreateDoubleSpin(0.0, 180.0, 1.0, 1);
    m_normalAlign = CreateDoubleSpin(0.0, 1.0, 0.05, 2);
    m_normalAlign->setToolTip(
        tr("0 = align to world up. 1 = align to surface normal."));
    rotationForm->addRow(tr("Yaw Random"), m_yaw);
    rotationForm->addRow(tr("Pitch Random"), m_pitch);
    rotationForm->addRow(tr("Roll Random"), m_roll);
    rotationForm->addRow(tr("Normal Align"), m_normalAlign);
    placementLayout->addWidget(rotationGroup);

    // Slope & height filter ---------------------------------------------------
    auto *filterGroup = new QGroupBox(tr("Surface Filter"), placementHost);
    auto *filterForm = new QFormLayout(filterGroup);
    m_slopeMin = CreateDoubleSpin(0.0, 89.0, 1.0, 1);
    m_slopeMax = CreateDoubleSpin(0.0, 89.0, 1.0, 1);
    // B1: heightMin / heightMax controls (data existed in ScatterObject,
    // the panel never exposed them).
    m_heightMin = CreateDoubleSpin(-100000.0, 100000.0, 1.0, 3, true);
    m_heightMin->setToolTip(tr("Minimum world-Y at which placements are kept."));
    m_heightMax = CreateDoubleSpin(-100000.0, 100000.0, 1.0, 3, true);
    m_heightMax->setToolTip(tr("Maximum world-Y at which placements are kept."));
    filterForm->addRow(tr("Slope Min (deg)"), m_slopeMin);
    filterForm->addRow(tr("Slope Max (deg)"), m_slopeMax);
    filterForm->addRow(tr("Y Min"), m_heightMin);
    filterForm->addRow(tr("Y Max"), m_heightMax);
    placementLayout->addWidget(filterGroup);

    // Distribution ------------------------------------------------------------
    auto *distGroup = new QGroupBox(tr("Distribution"), placementHost);
    auto *distForm = new QFormLayout(distGroup);
    m_jitter = CreateDoubleSpin(0.0, 1000.0, 0.05, 3, true);
    m_edgeAvoidance = CreateDoubleSpin(0.0, 0.33, 0.01, 2);
    m_edgeAvoidance->setToolTip(
        tr("Skip placements within this fractional barycentric distance of a "
           "triangle edge. 0 = off, ~0.1 hides instances from tile seams."));
    m_collisionAvoidance = CreateDoubleSpin(0.0, 10000.0, 0.05, 3, true);
    m_collisionAvoidance->setToolTip(
        tr("Minimum world-space distance between accepted instances within "
           "this prototype. 0 = off. Per-object (does not see other "
           "prototypes)."));
    // D5: avoid scene lights.
    m_avoidLightRadius = CreateDoubleSpin(0.0, 10000.0, 0.1, 3, true);
    m_avoidLightRadius->setToolTip(
        tr("Skip placements within this radius of any enabled scene light. "
           "Useful for 'don't grow grass under lamps' patterns. 0 = off."));
    distForm->addRow(tr("Jitter (m)"), m_jitter);
    distForm->addRow(tr("Edge Avoid"), m_edgeAvoidance);
    distForm->addRow(tr("Avoid Collision (m)"), m_collisionAvoidance);
    distForm->addRow(tr("Avoid Lights (m)"), m_avoidLightRadius);
    placementLayout->addWidget(distGroup);

    // Clumping ----------------------------------------------------------------
    auto *clumpGroup = new QGroupBox(tr("Clumping"), placementHost);
    auto *clumpForm = new QFormLayout(clumpGroup);
    m_clumpScale = CreateDoubleSpin(0.0, 10000.0, 0.25, 2, true);
    m_clumpScale->setToolTip(
        tr("World-space spatial scale of clump noise. 0 = uniform."));
    m_clumpStrength = CreateDoubleSpin(0.0, 1.0, 0.05, 2);
    m_clumpStrength->setToolTip(
        tr("How aggressively to thin out non-clump regions. 0 = uniform, "
           "1 = full noise mask."));
    clumpForm->addRow(tr("Clump Scale"), m_clumpScale);
    clumpForm->addRow(tr("Clump Strength"), m_clumpStrength);
    placementLayout->addWidget(clumpGroup);

    // Camera / fade -----------------------------------------------------------
    auto *cameraGroup = new QGroupBox(tr("Camera Fade"), placementHost);
    auto *cameraForm = new QFormLayout(cameraGroup);
    m_minDistance = CreateDoubleSpin(0.0, 100000.0, 1.0, 2, true);
    m_minDistance->setToolTip(
        tr("Skip placements closer than this distance from the camera."));
    m_maxDistance = CreateDoubleSpin(0.0, 100000.0, 1.0, 2, true);
    m_maxDistance->setToolTip(
        tr("Skip placements further than this distance from the camera. "
           "0 = no max."));
    // D7: smooth fade inside maxDistance.
    m_distanceFade = CreateDoubleSpin(0.0, 100000.0, 1.0, 2, true);
    m_distanceFade->setToolTip(
        tr("Soft falloff width inside Max Distance. Density ramps from "
           "full at (max - fade) to zero at max. 0 = hard cutoff."));
    cameraForm->addRow(tr("Min Distance"), m_minDistance);
    cameraForm->addRow(tr("Max Distance"), m_maxDistance);
    cameraForm->addRow(tr("Fade Width"), m_distanceFade);
    placementLayout->addWidget(cameraGroup);

    placementLayout->addStretch(1);
    m_tabs->addTab(placementHost, tr("Placement"));

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
    connect(m_targetEnabled, &QCheckBox::toggled, this, [this](bool) { applyTargetEdit(); });
    connect(m_targetWeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { applyTargetEdit(); });
    connect(m_addTargetsButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterTargets(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_pickTargetButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::SetScatterPickTarget(static_cast<size_t>(row));
            syncInspector();
        }
    });
    connect(m_cancelPickButton, &QPushButton::clicked, this, [this]() {
        Scene::CancelScatterPick();
        syncInspector();
    });
    connect(m_addObjectsButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::AddSelectedNodesAsScatterObjects(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_cleanupObjectsButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row >= 0) {
            Scene::RemoveUnusedScatterObjects(static_cast<size_t>(row));
            refreshUi();
        }
    });
    connect(m_bakeToNodesButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedModelIndex();
        if (row < 0) {
            return;
        }
        const size_t created =
            Scene::BakeScatterModelToNodes(static_cast<size_t>(row));
        if (created > 0) {
            // Disable the source model so the baked nodes don't double-render.
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
    connect(m_hideSourceButton, &QPushButton::clicked, this, [this]() {
        const int modelRow = selectedModelIndex();
        const int objectRow = selectedObjectIndex();
        const auto &models = Scene::GetScatterModels();
        if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
            objectRow < 0 ||
            objectRow >= static_cast<int>(models[static_cast<size_t>(modelRow)].objects.size())) {
            return;
        }
        const Scene::ScatterObject &object =
            models[static_cast<size_t>(modelRow)].objects[static_cast<size_t>(objectRow)];
        Scene::SetScatterObjectSourcesHidden(static_cast<size_t>(modelRow),
                                             static_cast<size_t>(objectRow),
                                             !object.librarySourceHidden);
        refreshUi();
    });

    connect(m_modelName, &QLineEdit::editingFinished, this, [this]() { applyModelEdit(); });
    connect(m_modelEnabled, &QCheckBox::toggled, this, [this](bool) { applyModelEdit(); });
    connect(m_modelSeed, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applyModelEdit(); });
    connect(m_previewDensityScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { applyModelEdit(); });
    connect(m_previewBudget, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { applyModelEdit(); });
    connect(m_reseedButton, &QPushButton::clicked, this, [this]() {
        // B3: stay inside QSpinBox int range; bumping bits 31..63 wraps to
        // negative and the spinbox clamps to 1, which would feel broken.
        const int randomSeed = static_cast<int>(
            QRandomGenerator::global()->bounded(1, std::numeric_limits<int>::max()));
        m_modelSeed->setValue(randomSeed);
    });

    auto objectEdit = [this]() { applyObjectEdit(); };
    connect(m_objectName, &QLineEdit::editingFinished, this, objectEdit);
    connect(m_objectEnabled, &QCheckBox::toggled, this, [objectEdit](bool) { objectEdit(); });
    connect(m_density, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_weight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_maxInstances, qOverload<int>(&QSpinBox::valueChanged), this, [objectEdit](int) { objectEdit(); });
    connect(m_previewMaxInstances, qOverload<int>(&QSpinBox::valueChanged), this, [objectEdit](int) { objectEdit(); });
    connect(m_minScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_maxScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_yaw, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_pitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_roll, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_normalAlign, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_slopeMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_slopeMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_jitter, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_heightMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_heightMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_minDistance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_maxDistance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_distanceFade, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_avoidLightRadius, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_clumpScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_clumpStrength, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_edgeAvoidance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
    connect(m_collisionAvoidance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [objectEdit](double) { objectEdit(); });
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
    m_pickTargetButton->setEnabled(hasModel);
    // B5: visual indicator of pick mode. Tells the user the next viewport
    // click will land a scatter target and dims the rest of the model so it
    // doesn't look like a regular toggle button.
    const bool picking = Scene::IsScatterPickingTarget();
    m_pickTargetButton->setText(picking ? tr("Picking\xe2\x80\xa6 click viewport")
                                        : tr("Pick Target"));
    m_pickTargetButton->setEnabled(hasModel && !picking);
    m_cancelPickButton->setEnabled(picking);
    m_addObjectsButton->setEnabled(hasModel);
    m_cleanupObjectsButton->setEnabled(hasModel);
    m_bakeToNodesButton->setEnabled(hasModel);
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
        m_statsLabel->setText(tr("Add a scatter model, then select surface nodes and prototype object nodes."));
    } else {
        const Scene::ScatterModel &model = models[static_cast<size_t>(modelRow)];
        m_modelName->setText(QString::fromStdString(model.name));
        m_modelEnabled->setChecked(model.enabled);
        m_modelSeed->setValue(static_cast<int>(std::max(1u, model.seed)));
        m_previewDensityScale->setValue(std::clamp(model.previewDensityScale, 0.0f, 1.0f));
        m_previewBudget->setValue(static_cast<int>(std::min<uint32_t>(model.previewInstanceBudget, 2000000u)));

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
        if (!model.targets.empty()) {
            m_targetList->setCurrentRow(std::clamp(previousTargetRow, 0, static_cast<int>(model.targets.size()) - 1));
        }
        if (!model.objects.empty()) {
            m_objectList->setCurrentRow(std::clamp(previousObjectRow, 0, static_cast<int>(model.objects.size()) - 1));
        }
        const Scene::ScatterRuntimeStats stats = Scene::GetScatterRuntimeStats();
        QString headline = tr("Generated %1 instances  \xe2\x80\xa2  "
                              "%2 targets active  \xe2\x80\xa2  "
                              "%3 objects active")
                               .arg(static_cast<qulonglong>(stats.generatedInstances))
                               .arg(static_cast<unsigned>(stats.activeTargets))
                               .arg(static_cast<unsigned>(stats.activeObjects));
        // A2: distinguish skipped-by-model-budget from skipped-by-object-cap
        // so the user knows which knob to raise.
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
            headline += tr("  \xe2\x80\xa2  Skipped: %1").arg(skipped.join(", "));
        }
        // B9: per-object breakdown (top entries by instance count).
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
            headline += "\n" + rows.join("  \xe2\x80\xa2  ");
        }
        m_statsLabel->setText(headline);
    }

    const int objectRow = selectedObjectIndex();
    const bool hasObject =
        hasModel && objectRow >= 0 &&
        objectRow < static_cast<int>(models[static_cast<size_t>(modelRow)].objects.size());
    m_removeTargetButton->setEnabled(hasModel && m_targetList->currentRow() >= 0);
    const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
    const bool hasTarget =
        hasModel && targetRow >= 0 &&
        targetRow < static_cast<int>(models[static_cast<size_t>(modelRow)].targets.size());
    m_targetEnabled->setEnabled(hasTarget);
    m_targetWeight->setEnabled(hasTarget);
    if (hasTarget) {
        const Scene::ScatterTarget &target =
            models[static_cast<size_t>(modelRow)].targets[static_cast<size_t>(targetRow)];
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
            models[static_cast<size_t>(modelRow)].objects[static_cast<size_t>(objectRow)];
        m_hideSourceButton->setText(object.librarySourceHidden ? tr("Show Source") : tr("Hide Source"));
        m_objectName->setText(QString::fromStdString(object.name));
        m_objectEnabled->setChecked(object.enabled);
        m_density->setValue(object.densityPerSquareMeter);
        m_weight->setValue(object.weight);
        m_maxInstances->setValue(static_cast<int>(std::min<uint32_t>(object.maxInstances, 2000000u)));
        m_previewMaxInstances->setValue(static_cast<int>(std::min<uint32_t>(object.previewMaxInstances, 2000000u)));
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
        m_hideSourceButton->setText(tr("Hide Source"));
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
    if (m_syncing) {
        return;
    }
    const int row = selectedModelIndex();
    const auto &models = Scene::GetScatterModels();
    if (row < 0 || row >= static_cast<int>(models.size())) {
        return;
    }
    // Header-only update — no mesh set change, so use the lite path that
    // requests a TlasRefresh instead of a full AS rebuild on every tick.
    Scene::ScatterModel header = models[static_cast<size_t>(row)];
    header.name = m_modelName->text().toStdString();
    header.enabled = m_modelEnabled->isChecked();
    header.seed = static_cast<uint32_t>(std::max(1, m_modelSeed->value()));
    header.previewDensityScale =
        std::clamp(static_cast<float>(m_previewDensityScale->value()), 0.0f, 1.0f);
    header.previewInstanceBudget =
        static_cast<uint32_t>(std::max(0, m_previewBudget->value()));
    Scene::UpdateScatterModelHeader(static_cast<size_t>(row), header);
}

void ScatterPanel::applyTargetEdit()
{
    if (m_syncing) {
        return;
    }
    const int modelRow = selectedModelIndex();
    const int targetRow = m_targetList ? m_targetList->currentRow() : -1;
    const auto &models = Scene::GetScatterModels();
    if (modelRow < 0 || modelRow >= static_cast<int>(models.size()) ||
        targetRow < 0 ||
        targetRow >= static_cast<int>(models[static_cast<size_t>(modelRow)].targets.size())) {
        return;
    }
    Scene::ScatterTarget target =
        models[static_cast<size_t>(modelRow)].targets[static_cast<size_t>(targetRow)];
    target.enabled = m_targetEnabled->isChecked();
    target.weight = static_cast<float>(m_targetWeight->value());
    Scene::UpdateScatterTarget(static_cast<size_t>(modelRow),
                               static_cast<size_t>(targetRow), target);
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
    Scene::ScatterObject object =
        models[static_cast<size_t>(modelRow)].objects[static_cast<size_t>(objectRow)];
    object.name = m_objectName->text().toStdString();
    object.enabled = m_objectEnabled->isChecked();
    object.densityPerSquareMeter = static_cast<float>(m_density->value());
    object.weight = static_cast<float>(m_weight->value());
    object.maxInstances = static_cast<uint32_t>(std::max(0, m_maxInstances->value()));
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
    object.collisionAvoidanceRadius = static_cast<float>(m_collisionAvoidance->value());
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
    if (m_modelList) {
        m_modelList->setCurrentRow(row);
    }
    refreshUi();
}
