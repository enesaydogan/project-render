#pragma once

#include "../assets/asset_loader.h"
#include <QPixmap>
#include <QSize>
#include <QWidget>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QColor;
class QFrame;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;
class SliderControl;
class QTabWidget;
class QStandardItemModel;

class MaterialEditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialEditorPanel(QWidget *parent = nullptr);
    ~MaterialEditorPanel() override;

private:
    enum TextureSlot {
        Albedo = 0,
        Opacity = 1,
        PackedMetalRough = 2,
        Metalness = 3,
        RoughnessGlossiness = 4,
        Normal = 5,
        CoatNormal = 6,
        Occlusion = 7,
        Emissive = 8,
        SpecularColor = 9,
        Thickness = 10,
        Parallax = 11,
        TextureSlotCount = 12
    };

    struct TextureSlotWidgets {
        QWidget *group = nullptr;
        QComboBox *combo = nullptr;
        SliderControl *amount = nullptr;
        QPushButton *clearButton = nullptr;
        QPushButton *loadButton = nullptr;
        QLabel *thumbLabel = nullptr;
    };

    void createUi();
    void refreshMaterials();
    void scheduleRefresh();
    void rebuildMaterialList();
    void syncInspector();
    void syncInspectorMaterialState(const Asset::Material &mat,
                                    bool refreshTextureUi);
    void updateQa();
    void updatePickUi();
    void updateCounts();
    void updateTextureOptions();
    void updateTextureSlotUi(TextureSlot slot, const Asset::Material &mat);
    void updateWorkflowUi(const Asset::Material &mat);
    int textureIndexForSlot(const Asset::Material &mat, TextureSlot slot) const;
    void setTextureIndexForSlot(Asset::Material &mat, TextureSlot slot, int index);
    int textureIndexFromVisibleCombo(int comboIndex) const;
    int visibleComboIndexForTexture(int textureIndex) const;
    uint64_t textureOptionsSignature() const;
    QPixmap createTexturePreview(const Asset::Texture &tex, const QSize &size) const;

    void applyMaterialChange(const std::function<void(Asset::Material &)> &fn,
                             bool markOpacityDirty = false,
                             bool requestAsRebuild = false,
                             bool refreshTextureUi = false);

    void setColorButton(QPushButton *button, const QColor &color);
    QColor getColorFromMaterial(const float color[3]) const;

    int currentMaterialIndex() const;
    void setSelectedMaterial(int materialIndex, bool ensureVisible);
    void showEvent(QShowEvent *event) override;

    bool m_syncing = false;
    bool m_refreshQueued = false;
    int m_selectedMaterial = -1;
    int m_lastSelectedNodeIndex = -2;
    size_t m_sceneChangeListenerId = 0;
    size_t m_editorStateListenerId = 0;

    std::vector<int> m_materialIndices;

    QPushButton *m_pickButton = nullptr;
    QLabel *m_pickStatusLabel = nullptr;
    QLabel *m_countsLabel = nullptr;

    QCheckBox *m_showAllCheck = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_nodeLabel = nullptr;
    QListWidget *m_materialList = nullptr;

    QGroupBox *m_inspectorGroup = nullptr;
    QLineEdit *m_materialNameEdit = nullptr;
    QLabel *m_materialIdLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_pasteButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_resetNoTexButton = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QPushButton *m_applyPresetButton = nullptr;

    QTabWidget *m_tabs = nullptr;

    // Surface tab
    QPushButton *m_baseColorButton = nullptr;
    QComboBox *m_materialClassCombo = nullptr;
    QComboBox *m_workflowCombo = nullptr;
    QLabel *m_secondarySurfaceLabel = nullptr;
    QLabel *m_roughnessSurfaceLabel = nullptr;
    QLabel *m_specularWeightLabel = nullptr;
    SliderControl *m_roughness = nullptr;
    SliderControl *m_metalness = nullptr;
    SliderControl *m_specularWeight = nullptr;
    QPushButton *m_specularColorButton = nullptr;
    SliderControl *m_ior = nullptr;
    SliderControl *m_transmission = nullptr;
    QPushButton *m_transmissionColorButton = nullptr;
    SliderControl *m_thickness = nullptr;
    SliderControl *m_attenuationDistance = nullptr;
    SliderControl *m_coatWeight = nullptr;
    SliderControl *m_coatRoughness = nullptr;
    SliderControl *m_coatIor = nullptr;
    SliderControl *m_translucency = nullptr;
    SliderControl *m_anisotropy = nullptr;
    SliderControl *m_anisotropyRotation = nullptr;
    SliderControl *m_sheenWeight = nullptr;
    QPushButton *m_sheenColorButton = nullptr;
    QCheckBox *m_thinWalled = nullptr;

    // Grass tab
    QCheckBox *m_grassEnabled = nullptr;
    QPushButton *m_grassColorButton = nullptr;
    QLabel *m_grassHint = nullptr;
    SliderControl *m_grassBladeSize = nullptr;
    SliderControl *m_grassBladeCount = nullptr;
    SliderControl *m_grassBladeVariation = nullptr;

    // Textures tab
    std::array<TextureSlotWidgets, TextureSlotCount> m_textureSlots;

    // Mapping tab
    SliderControl *m_uvScaleX = nullptr;
    SliderControl *m_uvScaleY = nullptr;
    SliderControl *m_uvOffsetX = nullptr;
    SliderControl *m_uvOffsetY = nullptr;
    QCheckBox *m_triPlanarEnabled = nullptr;
    SliderControl *m_triPlanarScale = nullptr;
    SliderControl *m_triPlanarSharpness = nullptr;
    SliderControl *m_triPlanarNormalStrength = nullptr;
    SliderControl *m_triPlanarRotationX = nullptr;
    SliderControl *m_triPlanarRotationY = nullptr;
    SliderControl *m_triPlanarRotationZ = nullptr;
    QComboBox *m_triPlanarVariationMode = nullptr;
    SliderControl *m_triPlanarVariationOffset = nullptr;
    SliderControl *m_stochasticTilingRotation = nullptr;
    QCheckBox *m_stochasticTilingMirror = nullptr;
    SliderControl *m_stochasticTilingColorVariation = nullptr;

    // Parallax tab
    QComboBox *m_parallaxMode = nullptr;
    SliderControl *m_parallaxDepthScale = nullptr;
    SliderControl *m_parallaxRoomDepth = nullptr;
    SliderControl *m_parallaxWindowAspect = nullptr;
    SliderControl *m_parallaxWindowBrightness = nullptr;
    SliderControl *m_parallaxScaleX = nullptr;
    SliderControl *m_parallaxScaleY = nullptr;
    SliderControl *m_parallaxOffsetX = nullptr;
    SliderControl *m_parallaxOffsetY = nullptr;
    QCheckBox *m_parallaxBackFace = nullptr;

    // Emission tab
    QPushButton *m_emissiveColorButton = nullptr;
    SliderControl *m_emissiveIntensity = nullptr;

    // Flags tab
    QCheckBox *m_doubleSided = nullptr;
    QComboBox *m_alphaMode = nullptr;
    SliderControl *m_alphaCutoff = nullptr;

    // QA tab
    QLabel *m_qaLabel = nullptr;

    // Clipboard
    std::unique_ptr<Asset::Material> m_clipboard;
    std::vector<int> m_visibleTextureIndices;
    QStandardItemModel *m_textureOptionsModel = nullptr;
    uint64_t m_textureOptionsSignature = 0;
};
