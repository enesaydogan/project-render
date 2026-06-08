#include "AssetManagerPanel.h"

#include "../asset_library/asset_metadata.h"
#include "../asset_library/asset_registry.h"
#include "../asset_library/cook_jobs.h"
#include "../asset_library/global_registry.h"
#include "../asset_library/import_hook.h"
#include "../asset_library/thumbnail_cache.h"
#include "asset_mime.h"

#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QTimer>

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>

using assetlib::AssetId;
using assetlib::AssetMetadata;
using assetlib::AssetQuery;
using assetlib::AssetRegistry;
using assetlib::AssetType;

namespace {

constexpr int kUserRoleId = Qt::UserRole + 1;   // AssetId hex string
constexpr int kUserRolePath = Qt::UserRole + 2; // folder virtualPath
constexpr int kUserRoleDrag = Qt::UserRole + 3; // drag payload "type:hex"
constexpr int kUserRoleCooking = Qt::UserRole + 4; // bool: cook in progress

// QListWidget that exposes a dragged asset as our shared MIME type so the
// viewport / other panels can accept it.
class AssetGridList : public QListWidget {
public:
  using QListWidget::QListWidget;

protected:
  QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {
    QMimeData *md = QListWidget::mimeData(items);
    if (!md)
      md = new QMimeData();
    if (!items.isEmpty()) {
      const QString payload = items.first()->data(kUserRoleDrag).toString();
      if (!payload.isEmpty())
        md->setData(kAssetMimeType, payload.toUtf8());
    }
    return md;
  }
};

// Accent color per asset type for the placeholder thumbnail.
QColor TypeColor(AssetType t) {
  switch (t) {
  case AssetType::Model:
    return QColor(0x4f, 0x8a, 0xc4);
  case AssetType::Material:
    return QColor(0xc4, 0x7f, 0x4f);
  case AssetType::Texture:
    return QColor(0x8a, 0xc4, 0x4f);
  case AssetType::ScatterObject:
  case AssetType::ScatterPreset:
    return QColor(0x6f, 0xc4, 0x8a);
  case AssetType::CloudVolume:
  case AssetType::CloudPreset:
    return QColor(0x9a, 0xa6, 0xc4);
  case AssetType::Hdri:
    return QColor(0xc4, 0xb0, 0x4f);
  case AssetType::EnvironmentPreset:
    return QColor(0x8a, 0x6f, 0xc4);
  default:
    return QColor(0x6a, 0x70, 0x74);
  }
}

QPixmap PlaceholderThumb(AssetType t, int size) {
  QPixmap pm(size, size);
  pm.fill(QColor(0x22, 0x26, 0x28));
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  QColor c = TypeColor(t);
  p.setBrush(c);
  p.setPen(Qt::NoPen);
  int m = size / 6;
  p.drawRoundedRect(m, m, size - 2 * m, size - 2 * m, size / 12, size / 12);
  // Type initial.
  p.setPen(QColor(0x10, 0x12, 0x13));
  QFont f = p.font();
  f.setBold(true);
  f.setPixelSize(size / 3);
  p.setFont(f);
  const char *name = assetlib::AssetTypeDisplayName(t);
  p.drawText(pm.rect(), Qt::AlignCenter, QString(QChar(name[0])));
  p.end();
  return pm;
}

// Draw a rotating arc "cooking" spinner over the lower-right of a thumbnail.
void DrawSpinnerOverlay(QPixmap &pm, int frame) {
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  // Darken the whole thumbnail slightly so the spinner reads as "busy".
  p.fillRect(pm.rect(), QColor(0, 0, 0, 90));
  const int d = qMax(16, pm.width() / 2);
  const QRectF arc((pm.width() - d) / 2.0, (pm.height() - d) / 2.0, d, d);
  const int pen = qMax(2, d / 10);
  p.setPen(QPen(QColor(0xff, 0xff, 0xff, 60), pen));
  p.drawArc(arc, 0, 360 * 16);
  p.setPen(QPen(QColor(0x58, 0xd0, 0xf4), pen, Qt::SolidLine, Qt::RoundCap));
  // Qt angles are in 1/16 degree; sweep 270° rotated by the frame.
  p.drawArc(arc, (90 - frame * 12) * 16, -270 * 16);
  p.end();
}

// Compose the final grid icon: real thumbnail (if any) or typed placeholder,
// dimmed when missing, with a spinner overlay while cooking.
QPixmap ComposeThumb(const QString &thumbPath, AssetType type, int sz,
                     bool missing, bool cooking, int spinnerFrame) {
  QPixmap pm;
  if (!thumbPath.isEmpty())
    pm.load(thumbPath);
  if (pm.isNull())
    pm = PlaceholderThumb(type, sz);
  else
    pm = pm.scaled(sz, sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  if (missing) {
    QPixmap dimmed(pm.size());
    dimmed.fill(Qt::transparent);
    QPainter p(&dimmed);
    p.setOpacity(0.4);
    p.drawPixmap(0, 0, pm);
    p.end();
    pm = dimmed;
  }
  if (cooking)
    DrawSpinnerOverlay(pm, spinnerFrame);
  return pm;
}

// For image-like assets, generate (and cache) a real preview from the source
// file. Models need a GPU render pass (a later phase) and stay placeholder.
// Returns the cached thumbnail path, or empty if none could be produced.
QString EnsureThumbnail(const AssetId &id, const AssetMetadata *m,
                        const assetlib::ThumbnailCache &thumbs) {
  const QString path = QString::fromStdString(thumbs.PathFor(id).string());
  if (thumbs.Has(id))
    return path;
  if (!m || m->sourcePath.empty())
    return {};
  if (m->type != AssetType::Texture && m->type != AssetType::Hdri)
    return {};
  QImage img(QString::fromStdString(m->sourcePath));
  if (img.isNull()) // e.g. exr/dds/tga that Qt can't decode
    return {};
  const QImage scaled =
      img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (!scaled.save(path, "PNG"))
    return {};
  return path;
}

} // namespace

AssetManagerPanel::AssetManagerPanel(QWidget *parent) : QWidget(parent) {
  m_registry = assetlib::GlobalRegistry();
  createUi();
  if (m_registry) {
    m_changeListenerId = m_registry->AddChangeListener([this]() {
      // Registry mutations happen on the UI thread; refresh inline but guard
      // against re-entrancy from refreshes that themselves mutate nothing.
      if (m_refreshing)
        return;
      refreshFolderTree();
      refreshGrid();
      refreshInspector();
    });
  }
  refreshFolderTree();
  refreshGrid();
  refreshInspector();
}

AssetManagerPanel::~AssetManagerPanel() {
  if (m_registry && m_changeListenerId)
    m_registry->RemoveChangeListener(m_changeListenerId);
}

void AssetManagerPanel::createUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(6);

