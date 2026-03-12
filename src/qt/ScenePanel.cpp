#include "ScenePanel.h"

#include "../scene.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
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

    auto *buttonRow = new QHBoxLayout();
    m_importButton = new QPushButton(tr("Import Model"), this);
    m_addPlaneButton = new QPushButton(tr("Add Ground Plane"), this);
    m_deleteButton = new QPushButton(tr("Delete Selected"), this);
    buttonRow->addWidget(m_importButton);
    buttonRow->addWidget(m_addPlaneButton);
    buttonRow->addWidget(m_deleteButton);
    layout->addLayout(buttonRow);

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
}

void ScenePanel::refreshSceneList()
{
    m_syncing = true;

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

    m_statusLabel->setText(
        tr("Nodes: %1\nLights: %2\nStatus: %3")
            .arg(static_cast<int>(nodes.size()))
            .arg(static_cast<int>(Scene::GetLights().size()))
            .arg(QString::fromStdString(Scene::LastStatus())));

    m_deleteButton->setEnabled(m_nodeList->currentRow() >= 0);
    m_syncing = false;
}