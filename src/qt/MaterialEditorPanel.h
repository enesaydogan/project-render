#pragma once

#include <QWidget>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace Asset {
struct Material;
}

class QCheckBox;
class QComboBox;
class QColor;
class QDoubleSpinBox;
class QFrame;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTimer;

class MaterialEditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialEditorPanel(QWidget *parent = nullptr);
    ~MaterialEditorPanel() override;

private:
    enum TextureSlot {
        Albedo = 0,
        MetalRough = 1,
        Normal = 2,
        Occlusion = 3,
        Emissive = 4,
        TextureSlotCount = 5
    };

    struct TextureSlotWidgets {
        QLabel *label = nullptr;
        QComboBox *combo = nullptr;
        QPushButton *clearButton = nullptr;
        QPushButton *loadButton = nullptr;
        QPushButton *editButton = nullptr;
        QLabel *infoLabel = nullptr;
    };

    void createUi();
    void refreshMaterials();
    void rebuildMaterialList();
    void syncInspector();
    void updateQa();
    void updatePickUi();
    void updateCounts();
    void updateTextureOptions();
    void updateTextureSlotUi(TextureSlot slot, const Asset::Material &mat);
    int textureIndexForSlot(const Asset::Material &mat, TextureSlot slot) const;
    void setTextureIndexForSlot(Asset::Material &mat, TextureSlot slot, int index);

    void applyMaterialChange(const std::function<void(Asset::Material &)> &fn,
                             bool markOpacityDirty = false,
                             bool requestAsRebuild = false);

    void setColorButton(QPushButton *button, const QColor &color);
    QColor getColorFromMaterial(const float color[3]) const;

    int currentMaterialIndex() const;
    void setSelectedMaterial(int materialIndex, bool ensureVisible);

    bool m_syncing = false;
    int m_selectedMaterial = -1;
    int m_lastSelectedNodeIndex = -2;

    QTimer *m_refreshTimer = nullptr;
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
    QDoubleSpinBox *m_roughness = nullptr;
    QDoubleSpinBox *m_metalness = nullptr;
    QDoubleSpinBox *m_specularWeight = nullptr;
    QDoubleSpinBox *m_ior = nullptr;
    QDoubleSpinBox *m_transmission = nullptr;
    QPushButton *m_transmissionColorButton = nullptr;
    QDoubleSpinBox *m_coatWeight = nullptr;
    QDoubleSpinBox *m_coatRoughness = nullptr;
    QDoubleSpinBox *m_translucency = nullptr;
    QCheckBox *m_thinWalled = nullptr;

    // Grass tab
    QCheckBox *m_grassEnabled = nullptr;
    QPushButton *m_grassColorButton = nullptr;
    QLabel *m_grassHint = nullptr;
    QDoubleSpinBox *m_grassBladeSize = nullptr;
    QDoubleSpinBox *m_grassBladeCount = nullptr;
    QDoubleSpinBox *m_grassBladeVariation = nullptr;

    // Textures tab
    std::array<TextureSlotWidgets, TextureSlotCount> m_textureSlots;

    // Mapping tab
    QDoubleSpinBox *m_uvScaleX = nullptr;
    QDoubleSpinBox *m_uvScaleY = nullptr;
    QDoubleSpinBox *m_uvOffsetX = nullptr;
    QDoubleSpinBox *m_uvOffsetY = nullptr;
    QCheckBox *m_triPlanarEnabled = nullptr;
    QDoubleSpinBox *m_triPlanarScale = nullptr;
    QDoubleSpinBox *m_triPlanarSharpness = nullptr;
    QDoubleSpinBox *m_triPlanarNormalStrength = nullptr;

    // Emission tab
    QPushButton *m_emissiveColorButton = nullptr;
    QDoubleSpinBox *m_emissiveIntensity = nullptr;

    // Flags tab
    QCheckBox *m_doubleSided = nullptr;
    QComboBox *m_alphaMode = nullptr;

    // QA tab
    QLabel *m_qaLabel = nullptr;

    // Clipboard
    std::unique_ptr<Asset::Material> m_clipboard;
};