  // Top toolbar: search + type filter + view-mode buttons + add.
  auto *toolbar = new QHBoxLayout();
  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Search name, tag, folder, attribution…"));
  connect(m_search, &QLineEdit::textChanged, this,
          [this]() { refreshGrid(); });
  toolbar->addWidget(m_search, 1);

  m_typeFilter = new QComboBox(this);
  m_typeFilter->addItem(tr("All Types"), -1);
  for (uint32_t i = 1; i <= 9; ++i) {
    AssetType t = static_cast<AssetType>(i);
    m_typeFilter->addItem(assetlib::AssetTypeDisplayName(t),
                          static_cast<int>(i));
  }
  connect(m_typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this]() { refreshGrid(); });
  toolbar->addWidget(m_typeFilter);

  auto makeViewButton = [&](const QString &text, ViewMode mode) {
    auto *b = new QPushButton(text, this);
    b->setCheckable(true);
    connect(b, &QPushButton::clicked, this, [this, mode]() {
      m_viewMode = mode;
      m_viewAllButton->setChecked(mode == ViewMode::All);
      m_viewFavoritesButton->setChecked(mode == ViewMode::Favorites);
      m_viewRecentButton->setChecked(mode == ViewMode::Recent);
      m_viewMissingButton->setChecked(mode == ViewMode::Missing);
      refreshGrid();
    });
    return b;
  };
  m_viewAllButton = makeViewButton(tr("All"), ViewMode::All);
  m_viewAllButton->setChecked(true);
  m_viewFavoritesButton = makeViewButton(tr("Favorites"), ViewMode::Favorites);
  m_viewRecentButton = makeViewButton(tr("Recent"), ViewMode::Recent);
  m_viewMissingButton = makeViewButton(tr("Missing"), ViewMode::Missing);
  toolbar->addWidget(m_viewAllButton);
  toolbar->addWidget(m_viewFavoritesButton);
  toolbar->addWidget(m_viewRecentButton);
  toolbar->addWidget(m_viewMissingButton);

  auto *addButton = new QPushButton(tr("Add Asset…"), this);
  connect(addButton, &QPushButton::clicked, this,
          &AssetManagerPanel::onAddAsset);
  toolbar->addWidget(addButton);

  // Live background-cook progress indicator + per-item spinner animation.
  m_cookStatus = new QLabel(this);
  m_cookStatus->setStyleSheet("color:#808890;");
  toolbar->addWidget(m_cookStatus);
  auto *cookTimer = new QTimer(this);
  connect(cookTimer, &QTimer::timeout, this, &AssetManagerPanel::onCookTick);
  cookTimer->start(150);

  root->addLayout(toolbar);

  // Main splitter: source tree | grid | inspector.
  auto *splitter = new QSplitter(Qt::Horizontal, this);

  // Left: source + folder tree.
  m_sourceTree = new QTreeWidget(splitter);
  m_sourceTree->setHeaderHidden(true);
  m_sourceTree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_sourceTree, &QTreeWidget::itemSelectionChanged, this,
          [this]() { refreshGrid(); });
  connect(m_sourceTree, &QTreeWidget::customContextMenuRequested, this,
          &AssetManagerPanel::onFolderContextMenu);
  splitter->addWidget(m_sourceTree);

  // Center: thumbnail size slider + grid.
  auto *centerWrap = new QWidget(splitter);
  auto *centerLayout = new QVBoxLayout(centerWrap);
  centerLayout->setContentsMargins(0, 0, 0, 0);
  auto *sizeRow = new QHBoxLayout();
  sizeRow->addWidget(new QLabel(tr("Thumbnail size"), centerWrap));
  m_thumbSize = new QSlider(Qt::Horizontal, centerWrap);
  m_thumbSize->setRange(48, 160);
  m_thumbSize->setValue(96);
  connect(m_thumbSize, &QSlider::valueChanged, this,
          [this]() { refreshGrid(); });
  sizeRow->addWidget(m_thumbSize, 1);
  centerLayout->addLayout(sizeRow);

  m_grid = new AssetGridList(centerWrap);
  m_grid->setViewMode(QListView::IconMode);
  m_grid->setResizeMode(QListView::Adjust);
  m_grid->setMovement(QListView::Static);
  m_grid->setWordWrap(true);
  m_grid->setSpacing(8);
  m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
  // Drag assets out to the viewport / other panels (drop targets read
  // kAssetMimeType). The panel itself does not accept drops.
  m_grid->setDragEnabled(true);
  m_grid->setDragDropMode(QAbstractItemView::DragOnly);
  connect(m_grid, &QListWidget::itemSelectionChanged, this,
          [this]() { refreshInspector(); });
  connect(m_grid, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *item) {
            if (!m_registry || !item)
              return;
            AssetId id;
            if (AssetId::FromString(
                    item->data(kUserRoleId).toString().toStdString(), id))
              m_registry->TouchRecent(id);
          });
  connect(m_grid, &QListWidget::customContextMenuRequested, this,
          &AssetManagerPanel::onGridContextMenu);
  centerLayout->addWidget(m_grid, 1);
  splitter->addWidget(centerWrap);

  // Right: inspector.
  auto *inspWrap = new QWidget(splitter);
  auto *inspLayout = new QVBoxLayout(inspWrap);
  inspLayout->setContentsMargins(0, 0, 0, 0);
  inspLayout->setSpacing(3);
  auto addRow = [&](const QString &label) {
    auto *l = new QLabel(label, inspWrap);
    l->setStyleSheet("color:#808890;");
    inspLayout->addWidget(l);
    auto *v = new QLabel(inspWrap);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inspLayout->addWidget(v);
    return v;
  };
  m_inspName = addRow(tr("Name"));
  m_inspType = addRow(tr("Type"));
  m_inspId = addRow(tr("Asset ID"));
  m_inspFolder = addRow(tr("Folder"));
  m_inspSource = addRow(tr("Source"));
  m_inspCook = addRow(tr("Cooked"));
  m_inspDeps = addRow(tr("Dependencies"));
  m_inspTags = addRow(tr("Tags"));
  m_inspLicense = addRow(tr("License / Attribution"));

  m_favoriteButton = new QPushButton(tr("Toggle Favorite"), inspWrap);
  connect(m_favoriteButton, &QPushButton::clicked, this, [this]() {
    AssetId id;
    if (m_registry && selectedAssetId(id))
      m_registry->SetFavorite(id, !m_registry->IsFavorite(id));
  });
  inspLayout->addWidget(m_favoriteButton);

  m_revealButton = new QPushButton(tr("Open Source Location"), inspWrap);
  connect(m_revealButton, &QPushButton::clicked, this, [this]() {
    AssetId id;
    if (!m_registry || !selectedAssetId(id))
      return;
    const AssetMetadata *m = m_registry->Get(id);
    if (m && !m->sourcePath.empty()) {
      QFileInfo fi(QString::fromStdString(m->sourcePath));
      QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
  });
  inspLayout->addWidget(m_revealButton);
  inspLayout->addStretch(1);
  splitter->addWidget(inspWrap);

  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 3);
  splitter->setStretchFactor(2, 1);
  root->addWidget(splitter, 1);
}

