#include "material_editor.h"
#include "assets/asset_loader.h"
#include "dx12_context.h"
#include "dxr_renderer.h"
#include "file_import.h"
#include "imgui.h"
#include "material/material_system.h"
#include "scene.h"
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>
#include <wrl.h>


// External globals from main.cpp
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;
extern UINT g_textureDescriptorCount;
extern D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart;

using namespace DX12Context;

namespace MaterialEditor {

static int s_pendingMaterialSelect = -1;
static bool s_pickingEnabled = false;
static size_t s_nextStateListenerId = 1;
static std::unordered_map<size_t, std::function<void()>> s_stateListeners;

static void NotifyStateListeners() {
  if (s_stateListeners.empty())
    return;

  std::vector<std::function<void()>> callbacks;
  callbacks.reserve(s_stateListeners.size());
  for (const auto &[_, callback] : s_stateListeners) {
    if (callback)
      callbacks.push_back(callback);
  }

  for (const auto &callback : callbacks)
    callback();
}

static ImTextureID GetImGuiTexIDForTextureIndex(int textureIndex) {
  if (textureIndex < 0)
    return (ImTextureID)0;
  if (!g_device)
    return (ImTextureID)0;
  if (g_texturesGpuStart.ptr == 0)
    return (ImTextureID)0;
  if ((UINT)textureIndex >= g_textureDescriptorCount)
    return (ImTextureID)0;

  const UINT inc = g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (inc == 0)
    return (ImTextureID)0;

  D3D12_GPU_DESCRIPTOR_HANDLE h = g_texturesGpuStart;
  h.ptr += (UINT64)textureIndex * (UINT64)inc;
  return (ImTextureID)h.ptr;
}

static bool IsReflectionGlossinessWorkflow(const Asset::Material &material) {
  return MaterialSystem::UsesReflectionGlossiness(material);
}

static const char *GetRoughnessLabel(const Asset::Material &material) {
  return MaterialSystem::GetMicrosurfaceLabel(material);
}

static const char *GetSecondarySurfaceLabel(const Asset::Material &material) {
  return MaterialSystem::GetSecondarySurfaceLabel(material);
}

static const char *GetRoughnessTextureLabel(const Asset::Material &material) {
  return MaterialSystem::GetMicrosurfaceTextureLabel(material);
}

bool IsPickingEnabled() { return s_pickingEnabled; }
void SetPickingEnabled(bool enabled) {
  if (s_pickingEnabled == enabled)
    return;
  s_pickingEnabled = enabled;
  NotifyStateListeners();
}

void SelectMaterial(int materialIndex) {
  s_pendingMaterialSelect = materialIndex;
  NotifyStateListeners();
}

size_t RegisterStateListener(std::function<void()> callback) {
  const size_t listenerId = s_nextStateListenerId++;
  s_stateListeners.emplace(listenerId, std::move(callback));
  return listenerId;
}

void UnregisterStateListener(size_t listenerId) {
  s_stateListeners.erase(listenerId);
}

int ConsumePendingMaterialSelect() {
  const int pending = s_pendingMaterialSelect;
  s_pendingMaterialSelect = -1;
  return pending;
}

static std::string WStringToUtf8Local(const std::wstring &ws) {
  if (ws.empty())
    return {};
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                                        NULL, 0, NULL, NULL);
  std::string s(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0],
                      size_needed, NULL, NULL);
  return s;
}

static bool IsHDRTexturePath(const std::wstring &path) {
  if (path.size() < 4)
    return false;
  std::wstring ext;
  size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos)
    return false;
  ext = path.substr(dot);
  for (auto &c : ext)
    c = (wchar_t)towlower(c);
  return (ext == L".hdr" || ext == L".exr");
}

