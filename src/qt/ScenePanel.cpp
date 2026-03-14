#include "ScenePanel.h"

#include "../editor_ui.h"
#include "../scene.h"

#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

extern HWND g_hwnd;

ScenePanel::ScenePanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    refreshSceneList();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        refreshSceneList();
    });
    m_refreshTimer->start(500);
}

void ScenePanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    m_importProgress = new QProgressBar(this);
    m_importProgress->setRange(0, 100);
    m_importProgress->setTextVisible(false);
    m_importProgress->hide();
    layout->addWidget(m_importProgress);

    m_importStatusLabel = new QLabel(this);
    m_importStatusLabel->setWordWrap(true);
    m_importStatusLabel->hide();
    layout->addWidget(m_importStatusLabel);

    auto *buttonRow = new QHBoxLayout();
    m_importButton = new QPushButton(tr("Import Model"), this);
    m_reimportButton = new QPushButton(tr("Reimport Selected"), this);
    m_addPlaneButton = new QPushButton(tr("Add Ground Plane"), this);
    m_deleteButton = new QPushButton(tr("Delete Selected"), this);
    buttonRow->addWidget(m_importButton);
    buttonRow->addWidget(m_reimportButton);
    buttonRow->addWidget(m_addPlaneButton);
    buttonRow->addWidget(m_deleteButton);
    layout->addLayout(buttonRow);

    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextFormat(Qt::RichText);
    m_sourceLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_sourceLabel->setOpenExternalLinks(false);
    layout->addWidget(m_sourceLabel);

    m_nodeList = new QListWidget(this);
    layout->addWidget(m_nodeList);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    connect(m_importButton, &QPushButton::clicked, this, []() {
        HWND owner = g_hwnd ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
        if (!owner) {
            owner = g_hwnd;
        }
        Scene::ImportModelWithDialog(owner);
    });
    connect(m_reimportButton, &QPushButton::clicked, this, [this]() {
        const int row = m_nodeList->currentRow();
        if (row >= 0) {
            Scene::ReimportNode(static_cast<size_t>(row));
            refreshSceneList();
        }
    });
    connect(m_addPlaneButton, &QPushButton::clicked, this, []() {
        Scene::AddDefaultPlane(0.0f);
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const int row = m_nodeList->currentRow();
        if (row >= 0) {
            Scene::DeleteNode(static_cast<size_t>(row));
            refreshSceneList();
        }
    });
    connect(m_nodeList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_syncing || row < 0) {
            return;
        }
        Scene::SelectNode(static_cast<size_t>(row));
    });
    connect(m_sourceLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link != QStringLiteral("reimport")) {
            return;
        }
        const int row = m_nodeList->currentRow();
        if (row >= 0) {
            Scene::ReimportNode(static_cast<size_t>(row));
            refreshSceneList();
        }
    });
}

void ScenePanel::refreshSceneList()
{
    m_syncing = true;

    if (IsSceneLoadInProgress()) {
        m_importProgress->hide();
        m_importStatusLabel->hide();
        m_importButton->setEnabled(false);
        m_addPlaneButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
        m_nodeList->setEnabled(false);
        m_statusLabel->setText(tr("Scene load in progress..."));
        m_syncing = false;
        return;
    }

    Scene::ProcessPendingImport();

    m_importButton->setEnabled(true);
    m_addPlaneButton->setEnabled(true);
    m_nodeList->setEnabled(true);

    const bool importing = Scene::IsImportInProgress();
    if (importing) {
        float progress = Scene::GetImportProgress();
        if (progress < 0.0f) {
            progress = 0.0f;
        } else if (progress > 1.0f) {
            progress = 1.0f;
        }
        m_importProgress->setValue(static_cast<int>(progress * 100.0f + 0.5f));
        m_importStatusLabel->setText(
            QString::fromStdString(Scene::GetImportStatus()));
        m_importProgress->show();
        m_importStatusLabel->show();
    } else {
        m_importProgress->hide();
        m_importStatusLabel->hide();
    }

    const auto &nodes = Scene::GetNodes();
    int selectedRow = -1;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].selected) {
            selectedRow = static_cast<int>(index);
            break;
        }
    }

    m_nodeList->clear();
    for (const auto &node : nodes) {
        QString label = QString::fromStdString(node.name);
        label += tr(" (%1 meshes)").arg(static_cast<int>(node.meshIndices.size()));
        if (!node.visible) {
            label += tr(" [hidden]");
        }
        m_nodeList->addItem(label);
    }

    if (selectedRow >= 0 && selectedRow < m_nodeList->count()) {
        m_nodeList->setCurrentRow(selectedRow);
    }

    if (selectedRow >= 0 && selectedRow < static_cast<int>(nodes.size()) &&
        Scene::CanReimportNode(static_cast<size_t>(selectedRow))) {
        const QString sourcePath = QString::fromStdString(nodes[static_cast<size_t>(selectedRow)].sourcePath);
        const QString fileName = QFileInfo(sourcePath).fileName().toHtmlEscaped();
        m_sourceLabel->setText(tr("Linked source: <a href=\"reimport\">%1</a>").arg(fileName));
        m_sourceLabel->setToolTip(sourcePath);
        m_sourceLabel->show();
        m_reimportButton->setEnabled(!importing);
    } else {
        m_sourceLabel->hide();
        m_sourceLabel->clear();
        m_reimportButton->setEnabled(false);
    }

    m_statusLabel->setText(
        tr("Nodes: %1\nLights: %2\nStatus: %3")
            .arg(static_cast<int>(nodes.size()))
            .arg(static_cast<int>(Scene::GetLights().size()))
            .arg(QString::fromStdString(Scene::LastStatus())));

    m_deleteButton->setEnabled(m_nodeList->currentRow() >= 0);
    m_syncing = false;
}