// ---------------------------------------------------------------------------

void AssetManagerPanel::refreshFolderTree() {
  if (!m_sourceTree)
    return;
  m_refreshing = true;
  const std::string keepSelected = selectedFolder();
  m_sourceTree->clear();

  auto *builtIn = new QTreeWidgetItem(m_sourceTree, {tr("Built-in (read-only)")});
  builtIn->setDisabled(true);

  auto *user = new QTreeWidgetItem(m_sourceTree, {tr("User Library")});
  user->setData(0, kUserRolePath, QString()); // root
  user->setExpanded(true);

  // Build folder tree from the registry's flat folder set.
  if (m_registry) {
    std::map<std::string, QTreeWidgetItem *> nodes;
    nodes[""] = user;
    // std::set iterates in sorted order, so parents precede children.
    for (const std::string &path : m_registry->Folders()) {
      std::string parent;
      std::string leaf = path;
      size_t slash = path.find_last_of('/');
      if (slash != std::string::npos) {
        parent = path.substr(0, slash);
        leaf = path.substr(slash + 1);
      }
      auto pit = nodes.find(parent);
      QTreeWidgetItem *parentItem = pit != nodes.end() ? pit->second : user;
      auto *item = new QTreeWidgetItem(parentItem,
                                       {QString::fromStdString(leaf)});
      item->setData(0, kUserRolePath, QString::fromStdString(path));
      nodes[path] = item;
    }
  }

  auto *project = new QTreeWidgetItem(m_sourceTree, {tr("Project (none)")});
  project->setDisabled(true);
  auto *packs = new QTreeWidgetItem(m_sourceTree, {tr("Mounted Packs (none)")});
  packs->setDisabled(true);

  // Restore selection by stored path.
  std::function<bool(QTreeWidgetItem *)> restore =
      [&](QTreeWidgetItem *item) -> bool {
    if (!item->isDisabled() &&
        item->data(0, kUserRolePath).toString().toStdString() == keepSelected) {
      item->setSelected(true);
      return true;
    }
    for (int i = 0; i < item->childCount(); ++i)
      if (restore(item->child(i)))
        return true;
    return false;
  };
  if (!restore(user))
    user->setSelected(true);

  m_refreshing = false;
}

