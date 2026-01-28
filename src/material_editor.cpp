#include "material_editor.h"
#include "scene.h"
#include "assets/asset_loader.h"
#include "imgui.h"
#include <vector>

// External globals from main.cpp
extern std::vector<Asset::GpuMesh> g_loadedMeshes;
extern std::vector<Asset::Material> g_loadedMaterials;

namespace MaterialEditor {

void Draw(bool &visible) {
    if (!visible) return;
    if (ImGui::Begin("Material Editor", &visible)) {
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
                    // Diffuse
                    ImGui::ColorEdit3("Diffuse Color", mat.diffuseColor);
                    
                    // PBR Parameters
                    float roughness = 1.0f - mat.reflectionGlossiness;
                    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
                        mat.reflectionGlossiness = 1.0f - roughness;
                    }
                    ImGui::SliderFloat("Metalness", &mat.metalness, 0.0f, 1.0f);

                    // Reflection (Legacy/Legacy naming but useful for color)
                    ImGui::ColorEdit3("Reflection Color", mat.reflectionColor);
                    
                    // Refraction (Keep as is)
                    ImGui::ColorEdit3("Refraction Color", mat.refractionColor);
                    ImGui::SliderFloat("Refraction Glossiness", &mat.refractionGlossiness, 0.0f, 1.0f);
                    ImGui::InputFloat("IOR", &mat.ior, 0.01f, 0.1f, "%.3f");

                    // Emissive
                    ImGui::ColorEdit3("Emissive Color", mat.emissiveColor);
                    ImGui::SliderFloat("Emissive Intensity", &mat.emissiveIntensity, 0.0f, 100.0f);
                    
                    // Texture Info
                    if (ImGui::TreeNode("Texture Maps (Indices)")) {
                        ImGui::LabelText("Diffuse", "%d", mat.diffuseTexture);
                        ImGui::LabelText("MetalRough", "%d", mat.metalRoughTexture);
                        ImGui::LabelText("Normal", "%d", mat.normalTexture);
                        ImGui::LabelText("Emissive", "%d", mat.emissiveTexture);
                        ImGui::LabelText("Occlusion", "%d", mat.occlusionTexture);
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
