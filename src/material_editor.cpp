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
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

} // namespace MaterialEditor