std::string AssetManagerPanel::selectedFolder() const {
  if (!m_sourceTree)
    return {};
  auto items = m_sourceTree->selectedItems();
  if (items.isEmpty())
    return {};
  QTreeWidgetItem *item = items.first();
  if (item->isDisabled())
    return {};
  return item->data(0, kUserRolePath).toString().toStdString();
}

void AssetManagerPanel::refreshGrid() {
  if (!m_grid || !m_registry)
    return;
  m_refreshing = true;
  m_grid->clear();

  const int sz = m_thumbSize ? m_thumbSize->value() : 96;
  m_grid->setIconSize(QSize(sz, sz));
  m_grid->setGridSize(QSize(sz + 24, sz + 40));

  // Build the candidate id list per current view mode.
  std::vector<AssetId> ids;
  switch (m_viewMode) {
  case ViewMode::Favorites:
    ids = m_registry->Favorites();
    break;
  case ViewMode::Recent:
    ids = m_registry->Recent();
    break;
  case ViewMode::Missing:
    ids = m_registry->MissingOrFailed();
    break;
  case ViewMode::All:
  default: {
    AssetQuery q;
    q.text = m_search ? m_search->text().toStdString() : std::string();
    int typeData = m_typeFilter ? m_typeFilter->currentData().toInt() : -1;
    if (typeData > 0)
      q.type = static_cast<AssetType>(typeData);
    std::string folder = selectedFolder();
    if (!folder.empty())
      q.folder = folder;
    ids = m_registry->SearchAssets(q);
    break;
  }
  }

  // For non-All views, still honor the search text and type filter so the
  // toggles compose with the search box.
  const std::string textLower = m_search ? m_search->text().toLower().toStdString()
                                          : std::string();
  const int typeData = m_typeFilter ? m_typeFilter->currentData().toInt() : -1;

  assetlib::ThumbnailCache thumbs(m_registry->paths());

  for (const AssetId &id : ids) {
    const AssetMetadata *m = m_registry->Get(id);
    // Favorites may reference assets no longer present; show them as missing.
    QString name;
    AssetType type = AssetType::Unknown;
    bool unavailable = false;
    if (m) {
      if (m_viewMode != ViewMode::All) {
        if (typeData > 0 && static_cast<int>(m->type) != typeData)
          continue;
        if (!textLower.empty() &&
            QString::fromStdString(m->displayName).toLower().toStdString().find(
                textLower) == std::string::npos)
          continue;
      }
      name = QString::fromStdString(m->displayName);
      type = m->type;
      unavailable = (m->sourceState == assetlib::SourceState::Missing);
    } else {
      if (m_viewMode != ViewMode::Favorites)
        continue; // only the favorites view surfaces dangling ids
      name = tr("(missing asset)");
      unavailable = true;
    }

    auto *item = new QListWidgetItem(m_grid);
    item->setText(name);
    item->setData(kUserRoleId, QString::fromStdString(id.ToString()));
    if (m) // draggable only when the asset is present
      item->setData(kUserRoleDrag,
                    QString::fromStdString(std::string(AssetTypeToString(type)) +
                                           ":" + id.ToString()));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);

    const bool cooking = (m && m->cookState == assetlib::CookState::Stale);
    item->setData(kUserRoleCooking, cooking);
    if (unavailable)
      item->setForeground(QColor(0xb0, 0x70, 0x70));
    const QString thumbPath = m ? EnsureThumbnail(id, m, thumbs) : QString();
    item->setIcon(QIcon(
        ComposeThumb(thumbPath, type, sz, unavailable, cooking, m_spinnerFrame)));
  }

  m_refreshing = false;
}

