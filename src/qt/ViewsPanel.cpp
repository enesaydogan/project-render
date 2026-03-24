#include "ViewsPanel.h"

#include "../saved_views.h"

#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kViewIndexRole = Qt::UserRole;

QPixmap BuildThumbnailPixmap(const SavedViews::SavedView &view)
{
    if (view.thumbnailRgba.empty() || view.thumbnailWidth == 0 ||
        view.thumbnailHeight == 0) {
        QImage placeholder(192, 108, QImage::Format_RGBA8888);
        placeholder.fill(qRgba(40, 40, 44, 255));
        return QPixmap::fromImage(placeholder);
    }

    QImage image(view.thumbnailRgba.data(),
                 static_cast<int>(view.thumbnailWidth),
                 static_cast<int>(view.thumbnailHeight),
                 static_cast<int>(view.thumbnailWidth * 4),
                 QImage::Format_RGBA8888);
    return QPixmap::fromImage(image.copy());
}

} // namespace

ViewsPanel::ViewsPanel(QWidget *parent)
    : QWidget(parent)
{
    createUi();
    syncFromViews();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        syncFromViews();
    });
    m_refreshTimer->start(500);
}

void ViewsPanel::createUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *buttonRow = new QHBoxLayout();
    m_createButton = new QPushButton(tr("Create View"), this);
    m_deleteButton = new QPushButton(tr("Delete View"), this);
    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_deleteButton);
    layout->addLayout(buttonRow);

    m_viewList = new QListWidget(this);
    m_viewList->setIconSize(QSize(192, 108));
    m_viewList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_viewList->setUniformItemSizes(false);
    layout->addWidget(m_viewList, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        const QString defaultName = tr("View %1")
                                        .arg(static_cast<int>(SavedViews::GetViews().size()) + 1);
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, tr("Create View"), tr("View Name"), QLineEdit::Normal,
            defaultName, &accepted);
        if (!accepted) {
            return;
        }
        const QString trimmed = name.trimmed();
        SavedViews::AddCurrentView(trimmed.isEmpty()
                                       ? std::string()
                                       : trimmed.toUtf8().constData());
        syncFromViews();
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const int index = selectedViewIndex();
        if (index >= 0) {
            SavedViews::RemoveView(static_cast<size_t>(index));
            syncFromViews();
        }
    });
    connect(m_viewList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
        if (m_syncing || !current) {
            return;
        }
        const QVariant viewIndex = current->data(kViewIndexRole);
        if (!viewIndex.isValid()) {
            return;
        }
        SavedViews::ApplyView(static_cast<size_t>(viewIndex.toInt()));
        syncFromViews();
    });
}

int ViewsPanel::selectedViewIndex() const
{
    if (!m_viewList || !m_viewList->currentItem()) {
        return -1;
    }
    const QVariant viewIndex = m_viewList->currentItem()->data(kViewIndexRole);
    return viewIndex.isValid() ? viewIndex.toInt() : -1;
}

void ViewsPanel::syncFromViews()
{
    m_syncing = true;

    const int previousSelection = selectedViewIndex();
    const auto &views = SavedViews::GetViews();

    m_viewList->clear();
    QListWidgetItem *selectedItem = nullptr;
    for (size_t index = 0; index < views.size(); ++index) {
        const auto &view = views[index];
        auto *item = new QListWidgetItem(BuildThumbnailPixmap(view),
                                         QString::fromUtf8(view.name.c_str()),
                                         m_viewList);
        item->setData(kViewIndexRole, static_cast<int>(index));
        item->setSizeHint(QSize(220, 124));
        if (static_cast<int>(index) == previousSelection) {
            selectedItem = item;
        }
    }

    if (selectedItem) {
        m_viewList->setCurrentItem(selectedItem);
    } else {
        m_viewList->clearSelection();
    }

    m_deleteButton->setEnabled(selectedViewIndex() >= 0);
    if (views.empty()) {
        m_statusLabel->setText(tr("No saved views. Create one from the current viewport."));
    } else {
        m_statusLabel->setText(
            tr("%1 saved views\n%2")
                .arg(static_cast<int>(views.size()))
                .arg(QString::fromUtf8(SavedViews::GetLastStatus().c_str())));
    }

    m_syncing = false;
}