#include "material_editor.h"
#include "scene.h"
#include "assets/asset_loader.h"
#include "file_import.h"
#include "imgui.h"
#include "dxr_renderer.h"
#include <vector>
#include <cmath>

// External globals from main.cpp
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;

namespace MaterialEditor {

static int s_pendingMaterialSelect = -1;
static bool s_pickingEnabled = false;

bool IsPickingEnabled() { return s_pickingEnabled; }
void SetPickingEnabled(bool enabled) { s_pickingEnabled = enabled; }

void SelectMaterial(int materialIndex) {
    s_pendingMaterialSelect = materialIndex;
}

static std::string WStringToUtf8Local(const std::wstring &ws) {
    if (ws.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), NULL, 0, NULL, NULL);
    std::string s(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], size_needed, NULL, NULL);
    return s;
}

static bool IsHDRTexturePath(const std::wstring &path) {
    if (path.size() < 4) return false;
    std::wstring ext;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    ext = path.substr(dot);
    for (auto &c : ext) c = (wchar_t)towlower(c);
    return (ext == L".hdr" || ext == L".exr");
}

void Draw(HWND hwnd, bool &visible) {
    if (!visible) return;
    if (ImGui::Begin("Material Editor", &visible)) {
        // Toolbar
        if (s_pickingEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Cancel Pick")) s_pickingEnabled = false;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Text("Click on object surface...");
        } else {
            if (ImGui::Button("Pick Material")) s_pickingEnabled = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to start picking material from scene objects");
        }
        ImGui::Separator();

        // Find selection
        int selectedIndex = -1;
        const auto& nodes = Scene::GetNodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].selected) {
                selectedIndex = (int)i;
                break;
            }
        }
        
        if (selectedIndex == -1) {
            ImGui::Text("No object selected");
        } else {
            const Scene::Node &node = nodes[selectedIndex];
            ImGui::Text("Node: %s", node.name.c_str());
            ImGui::Separator();
            
            // Material Editor
            if (node.meshIndices.empty()) {
                ImGui::Text("No meshes in this node.");
            } else {
                // Collect materials for the dropdown
                std::vector<int> materialIndices;
                std::vector<std::string> comboNames;
                
                for (size_t i = 0; i < node.meshIndices.size(); ++i) {
                    size_t mi = node.meshIndices[i];
                    if (mi >= g_loadedMeshes.size()) continue;
                    int matIdx = g_loadedMeshes[mi].materialIndex;
                    if (matIdx >= 0 && matIdx < (int)g_loadedMaterials.size()) {
                        // Avoid duplicates in the list if multiple meshes share the same material
                        bool exists = false;
                        for (int existing : materialIndices) {
                            if (existing == matIdx) { exists = true; break; }
                        }
                        if (!exists) {
                            materialIndices.push_back(matIdx);
                            comboNames.push_back(std::string(g_loadedMaterials[matIdx].name) + " (ID: " + std::to_string(matIdx) + ")");
                        }
                    }
                }

                static int selectedComboIdx = 0;
                
                // Handle external selection request (Picking)
                if (s_pendingMaterialSelect != -1) {
                    bool found = false;
                    for (int i = 0; i < (int)materialIndices.size(); ++i) {
                        if (materialIndices[i] == s_pendingMaterialSelect) {
                            selectedComboIdx = i;
                            found = true;
                            fprintf(stderr, "MaterialEditor: Auto-selecting material index %d (Combo index %d)\n", s_pendingMaterialSelect, i);
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "MaterialEditor: Pending material select %d not found in node's material list.\n", s_pendingMaterialSelect);
                    }
                    s_pendingMaterialSelect = -1;
                }

                // Reset index if it's out of bounds for the newly selected node
                if (selectedComboIdx >= (int)comboNames.size()) selectedComboIdx = 0;

                if (!comboNames.empty()) {
                    std::vector<const char*> comboChars;
                    for (const auto& s : comboNames) comboChars.push_back(s.c_str());

                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::Combo("##MaterialSelector", &selectedComboIdx, comboChars.data(), (int)comboChars.size());
                    ImGui::Separator();

                    int matIdx = materialIndices[selectedComboIdx];
                    Asset::Material &mat = g_loadedMaterials[matIdx];
                    
                    ImGui::PushID(matIdx);

                    auto ApplyPreset = [](Asset::Material& m, int presetIdx) {
                        auto SetRoughness = [&](float r) {
                            r = (r < 0.0f) ? 0.0f : (r > 1.0f ? 1.0f : r);
                            m.reflectionGlossiness = 1.0f - r;
                        };

                        // Defaults that keep behavior stable
                        m.clearcoat = 0.0f;
                        m.clearcoatRoughness = 0.1f;
                        m.thinWalled = 0.0f;
                        m.translucency = 0.0f;
                        m.uvScale[0] = 1.0f; m.uvScale[1] = 1.0f;
                        m.uvOffset[0] = 0.0f; m.uvOffset[1] = 0.0f;

                        m.triPlanarEnabled = 0.0f;
                        m.triPlanarScale = 1.0f;
                        m.triPlanarSharpness = 4.0f;
                        m.triPlanarNormalStrength = 1.0f;

                        // Keep refraction off unless preset wants it
                        m.refractionColor[0] = 0.0f;
                        m.refractionColor[1] = 0.0f;
                        m.refractionColor[2] = 0.0f;
                        m.refractionGlossiness = 1.0f;

                        // Corona-like archviz presets (engine approximation)
                        switch (presetIdx) {
                        default:
                        case 0: // Dielectric Generic
                            m.metalness = 0.0f;
                            m.ior = 1.5f;
                            SetRoughness(0.5f);
                            break;
                        case 1: // Paint / Plaster
                            m.metalness = 0.0f;
                            m.ior = 1.45f;
                            SetRoughness(0.75f);
                            break;
                        case 2: // Concrete
                            m.metalness = 0.0f;
                            m.ior = 1.5f;
                            SetRoughness(0.85f);
                            break;
                        case 3: // Wood (Raw)
                            m.metalness = 0.0f;
                            m.ior = 1.5f;
                            SetRoughness(0.6f);
                            break;
                        case 4: // Wood (Varnished)
                            m.metalness = 0.0f;
                            m.ior = 1.5f;
                            SetRoughness(0.35f);
                            m.clearcoat = 0.8f;
                            m.clearcoatRoughness = 0.08f;
                            break;
                        case 5: // Tile (Ceramic)
                            m.metalness = 0.0f;
                            m.ior = 1.52f;
                            SetRoughness(0.25f);
                            m.clearcoat = 0.6f;
                            m.clearcoatRoughness = 0.12f;
                            break;
                        case 6: // Metal (Brushed)
                            m.metalness = 1.0f;
                            m.ior = 1.0f;
                            SetRoughness(0.35f);
                            break;
                        case 7: // Metal (Polished)
                            m.metalness = 1.0f;
                            m.ior = 1.0f;
                            SetRoughness(0.08f);
                            break;
                        case 8: // Plastic
                            m.metalness = 0.0f;
                            m.ior = 1.45f;
                            SetRoughness(0.35f);
                            m.clearcoat = 0.25f;
                            m.clearcoatRoughness = 0.15f;
                            break;
                        case 9: // Glass (Clear Window, Thin)
                            m.metalness = 0.0f;
                            m.ior = 1.52f;
                            SetRoughness(0.02f);
                            m.refractionColor[0] = 1.0f;
                            m.refractionColor[1] = 1.0f;
                            m.refractionColor[2] = 1.0f;
                            m.refractionGlossiness = 1.0f;
                            m.thinWalled = 1.0f;
                            break;
                        case 10: // Glass (Frosted, Thin)
                            m.metalness = 0.0f;
                            m.ior = 1.52f;
                            SetRoughness(0.35f);
                            m.refractionColor[0] = 1.0f;
                            m.refractionColor[1] = 1.0f;
                            m.refractionColor[2] = 1.0f;
                            m.refractionGlossiness = 0.6f;
                            m.thinWalled = 1.0f;
                            break;
                        case 11: // Glass (Tinted, Thin)
                            m.metalness = 0.0f;
                            m.ior = 1.52f;
                            SetRoughness(0.05f);
                            m.refractionColor[0] = 0.85f;
                            m.refractionColor[1] = 0.95f;
                            m.refractionColor[2] = 1.0f;
                            m.refractionGlossiness = 1.0f;
                            m.thinWalled = 1.0f;
                            break;
                        case 12: // Fabric (Approx)
                            m.metalness = 0.0f;
                            m.ior = 1.4f;
                            SetRoughness(0.8f);
                            m.translucency = 0.15f;
                            break;
                        case 13: // Vegetation Leaf (Approx)
                            m.metalness = 0.0f;
                            m.ior = 1.4f;
                            SetRoughness(0.65f);
                            m.translucency = 0.6f;
                            m.thinWalled = 1.0f;
                            break;
                        }
                    };

                    // Presets
                    {
                        static int presetIdx = 0;
                        const char* presets[] = {
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
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::Combo("Preset", &presetIdx, presets, (int)(sizeof(presets) / sizeof(presets[0])));
                        if (ImGui::Button("Apply Preset")) {
                            ApplyPreset(mat, presetIdx);
                            DxrRenderer::ResetAccumulation();
                        }
                        ImGui::Separator();
                    }

                    // Diffuse
                    if (ImGui::ColorEdit3("Diffuse Color", mat.diffuseColor)) DxrRenderer::ResetAccumulation();
                    
                    // PBR Parameters
                    float roughness = 1.0f - mat.reflectionGlossiness;
                    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
                        mat.reflectionGlossiness = 1.0f - roughness;
                        DxrRenderer::ResetAccumulation();
                    }
                    if (ImGui::SliderFloat("Metalness", &mat.metalness, 0.0f, 1.0f)) DxrRenderer::ResetAccumulation();

                    // Reflection (Legacy/Legacy naming but useful for color)
                    if (ImGui::ColorEdit3("Reflection Color", mat.reflectionColor)) DxrRenderer::ResetAccumulation();
                    
                    // Refraction (Keep as is)
                    if (ImGui::ColorEdit3("Refraction Color", mat.refractionColor)) DxrRenderer::ResetAccumulation();
                    if (ImGui::SliderFloat("Refraction Glossiness", &mat.refractionGlossiness, 0.0f, 1.0f)) DxrRenderer::ResetAccumulation();
                    if (ImGui::InputFloat("IOR", &mat.ior, 0.01f, 0.1f, "%.3f")) DxrRenderer::ResetAccumulation();

                    // Archviz Extensions
                    if (ImGui::SliderFloat("Clearcoat", &mat.clearcoat, 0.0f, 1.0f)) DxrRenderer::ResetAccumulation();
                    if (ImGui::SliderFloat("Clearcoat Roughness", &mat.clearcoatRoughness, 0.0f, 1.0f)) DxrRenderer::ResetAccumulation();
                    {
                        bool thin = mat.thinWalled > 0.5f;
                        if (ImGui::Checkbox("Thin Walled", &thin)) {
                            mat.thinWalled = thin ? 1.0f : 0.0f;
                            DxrRenderer::ResetAccumulation();
                        }
                    }
                    if (ImGui::SliderFloat("Translucency", &mat.translucency, 0.0f, 1.0f)) DxrRenderer::ResetAccumulation();
                    if (ImGui::InputFloat2("UV Scale", mat.uvScale)) DxrRenderer::ResetAccumulation();
                    if (ImGui::InputFloat2("UV Offset", mat.uvOffset)) DxrRenderer::ResetAccumulation();

                    // Tri-planar mapping
                    if (ImGui::TreeNode("Tri-Planar Mapping")) {
                        bool tri = mat.triPlanarEnabled > 0.5f;
                        if (ImGui::Checkbox("Enable", &tri)) {
                            mat.triPlanarEnabled = tri ? 1.0f : 0.0f;
                            DxrRenderer::ResetAccumulation();
                        }
                        if (ImGui::SliderFloat("Scale", &mat.triPlanarScale, 0.001f, 50.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) DxrRenderer::ResetAccumulation();
                        if (ImGui::SliderFloat("Sharpness", &mat.triPlanarSharpness, 0.25f, 16.0f, "%.2f")) DxrRenderer::ResetAccumulation();
                        if (ImGui::SliderFloat("Normal Strength", &mat.triPlanarNormalStrength, 0.0f, 4.0f, "%.2f")) DxrRenderer::ResetAccumulation();
                        ImGui::TreePop();
                    }

                    // QA / plausibility hints
                    {
                        float rough = 1.0f - mat.reflectionGlossiness;
                        bool isMetal = mat.metalness > 0.5f;
                        bool isGlass = (mat.refractionColor[0] + mat.refractionColor[1] + mat.refractionColor[2]) > 0.01f;
                        float aMin = fminf(mat.diffuseColor[0], fminf(mat.diffuseColor[1], mat.diffuseColor[2]));
                        float aMax = fmaxf(mat.diffuseColor[0], fmaxf(mat.diffuseColor[1], mat.diffuseColor[2]));

                        if (!isMetal && !isGlass && (aMin < 0.02f || aMax > 0.90f)) {
                            ImGui::TextColored(ImVec4(1, 0.65f, 0, 1), "Warning: Dielectric albedo out of typical range (avoid near-black/white).\n");
                        }
                        if (rough < 0.02f) {
                            ImGui::TextColored(ImVec4(1, 0.65f, 0, 1), "Warning: Roughness < 0.02 can cause fireflies.");
                        }
                        if (mat.clearcoat > 0.01f && mat.clearcoatRoughness < 0.02f) {
                            ImGui::TextColored(ImVec4(1, 0.65f, 0, 1), "Note: Clearcoat roughness very low; may sparkle.");
                        }
                    }

                    // Emissive
                    if (ImGui::ColorEdit3("Emissive Color", mat.emissiveColor)) DxrRenderer::ResetAccumulation();
                    if (ImGui::SliderFloat("Emissive Intensity", &mat.emissiveIntensity, 0.0f, 100.0f)) DxrRenderer::ResetAccumulation();
                    
                    // Texture Info
                    if (ImGui::TreeNode("Texture Maps (Indices)")) {
                        auto DrawTextureSlot = [&](const char* label, int& idx) {
                            ImGui::PushID(label);

                            // Build list: 0 = None (-1), then actual textures.
                            static std::vector<std::string> names;
                            static std::vector<const char*> cstr;
                            names.clear();
                            cstr.clear();
                            names.emplace_back("None");
                            for (size_t ti = 0; ti < g_loadedTextures.size(); ++ti) {
                                const auto& t = g_loadedTextures[ti];
                                std::string s = "#" + std::to_string((int)ti);
                                if (t.width > 0 && t.height > 0) {
                                    s += " (" + std::to_string(t.width) + "x" + std::to_string(t.height) + ")";
                                }
                                if (!t.resource) {
                                    s += " [missing]";
                                }
                                names.emplace_back(std::move(s));
                            }
                            for (auto& s : names) cstr.push_back(s.c_str());

                            int comboIdx = (idx < 0) ? 0 : (idx + 1);
                            if (comboIdx < 0 || comboIdx >= (int)cstr.size()) comboIdx = 0;

                            ImGui::TextUnformatted(label);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            if (ImGui::Combo("##tex", &comboIdx, cstr.data(), (int)cstr.size())) {
                                int newIdx = comboIdx - 1;
                                // If user picked a missing resource texture, treat as None.
                                if (newIdx >= 0 && newIdx < (int)g_loadedTextures.size() && !g_loadedTextures[newIdx].resource) {
                                    newIdx = -1;
                                }
                                idx = newIdx;
                                DxrRenderer::ResetAccumulation();
                            }

                            // Manual edit + clear
                            int tmp = idx;
                            if (ImGui::InputInt("##idx", &tmp)) {
                                if (tmp < -1) tmp = -1;
                                if (tmp >= (int)g_loadedTextures.size()) tmp = -1;
                                if (tmp >= 0 && !g_loadedTextures[tmp].resource) tmp = -1;
                                idx = tmp;
                                DxrRenderer::ResetAccumulation();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Clear")) {
                                idx = -1;
                                DxrRenderer::ResetAccumulation();
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Load...")) {
                                std::wstring chosen;
                                if (OpenTextureFileDialog(hwnd, chosen) && !chosen.empty()) {
                                    bool isHDR = IsHDRTexturePath(chosen);
                                    int newTex = Scene::AddTextureFromFile(WStringToUtf8Local(chosen), isHDR);
                                    if (newTex >= 0) {
                                        idx = newTex;
                                        DxrRenderer::ResetAccumulation();
                                    }
                                }
                            }

                            ImGui::PopID();
                        };

                        DrawTextureSlot("Diffuse", mat.diffuseTexture);
                        DrawTextureSlot("Reflection", mat.reflectionTexture);
                        DrawTextureSlot("MetalRough", mat.metalRoughTexture);
                        DrawTextureSlot("Normal", mat.normalTexture);
                        DrawTextureSlot("Emissive", mat.emissiveTexture);
                        DrawTextureSlot("Occlusion", mat.occlusionTexture);
                        DrawTextureSlot("Refract", mat.refractionTexture);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                } else {
                    ImGui::Text("No valid materials found on this model.");
                }
            }
        }
    }
    ImGui::End();
}

} // namespace MaterialEditor