bool AssetManagerPanel::selectedAssetId(AssetId &out) const {
  if (!m_grid)
    return false;
  auto items = m_grid->selectedItems();
  if (items.isEmpty())
    return false;
  return AssetId::FromString(
      items.first()->data(kUserRoleId).toString().toStdString(), out);
}

void AssetManagerPanel::refreshInspector() {
  auto setAll = [&](const QString &v) {
    m_inspName->setText(v);
    m_inspType->setText(v);
    m_inspId->setText(v);
    m_inspFolder->setText(v);
    m_inspSource->setText(v);
    m_inspCook->setText(v);
    m_inspDeps->setText(v);
    m_inspTags->setText(v);
    m_inspLicense->setText(v);
  };

  AssetId id;
  if (!m_registry || !selectedAssetId(id)) {
    setAll(QStringLiteral("—"));
    m_favoriteButton->setEnabled(false);
    m_revealButton->setEnabled(false);
    return;
  }
  const AssetMetadata *m = m_registry->Get(id);
  if (!m) {
    setAll(QStringLiteral("—"));
    m_inspName->setText(tr("(missing asset)"));
    m_inspId->setText(QString::fromStdString(id.ToString()));
    m_favoriteButton->setEnabled(true);
    m_revealButton->setEnabled(false);
    return;
  }

  m_inspName->setText(QString::fromStdString(m->displayName));
  m_inspType->setText(assetlib::AssetTypeDisplayName(m->type));
  m_inspId->setText(QString::fromStdString(id.ToString()));
  m_inspFolder->setText(m->virtualPath.empty()
                            ? tr("(root)")
                            : QString::fromStdString(m->virtualPath));

  QString src = m->sourcePath.empty()
                    ? tr("(none)")
                    : QString::fromStdString(m->sourcePath);
  if (m->sourceState == assetlib::SourceState::Missing)
    src += tr("  [MISSING]");
  m_inspSource->setText(src);
  m_inspCook->setText(QString::fromUtf8(assetlib::CookStateToString(m->cookState)));
  m_inspDeps->setText(QString::number(m->dependencies.size()));

  QStringList tagList;
  for (const auto &t : m->tags)
    tagList << QString::fromStdString(t);
  m_inspTags->setText(tagList.isEmpty() ? tr("(none)") : tagList.join(", "));

  QString lic = QString::fromStdString(m->license);
  if (!m->attribution.empty())
    lic += (lic.isEmpty() ? QString() : QStringLiteral(" — ")) +
           QString::fromStdString(m->attribution);
  m_inspLicense->setText(lic.isEmpty() ? tr("(none)") : lic);

  m_favoriteButton->setEnabled(true);
  m_favoriteButton->setText(m_registry->IsFavorite(id) ? tr("Unfavorite")
                                                       : tr("Add to Favorites"));
  m_revealButton->setEnabled(!m->sourcePath.empty());
}

