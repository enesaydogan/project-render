#pragma once

#include <QWidget>

#include "../asset_library/asset_id.h"

#include <cstddef>
#include <string>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QTreeWidget;
class QTreeWidgetItem;

namespace assetlib {
class AssetRegistry;
}

// Phase 1 Asset Manager browser. Browses the writable User library held by the
// global assetlib::AssetRegistry: source/folder tree on the left, a thumbnail
// grid in the center, and an inspector on the right. Cooking, .prpak, and
// drag-into-scene are later phases; drag-out is intentionally not yet wired.
class AssetManagerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetManagerPanel(QWidget *parent = nullptr);
    ~AssetManagerPanel() override;

private:
    enum class ViewMode { All, Favorites, Recent, Missing };

    void createUi();
    void refreshFolderTree();
    void refreshGrid();
    void refreshInspector();

    std::string selectedFolder() const;  // "" == root / no folder selected
    bool selectedAssetId(assetlib::AssetId &out) const;

    void onAddAsset();
    void onNewFolder();
    void onRenameFolder();
    void onDeleteFolder();
    void onGridContextMenu(const QPoint &pos);
    void onFolderContextMenu(const QPoint &pos);

    assetlib::AssetRegistry *m_registry = nullptr;
    size_t m_changeListenerId = 0;
    bool m_refreshing = false;
    ViewMode m_viewMode = ViewMode::All;

    // Left
    QTreeWidget *m_sourceTree = nullptr;

    // Center
    QLineEdit *m_search = nullptr;
    QComboBox *m_typeFilter = nullptr;
    QPushButton *m_viewAllButton = nullptr;
    QPushButton *m_viewFavoritesButton = nullptr;
    QPushButton *m_viewRecentButton = nullptr;
    QPushButton *m_viewMissingButton = nullptr;
    QSlider *m_thumbSize = nullptr;
    QListWidget *m_grid = nullptr;

    // Right (inspector)
    QLabel *m_inspName = nullptr;
    QLabel *m_inspType = nullptr;
    QLabel *m_inspId = nullptr;
    QLabel *m_inspFolder = nullptr;
    QLabel *m_inspSource = nullptr;
    QLabel *m_inspCook = nullptr;
    QLabel *m_inspDeps = nullptr;
    QLabel *m_inspTags = nullptr;
    QLabel *m_inspLicense = nullptr;
    QPushButton *m_revealButton = nullptr;
    QPushButton *m_favoriteButton = nullptr;
};
