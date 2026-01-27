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
    if (ImGui::Begin("Inspector", &visible)) {
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
            ImGui::Text("Materials (%zu)", node.meshIndices.size());
            
            for (size_t mi : node.meshIndices) {
                if (mi >= g_loadedMeshes.size()) continue;
                Asset::GpuMesh &mesh = g_loadedMeshes[mi];
                if (mesh.materialIndex < 0 || mesh.materialIndex >= (int)g_loadedMaterials.size()) continue;
                
                Asset::Material &mat = g_loadedMaterials[mesh.materialIndex];
                
                ImGui::PushID(mesh.materialIndex);
                if (ImGui::CollapsingHeader(mat.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                    // Diffuse
                    ImGui::ColorEdit3("Diffuse Color", mat.diffuseColor);
                    
                    // Reflection
                    ImGui::ColorEdit3("Reflection Color", mat.reflectionColor);
                    ImGui::SliderFloat("Reflection Glossiness", &mat.reflectionGlossiness, 0.0f, 1.0f);
                    
                    // Refraction
                    ImGui::ColorEdit3("Refraction Color", mat.refractionColor);
                    ImGui::SliderFloat("Refraction Glossiness", &mat.refractionGlossiness, 0.0f, 1.0f);
                    ImGui::InputFloat("IOR", &mat.ior, 0.01f, 0.1f, "%.3f");

                    // Emissive
                    ImGui::ColorEdit3("Emissive Color", mat.emissiveColor);
                    
                    // Texture Info
                    if (ImGui::TreeNode("Texture Maps (Indices)")) {
                        ImGui::LabelText("Diffuse", "%d", mat.diffuseTexture);
                        ImGui::LabelText("Reflection", "%d", mat.reflectionTexture);
                        ImGui::LabelText("Refraction", "%d", mat.refractionTexture);
                        ImGui::LabelText("Normal", "%d", mat.normalTexture);
                        ImGui::TreePop();
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

} // namespace MaterialEditor