// ---------------------------------------------------------------------------

void AssetManagerPanel::onAddAsset() {
  if (!m_registry)
    return;
  const QStringList files = QFileDialog::getOpenFileNames(
      this, tr("Add Assets"), QString(),
      tr("Assets (*.gltf *.glb *.obj *.stl *.fbx *.png *.jpg *.jpeg *.tga *.exr "
         "*.hdr *.dds *.vdb);;All Files (*.*)"));
  if (files.isEmpty())
    return;

  const std::string folder = selectedFolder();
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  int imported = 0, cataloged = 0, failed = 0;
  for (const QString &file : files) {
    QFileInfo fi(file);
    // Decode + cook supported files into the library so they are immediately
    // usable (draggable into the scene). Models land under Imported/Models and
    // textures under Imported/Textures.
    const assetlib::AssetId id =
        assetlib::ImportFileToLibrary(fi.absoluteFilePath().toStdString());
    if (id.valid()) {
      ++imported;
      continue;
    }

    // Unsupported-for-cooking types (e.g. .vdb until Phase 5): catalog the
    // path only. These appear in the library but cannot yet be instantiated.
    const QString ext = fi.suffix().toLower();
    if (ext == "gltf" || ext == "glb" || ext == "obj" || ext == "stl" ||
        ext == "fbx" || ext == "png" || ext == "jpg" || ext == "jpeg" ||
        ext == "tga" || ext == "dds" || ext == "exr" || ext == "hdr" ||
        ext == "bmp") {
      ++failed; // a supported type that failed to decode
      continue;
    }
    AssetMetadata m;
    m.displayName = fi.completeBaseName().toStdString();
    m.virtualPath = folder;
    m.sourcePath = fi.absoluteFilePath().toStdString();
    m.type = (ext == "vdb") ? AssetType::CloudVolume : AssetType::Texture;
    m_registry->Add(std::move(m));
    ++cataloged;
  }
  m_registry->RefreshSourceStates();
  m_registry->Save();
  QGuiApplication::restoreOverrideCursor();

  if (failed > 0)
    QMessageBox::warning(
        this, tr("Add Asset"),
        tr("%1 file(s) could not be decoded and were skipped.").arg(failed));
}

void AssetManagerPanel::onCookTick() {
  // Apply finished cook results here too, not only from the render loop: when
  // the app is idle the renderer may not tick ProcessPendingImport, which would
  // leave cookState stuck at Stale (and the spinner spinning) until the next
  // frame. This timer is a reliable main-thread heartbeat.
  if (m_registry)
    assetlib::CookService::Get().Pump(*m_registry);

  const size_t pending = assetlib::CookService::Get().pending();
  if (m_cookStatus)
    m_cookStatus->setText(
        pending == 0 ? QString()
                     : tr("Cooking %1…").arg(static_cast<int>(pending)));
  if (!m_registry || !m_grid)
    return;

  // Advance the spinner and repaint only the cooking items in place (no grid
  // rebuild, so selection/scroll are preserved). Items flip out of the cooking
  // state once their cookState is no longer Stale.
  m_spinnerFrame = (m_spinnerFrame + 1) % 30;
  const int sz = m_thumbSize ? m_thumbSize->value() : 96;
  assetlib::ThumbnailCache thumbs(m_registry->paths());
  for (int i = 0; i < m_grid->count(); ++i) {
    QListWidgetItem *item = m_grid->item(i);
    if (!item->data(kUserRoleCooking).toBool())
      continue;
    AssetId id;
    if (!AssetId::FromString(item->data(kUserRoleId).toString().toStdString(),
                             id))
      continue;
    const AssetMetadata *m = m_registry->Get(id);
    const bool stillCooking = (m && m->cookState == assetlib::CookState::Stale);
    const AssetType type = m ? m->type : AssetType::Unknown;
    const bool missing = (m && m->sourceState == assetlib::SourceState::Missing);
    if (!stillCooking)
      item->setData(kUserRoleCooking, false);
    const QString thumbPath = m ? EnsureThumbnail(id, m, thumbs) : QString();
    item->setIcon(QIcon(
        ComposeThumb(thumbPath, type, sz, missing, stillCooking, m_spinnerFrame)));
  }
}