void Draw(HWND hwnd, bool &visible) {
  if (!visible)
    return;
  if (ImGui::Begin("Material Editor", &visible)) {
    bool uiChanged = false;
    int visibleTextureCount = 0;
    for (const auto &texture : g_loadedTextures) {
      if (!texture.hiddenInEditor)
        ++visibleTextureCount;
    }
    // --- Header / toolbar ---
    if (s_pickingEnabled) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
      if (ImGui::Button("Cancel Pick")) {
        s_pickingEnabled = false;
        uiChanged = true;
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::TextUnformatted("Click on object surface...");
    } else {
      if (ImGui::Button("Pick Material")) {
        s_pickingEnabled = true;
        uiChanged = true;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Pick material by clicking the surface in the viewport");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|  Scene materials: %d, textures: %d",
              (int)g_loadedMaterials.size(),
                        visibleTextureCount);
    ImGui::Separator();

    // --- Determine currently selected node ---
    int selectedNodeIndex = -1;
    const auto &nodes = Scene::GetNodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (nodes[i].selected) {
        selectedNodeIndex = (int)i;
        break;
      }
    }
    const Scene::Node *selectedNode =
        (selectedNodeIndex >= 0) ? &nodes[selectedNodeIndex] : nullptr;

    // --- Build material list (scope + filter) ---
    static bool s_showAllMaterials = false;
    static char s_filter[128] = "";
    static int s_selectedMaterial = -1; // global index into g_loadedMaterials
    static int s_lastSelectedNodeIndex = -2;

    std::vector<int> materialIndices;
    auto gatherSceneMaterialIndices = [&]() {
      std::vector<int> indices;
      for (const Scene::Node &node : nodes) {
        for (size_t meshIndex : node.meshIndices) {
          if (meshIndex >= g_loadedMeshes.size())
            continue;
          const int matIdx = g_loadedMeshes[meshIndex].materialIndex;
          if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
            continue;
          if (std::find(indices.begin(), indices.end(), matIdx) == indices.end())
            indices.push_back(matIdx);
        }
      }
      return indices;
    };
    if (s_showAllMaterials || !selectedNode) {
      materialIndices = gatherSceneMaterialIndices();
    } else {
      // Unique materials used by the selected node
      for (size_t i = 0; i < selectedNode->meshIndices.size(); ++i) {
        size_t mi = selectedNode->meshIndices[i];
        if (mi >= g_loadedMeshes.size())
          continue;
        int matIdx = g_loadedMeshes[mi].materialIndex;
        if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
          continue;
        bool exists = false;
        for (int existing : materialIndices) {
          if (existing == matIdx) {
            exists = true;
            break;
          }
        }
        if (!exists)
          materialIndices.push_back(matIdx);
      }
    }

    auto FilterPass = [&](int matIdx) -> bool {
      if (s_filter[0] == '\0')
        return true;
      const char *name = g_loadedMaterials[matIdx].name;
      // case-insensitive substring match
      for (const char *p = name; *p; ++p) {
        const char *a = p;
        const char *b = s_filter;
        while (*a && *b) {
          char ca = *a;
          char cb = *b;
          if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
          if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
          if (ca != cb)
            break;
          ++a;
          ++b;
        }
        if (*b == '\0')
          return true;
      }
      return false;
    };

    // Handle external selection request (picking)
    const int pendingSelect = ConsumePendingMaterialSelect();
    if (pendingSelect != -1) {
      if (pendingSelect >= 0 &&
          pendingSelect < (int)g_loadedMaterials.size()) {
        s_selectedMaterial = pendingSelect;
        // If we're scoped to the selected node and the picked material isn't in
        // it, auto-switch to all.
        if (!s_showAllMaterials && selectedNode) {
          bool inNode = false;
          for (int mi : materialIndices) {
            if (mi == s_selectedMaterial) {
              inNode = true;
              break;
            }
          }
          if (!inNode)
            s_showAllMaterials = true;
        }
        fprintf(stderr, "MaterialEditor: picked material index %d\n",
                s_selectedMaterial);
      }
    }

    // If node changed and we're scoped, keep selection stable if possible.
    if (!s_showAllMaterials && selectedNodeIndex != s_lastSelectedNodeIndex) {
      s_lastSelectedNodeIndex = selectedNodeIndex;
      bool ok = false;
      for (int mi : materialIndices) {
        if (mi == s_selectedMaterial) {
          ok = true;
          break;
        }
      }
      if (!ok)
        s_selectedMaterial =
            materialIndices.empty() ? -1 : materialIndices.front();
    }

    // Default selection
    if (s_selectedMaterial < 0 && !materialIndices.empty()) {
      s_selectedMaterial = materialIndices.front();
    }

    // --- Two-pane layout: list on top, inspector below ---
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // reserve roughly 30% of the vertical space for the material list
    float listHeight = avail.y * 0.3f;
    if (listHeight < 150.0f)
      listHeight = 150.0f; // minimum height so the list isn't too small

    ImGui::BeginChild("##mat_list", ImVec2(0, listHeight), true);
    {
      if (selectedNode) {
        ImGui::Text("Node: %s", selectedNode->name.c_str());
      } else {
        ImGui::TextDisabled("No node selected");
      }
      if (ImGui::Checkbox("Show all materials", &s_showAllMaterials))
        uiChanged = true;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputTextWithHint("##filter", "Search material name...",
                                   s_filter, sizeof(s_filter)))
        uiChanged = true;
      ImGui::Separator();

      if (!s_showAllMaterials && !selectedNode) {
        ImGui::TextWrapped("Select an object, or enable 'Show all materials'.");
      } else if (materialIndices.empty()) {
        ImGui::TextWrapped("No materials in this scope.");
      } else {
        for (int matIdx : materialIndices) {
          if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size())
            continue;
          if (!FilterPass(matIdx))
            continue;

          const Asset::Material &m = g_loadedMaterials[matIdx];
          std::string label =
              std::string(m.name) + "  ##mat_" + std::to_string(matIdx);
          bool selected = (matIdx == s_selectedMaterial);
          if (ImGui::Selectable(label.c_str(), selected)) {
            s_selectedMaterial = matIdx;
            uiChanged = true;
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Material ID: %d", matIdx);
          }
        }
      }
    }
    ImGui::EndChild();

    // separator between list and inspector for clarity
    ImGui::Separator();

    ImGui::BeginChild("##mat_inspector", ImVec2(0, 0), false);
    {
      if (s_selectedMaterial < 0 ||
          s_selectedMaterial >= (int)g_loadedMaterials.size()) {
        ImGui::TextWrapped("Select a material from the list.");
      } else {
        // Reset accumulation once per window when any UI widget changed
        if (uiChanged)
          DxrRenderer::ResetAccumulation();
        int matIdx = s_selectedMaterial;
        Asset::Material &mat = g_loadedMaterials[matIdx];
        auto MarkOpacityDirty = [&]() {
          DxrRenderer::MarkMaterialDirty(matIdx);
          DxrRenderer::ResetAccumulation();
        };
        ImGui::PushID(matIdx);

        // Clipboard helpers
        static Asset::Material s_clipboard;
        static bool s_hasClipboard = false;

        auto SetRoughness = [&](float r) {
          if (r < 0.0f)
            r = 0.0f;
          if (r > 1.0f)
            r = 1.0f;
          mat.roughness = r;
        };

        auto ResetDefaults = [&](bool keepTextures) {
          Asset::Material def;
          // Preserve name; keepTextures optionally
          char nameBuf[64];
          strncpy_s(nameBuf, mat.name, _TRUNCATE);
          int d = mat.diffuseTexture;
          int a = mat.opacityTexture;
          int n = mat.normalTexture;
          int cn = mat.coatNormalTexture;
          int e = mat.emissiveTexture;
          int o = mat.occlusionTexture;
          int mr = mat.metalRoughTexture;
          int mt = mat.metalnessTexture;
          int rg = mat.roughnessGlossTexture;
          int sc = mat.specularColorTexture;
          int tk = mat.thicknessTexture;
          float da = mat.diffuseTextureAmount;
          float aa = mat.opacityTextureAmount;
          float na = mat.normalTextureAmount;
          float cna = mat.coatNormalTextureAmount;
          float ea = mat.emissiveTextureAmount;
          float oa = mat.occlusionTextureAmount;
          float mra = mat.metalRoughTextureAmount;
          float mta = mat.metalnessTextureAmount;
          float rga = mat.roughnessGlossTextureAmount;
          float sca = mat.specularColorTextureAmount;
          float tka = mat.thicknessTextureAmount;

          mat = def;
          strncpy_s(mat.name, nameBuf, _TRUNCATE);
          if (keepTextures) {
            mat.diffuseTexture = d;
            mat.opacityTexture = a;
            mat.normalTexture = n;
            mat.coatNormalTexture = cn;
            mat.emissiveTexture = e;
            mat.occlusionTexture = o;
            mat.metalRoughTexture = mr;
            mat.metalnessTexture = mt;
            mat.roughnessGlossTexture = rg;
            mat.specularColorTexture = sc;
            mat.thicknessTexture = tk;
            mat.diffuseTextureAmount = da;
            mat.opacityTextureAmount = aa;
            mat.normalTextureAmount = na;
            mat.coatNormalTextureAmount = cna;
            mat.emissiveTextureAmount = ea;
            mat.occlusionTextureAmount = oa;
            mat.metalRoughTextureAmount = mra;
            mat.metalnessTextureAmount = mta;
            mat.roughnessGlossTextureAmount = rga;
            mat.specularColorTextureAmount = sca;
            mat.thicknessTextureAmount = tka;
          }
        };

        // Inspector header
        ImGui::Text("Material: %s", mat.name);
        ImGui::SameLine();
        ImGui::TextDisabled("(ID %d)", matIdx);

        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputText("Name", mat.name, sizeof(mat.name))) {
          DxrRenderer::ResetAccumulation();
        }

        // Quick actions
        if (ImGui::Button("Copy")) {
          s_clipboard = mat;
          s_hasClipboard = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste") && s_hasClipboard) {
          mat = s_clipboard;
          MarkOpacityDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
          ResetDefaults(true);
          MarkOpacityDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset (No Tex)")) {
          ResetDefaults(false);
          MarkOpacityDirty();
        }

        ImGui::Separator();

        // Presets (kept in header for fast iteration)
        static int presetIdx = 0;
        const char *presets[] = {
            "Dielectric Generic",
            "Paint / Plaster",
            "Concrete",
            "Wood (Raw)",
            "Wood (Varnished)",
            "Tile (Ceramic)",
            "Metal (Brushed)",
            "Metal (Polished)",
            "Plastic",
            "Glass (Clear Window, Thin)",
            "Glass (Frosted, Thin)",
            "Glass (Tinted, Thin)",
            "Fabric (Approx)",
            "Vegetation Leaf (Approx)",
        };
        ImGui::SetNextItemWidth(260.0f);
        ImGui::Combo("Preset", &presetIdx, presets,
                     (int)(sizeof(presets) / sizeof(presets[0])));
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
          MaterialSystem::ApplyPreset(mat, presetIdx);
          MarkOpacityDirty();
        }

        ImGui::Separator();

        // Tabs
        if (ImGui::BeginTabBar("##mat_tabs")) {
          if (ImGui::BeginTabItem("Surface")) {
            if (ImGui::ColorEdit3("Base Color", mat.diffuseColor))
              DxrRenderer::ResetAccumulation();

            int workflow = (int)mat.workflow;
            const char *workflows[] = {"Metalness / Roughness",
                                       "Reflection / Glossiness"};
            if (ImGui::Combo("Workflow", &workflow, workflows,
                             IM_ARRAYSIZE(workflows))) {
              mat.workflow = (uint32_t)workflow;
              if (IsReflectionGlossinessWorkflow(mat))
                mat.metalness = 0.0f;
              DxrRenderer::ResetAccumulation();
            }

            float roughnessValue = IsReflectionGlossinessWorkflow(mat)
                                       ? (1.0f - mat.roughness)
                                       : mat.roughness;
            if (ImGui::SliderFloat(GetRoughnessLabel(mat), &roughnessValue,
                                   0.0f, 1.0f)) {
              SetRoughness(IsReflectionGlossinessWorkflow(mat)
                               ? (1.0f - roughnessValue)
                               : roughnessValue);
              DxrRenderer::ResetAccumulation();
            }

            float secondaryValue = IsReflectionGlossinessWorkflow(mat)
                                       ? mat.specularWeight
                                       : mat.metalness;
            if (ImGui::SliderFloat(GetSecondarySurfaceLabel(mat),
                                   &secondaryValue, 0.0f, 1.0f)) {
              if (IsReflectionGlossinessWorkflow(mat)) {
                mat.specularWeight = secondaryValue;
                mat.metalness = 0.0f;
              } else {
                mat.metalness = secondaryValue;
              }
              DxrRenderer::ResetAccumulation();
            }

            if (!IsReflectionGlossinessWorkflow(mat) &&
                ImGui::SliderFloat("Specular Weight", &mat.specularWeight,
                                   0.0f, 1.0f))
              DxrRenderer::ResetAccumulation();
            if (ImGui::ColorEdit3("Specular Color", mat.specularColor))
              DxrRenderer::ResetAccumulation();

            if (ImGui::SliderFloat("IOR", &mat.ior, MaterialSystem::kMinMaterialIor,
                                   MaterialSystem::kMaxMaterialIor, "%.3f"))
              DxrRenderer::ResetAccumulation();

            ImGui::SeparatorText("Transmission / Coat");
            float transmission = mat.transmissionWeight;
            if (ImGui::SliderFloat("Transmission", &transmission, 0.0f,
                                   1.0f)) {
              mat.transmissionWeight = transmission;
              MarkOpacityDirty();
            }
            if (ImGui::ColorEdit3("Transmission Color", mat.transmissionColor)) {
              DxrRenderer::ResetAccumulation();
            }
            if (ImGui::SliderFloat("Thickness", &mat.thickness, 0.0f, 1.0f,
                                   "%.4f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Attenuation Distance",
                                   &mat.attenuationDistance, 0.0f, 100.0f,
                                   "%.3f"))
              DxrRenderer::ResetAccumulation();
            float coatWeight = mat.coatWeight;
            if (ImGui::SliderFloat("Coat", &coatWeight, 0.0f, 1.0f)) {
              mat.coatWeight = coatWeight;
              DxrRenderer::ResetAccumulation();
            }
            float coatRoughness = mat.coatRoughness;
            if (ImGui::SliderFloat("Coat Roughness",
                                   &coatRoughness, 0.0f, 1.0f)) {
              mat.coatRoughness = coatRoughness;
              DxrRenderer::ResetAccumulation();
            }
            if (ImGui::SliderFloat("Coat IOR", &mat.coatIor,
                                   MaterialSystem::kMinMaterialIor,
                                   MaterialSystem::kMaxMaterialIor, "%.3f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Translucency", &mat.translucency, 0.0f,
                                   1.0f))
              MarkOpacityDirty();
            if (ImGui::SliderFloat("Anisotropy", &mat.anisotropy, -1.0f, 1.0f))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Aniso Rotation", &mat.anisotropyRotation,
                                   0.0f, 360.0f, "%.1f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Sheen", &mat.sheenWeight, 0.0f, 1.0f))
              DxrRenderer::ResetAccumulation();
            if (ImGui::ColorEdit3("Sheen Color", mat.sheenColor))
              DxrRenderer::ResetAccumulation();
            {
              bool thin = mat.thinWalled > 0.5f;
              if (ImGui::Checkbox("Thin Walled", &thin)) {
                mat.thinWalled = thin ? 1.0f : 0.0f;
                MarkOpacityDirty();
              }
            }

            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Grass")) {
            bool grassChanged = false;

            if (ImGui::Checkbox("Enable Grass", &mat.isGrass)) {
              if (mat.isGrass) {
                Asset::ApplyDefaultGrassLook(mat);
              }
              grassChanged = true;
            }

            if (!mat.isGrass) {
              ImGui::TextDisabled(
                  "Enable Grass to use this material as a grass emitter.");
            } else {
              if (ImGui::ColorEdit3("Grass Color", mat.grassColor)) {
                mat.diffuseColor[0] = mat.grassColor[0];
                mat.diffuseColor[1] = mat.grassColor[1];
                mat.diffuseColor[2] = mat.grassColor[2];
                DxrRenderer::ResetAccumulation();
              }
              if (ImGui::SliderFloat("Blade Size", &mat.grassBladeSize, 0.05f,
                                     5.0f, "%.2f")) {
                grassChanged = true;
              }
              if (ImGui::SliderFloat("Patch Density / m2", &mat.grassBladeCount,
                                     0.0f, 1024.0f, "%.1f")) {
                grassChanged = true;
              }
              if (ImGui::SliderFloat("Blade Variation",
                                     &mat.grassBladeVariation, 0.0f, 1.0f,
                                     "%.2f")) {
                grassChanged = true;
              }
            }

            if (grassChanged) {
              DxrRenderer::ResetAccumulation();
              DxrRenderer::RequestAccelerationStructureRebuild();
            }

            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Textures")) {
            auto DrawTextureSlot = [&](const char *label,
                                       MaterialSystem::TextureSlot slot) {
              ImGui::PushID(label);

              int idx = MaterialSystem::GetTextureIndex(mat, slot);
              float amount = MaterialSystem::GetTextureAmount(mat, slot);

              static std::vector<std::string> names;
              static std::vector<const char *> cstr;
              static std::vector<int> visibleTextureIndices;
              names.clear();
              cstr.clear();
              visibleTextureIndices.clear();
              names.emplace_back("None");
              for (size_t ti = 0; ti < g_loadedTextures.size(); ++ti) {
                const auto &t = g_loadedTextures[ti];
                if (t.hiddenInEditor)
                  continue;
                std::string s = "#" + std::to_string((int)ti);
                if (t.width > 0 && t.height > 0) {
                  s += " (" + std::to_string(t.width) + "x" +
                       std::to_string(t.height) + ")";
                }
                if (!t.resource) {
                  s += " [missing]";
                }
                visibleTextureIndices.push_back((int)ti);
                names.emplace_back(std::move(s));
              }
              for (auto &s : names)
                cstr.push_back(s.c_str());

              int comboIdx = 0;
              if (idx >= 0) {
                for (size_t visibleIndex = 0;
                     visibleIndex < visibleTextureIndices.size();
                     ++visibleIndex) {
                  if (visibleTextureIndices[visibleIndex] == idx) {
                    comboIdx = (int)visibleIndex + 1;
                    break;
                  }
                }
              }

              ImGui::AlignTextToFramePadding();
              ImGui::TextUnformatted(label);

              // Thumbnail preview (uses engine SRV heap; requires ImGui to be
              // bound to same heap)
              const float thumbSize = 48.0f;
              ImGui::SameLine(120);
              ImTextureID texID = GetImGuiTexIDForTextureIndex(idx);
              if (texID != (ImTextureID)0) {
                ImGui::Image(texID, ImVec2(thumbSize, thumbSize));
                if (ImGui::IsItemHovered() && idx >= 0 &&
                    idx < (int)g_loadedTextures.size()) {
                  const auto &t = g_loadedTextures[idx];
                  ImGui::SetTooltip("#%d (%ux%u) mips=%u", idx, t.width,
                                    t.height, t.mipLevels);
                }
              } else {
                ImGui::Dummy(ImVec2(thumbSize, thumbSize));
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("No texture bound");
                }
              }

              ImGui::SameLine(120 + thumbSize + 10);
              ImGui::SetNextItemWidth(260.0f);
              if (ImGui::Combo("##tex", &comboIdx, cstr.data(),
                               (int)cstr.size())) {
                int newIdx = -1;
                if (comboIdx > 0 &&
                    comboIdx - 1 < (int)visibleTextureIndices.size()) {
                  newIdx = visibleTextureIndices[(size_t)comboIdx - 1];
                }
                MaterialSystem::SetTextureIndex(mat, slot, newIdx);
                DxrRenderer::ResetAccumulation();
                idx = MaterialSystem::GetTextureIndex(mat, slot);
              }

              ImGui::SameLine();
              ImGui::SetNextItemWidth(180.0f);
              const bool hasTexture = idx >= 0;
              ImGui::BeginDisabled(!hasTexture);
              if (ImGui::SliderFloat("##amount", &amount, 0.0f, 1.0f,
                                     "Amount %.2f")) {
                MaterialSystem::SetTextureAmount(mat, slot, amount);
                DxrRenderer::ResetAccumulation();
              }
              ImGui::EndDisabled();

              // Buttons on their own line to avoid horizontally wide layout
              ImGui::NewLine();
              ImGui::SameLine(120 + thumbSize + 10);
              if (ImGui::Button("Clear")) {
                MaterialSystem::SetTextureIndex(mat, slot, -1);
                DxrRenderer::ResetAccumulation();
                idx = -1;
              }
              ImGui::SameLine();
              if (ImGui::Button("Load...")) {
                std::wstring chosen;
                if (OpenTextureFileDialog(hwnd, chosen) && !chosen.empty()) {
                  bool isHDR = IsHDRTexturePath(chosen);
                  int newTex = Scene::AddTextureFromFile(
                      WStringToUtf8Local(chosen), isHDR);
                  if (newTex >= 0) {
                    MaterialSystem::SetTextureIndex(mat, slot, newTex);
                    DxrRenderer::ResetAccumulation();
                    idx = newTex;
                  }
                }
              }

              // Advanced index edit (hidden by default)
              if (ImGui::TreeNode("Advanced##adv")) {
                int tmp = idx;
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputInt("Index", &tmp)) {
                  if (tmp < -1)
                    tmp = -1;
                  if (tmp >= (int)g_loadedTextures.size())
                    tmp = -1;
                  if (tmp >= 0 && !g_loadedTextures[tmp].resource)
                    tmp = -1;
                  MaterialSystem::SetTextureIndex(mat, slot, tmp);
                  DxrRenderer::ResetAccumulation();
                }
                ImGui::TreePop();
              }

              ImGui::PopID();
            };

            DrawTextureSlot("Albedo", MaterialSystem::TextureSlot::BaseColor);
            DrawTextureSlot("Opacity", MaterialSystem::TextureSlot::Opacity);
            DrawTextureSlot("Packed Metal/Rough (Legacy)",
                            MaterialSystem::TextureSlot::PackedSurface);
            if (!IsReflectionGlossinessWorkflow(mat))
              DrawTextureSlot("Metalness",
                              MaterialSystem::TextureSlot::Metalness);
            DrawTextureSlot(GetRoughnessTextureLabel(mat),
                            MaterialSystem::TextureSlot::RoughnessOrGlossiness);
            DrawTextureSlot("Normal", MaterialSystem::TextureSlot::Normal);
            DrawTextureSlot("Coat Normal", MaterialSystem::TextureSlot::CoatNormal);
            DrawTextureSlot("Occlusion", MaterialSystem::TextureSlot::Occlusion);
            DrawTextureSlot("Emissive", MaterialSystem::TextureSlot::Emissive);
            DrawTextureSlot("Specular Color",
                            MaterialSystem::TextureSlot::SpecularColor);
            DrawTextureSlot("Thickness", MaterialSystem::TextureSlot::Thickness);

            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Mapping")) {
            if (ImGui::InputFloat2("UV Scale", mat.uvScale))
              DxrRenderer::ResetAccumulation();
            if (ImGui::InputFloat2("UV Offset", mat.uvOffset))
              DxrRenderer::ResetAccumulation();

            ImGui::SeparatorText("Tri-Planar");
            bool tri = mat.triPlanarEnabled > 0.5f;
            if (ImGui::Checkbox("Enable", &tri)) {
              mat.triPlanarEnabled = tri ? 1.0f : 0.0f;
              DxrRenderer::ResetAccumulation();
            }
            if (ImGui::SliderFloat("Scale", &mat.triPlanarScale, 0.001f, 50.0f,
                                   "%.3f", ImGuiSliderFlags_Logarithmic))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Sharpness", &mat.triPlanarSharpness, 0.25f,
                                   16.0f, "%.2f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Normal Strength",
                                   &mat.triPlanarNormalStrength, 0.0f, 4.0f,
                                   "%.2f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat3("Rotation XYZ (deg)",
                                    mat.triPlanarRotationDegrees, 0.0f,
                                    360.0f, "%.1f"))
              DxrRenderer::ResetAccumulation();
            const char *variationModes[] = {"Off", "Per Mesh", "Per Surface"};
            int variationMode = static_cast<int>(mat.triPlanarVariationMode);
            if (ImGui::Combo("Stochastic Tiling", &variationMode,
                             variationModes,
                             IM_ARRAYSIZE(variationModes))) {
              mat.triPlanarVariationMode =
                  static_cast<uint32_t>((std::clamp)(variationMode, 0, 2));
              DxrRenderer::ResetAccumulation();
            }
            const bool stochasticActive = variationMode != 0;
            ImGui::BeginDisabled(!stochasticActive);
            if (ImGui::SliderFloat("Offset Jitter",
                                   &mat.triPlanarVariationOffset, 0.0f, 1.0f,
                                   "%.2f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Random Rotation",
                                   &mat.stochasticTilingRotationDegrees,
                                   0.0f, 360.0f, "%.1f"))
              DxrRenderer::ResetAccumulation();
            if (ImGui::Checkbox("Mirror Tiles",
                                &mat.stochasticTilingMirror))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Color Variation",
                                   &mat.stochasticTilingColorVariation,
                                   0.0f, 1.0f, "%.2f"))
              DxrRenderer::ResetAccumulation();
            ImGui::EndDisabled();

            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Emission")) {
            if (ImGui::ColorEdit3("Emissive Color", mat.emissiveColor))
              DxrRenderer::ResetAccumulation();
            if (ImGui::SliderFloat("Emissive Intensity", &mat.emissiveIntensity,
                                   0.0f, 1000000.0f))
              DxrRenderer::ResetAccumulation();
            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Flags")) {
            if (ImGui::Checkbox("Double Sided", &mat.doubleSided))
              DxrRenderer::ResetAccumulation();
            const char *alphaModes[] = {"OPAQUE", "MASK", "BLEND"};
            int mode = 0;
            if (mat.alphaMode == "MASK")
              mode = 1;
            else if (mat.alphaMode == "BLEND")
              mode = 2;
            if (ImGui::Combo("Alpha Mode", &mode, alphaModes, 3)) {
              mat.alphaMode = alphaModes[mode];
              MarkOpacityDirty();
            }
            if (mat.alphaMode == "MASK") {
              if (ImGui::SliderFloat("Alpha Cutoff", &mat.alphaCutoff, 0.0f,
                                     1.0f))
                MarkOpacityDirty();
            }
            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("QA")) {
            float rough = (std::clamp)(mat.roughness, 0.0f, 1.0f);
            bool isMetal = mat.metalness > 1.0e-5f;
            bool isGlass = mat.transmissionWeight > 1.0e-5f;
            float aMin = fminf(mat.diffuseColor[0],
                               fminf(mat.diffuseColor[1], mat.diffuseColor[2]));
            float aMax = fmaxf(mat.diffuseColor[0],
                               fmaxf(mat.diffuseColor[1], mat.diffuseColor[2]));

            if (!isMetal && !isGlass && (aMin < 0.02f || aMax > 0.90f)) {
              ImGui::TextColored(ImVec4(1, 0.65f, 0, 1),
                                 "Dielectric albedo is outside typical range "
                                 "(avoid near-black/white).\n");
            }
            if (rough < 0.001f) {
              ImGui::TextColored(ImVec4(1, 0.65f, 0, 1),
                                 "Near-zero roughness can need more samples for "
                                 "stable sharp highlights.\n");
            }
            const float qaCoatWeight = mat.coatWeight;
            const float qaCoatRoughness = mat.coatRoughness;
            if (qaCoatWeight > 1.0e-5f && qaCoatRoughness < 0.001f) {
              ImGui::TextColored(
                  ImVec4(1, 0.65f, 0, 1),
              "Coat roughness very low; may sparkle.\n");
            }
            ImGui::EndTabItem();
          }

          ImGui::EndTabBar();
        }

        ImGui::PopID();
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

} // namespace MaterialEditor