void AssetManagerPanel::onNewFolder() {
  if (!m_registry)
    return;
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                            QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  std::string parent = selectedFolder();
  std::string path = parent.empty() ? name.trimmed().toStdString()
                                     : parent + "/" + name.trimmed().toStdString();
  m_registry->CreateFolder(path);
  m_registry->Save();
}

void AssetManagerPanel::onRenameFolder() {
  if (!m_registry)
    return;
  const std::string folder = selectedFolder();
  if (folder.empty())
    return;
  std::string leaf = folder;
  size_t slash = folder.find_last_of('/');
  std::string parent;
  if (slash != std::string::npos) {
    parent = folder.substr(0, slash);
    leaf = folder.substr(slash + 1);
  }
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, tr("Rename Folder"), tr("New name:"), QLineEdit::Normal,
      QString::fromStdString(leaf), &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  std::string newPath = parent.empty()
                            ? name.trimmed().toStdString()
                            : parent + "/" + name.trimmed().toStdString();
  if (!m_registry->RenameFolder(folder, newPath))
    QMessageBox::warning(this, tr("Rename Folder"),
                         tr("Could not rename the folder."));
  m_registry->Save();
}

void AssetManagerPanel::onDeleteFolder() {
  if (!m_registry)
    return;
  const std::string folder = selectedFolder();
  if (folder.empty())
    return;
  const auto choice = QMessageBox::question(
      this, tr("Delete Folder"),
      tr("Delete folder \"%1\"?\n\nYes: also delete assets inside it.\n"
         "No: move contained assets to the parent folder.")
          .arg(QString::fromStdString(folder)),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  if (choice == QMessageBox::Cancel)
    return;
  m_registry->DeleteFolder(folder, choice == QMessageBox::Yes);
  m_registry->Save();
}

void AssetManagerPanel::onFolderContextMenu(const QPoint &pos) {
  if (!m_sourceTree)
    return;
  QTreeWidgetItem *item = m_sourceTree->itemAt(pos);
  if (item)
    item->setSelected(true);
  QMenu menu(this);
  menu.addAction(tr("New Folder…"), this, &AssetManagerPanel::onNewFolder);
  if (!selectedFolder().empty()) {
    menu.addAction(tr("Rename…"), this, &AssetManagerPanel::onRenameFolder);
    menu.addAction(tr("Delete…"), this, &AssetManagerPanel::onDeleteFolder);
  }
  menu.exec(m_sourceTree->viewport()->mapToGlobal(pos));
}

void AssetManagerPanel::onGridContextMenu(const QPoint &pos) {
  if (!m_grid || !m_registry)
    return;
  AssetId id;
  if (!selectedAssetId(id))
    return;
  const AssetMetadata *m = m_registry->Get(id);

  QMenu menu(this);
  const bool fav = m_registry->IsFavorite(id);
  menu.addAction(fav ? tr("Remove from Favorites") : tr("Add to Favorites"),
                 this, [this, id, fav]() {
                   m_registry->SetFavorite(id, !fav);
                 });
  if (m && !m->sourcePath.empty()) {
    menu.addAction(tr("Open Source Location"), this, [this, m]() {
      QFileInfo fi(QString::fromStdString(m->sourcePath));
      QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    });
  }
  menu.addSeparator();
  menu.addAction(tr("Remove from Library"), this, [this, id]() {
    m_registry->Remove(id);
    m_registry->Save();
  });
  menu.exec(m_grid->viewport()->mapToGlobal(pos));
}
